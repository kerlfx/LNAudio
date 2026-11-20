#ifndef LNPLAYER_FFMPEG_MEDIA_BACKEND_H
#define LNPLAYER_FFMPEG_MEDIA_BACKEND_H

#include "media_backend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lnplayer
{

std::unique_ptr<MediaBackend> create_ffmpeg_media_backend();

bool convert_d3d11_frame_to_bgra(
    const std::shared_ptr<void>& frame_owner,
    std::vector<uint8_t>& pixels,
    int& width,
    int& height,
    int& stride,
    float& display_aspect_ratio,
    std::string& error);

}

#endif
