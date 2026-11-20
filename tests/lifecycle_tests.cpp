#include "lnplayer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{

struct CallbackState
{
    std::atomic<int> destroy_result{-1};
};

void event_callback(lnplayer_t* player, const lnplayer_event_t* event, void* user_data)
{
    if (event->type != LNPLAYER_EVENT_ERROR)
        return;

    auto* state = static_cast<CallbackState*>(user_data);
    state->destroy_result.store(
        static_cast<int>(lnplayer_destroy(player)),
        std::memory_order_release);
}

bool expect(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

bool test_callback_reentrant_destroy()
{
    CallbackState callback_state;
    lnplayer_config_t config{};
    config.struct_size = sizeof(config);
    config.event_callback = event_callback;
    config.user_data = &callback_state;

    lnplayer_t* player = nullptr;
    if (!expect(lnplayer_create(&config, &player) == LNPLAYER_RESULT_OK,
            "create Player for callback test"))
        return false;

    const lnplayer_result_t play_result = lnplayer_play(player);
    const int callback_result = callback_state.destroy_result.load(std::memory_order_acquire);
    const lnplayer_result_t destroy_result = lnplayer_destroy(player);

    return expect(play_result == LNPLAYER_RESULT_INVALID_STATE,
               "play without an open media source returns INVALID_STATE") &&
        expect(callback_result == LNPLAYER_RESULT_WOULD_DEADLOCK,
               "destroy from the event callback returns WOULD_DEADLOCK") &&
        expect(destroy_result == LNPLAYER_RESULT_OK,
               "destroy from the owning thread succeeds after callback returns");
}

bool test_concurrent_calls_and_destroy()
{
    lnplayer_t* player = nullptr;
    if (!expect(lnplayer_create(nullptr, &player) == LNPLAYER_RESULT_OK,
            "create Player for concurrent destroy test"))
        return false;

    std::atomic<bool> failed{false};
    std::vector<std::thread> callers;
    for (int index = 0; index < 4; ++index)
    {
        callers.emplace_back([player, &failed]
        {
            for (;;)
            {
                lnplayer_snapshot_t snapshot{};
                snapshot.struct_size = sizeof(snapshot);
                const lnplayer_result_t snapshot_result =
                    lnplayer_get_snapshot(player, &snapshot);
                if (snapshot_result == LNPLAYER_RESULT_INVALID_HANDLE)
                    break;
                if (snapshot_result != LNPLAYER_RESULT_OK)
                {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }

                const lnplayer_result_t volume_result = lnplayer_set_volume(player, 750);
                if (volume_result == LNPLAYER_RESULT_INVALID_HANDLE)
                    break;
                if (volume_result != LNPLAYER_RESULT_OK)
                {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const lnplayer_result_t destroy_result = lnplayer_destroy(player);
    for (std::thread& caller : callers)
        caller.join();

    return expect(destroy_result == LNPLAYER_RESULT_OK,
               "destroy waits for active API calls and succeeds") &&
        expect(!failed.load(std::memory_order_relaxed),
               "concurrent API calls return only success or invalid handle");
}

struct OpenCallbackState
{
    std::atomic<int> open_completed{0};
};

void open_event_callback(lnplayer_t*, const lnplayer_event_t* event, void* user_data)
{
    if (event->type == LNPLAYER_EVENT_OPEN_COMPLETED)
        static_cast<OpenCallbackState*>(user_data)->open_completed.fetch_add(
            1, std::memory_order_relaxed);
}

bool test_async_open_is_drained_before_destroy()
{
    OpenCallbackState callback_state;
    lnplayer_config_t config{};
    config.struct_size = sizeof(config);
    config.event_callback = open_event_callback;
    config.user_data = &callback_state;

    lnplayer_t* player = nullptr;
    if (!expect(lnplayer_create(&config, &player) == LNPLAYER_RESULT_OK,
            "create Player for pending-open shutdown test"))
        return false;

    uint64_t operation_id = 0;
    const lnplayer_result_t open_result =
        lnplayer_open(player, "LNPlayer_missing_test_media_file.mkv", &operation_id);
    const lnplayer_result_t destroy_result = lnplayer_destroy(player);

    return expect(open_result == LNPLAYER_RESULT_OK && operation_id != 0,
               "missing media open is accepted asynchronously") &&
        expect(destroy_result == LNPLAYER_RESULT_OK,
               "destroy completes while an async open was in flight") &&
        expect(callback_state.open_completed.load(std::memory_order_relaxed) == 1,
               "open completion callback is drained before Controller destruction");
}

bool test_create_after_destroy()
{
    lnplayer_t* player = nullptr;
    if (!expect(lnplayer_create(nullptr, &player) == LNPLAYER_RESULT_OK,
            "create Player after previous Player was destroyed"))
        return false;
    return expect(lnplayer_destroy(player) == LNPLAYER_RESULT_OK,
        "destroy replacement Player");
}

}

int main()
{
    if (!test_callback_reentrant_destroy())
        return 1;
    if (!test_concurrent_calls_and_destroy())
        return 1;
    if (!test_async_open_is_drained_before_destroy())
        return 1;
    if (!test_create_after_destroy())
        return 1;

    std::puts("LNPlayer lifecycle tests passed.");
    return 0;
}
