#include "d3d11_video_renderer.h"

#ifdef _WIN32

#include <algorithm>
#include <cstring>
#include <iterator>
#include <mutex>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace lnplayer::windows
{
namespace
{

using Microsoft::WRL::ComPtr;

constexpr char kVertexShader[] = R"(
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertex_id : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    const float2 coordinates[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    VertexOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = coordinates[vertex_id];
    return output;
}
)";

constexpr char kPixelShader[] = R"(
Texture2D image_texture : register(t0);
SamplerState image_sampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return image_texture.Sample(image_sampler, uv);
}
)";

}

class D3D11VideoRenderer::Impl
{
public:
    bool initialize(HWND target)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_ && window_ == target)
            return true;

        release_resources();
        if (!target || !IsWindow(target))
            return false;

        RECT client{};
        if (!GetClientRect(target, &client))
            return false;

        const UINT width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

        DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
        swap_chain_desc.BufferDesc.Width = width;
        swap_chain_desc.BufferDesc.Height = height;
        swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
        swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
        swap_chain_desc.SampleDesc.Count = 1;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.BufferCount = 2;
        swap_chain_desc.OutputWindow = target;
        swap_chain_desc.Windowed = TRUE;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1
        };
        D3D_FEATURE_LEVEL selected_level{};
        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &swap_chain_desc,
            swap_chain_.GetAddressOf(),
            device_.GetAddressOf(),
            &selected_level,
            context_.GetAddressOf());

        if (result == E_INVALIDARG)
        {
            release_resources();
            const D3D_FEATURE_LEVEL compatible_levels[] = {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1
            };
            result = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                compatible_levels,
                static_cast<UINT>(std::size(compatible_levels)),
                D3D11_SDK_VERSION,
                &swap_chain_desc,
                swap_chain_.GetAddressOf(),
                device_.GetAddressOf(),
                &selected_level,
                context_.GetAddressOf());
        }

        if (FAILED(result))
        {
            release_resources();
            result = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &swap_chain_desc,
                swap_chain_.GetAddressOf(),
                device_.GetAddressOf(),
                &selected_level,
                context_.GetAddressOf());
        }

        if (FAILED(result))
        {
            release_resources();
            return false;
        }

        device_.As(&video_device_);
        context_.As(&video_context_);

        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(context_.As(&multithread)))
            multithread->SetMultithreadProtected(TRUE);

        window_ = target;
        if (!create_render_target() || !create_pipeline())
        {
            release_resources();
            return false;
        }
        return true;
    }

    void* native_device()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ID3D11Device* device = device_.Get();
        if (device)
            device->AddRef();
        return device;
    }

    void resize(uint32_t width, uint32_t height)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!swap_chain_ || width == 0 || height == 0)
            return;

        context_->OMSetRenderTargets(0, nullptr, nullptr);
        render_target_.Reset();
        const HRESULT resize_result = swap_chain_->ResizeBuffers(
            0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (SUCCEEDED(resize_result) && create_render_target())
            return;

        HWND target = window_;
        release_resources();
        lock.unlock();
        initialize(target);
    }

    void set_stretch(bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stretch_ = enabled;
    }

    void set_aspect_ratio(float ratio)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        aspect_ratio_ = ratio;
        stretch_ = false;
    }

    bool present(const uint8_t* pixels, int width, int height, int stride)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!pixels || width <= 0 || height <= 0 || stride < width * 4)
            return false;
        if (!swap_chain_ || !context_ || !render_target_)
        {
            HWND target = window_;
            lock.unlock();
            return target && initialize(target) && present(pixels, width, height, stride);
        }

        if (!video_texture_ || texture_width_ != width || texture_height_ != height)
        {
            video_view_.Reset();
            video_texture_.Reset();

            D3D11_TEXTURE2D_DESC texture_desc{};
            texture_desc.Width = static_cast<UINT>(width);
            texture_desc.Height = static_cast<UINT>(height);
            texture_desc.MipLevels = 1;
            texture_desc.ArraySize = 1;
            texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            texture_desc.SampleDesc.Count = 1;
            texture_desc.Usage = D3D11_USAGE_DYNAMIC;
            texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            if (FAILED(device_->CreateTexture2D(&texture_desc, nullptr, video_texture_.GetAddressOf())) ||
                FAILED(device_->CreateShaderResourceView(video_texture_.Get(), nullptr, video_view_.GetAddressOf())))
                return false;

            texture_width_ = width;
            texture_height_ = height;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(video_texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return false;

        for (int row = 0; row < height; ++row)
        {
            std::memcpy(
                static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
                pixels + static_cast<size_t>(row) * stride,
                static_cast<size_t>(width) * 4);
        }
        context_->Unmap(video_texture_.Get(), 0);

        RECT client{};
        GetClientRect(window_, &client);
        const float target_width = static_cast<float>(client.right - client.left);
        const float target_height = static_cast<float>(client.bottom - client.top);
        if (target_width <= 0.0f || target_height <= 0.0f)
            return true;

        const float display_aspect = aspect_ratio_ > 0.0f
            ? aspect_ratio_
            : static_cast<float>(width) / static_cast<float>(height);
        float viewport_width = target_width;
        float viewport_height = target_height;
        if (!stretch_)
        {
            if (target_width / target_height > display_aspect)
                viewport_width = target_height * display_aspect;
            else
                viewport_height = target_width / display_aspect;
        }

        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = (target_width - viewport_width) * 0.5f;
        viewport.TopLeftY = (target_height - viewport_height) * 0.5f;
        viewport.Width = viewport_width;
        viewport.Height = viewport_height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        const float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
        context_->ClearRenderTargetView(render_target_.Get(), black);
        context_->OMSetRenderTargets(1, render_target_.GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &viewport);
        context_->RSSetState(rasterizer_.Get());
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* views[] = {video_view_.Get()};
        context_->PSSetShaderResources(0, 1, views);
        ID3D11SamplerState* samplers[] = {sampler_.Get()};
        context_->PSSetSamplers(0, 1, samplers);
        context_->Draw(3, 0);

        ID3D11ShaderResourceView* empty_views[] = {nullptr};
        context_->PSSetShaderResources(0, 1, empty_views);
        const HRESULT present_result = swap_chain_->Present(1, 0);
        if (present_result == DXGI_ERROR_DEVICE_REMOVED ||
            present_result == DXGI_ERROR_DEVICE_RESET)
        {
            HWND target = window_;
            release_resources();
            lock.unlock();
            return target && initialize(target) && present(pixels, width, height, stride);
        }
        return SUCCEEDED(present_result);
    }

    bool present_hardware(
        void* native_texture,
        uint32_t array_slice,
        uint32_t format,
        int width,
        int height)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!native_texture || width <= 0 || height <= 0 ||
            !swap_chain_ || !device_ || !video_device_ || !video_context_)
            return false;

        RECT client{};
        if (!GetClientRect(window_, &client))
            return false;
        const UINT output_width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
        const UINT output_height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));
        const DXGI_FORMAT input_format = static_cast<DXGI_FORMAT>(format);
        if (input_format != DXGI_FORMAT_NV12 && input_format != DXGI_FORMAT_P010)
            return false;

        if (!video_processor_ || processor_input_width_ != static_cast<UINT>(width) ||
            processor_input_height_ != static_cast<UINT>(height) ||
            processor_output_width_ != output_width ||
            processor_output_height_ != output_height ||
            processor_format_ != input_format)
        {
            video_processor_.Reset();
            video_processor_enumerator_.Reset();

            D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
            content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            content.InputFrameRate = {60, 1};
            content.InputWidth = static_cast<UINT>(width);
            content.InputHeight = static_cast<UINT>(height);
            content.OutputFrameRate = {60, 1};
            content.OutputWidth = output_width;
            content.OutputHeight = output_height;
            content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

            if (FAILED(video_device_->CreateVideoProcessorEnumerator(
                    &content, video_processor_enumerator_.GetAddressOf())) ||
                FAILED(video_device_->CreateVideoProcessor(
                    video_processor_enumerator_.Get(), 0, video_processor_.GetAddressOf())))
                return false;

            processor_input_width_ = static_cast<UINT>(width);
            processor_input_height_ = static_cast<UINT>(height);
            processor_output_width_ = output_width;
            processor_output_height_ = output_height;
            processor_format_ = input_format;
        }

        ID3D11Texture2D* texture = static_cast<ID3D11Texture2D*>(native_texture);
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
        input_desc.FourCC = 0;
        input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        input_desc.Texture2D.MipSlice = 0;
        input_desc.Texture2D.ArraySlice = array_slice;

        ComPtr<ID3D11VideoProcessorInputView> input_view;
        if (FAILED(video_device_->CreateVideoProcessorInputView(
                texture,
                video_processor_enumerator_.Get(),
                &input_desc,
                input_view.GetAddressOf())))
            return false;

        ComPtr<ID3D11Texture2D> back_buffer;
        if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()))))
            return false;

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
        output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        output_desc.Texture2D.MipSlice = 0;
        ComPtr<ID3D11VideoProcessorOutputView> output_view;
        if (FAILED(video_device_->CreateVideoProcessorOutputView(
                back_buffer.Get(),
                video_processor_enumerator_.Get(),
                &output_desc,
                output_view.GetAddressOf())))
            return false;

        const float source_aspect = aspect_ratio_ > 0.0f
            ? aspect_ratio_
            : static_cast<float>(width) / static_cast<float>(height);
        float target_width = static_cast<float>(output_width);
        float target_height = static_cast<float>(output_height);
        if (!stretch_)
        {
            if (target_width / target_height > source_aspect)
                target_width = target_height * source_aspect;
            else
                target_height = target_width / source_aspect;
        }

        RECT target_rect{
            static_cast<LONG>((output_width - target_width) * 0.5f),
            static_cast<LONG>((output_height - target_height) * 0.5f),
            static_cast<LONG>((output_width + target_width) * 0.5f),
            static_cast<LONG>((output_height + target_height) * 0.5f)
        };

        const float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
        context_->ClearRenderTargetView(render_target_.Get(), black);
        video_context_->VideoProcessorSetOutputTargetRect(
            video_processor_.Get(), TRUE, &target_rect);
        video_context_->VideoProcessorSetStreamSourceRect(
            video_processor_.Get(), 0, TRUE, nullptr);

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input_view.Get();
        const HRESULT blt_result = video_context_->VideoProcessorBlt(
            video_processor_.Get(), output_view.Get(), 0, 1, &stream);
        if (FAILED(blt_result))
            return false;

        return SUCCEEDED(swap_chain_->Present(1, 0));
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!context_ || !render_target_ || !swap_chain_)
            return;
        const float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
        context_->ClearRenderTargetView(render_target_.Get(), black);
        swap_chain_->Present(1, 0);
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        release_resources();
    }

private:
    bool create_render_target()
    {
        ComPtr<ID3D11Texture2D> back_buffer;
        if (FAILED(swap_chain_->GetBuffer(
            0, IID_PPV_ARGS(back_buffer.GetAddressOf()))))
            return false;
        return SUCCEEDED(device_->CreateRenderTargetView(
            back_buffer.Get(), nullptr, render_target_.GetAddressOf()));
    }

    bool create_pipeline()
    {
        ComPtr<ID3DBlob> vertex_code;
        ComPtr<ID3DBlob> pixel_code;
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3DCompile(
            kVertexShader, sizeof(kVertexShader) - 1, nullptr, nullptr, nullptr,
            "main", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
            vertex_code.GetAddressOf(), errors.GetAddressOf())) ||
            FAILED(D3DCompile(
            kPixelShader, sizeof(kPixelShader) - 1, nullptr, nullptr, nullptr,
            "main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
            pixel_code.GetAddressOf(), errors.GetAddressOf())))
            return false;

        if (FAILED(device_->CreateVertexShader(
                vertex_code->GetBufferPointer(),
                vertex_code->GetBufferSize(),
                nullptr,
                vertex_shader_.GetAddressOf())) ||
            FAILED(device_->CreatePixelShader(
                pixel_code->GetBufferPointer(),
                pixel_code->GetBufferSize(),
                nullptr,
                pixel_shader_.GetAddressOf())))
            return false;

        D3D11_RASTERIZER_DESC rasterizer_desc{};
        rasterizer_desc.FillMode = D3D11_FILL_SOLID;
        rasterizer_desc.CullMode = D3D11_CULL_NONE;
        rasterizer_desc.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rasterizer_desc, rasterizer_.GetAddressOf())))
            return false;

        D3D11_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        return SUCCEEDED(device_->CreateSamplerState(&sampler_desc, sampler_.GetAddressOf()));
    }

    void release_resources()
    {
        if (context_)
            context_->ClearState();
        video_view_.Reset();
        video_texture_.Reset();
        video_processor_.Reset();
        video_processor_enumerator_.Reset();
        video_context_.Reset();
        video_device_.Reset();
        render_target_.Reset();
        sampler_.Reset();
        rasterizer_.Reset();
        pixel_shader_.Reset();
        vertex_shader_.Reset();
        swap_chain_.Reset();
        context_.Reset();
        device_.Reset();
        window_ = nullptr;
        texture_width_ = 0;
        texture_height_ = 0;
    }

    std::mutex mutex_;
    HWND window_ = nullptr;
    UINT texture_width_ = 0;
    UINT texture_height_ = 0;
    bool stretch_ = false;
    float aspect_ratio_ = 0.0f;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_device_;
    ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<ID3D11VideoProcessorEnumerator> video_processor_enumerator_;
    ComPtr<ID3D11VideoProcessor> video_processor_;
    UINT processor_input_width_ = 0;
    UINT processor_input_height_ = 0;
    UINT processor_output_width_ = 0;
    UINT processor_output_height_ = 0;
    DXGI_FORMAT processor_format_ = DXGI_FORMAT_UNKNOWN;
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11RenderTargetView> render_target_;
    ComPtr<ID3D11Texture2D> video_texture_;
    ComPtr<ID3D11ShaderResourceView> video_view_;
    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11SamplerState> sampler_;
};

D3D11VideoRenderer::D3D11VideoRenderer()
    : impl_(std::make_unique<Impl>())
{
}

D3D11VideoRenderer::~D3D11VideoRenderer()
{
    shutdown();
}

bool D3D11VideoRenderer::initialize(HWND window)
{
    return impl_->initialize(window);
}

void* D3D11VideoRenderer::native_device()
{
    return impl_->native_device();
}

void D3D11VideoRenderer::resize(uint32_t width, uint32_t height)
{
    impl_->resize(width, height);
}

void D3D11VideoRenderer::set_stretch(bool enabled)
{
    impl_->set_stretch(enabled);
}

void D3D11VideoRenderer::set_aspect_ratio(float ratio)
{
    impl_->set_aspect_ratio(ratio);
}

bool D3D11VideoRenderer::present(const uint8_t* bgra, int width, int height, int stride)
{
    return impl_->present(bgra, width, height, stride);
}

bool D3D11VideoRenderer::present_hardware(
    void* texture, uint32_t array_slice, uint32_t format, int width, int height)
{
    return impl_->present_hardware(texture, array_slice, format, width, height);
}

void D3D11VideoRenderer::clear()
{
    impl_->clear();
}

void D3D11VideoRenderer::shutdown()
{
    if (impl_)
        impl_->shutdown();
}

}

#endif
