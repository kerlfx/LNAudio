#ifndef LNPLAYER_PLAYBACK_SESSION_H
#define LNPLAYER_PLAYBACK_SESSION_H

#include "media_backend.h"

#include <cstdint>
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
    SessionResult play();
    SessionResult pause();
    SessionResult seek(int64_t position_ms);
    SessionResult stop();
    SessionResult close();
    void set_volume(float volume);

    SessionSnapshot snapshot() const;

private:
    SessionResult make_result(bool success, std::string message = {}) const;
    void reset_media_state();

    MediaBackendFactory backend_factory_;
    mutable std::mutex mutex_;
    std::unique_ptr<MediaBackend> backend_;
    SessionSnapshot snapshot_;
};

}

#endif
