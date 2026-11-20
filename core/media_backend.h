#ifndef LNPLAYER_MEDIA_BACKEND_H
#define LNPLAYER_MEDIA_BACKEND_H

#include <cstdint>
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
};

using MediaBackendFactory = std::unique_ptr<MediaBackend> (*)();

}

#endif
