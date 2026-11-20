#ifndef LNPLAYER_MEDIA_BACKEND_H
#define LNPLAYER_MEDIA_BACKEND_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lnplayer
{

struct MediaInfo
{
    int64_t duration_ms = -1;
    bool has_audio = false;
    bool has_video = false;
};

struct MediaBackendResult
{
    bool success = false;
    std::string message;
};

struct AudioFrame
{
    std::vector<float> samples;
    int sample_rate = 48000;
    int channels = 2;
    int64_t pts_ms = 0;
};

struct VideoFrame
{
    std::vector<uint8_t> bgra;
    int width = 0;
    int height = 0;
    int stride = 0;
    int64_t pts_ms = 0;
    uint64_t generation = 0;
};

class MediaBackend
{
public:
    virtual ~MediaBackend() = default;

    virtual MediaBackendResult open(const std::string& source, MediaInfo& info) = 0;
    virtual MediaBackendResult play() = 0;
    virtual MediaBackendResult pause() = 0;
    virtual MediaBackendResult seek(int64_t position_ms) = 0;
    virtual MediaBackendResult stop() = 0;
    virtual MediaBackendResult close() = 0;
    virtual MediaBackendResult read_audio_frame(AudioFrame& frame, bool& end_of_stream) = 0;
    virtual MediaBackendResult read_video_frame(VideoFrame& frame, bool& end_of_stream) = 0;
    virtual int64_t position_ms() const = 0;
    virtual bool playback_ended() const = 0;
    virtual void set_volume(float volume) = 0;
    virtual void set_playback_rate(float rate) = 0;
    virtual void set_playback_end_callback(std::function<void()> callback) = 0;
    virtual bool playback_failed() const = 0;
    virtual void set_playback_error_callback(std::function<void(std::string)> callback) = 0;
};

using MediaBackendFactory = std::unique_ptr<MediaBackend> (*)();

}

#endif
