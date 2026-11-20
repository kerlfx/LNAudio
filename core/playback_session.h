#ifndef LNPLAYER_PLAYBACK_SESSION_H
#define LNPLAYER_PLAYBACK_SESSION_H

#include "media_backend.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace lnplayer
{

enum class SessionState
{
    Empty,
    Opening,
    Ready,
    Playing,
    Paused,
    Ended,
    Error,
    Closing
};

struct SessionSnapshot
{
    SessionState state = SessionState::Empty;
    int64_t position_ms = 0;
    int64_t duration_ms = -1;
    uint64_t generation = 0;
    bool has_audio = false;
    bool has_video = false;
};

struct SessionResult
{
    bool success = false;
    std::string message;
    SessionSnapshot snapshot;
};

class PlaybackSession
{
public:
    explicit PlaybackSession(MediaBackendFactory backend_factory);

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    SessionResult open(const std::string& source);
#ifdef _WIN32
    void set_video_device(void* device);
#endif
    SessionResult play();
    SessionResult pause();
    SessionResult seek(int64_t position_ms);
    SessionResult stop();
    SessionResult close();
    MediaBackendResult read_video_frame(VideoFrame& frame, bool& end_of_stream);
    void set_volume(float volume);
    void set_playback_rate(float rate);
    void set_playback_end_callback(std::function<void()> callback);
    void set_playback_error_callback(std::function<void(std::string)> callback);

    SessionSnapshot snapshot() const;

private:
    SessionResult make_result(bool success, std::string message = {}) const;
    void reset_media_state();

    MediaBackendFactory backend_factory_;
    mutable std::mutex mutex_;
    std::unique_ptr<MediaBackend> backend_;
    SessionSnapshot snapshot_;
    float volume_ = 1.0f;
    float playback_rate_ = 1.0f;
    std::function<void()> playback_end_callback_;
    std::function<void(std::string)> playback_error_callback_;
};

}

#endif
