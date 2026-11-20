#include "lnplayer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <string>
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


void count_chapter(const lnplayer_chapter_info_t*, void* user_data)
{
    ++*static_cast<int*>(user_data);
}

bool test_video_metadata_api_contract()
{
    lnplayer_t* player = nullptr;
    if (!expect(lnplayer_create(nullptr, &player) == LNPLAYER_RESULT_OK,
            "create Player for video metadata API test"))
        return false;

    lnplayer_video_info_t info{};
    info.struct_size = sizeof(info);
    const lnplayer_result_t query_result = lnplayer_get_video_info(player, &info);
    const bool empty_metadata =
        info.width == 0 && info.height == 0 && info.bit_rate == 0 &&
        info.mastering_max_luminance == 0.0f &&
        info.max_content_light_level == 0;
    const lnplayer_result_t invalid_probe =
        lnplayer_probe_media(nullptr, nullptr, nullptr);
    int chapter_count = 0;
    const lnplayer_result_t chapters_result =
        lnplayer_enumerate_chapters(player, count_chapter, &chapter_count);
    const lnplayer_result_t invalid_chapters =
        lnplayer_enumerate_chapters(player, nullptr, nullptr);
    const lnplayer_result_t destroy_result = lnplayer_destroy(player);

    return expect(query_result == LNPLAYER_RESULT_OK,
               "video metadata query succeeds before media is opened") &&
        expect(empty_metadata,
               "video metadata query returns cleared fields without an open media") &&
        expect(invalid_probe == LNPLAYER_RESULT_INVALID_ARGUMENT,
               "media probe rejects invalid arguments") &&
        expect(chapters_result == LNPLAYER_RESULT_OK && chapter_count == 0,
               "chapter enumeration returns an empty list before media is opened") &&
        expect(invalid_chapters == LNPLAYER_RESULT_INVALID_ARGUMENT,
               "chapter enumeration rejects a null callback") &&
        expect(destroy_result == LNPLAYER_RESULT_OK,
               "destroy Player after video metadata API test");
}

bool test_last_error_buffer()
{
    const lnplayer_result_t invalid_create = lnplayer_create(nullptr, nullptr);
    size_t required_size = 0;
    const lnplayer_result_t size_result =
        lnplayer_get_last_error(nullptr, 0, &required_size);
    if (!expect(invalid_create == LNPLAYER_RESULT_INVALID_ARGUMENT,
            "invalid create arguments return INVALID_ARGUMENT") ||
        !expect(size_result == LNPLAYER_RESULT_BUFFER_TOO_SMALL && required_size > 1,
            "last-error query reports the required buffer size"))
        return false;

    std::vector<char> buffer(required_size);
    const lnplayer_result_t read_result =
        lnplayer_get_last_error(buffer.data(), buffer.size(), nullptr);
    return expect(read_result == LNPLAYER_RESULT_OK,
               "last-error text can be copied into the reported buffer") &&
        expect(std::string(buffer.data()).find("player output is null") != std::string::npos,
               "last-error text preserves the original diagnostic");
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
    if (!test_video_metadata_api_contract())
        return 1;
    if (!test_last_error_buffer())
        return 1;

    std::puts("LNPlayer lifecycle tests passed.");
    return 0;
}
