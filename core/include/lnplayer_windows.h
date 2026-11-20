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
    void* d3d11_texture;
    uint32_t d3d11_array_slice;
    uint32_t dxgi_format;
    const void* frame_owner;
    const char* subtitle_utf8;
    const uint8_t* subtitle_bgra;
    int32_t subtitle_width;
    int32_t subtitle_height;
    int32_t subtitle_stride;
} lnplayer_windows_video_frame_t;

typedef void (*lnplayer_windows_video_frame_callback_t)(
    const lnplayer_windows_video_frame_t* frame,
    void* user_data);

/*
 * Attach, replace, or detach the D3D11 device owned by the video renderer.
 * Passing null detaches the renderer device and forces software video decoding.
 * Replacing or detaching the device rebuilds the active media backend and
 * restores its playback position and playing/paused state. A non-null device
 * transfers one owned COM reference from the caller.
 */
LNPLAYER_API lnplayer_result_t lnplayer_windows_set_d3d11_device(
    lnplayer_t* player,
    void* d3d11_device);

/*
 * Windows video-output extension.
 * Decodes one frame synchronously on a Core worker. The callback executes
 * on the calling thread. BGRA pixels are valid only during callback. A D3D11
 * frame can be retained with lnplayer_windows_retain_video_frame() and must be
 * released with lnplayer_windows_release_video_frame(). Call from a dedicated
 * background thread, never from the UI or event thread.
 */
LNPLAYER_API lnplayer_result_t lnplayer_windows_retain_video_frame(
    const lnplayer_windows_video_frame_t* frame,
    void** retained_owner);

LNPLAYER_API void lnplayer_windows_release_video_frame(void* retained_owner);

LNPLAYER_API lnplayer_result_t lnplayer_windows_convert_video_frame(
    void* retained_owner,
    uint64_t generation,
    int64_t pts_ms,
    lnplayer_windows_video_frame_callback_t callback,
    void* user_data);

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
