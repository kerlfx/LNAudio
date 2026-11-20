#include "playback_session.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

namespace
{

struct BlockingState
{
    std::mutex mutex;
    std::condition_variable condition;
    bool read_started = false;
    bool release_read = false;
};

std::shared_ptr<BlockingState> backend_state;

class BlockingBackend final : public lnplayer::MediaBackend
{
public:
    explicit BlockingBackend(std::shared_ptr<BlockingState> state)
        : state_(std::move(state))
    {
    }

#ifdef _WIN32
    void set_video_device(void*) override {}
#endif

    lnplayer::MediaBackendResult open(const std::string&, lnplayer::MediaInfo& info) override
    {
        info.duration_ms = 10000;
        info.has_video = true;
        return {true, {}};
    }

    lnplayer::MediaBackendResult play() override { return {true, {}}; }
    lnplayer::MediaBackendResult pause() override { return {true, {}}; }
    lnplayer::MediaBackendResult seek(int64_t position_ms) override
    {
        position_ms_ = position_ms;
        return {true, {}};
    }
    lnplayer::MediaBackendResult stop() override
    {
        position_ms_ = 0;
        return {true, {}};
    }
    lnplayer::MediaBackendResult close() override { return {true, {}}; }

    lnplayer::MediaBackendResult read_audio_frame(
        lnplayer::AudioFrame&, bool& end_of_stream) override
    {
        end_of_stream = false;
        return {true, {}};
    }

    lnplayer::MediaBackendResult read_video_frame(
        lnplayer::VideoFrame& frame, bool& end_of_stream) override
    {
        end_of_stream = false;
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->read_started = true;
            state_->condition.notify_all();
            state_->condition.wait(lock, [this]
            {
                return state_->release_read;
            });
        }
        frame.width = 1;
        frame.height = 1;
        frame.stride = 4;
        frame.bgra = {0, 0, 0, 255};
        return {true, {}};
    }

    int64_t position_ms() const override { return position_ms_; }
    bool playback_ended() const override { return false; }
    void set_volume(float) override {}
    void set_playback_rate(float) override {}
    void set_playback_end_callback(std::function<void()>) override {}
    bool playback_failed() const override { return false; }
    void set_playback_error_callback(std::function<void(std::string)>) override {}

private:
    std::shared_ptr<BlockingState> state_;
    int64_t position_ms_ = 0;
};

std::unique_ptr<lnplayer::MediaBackend> create_blocking_backend()
{
    return std::make_unique<BlockingBackend>(backend_state);
}

bool expect(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

bool wait_for_read(const std::shared_ptr<BlockingState>& state)
{
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->condition.wait_for(lock, std::chrono::seconds(2), [&]
    {
        return state->read_started;
    });
}

void release_read(const std::shared_ptr<BlockingState>& state)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    state->release_read = true;
    state->condition.notify_all();
}

bool test_commands_run_during_frame_decode()
{
    backend_state = std::make_shared<BlockingState>();
    lnplayer::PlaybackSession session(create_blocking_backend);
    if (!expect(session.open("blocking-test").success, "open fake media backend"))
        return false;
    if (!expect(session.play().success, "start fake media backend"))
        return false;

    lnplayer::VideoFrame frame;
    bool end_of_stream = false;
    lnplayer::MediaBackendResult read_result;
    std::thread reader([&]
    {
        read_result = session.read_video_frame(frame, end_of_stream);
    });
    if (!expect(wait_for_read(backend_state), "frame read enters blocking backend"))
    {
        release_read(backend_state);
        reader.join();
        return false;
    }

    std::atomic<bool> pause_completed{false};
    std::thread pauser([&]
    {
        pause_completed.store(session.pause().success, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool pause_was_not_blocked = pause_completed.load(std::memory_order_acquire);
    release_read(backend_state);
    pauser.join();
    reader.join();

    const bool result = expect(pause_was_not_blocked,
            "pause completes while video frame decoding is in progress") &&
        expect(read_result.success && !frame.bgra.empty(),
            "frame read completes successfully after pause");
    session.close();
    backend_state.reset();
    return result;
}

bool test_seek_invalidates_inflight_frame()
{
    backend_state = std::make_shared<BlockingState>();
    lnplayer::PlaybackSession session(create_blocking_backend);
    if (!expect(session.open("blocking-test").success, "open fake media backend for seek"))
        return false;

    lnplayer::VideoFrame frame;
    bool end_of_stream = false;
    lnplayer::MediaBackendResult read_result;
    std::thread reader([&]
    {
        read_result = session.read_video_frame(frame, end_of_stream);
    });
    if (!expect(wait_for_read(backend_state), "frame read enters backend before seek"))
    {
        release_read(backend_state);
        reader.join();
        return false;
    }

    std::atomic<bool> seek_completed{false};
    std::thread seeker([&]
    {
        seek_completed.store(session.seek(5000).success, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool seek_was_not_blocked = seek_completed.load(std::memory_order_acquire);
    release_read(backend_state);
    seeker.join();
    reader.join();

    const uint64_t current_generation = session.snapshot().generation;
    const bool result = expect(seek_was_not_blocked,
            "seek can update session state while frame decode is in progress") &&
        expect(read_result.success && frame.bgra.empty(),
            "frame decoded across a generation change is discarded") &&
        expect(frame.generation == current_generation,
            "discard marker carries the current media generation");
    session.close();
    backend_state.reset();
    return result;
}

}

int main()
{
    if (!test_commands_run_during_frame_decode())
        return 1;
    if (!test_seek_invalidates_inflight_frame())
        return 1;
    std::puts("LNPlayer session concurrency tests passed.");
    return 0;
}
