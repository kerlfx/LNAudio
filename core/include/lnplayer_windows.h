#ifndef LNPLAYER_WINDOWS_H
#define LNPLAYER_WINDOWS_H

#include "lnplayer.h"

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lnplayer_windows_video_frame
{
    uint32_t struct_size;
    const uint8_t* bgra;
    int32_t width;
    int32_t height;
    int32_t stride;
    int64_t pts_ms;
    uint64_t generation;
    float display_aspect_ratio;
} lnplayer_windows_video_frame_t;

typedef void (*lnplayer_windows_video_frame_callback_t)(
    const lnplayer_windows_video_frame_t* frame,
    void* user_data);

/*
 * Attach the D3D11 device owned by the video renderer before opening media.
 * The function takes ownership of one COM reference supplied by the caller.
 */
LNPLAYER_API lnplayer_result_t lnplayer_windows_set_d3d11_device(
    lnplayer_t* player,
    void* d3d11_device);

/*
 * Windows video-output extension.
 * Decodes one frame synchronously on a Core worker. The callback executes
 * on the calling thread and the pixel buffer is valid only during callback.
 * Call from a dedicated background thread, never from the UI or event thread.
 */
LNPLAYER_API lnplayer_result_t lnplayer_windows_read_video_frame(
    lnplayer_t* player,
    lnplayer_windows_video_frame_callback_t callback,
    void* user_data,
    int* end_of_stream);

#ifdef __cplusplus
}
#endif

#endif
#endif
