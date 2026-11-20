#include "lnplayer.h"

#include "runtime.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>

struct lnplayer
{
    explicit lnplayer(const lnplayer_config_t* config)
        : event_callback(config ? config->event_callback : nullptr)
        , user_data(config ? config->user_data : nullptr)
    {
    }

    lnplayer::Runtime runtime;
    lnplayer_event_callback_t event_callback;
    void* user_data;
    std::atomic<lnplayer_state_t> state{LNPLAYER_STATE_IDLE};
    std::atomic<int64_t> position_ms{0};
    std::atomic<int64_t> duration_ms{-1};
    std::atomic<uint32_t> volume{1000};
    std::atomic<uint64_t> generation{0};
};

namespace
{

std::mutex g_player_mutex;
lnplayer_t* g_player = nullptr;
thread_local std::string g_last_error;

lnplayer_result_t set_error(lnplayer_result_t result, const char* message)
{
    g_last_error = message;
    return result;
}

bool is_current_player(lnplayer_t* player)
{
    std::lock_guard<std::mutex> lock(g_player_mutex);
    return player && player == g_player;
}

lnplayer_result_t not_implemented()
{
    return set_error(
        LNPLAYER_RESULT_NOT_IMPLEMENTED,
        "Playback Controller has not been implemented yet.");
}

}

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

    if (g_player)
        return set_error(LNPLAYER_RESULT_ALREADY_EXISTS, "This process already owns a Player.");

    try
    {
        std::unique_ptr<lnplayer_t> instance(new lnplayer(config));
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
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");

    {
        std::lock_guard<std::mutex> lock(g_player_mutex);
        g_player = nullptr;
    }

    delete player;
    g_last_error.clear();
    return LNPLAYER_RESULT_OK;
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
    return not_implemented();
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
    return not_implemented();
}

lnplayer_result_t lnplayer_play(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return not_implemented();
}

lnplayer_result_t lnplayer_pause(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return not_implemented();
}

lnplayer_result_t lnplayer_stop(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return not_implemented();
}

lnplayer_result_t lnplayer_close(lnplayer_t* player)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    return not_implemented();
}

lnplayer_result_t lnplayer_get_snapshot(
    lnplayer_t* player,
    lnplayer_snapshot_t* snapshot)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (!snapshot || snapshot->struct_size < sizeof(lnplayer_snapshot_t))
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Snapshot structure is null or too small.");

    snapshot->state = player->state.load(std::memory_order_relaxed);
    snapshot->position_ms = player->position_ms.load(std::memory_order_relaxed);
    snapshot->duration_ms = player->duration_ms.load(std::memory_order_relaxed);
    snapshot->volume = player->volume.load(std::memory_order_relaxed);
    snapshot->reserved = 0;
    snapshot->generation = player->generation.load(std::memory_order_relaxed);
    g_last_error.clear();
    return LNPLAYER_RESULT_OK;
}

lnplayer_result_t lnplayer_set_volume(lnplayer_t* player, uint32_t volume)
{
    if (!is_current_player(player))
        return set_error(LNPLAYER_RESULT_INVALID_HANDLE, "Invalid Player handle.");
    if (volume > 1000)
        return set_error(LNPLAYER_RESULT_INVALID_ARGUMENT, "Volume must be between 0 and 1000.");

    player->volume.store(volume, std::memory_order_relaxed);
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
