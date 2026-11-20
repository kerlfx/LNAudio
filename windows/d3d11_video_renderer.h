#ifndef LNPLAYER_D3D11_VIDEO_RENDERER_H
#define LNPLAYER_D3D11_VIDEO_RENDERER_H

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace lnplayer::windows
{

class D3D11VideoRenderer
{
public:
    D3D11VideoRenderer();
    ~D3D11VideoRenderer();

    D3D11VideoRenderer(const D3D11VideoRenderer&) = delete;
    D3D11VideoRenderer& operator=(const D3D11VideoRenderer&) = delete;

    bool initialize(HWND window);
    void* native_device();
    void resize(uint32_t width, uint32_t height);
    void set_stretch(bool enabled);
    void set_aspect_ratio(float ratio);
    void set_video_filter(
        float brightness, float contrast, float saturation, float hue_degrees,
        float sharpness, float denoise, bool grayscale);
    void set_hdr_metadata(int transfer, int color_primaries, float peak_luminance);
    void set_video_color_space(int ycbcr_matrix, int full_range);
    bool present(const uint8_t* bgra, int width, int height, int stride);
    bool present_hardware(
        void* texture, uint32_t array_slice, uint32_t format,
        int width, int height, std::shared_ptr<void> frame_owner,
        const uint8_t* overlay, int overlay_stride);
    bool capture_bgra(std::vector<uint8_t>& pixels, int& width, int& height);
    void clear();
    void shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif

#endif
