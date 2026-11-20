#include "lnplayer.h"

#include "runtime.h"
#include "ffmpeg_media_backend.h"
#include "playback_session.h"
#ifdef _WIN32
#include "lnplayer_windows.h"
#endif

#include <atomic>
#include <cstring>
#include <cmath>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{

struct PlayerSnapshot
{
    lnplayer_state_t state = LNPLAYER_STATE_IDLE;
    int64_t position_ms = 0;
    int64_t duration_ms = -1;
    uint32_t volume = 1000;
    uint64_t generation = 0;
};

class PlaybackController
{
public:
    PlaybackController(
        lnplayer::Runtime& runtime,
        lnplayer_t* owner,
        lnplayer_event_callback_t callback,
        void* user_data)
        : runtime_(runtime)
        , worker_strand_(asio::make_strand(runtime.thread_pool()))
        , owner_(owner)
        , callback_(callback)
        , user_data_(user_data)
        , session_(lnplayer::create_ffmpeg_media_backend)
    {
        session_.set_playback_end_callback([this]
        {
            try
            {
                asio::post(runtime_.io_context(), [this]
                {
                    const lnplayer::SessionSnapshot snapshot = session_.snapshot();
                    publish_snapshot(snapshot);
                    emit(LNPLAYER_EVENT_PLAYBACK_ENDED, LNPLAYER_RESULT_OK, 0, nullptr);
                });
            }
            catch (...)
            {
            }
        });
        session_.set_playback_error_callback([this](std::string message)
        {
            try
            {
                asio::post(runtime_.io_context(), [this, message = std::move(message)]
                {
                    const lnplayer::SessionSnapshot snapshot = session_.snapshot();
                    publish_snapshot(snapshot);
                    emit(
                        LNPLAYER_EVENT_ERROR,
                        LNPLAYER_RESULT_INTERNAL_ERROR,
                        0,
                        message.c_str());
                });
            }
            catch (...)
            {
            }
        });
    }

    lnplayer_result_t open(const char* source, uint64_t* operation_id)
    {
        const uint64_t id = next_operation_id_.fetch_add(1, std::memory_order_relaxed);
        const std::string source_copy(source);

        try
        {
            asio::post(runtime_.io_context(), [this, id, source_copy]
            {
                update_state(LNPLAYER_STATE_OPENING);
                asio::post(worker_strand_, [this, id, source_copy]
                {
                    lnplayer::SessionResult result;
                    try
                    {
                        result = session_.open(source_copy);
                    }
                    catch (...)
                    {
                        result = {false, "Unexpected error while opening media.", session_.snapshot()};
                    }

                    asio::post(runtime_.io_context(), [this, id, result = std::move(result)]() mutable
                    {
                        publish_snapshot(result.snapshot);
                        emit(
                            LNPLAYER_EVENT_OPEN_COMPLETED,
                            result.success ? LNPLAYER_RESULT_OK : LNPLAYER_RESULT_INTERNAL_ERROR,
                            id,
                            result.message.empty() ? nullptr : result.message.c_str());
                    });
                });
            });
        }
        catch (...)
        {
            return LNPLAYER_RESULT_OUT_OF_MEMORY;
        }

        *operation_id = id;
        return LNPLAYER_RESULT_OK;
    }

    lnplayer_result_t seek(int64_t position_ms, uint64_t* operation_id)
    {
        const uint64_t id = next_operation_id_.fetch_add(1, std::memory_order_relaxed);

        try
        {
            asio::post(runtime_.io_context(), [this, id, position_ms]
            {
                asio::post(worker_strand_, [this, id, position_ms]
                {
                    lnplayer::SessionResult result;
                    try
                    {
                        result = session_.seek(position_ms);
                    }
                    catch (...)
                    {
                        result = {false, "Unexpected error while seeking media.", session_.snapshot()};
                    }

                    asio::post(runtime_.io_context(), [this, id, result = std::move(result)]() mutable
                    {
                        publish_snapshot(result.snapshot);
                        emit(
                            LNPLAYER_EVENT_SEEK_COMPLETED,
                            result.success ? LNPLAYER_RESULT_OK : LNPLAYER_RESULT_INVALID_STATE,
                            id,
                            result.message.empty() ? nullptr : result.message.c_str());
                    });
                });
            });
        }
        catch (...)
        {
            return LNPLAYER_RESULT_OUT_OF_MEMORY;
        }

        *operation_id = id;
        return LNPLAYER_RESULT_OK;
    }

    lnplayer_result_t play()
    {
        return invoke_sync([this] { return session_.play(); });
    }

    lnplayer_result_t pause()
    {
        return invoke_sync([this] { return session_.pause(); });
    }

    lnplayer_result_t stop()
    {
        return invoke_sync([this] { return session_.stop(); });
    }

    lnplayer_result_t close()
    {
        return invoke_sync([this] { return session_.close(); });
    }

    lnplayer_result_t select_audio_track(int stream_index)
    {
        return invoke_sync([this, stream_index]
        {
            return session_.select_audio_track(stream_index);
        });
    }

    std::vector<lnplayer::MediaTrackInfo> audio_tracks() const
    {
        return session_.audio_tracks();
    }

    lnplayer_result_t select_subtitle_track(int stream_index)
    {
        return invoke_sync([this, stream_index]
        {
            return session_.select_subtitle_track(stream_index);
        });
    }

    std::vector<lnplayer::MediaSubtitleTrackInfo> subtitle_tracks() const
    {
        return session_.subtitle_tracks();
    }

    void set_subtitle_offset_ms(int64_t offset_ms)
    {
        session_.set_subtitle_offset_ms(offset_ms);
    }

    lnplayer_result_t shutdown()
    {
        const lnplayer_result_t result =
            invoke_sync([this] { return session_.close(); }, true);
        if (result != LNPLAYER_RESULT_OK)
            return result;

        // The backend may have posted its final completion event immediately
        // before the close operation completed. Drain those handlers while
        // this Controller is still alive.
        auto drained = std::make_shared<std::promise<void>>();
        auto future = drained->get_future();
        try
        {
            asio::post(runtime_.io_context(), [drained]
            {
                drained->set_value();
            });
        }
        catch (...)
        {
            return LNPLAYER_RESULT_OUT_OF_MEMORY;
        }
        future.wait();
        return result;
    }

    void get_snapshot(lnplayer_snapshot_t* output) const
    {
        const lnplayer::SessionSnapshot session_snapshot = session_.snapshot();
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        output->state = convert_state(session_snapshot.state);
        output->position_ms = session_snapshot.position_ms;
        output->duration_ms = session_snapshot.duration_ms;
        output->volume = snapshot_.volume;
        output->reserved =
            (session_snapshot.has_audio ? LNPLAYER_MEDIA_HAS_AUDIO : 0u) |
            (session_snapshot.has_video ? LNPLAYER_MEDIA_HAS_VIDEO : 0u);
        output->generation = session_snapshot.generation;
    }

#ifdef _WIN32
    lnplayer_result_t set_video_device(void* device)
    {
        const lnplayer::SessionResult result = session_.set_video_device(device);
        if (!result.success)
            return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, result.message.c_str());
        return LNPLAYER_RESULT_OK;
    }

    lnplayer_result_t read_video_frame(
        lnplayer_windows_video_frame_callback_t callback,
        void* user_data,
        int* end_of_stream,
        std::string& error)
    {
        if (runtime_.running_in_io_thread())
            return LNPLAYER_RESULT_WOULD_DEADLOCK;

        struct ReadResult
        {
            lnplayer::MediaBackendResult result;
            lnplayer::VideoFrame frame;
            bool end_of_stream = false;
        };

        auto completion = std::make_shared<std::promise<ReadResult>>();
        auto future = completion->get_future();
        try
        {
            asio::post(runtime_.io_context(), [this, completion]
            {
                asio::post(worker_strand_, [this, completion]
                {
                    ReadResult read;
                    try
                    {
                        read.result = session_.read_video_frame(read.frame, read.end_of_stream);
                    }
                    catch (...)
                    {
                        read.result = {false, "Unexpected error while decoding a video frame."};
                    }
                    asio::post(runtime_.io_context(), [completion, read = std::move(read)]() mutable
                    {
                        completion->set_value(std::move(read));
                    });
                });
            });
        }
        catch (...)
        {
            return LNPLAYER_RESULT_OUT_OF_MEMORY;
        }

        ReadResult read = future.get();
        if (!read.result.success)
        {
            error = read.result.message;
            return LNPLAYER_RESULT_INTERNAL_ERROR;
        }

        *end_of_stream = read.end_of_stream ? 1 : 0;
        if (!read.end_of_stream && callback &&
            (!read.frame.bgra.empty() || read.frame.d3d11_texture))
        {
            const lnplayer_windows_video_frame_t frame{
                sizeof(lnplayer_windows_video_frame_t),
                read.frame.bgra.data(),
                read.frame.width,
                read.frame.height,
                read.frame.stride,
                read.frame.pts_ms,
                read.frame.generation,
                read.frame.display_aspect_ratio,
                read.frame.d3d11_texture,
                read.frame.d3d11_array_slice,
                read.frame.dxgi_format,
                read.frame.hardware_frame_owner
                    ? static_cast<const void*>(&read.frame.hardware_frame_owner)
                    : nullptr,
                read.frame.subtitle_text.c_str(),
                read.frame.subtitle_bgra.data(),
                read.frame.subtitle_width,
                read.frame.subtitle_height,
                read.frame.subtitle_stride,
                read.frame.hdr_transfer,
                read.frame.color_primaries,
                read.frame.ycbcr_matrix,
                read.frame.ycbcr_full_range
            };
            callback(&frame, user_data);
        }

        return LNPLAYER_RESULT_OK;
    }
#endif

    void set_volume(uint32_t volume)
    {
        session_.set_volume(static_cast<float>(volume) / 1000.0f);
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        snapshot_.volume = volume;
    }

    void set_playback_rate(float rate)
    {
        session_.set_playback_rate(rate);
    }

private:
    static lnplayer_state_t convert_state(lnplayer::SessionState state)
    {
        switch (state)
        {
        case lnplayer::SessionState::Opening: return LNPLAYER_STATE_OPENING;
        case lnplayer::SessionState::Ready: return LNPLAYER_STATE_READY;
        case lnplayer::SessionState::Playing: return LNPLAYER_STATE_PLAYING;
        case lnplayer::SessionState::Paused: return LNPLAYER_STATE_PAUSED;
        case lnplayer::SessionState::Ended: return LNPLAYER_STATE_ENDED;
        case lnplayer::SessionState::Error: return LNPLAYER_STATE_ERROR;
        case lnplayer::SessionState::Closing: return LNPLAYER_STATE_CLOSING;
        case lnplayer::SessionState::Empty:
        default: return LNPLAYER_STATE_IDLE;
        }
    }

    void publish_snapshot(const lnplayer::SessionSnapshot& session_snapshot)
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            const lnplayer_state_t next = convert_state(session_snapshot.state);
            changed = snapshot_.state != next;
            snapshot_.state = next;
            snapshot_.position_ms = session_snapshot.position_ms;
            snapshot_.duration_ms = session_snapshot.duration_ms;
            snapshot_.generation = session_snapshot.generation;
        }

        if (changed)
            emit(LNPLAYER_EVENT_STATE_CHANGED, LNPLAYER_RESULT_OK, 0, nullptr);
    }

    template<typename Operation>
    lnplayer_result_t invoke_sync(Operation operation, bool closing = false)
    {
        if (runtime_.running_in_io_thread())
            return LNPLAYER_RESULT_WOULD_DEADLOCK;

        auto result = std::make_shared<std::promise<lnplayer_result_t>>();
        auto future = result->get_future();

        try
        {
            asio::post(runtime_.io_context(), [this, result, operation = std::move(operation), closing]() mutable
            {
                try
                {
                    asio::post(worker_strand_, [this, result, operation = std::move(operation), closing]() mutable
                    {
                        lnplayer::SessionResult session_result;
                        try
                        {
                            session_result = operation();
                        }
                        catch (...)
                        {
                            session_result = {
                                false,
                                "Unexpected error while executing media operation.",
                                session_.snapshot()};
                        }

                        asio::post(runtime_.io_context(),
                            [this, result, session_result = std::move(session_result), closing]() mutable
                            {
                                publish_snapshot(session_result.snapshot);
                                if (closing)
                                    update_state(LNPLAYER_STATE_CLOSING);

                                const lnplayer_result_t operation_result = session_result.success
                                    ? LNPLAYER_RESULT_OK
                                    : LNPLAYER_RESULT_INVALID_STATE;
                                if (!session_result.success)
                                    emit(
                                        LNPLAYER_EVENT_ERROR,
                                        operation_result,
                                        0,
                                        session_result.message.empty()
                                            ? "Media operation failed."
                                            : session_result.message.c_str());
                                result->set_value(operation_result);
                            });
                    });
                }
                catch (...)
                {
                    result->set_value(LNPLAYER_RESULT_OUT_OF_MEMORY);
                }
            });
        }
        catch (...)
        {
            return LNPLAYER_RESULT_OUT_OF_MEMORY;
        }

        return future.get();
    }

    void update_state(lnplayer_state_t next)
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            changed = snapshot_.state != next;
            snapshot_.state = next;
        }

        if (changed)
            emit(LNPLAYER_EVENT_STATE_CHANGED, LNPLAYER_RESULT_OK, 0, nullptr);
    }

    void emit(
        lnplayer_event_type_t type,
        lnplayer_result_t result,
        uint64_t operation_id,
        const char* message)
    {
        if (!callback_)
            return;

        lnplayer_snapshot_t snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        get_snapshot(&snapshot);

        lnplayer_event_t event{};
        event.struct_size = sizeof(event);
        event.type = type;
        event.result = result;
        event.state = snapshot.state;
        event.operation_id = operation_id;
        event.generation = snapshot.generation;
        event.position_ms = snapshot.position_ms;
        event.duration_ms = snapshot.duration_ms;
        event.message = message;
        callback_(owner_, &event, user_data_);
    }

    lnplayer::Runtime& runtime_;
    asio::strand<asio::thread_pool::executor_type> worker_strand_;
    lnplayer_t* owner_;
    lnplayer_event_callback_t callback_;
    void* user_data_;
    lnplayer::PlaybackSession session_;
    mutable std::mutex snapshot_mutex_;
    PlayerSnapshot snapshot_;
    std::atomic<uint64_t> next_operation_id_{1};
};

std::mutex g_player_mutex;
lnplayer_t* g_player = nullptr;
bool g_destroying = false;
thread_local std::string g_last_error;

lnplayer_result_t set_error(lnplayer_result_t result, const char* message)
{
    g_last_error = message;
    return result;
}

bool is_current_player(lnplayer_t* player)
{
    std::lock_guard<std::mutex> lock(g_player_mutex);
    return player && player == g_player && !g_destroying;
}

lnplayer_result_t finish_result(lnplayer_result_t result)
{
    if (result == LNPLAYER_RESULT_OK)
        g_last_error.clear();
    else
        set_error(result, result == LNPLAYER_RESULT_NOT_IMPLEMENTED
            ? "The media backend is not connected."
            : result == LNPLAYER_RESULT_INVALID_STATE
                ? "The operation is not valid in the current Player state."
                : result == LNPLAYER_RESULT_WOULD_DEADLOCK
                    ? "A synchronous operation cannot run on the Core event thread."
                    : "The Player operation failed.");
    return result;
}

}

struct lnplayer_handle
{
    explicit lnplayer_handle(const lnplayer_config_t* config)
        : event_callback(config ? config->event_callback : nullptr)
        , user_data(config ? config->user_data : nullptr)
    {
    }

    lnplayer::Runtime runtime;
    lnplayer_event_callback_t event_callback;
    void* user_data;
    std::unique_ptr<PlaybackController> controller;
};

extern "C"
{

uint32_t lnplayer_get_abi_version(void)
{
    return LNPLAYER_ABI_VERSION;
}

lnplayer_result_t lnplayer_create(
    const lnplayer_config_t* config,
    lnplayer_t** player)
{
    if (!player)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "player output is null.");

    *player = nullptr;

    if (config && config->struct_size < sizeof(lnplayer_config_t))
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "config structure is too small.");

    std::lock_guard<std::mutex> lock(g_player_mutex);

    if (g_player || g_destroying)
        return set_error(LNPLAYER_RESULT_ALREADY_EXISTS, "This process already owns a Player.");

    try
    {
        std::unique_ptr<lnplayer_t> instance(new lnplayer_handle(config));
        instance->controller = std::make_unique<PlaybackController>(
            instance->runtime,
            instance.get(),
            instance->event_callback,
            instance->user_data);
        g_player = instance.release();
        *player = g_player;
        g_last_error.clear();
        return LNPLAYER_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return set_error(LNPLAYER_RESULT_OUT_OF_MEMORY, "Unable to allocate Player.");
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, "Unable to initialize Player Runtime.");
    }
}

lnplayer_result_t lnplayer_destroy(lnplayer_t* player)
{
    if (player && is_current_player(player) && player->runtime.running_in_io_thread())
        return set_error(LNPLAYER_RESULT_WOULD_DEADLOCK, "Player cannot be destroyed from its event thread.");

    {
        std::lock_guard<std::mutex> lock(g_player_mutex);
        if (!player || player != g_player || g_destroying)
            return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");

        g_destroying = true;
        g_player = nullptr;
    }

    const lnplayer_result_t shutdown_result = player->controller->shutdown();
    delete player;

    {
        std::lock_guard<std::mutex> lock(g_player_mutex);
        g_destroying = false;
    }

    return finish_result(shutdown_result);
}

lnplayer_result_t lnplayer_open(
    lnplayer_t* player,
    const char* source_utf8,
    uint64_t* operation_id)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!source_utf8 || !source_utf8[0] || !operation_id)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Invalid open arguments.");

    const auto result = player->controller->open(source_utf8, operation_id);
    return finish_result(result);
}

lnplayer_result_t lnplayer_seek(
    lnplayer_t* player,
    int64_t position_ms,
    uint64_t* operation_id)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (position_ms < 0 || !operation_id)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Invalid seek arguments.");

    const auto result = player->controller->seek(position_ms, operation_id);
    return finish_result(result);
}

lnplayer_result_t lnplayer_play(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return finish_result(player->controller->play());
}

lnplayer_result_t lnplayer_pause(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return finish_result(player->controller->pause());
}

lnplayer_result_t lnplayer_stop(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return finish_result(player->controller->stop());
}

lnplayer_result_t lnplayer_close(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return finish_result(player->controller->close());
}

lnplayer_result_t lnplayer_library_add_directory(
    lnplayer_t* player,
    const char* path_utf8,
    int recursive)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!path_utf8 || !path_utf8[0])
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Library directory path is empty.");

    std::string error;
    if (!player->runtime.music_library().add_directory(path_utf8, recursive != 0, error))
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, error.c_str());
    return finish_result(LNPLAYER_RESULT_OK);
}

lnplayer_result_t lnplayer_library_remove_directory(
    lnplayer_t* player,
    const char* path_utf8)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!path_utf8 || !path_utf8[0])
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Library directory path is empty.");

    std::string error;
    if (!player->runtime.music_library().remove_directory(path_utf8, error))
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, error.c_str());
    return finish_result(LNPLAYER_RESULT_OK);
}

lnplayer_result_t lnplayer_library_scan_directory(
    lnplayer_t* player,
    const char* path_utf8,
    int recursive,
    int32_t* imported_count)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!path_utf8 || !path_utf8[0] || !imported_count)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Invalid library scan arguments.");

    try
    {
        std::string error;
        int count = 0;
        if (!player->runtime.music_library().scan_directory(
            path_utf8, recursive != 0, count, error))
            return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, error.c_str());

        *imported_count = count;
        return finish_result(LNPLAYER_RESULT_OK);
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, "Unexpected error while scanning library directory.");
    }
}

lnplayer_result_t lnplayer_library_search_tracks(
    lnplayer_t* player,
    const char* query_utf8,
    int32_t limit,
    lnplayer_library_track_callback_t callback,
    void* user_data)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!query_utf8 || !callback)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Invalid library search arguments.");

    try
    {
        std::string error;
        const auto tracks = player->runtime.music_library().search_tracks(
            query_utf8, limit, error);
        if (!error.empty())
            return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, error.c_str());

        for (const auto& track : tracks)
        {
            const lnplayer_library_track_t item{
                sizeof(lnplayer_library_track_t),
                track.id,
                track.duration_ms,
                track.path.c_str(),
                track.title.c_str(),
                track.artist.c_str(),
                track.album.c_str()
            };
            callback(&item, user_data);
        }

        return finish_result(LNPLAYER_RESULT_OK);
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, "Unexpected error while querying music library.");
    }
}

#ifdef _WIN32
lnplayer_result_t lnplayer_windows_set_d3d11_device(
    lnplayer_t* player,
    void* d3d11_device)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    const lnplayer_result_t result = player->controller->set_video_device(d3d11_device);
    if (result != LNPLAYER_RESULT_OK)
        return result;
    return finish_result(LNPLAYER_RESULT_OK);
}

lnplayer_result_t lnplayer_windows_retain_video_frame(
    const lnplayer_windows_video_frame_t* frame,
    void** retained_owner)
{
    if (!frame || frame->struct_size < sizeof(lnplayer_windows_video_frame_t) ||
        !frame->frame_owner || !retained_owner)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Video frame cannot be retained.");

    try
    {
        const auto* owner =
            static_cast<const std::shared_ptr<void>*>(frame->frame_owner);
        *retained_owner = new std::shared_ptr<void>(*owner);
        return finish_result(LNPLAYER_RESULT_OK);
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_OUT_OF_MEMORY, "Unable to retain video frame.");
    }
}

void lnplayer_windows_release_video_frame(void* retained_owner)
{
    delete static_cast<std::shared_ptr<void>*>(retained_owner);
}

lnplayer_result_t lnplayer_windows_convert_video_frame(
    void* retained_owner,
    uint64_t generation,
    int64_t pts_ms,
    int32_t ycbcr_matrix,
    int32_t ycbcr_full_range,
    lnplayer_windows_video_frame_callback_t callback,
    void* user_data)
{
    if (!retained_owner || !callback)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Retained frame or callback is null.");

    try
    {
        const auto& owner = *static_cast<std::shared_ptr<void>*>(retained_owner);
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        int stride = 0;
        float display_aspect_ratio = 0.0f;
        std::string error;
        if (!lnplayer::convert_d3d11_frame_to_bgra(
                owner, pixels, width, height, stride, display_aspect_ratio,
                ycbcr_matrix, ycbcr_full_range, error))
            return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, error.c_str());

        lnplayer_windows_video_frame_t frame{};
        frame.struct_size = sizeof(frame);
        frame.bgra = pixels.data();
        frame.width = width;
        frame.height = height;
        frame.stride = stride;
        frame.pts_ms = pts_ms;
        frame.generation = generation;
        frame.display_aspect_ratio = display_aspect_ratio;
        frame.ycbcr_matrix = ycbcr_matrix;
        frame.ycbcr_full_range = ycbcr_full_range;
        callback(&frame, user_data);
        g_last_error.clear();
        return LNPLAYER_RESULT_OK;
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, "Unexpected error converting D3D11 video frame.");
    }
}


lnplayer_result_t lnplayer_windows_read_video_frame(
    lnplayer_t* player,
    lnplayer_windows_video_frame_callback_t callback,
    void* user_data,
    int* end_of_stream)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!callback || !end_of_stream)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Video frame callback or end-of-stream output is null.");

    try
    {
        std::string error;
        const lnplayer_result_t result =
            player->controller->read_video_frame(callback, user_data, end_of_stream, error);
        if (result == LNPLAYER_RESULT_OK)
        {
            g_last_error.clear();
            return result;
        }
        return set_error(
            result,
            error.empty() ? "Unable to read a video frame." : error.c_str());
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, "Unexpected error while reading a video frame.");
    }
}
#endif

lnplayer_result_t lnplayer_get_snapshot(
    lnplayer_t* player,
    lnplayer_snapshot_t* snapshot)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!snapshot || snapshot->struct_size < sizeof(lnplayer_snapshot_t))
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Snapshot structure is null or too small.");

    player->controller->get_snapshot(snapshot);
    g_last_error.clear();
    return LNPLAYER_RESULT_OK;
}

lnplayer_result_t lnplayer_set_volume(lnplayer_t* player, uint32_t volume)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (volume > 1000)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Volume must be between 0 and 1000.");

    player->controller->set_volume(volume);
    g_last_error.clear();
    return LNPLAYER_RESULT_OK;
}

lnplayer_result_t lnplayer_set_playback_rate(lnplayer_t* player, float rate)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!std::isfinite(rate) || rate < 0.25f || rate > 4.0f)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Playback rate must be between 0.25 and 4.0.");

    player->controller->set_playback_rate(rate);
    return finish_result(LNPLAYER_RESULT_OK);
}

lnplayer_result_t lnplayer_enumerate_audio_tracks(
    lnplayer_t* player,
    lnplayer_audio_track_callback_t callback,
    void* user_data)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!callback)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Audio track callback is null.");

    try
    {
        const auto tracks = player->controller->audio_tracks();
        for (const auto& track : tracks)
        {
            const lnplayer_audio_track_info_t item{
                sizeof(lnplayer_audio_track_info_t),
                track.stream_index,
                track.channels,
                track.sample_rate,
                track.is_default ? 1 : 0,
                track.is_selected ? 1 : 0,
                track.title.c_str(),
                track.language.c_str(),
                track.codec.c_str()
            };
            callback(&item, user_data);
        }
        return finish_result(LNPLAYER_RESULT_OK);
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, "Unable to enumerate media audio tracks.");
    }
}

lnplayer_result_t lnplayer_select_audio_track(lnplayer_t* player, int32_t stream_index)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (stream_index < 0)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Audio stream index is invalid.");
    return finish_result(player->controller->select_audio_track(stream_index));
}

lnplayer_result_t lnplayer_enumerate_subtitle_tracks(
    lnplayer_t* player,
    lnplayer_subtitle_track_callback_t callback,
    void* user_data)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!callback)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Subtitle track callback is null.");

    try
    {
        const auto tracks = player->controller->subtitle_tracks();
        for (const auto& track : tracks)
        {
            const lnplayer_subtitle_track_info_t item{
                sizeof(lnplayer_subtitle_track_info_t),
                track.stream_index,
                track.is_default ? 1 : 0,
                track.is_selected ? 1 : 0,
                track.title.c_str(),
                track.language.c_str(),
                track.codec.c_str()
            };
            callback(&item, user_data);
        }
        return finish_result(LNPLAYER_RESULT_OK);
    }
    catch (...)
    {
        return set_error(LNPLAYER_RESULT_INTERNAL_ERROR, "Unable to enumerate embedded subtitle tracks.");
    }
}

lnplayer_result_t lnplayer_select_subtitle_track(lnplayer_t* player, int32_t stream_index)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (stream_index < -1)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Subtitle stream index must be -1 or a valid stream index.");
    return finish_result(player->controller->select_subtitle_track(stream_index));
}

lnplayer_result_t lnplayer_set_subtitle_offset(lnplayer_t* player, int64_t offset_ms)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    player->controller->set_subtitle_offset_ms(offset_ms);
    return finish_result(LNPLAYER_RESULT_OK);
}

lnplayer_result_t lnplayer_get_last_error(
    char* buffer,
    size_t buffer_size,
    size_t* required_size)
{
    const size_t required = g_last_error.size() + 1;

    if (required_size)
        *required_size = required;

    if (!buffer || buffer_size < required)
        return LNPLAYER_RESULT_BUFFER_TOO_SMALL;

    std::memcpy(buffer, g_last_error.c_str(), required);
    return LNPLAYER_RESULT_OK;
}

}
