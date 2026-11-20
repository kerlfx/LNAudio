#ifndef LNPLAYER_D3D11_VIDEO_RENDERER_H
#define LNPLAYER_D3D11_VIDEO_RENDERER_H

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <memory>

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
    void resize(uint32_t width, uint32_t height);
    void set_stretch(bool enabled);
    bool present(const uint8_t* bgra, int width, int height, int stride);
    void clear();
    void shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif

#endif
