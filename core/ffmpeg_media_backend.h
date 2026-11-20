#ifndef LNPLAYER_FFMPEG_MEDIA_BACKEND_H
#define LNPLAYER_FFMPEG_MEDIA_BACKEND_H

#include "media_backend.h"

namespace lnplayer
{

std::unique_ptr<MediaBackend> create_ffmpeg_media_backend();

}

#endif
