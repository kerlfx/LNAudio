#include "playback_session.h"

#include <utility>

namespace lnplayer
{

PlaybackSession::PlaybackSession(MediaBackendFactory backend_factory)
    : backend_factory_(backend_factory)
{
}

SessionResult PlaybackSession::open(const std::string& source)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (source.empty())
        return make_result(false, "Media source is empty.");

    snapshot_.state = SessionState::Opening;
    ++snapshot_.generation;
    snapshot_.position_ms = 0;
    snapshot_.duration_ms = -1;
    snapshot_.has_audio = false;
    snapshot_.has_video = false;

    if (backend_)
    {
        const MediaBackendResult close_result = backend_->close();
        if (!close_result.success)
        {
            snapshot_.state = SessionState::Error;
            return make_result(false, close_result.message);
        }
        backend_.reset();
    }

    if (!backend_factory_)
    {
        snapshot_.state = SessionState::Error;
        return make_result(false, "No media backend is configured.");
    }

    backend_ = backend_factory_();
    if (!backend_)
    {
        snapshot_.state = SessionState::Error;
        return make_result(false, "Unable to create media backend.");
    }

    backend_->set_volume(volume_);

    MediaInfo info;
    const MediaBackendResult result = backend_->open(source, info);
    if (!result.success)
    {
        backend_.reset();
        snapshot_.state = SessionState::Error;
        return make_result(false, result.message);
    }

    snapshot_.state = SessionState::Ready;
    snapshot_.duration_ms = info.duration_ms;
    snapshot_.has_audio = info.has_audio;
    snapshot_.has_video = info.has_video;
    return make_result(true);
}

SessionResult PlaybackSession::play()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (backend_ && backend_->playback_ended())
        snapshot_.state = SessionState::Ended;

    if (!backend_ ||
        (snapshot_.state != SessionState::Ready &&
         snapshot_.state != SessionState::Paused &&
         snapshot_.state != SessionState::Ended))
        return make_result(false, "Session is not ready to play.");

    if (snapshot_.state == SessionState::Ended)
    {
        const MediaBackendResult seek_result = backend_->seek(0);
        if (!seek_result.success)
            return make_result(false, seek_result.message);
        snapshot_.position_ms = 0;
    }

    const MediaBackendResult result = backend_->play();
    if (!result.success)
        return make_result(false, result.message);

    snapshot_.state = SessionState::Playing;
    return make_result(true);
}

SessionResult PlaybackSession::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (backend_ && backend_->playback_ended())
        snapshot_.state = SessionState::Ended;

    if (!backend_ || snapshot_.state != SessionState::Playing)
        return make_result(false, "Session is not playing.");

    const MediaBackendResult result = backend_->pause();
    if (!result.success)
        return make_result(false, result.message);

    snapshot_.state = SessionState::Paused;
    return make_result(true);
}

SessionResult PlaybackSession::seek(int64_t position_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!backend_ || position_ms < 0 ||
        (snapshot_.state != SessionState::Ready &&
         snapshot_.state != SessionState::Playing &&
         snapshot_.state != SessionState::Paused))
        return make_result(false, "Session cannot seek in its current state.");

    const MediaBackendResult result = backend_->seek(position_ms);
    if (!result.success)
        return make_result(false, result.message);

    snapshot_.position_ms = position_ms;
    return make_result(true);
}

SessionResult PlaybackSession::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!backend_)
    {
        reset_media_state();
        return make_result(true);
    }

    const MediaBackendResult result = backend_->stop();
    if (!result.success)
        return make_result(false, result.message);

    snapshot_.state = SessionState::Ready;
    snapshot_.position_ms = 0;
    return make_result(true);
}

SessionResult PlaybackSession::close()
{
    std::lock_guard<std::mutex> lock(mutex_);

    snapshot_.state = SessionState::Closing;
    if (backend_)
    {
        const MediaBackendResult result = backend_->close();
        if (!result.success)
        {
            snapshot_.state = SessionState::Error;
            return make_result(false, result.message);
        }
        backend_.reset();
    }

    ++snapshot_.generation;
    reset_media_state();
    return make_result(true);
}

void PlaybackSession::set_volume(float volume)
{
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = volume;
    if (backend_)
        backend_->set_volume(volume_);
}

SessionSnapshot PlaybackSession::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    SessionSnapshot current = snapshot_;
    if (backend_)
    {
        current.position_ms = backend_->position_ms();
        if (backend_->playback_ended())
            current.state = SessionState::Ended;
    }
    return current;
}

SessionResult PlaybackSession::make_result(bool success, std::string message) const
{
    return SessionResult{success, std::move(message), snapshot_};
}

void PlaybackSession::reset_media_state()
{
    snapshot_.state = SessionState::Empty;
    snapshot_.position_ms = 0;
    snapshot_.duration_ms = -1;
    snapshot_.has_audio = false;
    snapshot_.has_video = false;
}

}
