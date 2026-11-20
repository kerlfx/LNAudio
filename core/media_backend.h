#ifndef LNPLAYER_MEDIA_BACKEND_H
#define LNPLAYER_MEDIA_BACKEND_H

#include <cstdint>
#include <memory>
#include <string>

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
};

using MediaBackendFactory = std::unique_ptr<MediaBackend> (*)();

}

#endif
