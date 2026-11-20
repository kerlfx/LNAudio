#include "lnplayer.h"

#include "runtime.h"

#include <atomic>
#include <cstring>
#include <future>
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
        , owner_(owner)
        , callback_(callback)
        , user_data_(user_data)
    {
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
                {
                    std::lock_guard<std::mutex> lock(snapshot_mutex_);
                    ++snapshot_.generation;
                    snapshot_.position_ms = 0;
                    snapshot_.duration_ms = -1;
                }

                // The command lifecycle is active; media probing is supplied by the
                // FFmpeg backend, which is not part of this implementation step.
                update_state(LNPLAYER_STATE_ERROR);
                emit(
                    LNPLAYER_EVENT_OPEN_COMPLETED,
                    LNPLAYER_RESULT_NOT_IMPLEMENTED,
                    id,
                    "Media backend is not connected.");
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
                (void)position_ms;
                const lnplayer_state_t current = state();
                const bool seekable =
                    current == LNPLAYER_STATE_READY ||
                    current == LNPLAYER_STATE_PLAYING ||
                    current == LNPLAYER_STATE_PAUSED;

                emit(
                    LNPLAYER_EVENT_SEEK_COMPLETED,
                    seekable ? LNPLAYER_RESULT_NOT_IMPLEMENTED : LNPLAYER_RESULT_INVALID_STATE,
                    id,
                    seekable
                        ? "Media backend is not connected."
                        : "Seek requires an opened media session.");
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
        return invoke_sync([this]
        {
            const auto current = state();
            if (current != LNPLAYER_STATE_READY &&
                current != LNPLAYER_STATE_PAUSED &&
                current != LNPLAYER_STATE_ENDED)
                return LNPLAYER_RESULT_INVALID_STATE;
            return LNPLAYER_RESULT_NOT_IMPLEMENTED;
        });
    }

    lnplayer_result_t pause()
    {
        return invoke_sync([this]
        {
            if (state() != LNPLAYER_STATE_PLAYING)
                return LNPLAYER_RESULT_INVALID_STATE;
            return LNPLAYER_RESULT_NOT_IMPLEMENTED;
        });
    }

    lnplayer_result_t stop()
    {
        return invoke_sync([this]
        {
            const auto current = state();
            if (current == LNPLAYER_STATE_IDLE || current == LNPLAYER_STATE_ERROR)
            {
                update_state(LNPLAYER_STATE_IDLE);
                reset_media_snapshot();
                return LNPLAYER_RESULT_OK;
            }
            return LNPLAYER_RESULT_NOT_IMPLEMENTED;
        });
    }

    lnplayer_result_t close()
    {
        return invoke_sync([this]
        {
            const auto current = state();
            if (current == LNPLAYER_STATE_IDLE)
                return LNPLAYER_RESULT_OK;
            if (current == LNPLAYER_STATE_ERROR)
            {
                update_state(LNPLAYER_STATE_IDLE);
                reset_media_snapshot();
                return LNPLAYER_RESULT_OK;
            }
            return LNPLAYER_RESULT_NOT_IMPLEMENTED;
        });
    }

    lnplayer_result_t shutdown()
    {
        return invoke_sync([this]
        {
            update_state(LNPLAYER_STATE_CLOSING);
            return LNPLAYER_RESULT_OK;
        });
    }

    void get_snapshot(lnplayer_snapshot_t* output) const
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        output->state = snapshot_.state;
        output->position_ms = snapshot_.position_ms;
        output->duration_ms = snapshot_.duration_ms;
        output->volume = snapshot_.volume;
        output->reserved = 0;
        output->generation = snapshot_.generation;
    }

    void set_volume(uint32_t volume)
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        snapshot_.volume = volume;
    }

private:
    template<typename Operation>
    lnplayer_result_t invoke_sync(Operation operation)
    {
        if (runtime_.running_in_io_thread())
            return LNPLAYER_RESULT_WOULD_DEADLOCK;

        auto result = std::make_shared<std::promise<lnplayer_result_t>>();
        auto future = result->get_future();

        try
        {
            asio::post(runtime_.io_context(), [result, operation = std::move(operation)]() mutable
            {
                try
                {
                    result->set_value(operation());
                }
                catch (...)
                {
                    result->set_value(LNPLAYER_RESULT_INTERNAL_ERROR);
                }
            });
        }
        catch (...)
        {
            return LNPLAYER_RESULT_OUT_OF_MEMORY;
        }

        return future.get();
    }

    lnplayer_state_t state() const
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        return snapshot_.state;
    }

    void reset_media_snapshot()
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        snapshot_.position_ms = 0;
        snapshot_.duration_ms = -1;
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
    lnplayer_t* owner_;
    lnplayer_event_callback_t callback_;
    void* user_data_;
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
