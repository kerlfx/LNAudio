#include "lnplayer.h"

#include "runtime.h"
#include "ffmpeg_media_backend.h"
#include "playback_session.h"

#include <atomic>
#include <cstring>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

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

    lnplayer_result_t shutdown()
    {
        return invoke_sync([this] { return session_.close(); }, true);
    }

    void get_snapshot(lnplayer_snapshot_t* output) const
    {
        const lnplayer::SessionSnapshot session_snapshot = session_.snapshot();
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        output->state = convert_state(session_snapshot.state);
        output->position_ms = session_snapshot.position_ms;
        output->duration_ms = session_snapshot.duration_ms;
        output->volume = snapshot_.volume;
        output->reserved = 0;
        output->generation = session_snapshot.generation;
    }

    void set_volume(uint32_t volume)
    {
        session_.set_volume(static_cast<float>(volume) / 1000.0f);
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        snapshot_.volume = volume;
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
