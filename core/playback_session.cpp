#include "playback_session.h"

#include <utility>

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace lnplayer
{

PlaybackSession::PlaybackSession(MediaBackendFactory backend_factory)
    : backend_factory_(backend_factory)
{
}

PlaybackSession::~PlaybackSession()
{
#ifdef _WIN32
    if (video_device_)
        static_cast<ID3D11Device*>(video_device_)->Release();
#endif
}

#ifdef _WIN32
SessionResult PlaybackSession::set_video_device(void* device)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (device == video_device_)
    {
        if (device)
            static_cast<ID3D11Device*>(device)->Release();
        return make_result(true);
    }

    const SessionState previous_state = snapshot_.state;
    const int64_t previous_position = backend_
        ? backend_->position_ms()
        : snapshot_.position_ms;

    if (backend_)
    {
        const MediaBackendResult close_result = backend_->close();
        if (!close_result.success)
        {
            if (device)
                static_cast<ID3D11Device*>(device)->Release();
            return make_result(false, close_result.message);
        }
        backend_.reset();
    }

    if (video_device_)
        static_cast<ID3D11Device*>(video_device_)->Release();
    video_device_ = device;
    ++snapshot_.generation;

    if (source_.empty() || !backend_factory_)
        return make_result(true);

    backend_ = backend_factory_();
    if (!backend_)
    {
        snapshot_.state = SessionState::Error;
        return make_result(false, "Unable to recreate media backend after D3D11 device change.");
    }

    backend_->set_video_device(video_device_);
    backend_->set_volume(volume_);
    backend_->set_playback_rate(playback_rate_);
    backend_->set_playback_end_callback(playback_end_callback_);
    backend_->set_playback_error_callback(playback_error_callback_);

    MediaInfo info;
    MediaBackendResult result = backend_->open(source_, info);
    if (!result.success)
    {
        backend_.reset();
        snapshot_.state = SessionState::Error;
        return make_result(false, "Unable to reopen media after D3D11 device change: " + result.message);
    }

    snapshot_.duration_ms = info.duration_ms;
    snapshot_.has_audio = info.has_audio;
    snapshot_.has_video = info.has_video;

    if (previous_position > 0)
    {
        result = backend_->seek(previous_position);
        if (!result.success)
        {
            backend_->close();
            backend_.reset();
            snapshot_.state = SessionState::Error;
            return make_result(false, "Unable to restore playback position after D3D11 device change: " + result.message);
        }
    }

    snapshot_.position_ms = previous_position;
    if (previous_state == SessionState::Playing || previous_state == SessionState::Paused)
    {
        result = backend_->play();
        if (!result.success)
        {
            backend_->close();
            backend_.reset();
            snapshot_.state = SessionState::Error;
            return make_result(false, "Unable to resume playback after D3D11 device change: " + result.message);
        }
        if (previous_state == SessionState::Paused)
        {
            result = backend_->pause();
            if (!result.success)
            {
                backend_->close();
                backend_.reset();
                snapshot_.state = SessionState::Error;
                return make_result(false, "Unable to restore paused state after D3D11 device change: " + result.message);
            }
        }
        snapshot_.state = previous_state;
    }
    else
    {
        snapshot_.state = previous_state == SessionState::Ended
            ? SessionState::Ready
            : previous_state;
    }

    return make_result(true);
}
#endif

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

    source_.clear();

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

#ifdef _WIN32
    backend_->set_video_device(video_device_);
#endif
    backend_->set_volume(volume_);
    backend_->set_playback_rate(playback_rate_);
    backend_->set_playback_end_callback(playback_end_callback_);
    backend_->set_playback_error_callback(playback_error_callback_);

    MediaInfo info;
    const MediaBackendResult result = backend_->open(source, info);
    if (!result.success)
    {
        backend_.reset();
        snapshot_.state = SessionState::Error;
        return make_result(false, result.message);
    }

    source_ = source;
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
        ++snapshot_.generation;
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
    ++snapshot_.generation;
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
    ++snapshot_.generation;
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
    source_.clear();
    reset_media_state();
    return make_result(true);
}

MediaBackendResult PlaybackSession::read_video_frame(VideoFrame& frame, bool& end_of_stream)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!backend_)
        return MediaBackendResult{false, "No media source is open."};
    const MediaBackendResult result = backend_->read_video_frame(frame, end_of_stream);
    if (result.success && !end_of_stream)
        frame.generation = snapshot_.generation;
    return result;
}

std::vector<MediaTrackInfo> PlaybackSession::audio_tracks() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return backend_ ? backend_->audio_tracks() : std::vector<MediaTrackInfo>{};
}

SessionResult PlaybackSession::select_audio_track(int stream_index)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!backend_ ||
        (snapshot_.state != SessionState::Ready &&
         snapshot_.state != SessionState::Paused))
        return make_result(false, "Audio tracks can only be switched while playback is paused.");

    const MediaBackendResult result = backend_->select_audio_track(stream_index);
    if (!result.success)
        return make_result(false, result.message);

    snapshot_.position_ms = backend_->position_ms();
    ++snapshot_.generation;
    return make_result(true);
}

std::vector<MediaSubtitleTrackInfo> PlaybackSession::subtitle_tracks() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return backend_ ? backend_->subtitle_tracks() : std::vector<MediaSubtitleTrackInfo>{};
}

SessionResult PlaybackSession::select_subtitle_track(int stream_index)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!backend_ ||
        (snapshot_.state != SessionState::Ready &&
         snapshot_.state != SessionState::Paused))
        return make_result(false, "Subtitle tracks can only be switched while playback is paused.");

    const MediaBackendResult result = backend_->select_subtitle_track(stream_index);
    if (!result.success)
        return make_result(false, result.message);

    snapshot_.position_ms = backend_->position_ms();
    ++snapshot_.generation;
    return make_result(true);
}

void PlaybackSession::set_subtitle_offset_ms(int64_t offset_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (backend_)
        backend_->set_subtitle_offset_ms(offset_ms);
}

void PlaybackSession::set_volume(float volume)
{
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = volume;
    if (backend_)
        backend_->set_volume(volume_);
}

void PlaybackSession::set_playback_rate(float rate)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playback_rate_ = rate;
    if (backend_)
        backend_->set_playback_rate(playback_rate_);
}

void PlaybackSession::set_playback_end_callback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playback_end_callback_ = std::move(callback);
    if (backend_)
        backend_->set_playback_end_callback(playback_end_callback_);
}

void PlaybackSession::set_playback_error_callback(std::function<void(std::string)> callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playback_error_callback_ = std::move(callback);
    if (backend_)
        backend_->set_playback_error_callback(playback_error_callback_);
}

SessionSnapshot PlaybackSession::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    SessionSnapshot current = snapshot_;
    if (backend_)
    {
        current.position_ms = backend_->position_ms();
        if (backend_->playback_failed())
            current.state = SessionState::Error;
        else if (backend_->playback_ended())
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
