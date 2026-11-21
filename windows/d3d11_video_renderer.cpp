#include "d3d11_video_renderer.h"

#ifdef _WIN32

#include <algorithm>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <utility>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>
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

cbuffer VideoFilterSettings : register(b0)
{
    float brightness;
    float contrast;
    float saturation;
    float hue_degrees;
    float sharpness;
    float denoise;
    float2 texel_size;
    float hdr_transfer;
    float color_primaries;
    float hdr_peak_luminance;
    float padding;
    float zoom;
    float3 padding2;
};

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float2 zoomed_uv = (uv - 0.5) / zoom + 0.5;
    float4 sampled = image_texture.Sample(image_sampler, zoomed_uv);
    float3 rgb = sampled.rgb;
    if (hdr_transfer > 0.5)
    {
        float3 linear_rgb;
        if (hdr_transfer < 1.5)
        {
            const float m1 = 0.1593017578;
            const float m2 = 78.84375;
            const float c1 = 0.8359375;
            const float c2 = 18.8515625;
            const float c3 = 18.6875;
            float3 p = pow(saturate(rgb), 1.0 / m2);
            linear_rgb = pow(
                max(p - c1, 0.0) / max(c2 - c3 * p, 0.000001),
                1.0 / m1) * 10000.0;
        }
        else
        {
            const float a = 0.17883277;
            const float b = 1.0 - 4.0 * a;
            const float c = 0.5 - a * log(4.0 * a);
            float3 scene = lerp(
                rgb * rgb / 3.0,
                (exp((rgb - c) / a) + b) / 12.0,
                step(0.5, rgb));
            linear_rgb = pow(max(scene, 0.0), 1.2) * max(hdr_peak_luminance, 100.0);
        }

        if (color_primaries > 0.5)
        {
            linear_rgb = float3(
                1.6605 * linear_rgb.r - 0.5876 * linear_rgb.g - 0.0728 * linear_rgb.b,
                -0.1246 * linear_rgb.r + 1.1329 * linear_rgb.g - 0.0083 * linear_rgb.b,
                -0.0182 * linear_rgb.r - 0.1006 * linear_rgb.g + 1.1187 * linear_rgb.b);
        }
        const float white_point = max(hdr_peak_luminance / 100.0, 1.0);
        float3 relative_luminance = max(linear_rgb, 0.0) / 100.0;
        float3 mapped = relative_luminance *
            (1.0 + relative_luminance / (white_point * white_point)) /
            (1.0 + relative_luminance);
        rgb = pow(saturate(mapped), 1.0 / 2.2);
    }
    else if (color_primaries > 0.5)
    {
        float3 linear_rgb = pow(saturate(rgb), 2.2);
        linear_rgb = float3(
            1.6605 * linear_rgb.r - 0.5876 * linear_rgb.g - 0.0728 * linear_rgb.b,
            -0.1246 * linear_rgb.r + 1.1329 * linear_rgb.g - 0.0083 * linear_rgb.b,
            -0.0182 * linear_rgb.r - 0.1006 * linear_rgb.g + 1.1187 * linear_rgb.b);
        rgb = pow(saturate(linear_rgb), 1.0 / 2.2);
    }

    if (sharpness > 0.0 || denoise > 0.0)
    {
        float3 neighbors =
            image_texture.Sample(image_sampler, uv + float2(texel_size.x, 0.0)).rgb +
            image_texture.Sample(image_sampler, uv - float2(texel_size.x, 0.0)).rgb +
            image_texture.Sample(image_sampler, uv + float2(0.0, texel_size.y)).rgb +
            image_texture.Sample(image_sampler, uv - float2(0.0, texel_size.y)).rgb;
        neighbors *= 0.25;
        rgb = lerp(rgb, neighbors, denoise);
        rgb += sharpness * (rgb - neighbors);
    }

    rgb = (rgb - 0.5) * contrast + 0.5 + brightness;
    float y = dot(rgb, float3(0.299, 0.587, 0.114));
    float i = dot(rgb, float3(0.596, -0.274, -0.322)) * saturation;
    float q = dot(rgb, float3(0.211, -0.523, 0.312)) * saturation;
    float angle = hue_degrees * 0.01745329252;
    float sine = sin(angle);
    float cosine = cos(angle);
    float rotated_i = i * cosine - q * sine;
    float rotated_q = i * sine + q * cosine;
    float3 adjusted = float3(
        y + 0.956 * rotated_i + 0.621 * rotated_q,
        y - 0.272 * rotated_i - 0.647 * rotated_q,
        y - 1.106 * rotated_i + 1.703 * rotated_q);
    return float4(saturate(adjusted), sampled.a);
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
        if (!swap_chain_ || !window_)
            return;

        RECT client{};
        if (!GetClientRect(window_, &client))
            return;
        width = static_cast<uint32_t>(std::max<LONG>(0, client.right - client.left));
        height = static_cast<uint32_t>(std::max<LONG>(0, client.bottom - client.top));
        if (width == 0 || height == 0)
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

    void set_zoom(float zoom)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        zoom_ = std::clamp(zoom, 1.0f, 3.0f);
    }

    void set_video_filter(
        float brightness, float contrast, float saturation, float hue_degrees,
        float sharpness, float denoise, bool grayscale)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        brightness_ = brightness;
        contrast_ = contrast;
        saturation_ = grayscale ? 0.0f : saturation;
        hue_degrees_ = hue_degrees;
        sharpness_ = sharpness;
        denoise_ = denoise;
        grayscale_ = grayscale;
    }

    void set_hdr_metadata(int transfer, int color_primaries, float peak_luminance)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hdr_transfer_ = transfer;
        color_primaries_ = color_primaries;
        hdr_peak_luminance_ = peak_luminance > 0.0f ? peak_luminance : 1000.0f;
    }

    void set_video_color_space(int ycbcr_matrix, int full_range)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ycbcr_matrix_ = ycbcr_matrix;
        ycbcr_full_range_ = full_range;
    }

    bool present(const uint8_t* pixels, int width, int height, int stride)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!pixels || width <= 0 || height <= 0 || stride < width * 4)
            return false;
        hardware_output_texture_.Reset();
        hardware_output_render_target_.Reset();
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
        update_filter_constants(width, height);
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
        int height,
        std::shared_ptr<void> frame_owner,
        const uint8_t* overlay,
        int overlay_stride)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!native_texture || width <= 0 || height <= 0 ||
            !frame_owner || !swap_chain_ || !device_ || !video_device_ || !video_context_)
            return false;
        if (brightness_ != 0.0f || contrast_ != 1.0f || saturation_ != 1.0f ||
            hue_degrees_ != 0.0f || sharpness_ != 0.0f || denoise_ != 0.0f ||
            grayscale_ || hdr_transfer_ != 0 || color_primaries_ != 0 ||
            ycbcr_matrix_ > 1)
            return false;

        reap_completed_hardware_frames();
        if (pending_hardware_frames_.size() >= 4)
            return true;

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
                    &content, video_processor_enumerator_.GetAddressOf())))
                return false;

            UINT input_support = 0;
            UINT output_support = 0;
            if (FAILED(video_processor_enumerator_->CheckVideoProcessorFormat(
                    input_format, &input_support)) ||
                !(input_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) ||
                FAILED(video_processor_enumerator_->CheckVideoProcessorFormat(
                    DXGI_FORMAT_B8G8R8A8_UNORM, &output_support)) ||
                !(output_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) ||
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

        D3D11_QUERY_DESC query_desc{};
        query_desc.Query = D3D11_QUERY_EVENT;
        ComPtr<ID3D11Query> completion_query;
        if (FAILED(device_->CreateQuery(&query_desc, completion_query.GetAddressOf())))
            return false;

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

        if (!hardware_output_texture_ ||
            hardware_output_width_ != output_width ||
            hardware_output_height_ != output_height)
        {
            hardware_output_texture_.Reset();
            D3D11_TEXTURE2D_DESC output_texture_desc{};
            output_texture_desc.Width = output_width;
            output_texture_desc.Height = output_height;
            output_texture_desc.MipLevels = 1;
            output_texture_desc.ArraySize = 1;
            output_texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            output_texture_desc.SampleDesc.Count = 1;
            output_texture_desc.Usage = D3D11_USAGE_DEFAULT;
            output_texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(device_->CreateTexture2D(
                    &output_texture_desc, nullptr, hardware_output_texture_.GetAddressOf())) ||
                FAILED(device_->CreateRenderTargetView(
                    hardware_output_texture_.Get(), nullptr,
                    hardware_output_render_target_.GetAddressOf())))
                return false;
            hardware_output_width_ = output_width;
            hardware_output_height_ = output_height;
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
        output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        output_desc.Texture2D.MipSlice = 0;
        ComPtr<ID3D11VideoProcessorOutputView> output_view;
        if (FAILED(video_device_->CreateVideoProcessorOutputView(
                hardware_output_texture_.Get(),
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

        target_width *= zoom_;
        target_height *= zoom_;
        RECT target_rect{
            static_cast<LONG>((output_width - target_width) * 0.5f),
            static_cast<LONG>((output_height - target_height) * 0.5f),
            static_cast<LONG>((output_width + target_width) * 0.5f),
            static_cast<LONG>((output_height + target_height) * 0.5f)
        };

        const float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        context_->ClearRenderTargetView(hardware_output_render_target_.Get(), black);

        D3D11_VIDEO_COLOR background{};
        background.RGBA.R = 0.0f;
        background.RGBA.G = 0.0f;
        background.RGBA.B = 0.0f;
        background.RGBA.A = 1.0f;
        video_context_->VideoProcessorSetOutputBackgroundColor(
            video_processor_.Get(), FALSE, &background);
        video_context_->VideoProcessorSetOutputTargetRect(
            video_processor_.Get(), TRUE, &target_rect);
        video_context_->VideoProcessorSetStreamDestRect(
            video_processor_.Get(), 0, TRUE, &target_rect);
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input_view.Get();
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE color_space{};
        color_space.RGB_Range = 0;
        color_space.YCbCr_Matrix = ycbcr_matrix_;
        color_space.YCbCr_xvYCC = 0;
        color_space.Nominal_Range = ycbcr_full_range_
            ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
            : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        video_context_->VideoProcessorSetStreamColorSpace(
            video_processor_.Get(), 0, &color_space);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_color_space{};
        output_color_space.RGB_Range = 0;
        video_context_->VideoProcessorSetOutputColorSpace(
            video_processor_.Get(), &output_color_space);
        const HRESULT blt_result = video_context_->VideoProcessorBlt(
            video_processor_.Get(), output_view.Get(), 0, 1, &stream);
        if (FAILED(blt_result))
        {
            recover_device_if_removed(lock);
            return false;
        }

        context_->End(completion_query.Get());
        context_->Flush();
        pending_hardware_frames_.push_back(
            PendingHardwareFrame{std::move(completion_query), std::move(frame_owner)});

        if (overlay && overlay_stride >= static_cast<int>(output_width * 4))
        {
            if (!overlay_texture_ ||
                overlay_texture_width_ != output_width ||
                overlay_texture_height_ != output_height)
            {
                overlay_texture_.Reset();
                overlay_view_.Reset();
                D3D11_TEXTURE2D_DESC overlay_desc{};
                overlay_desc.Width = output_width;
                overlay_desc.Height = output_height;
                overlay_desc.MipLevels = 1;
                overlay_desc.ArraySize = 1;
                overlay_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                overlay_desc.SampleDesc.Count = 1;
                overlay_desc.Usage = D3D11_USAGE_DYNAMIC;
                overlay_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                overlay_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(device_->CreateTexture2D(
                        &overlay_desc, nullptr, overlay_texture_.GetAddressOf())) ||
                    FAILED(device_->CreateShaderResourceView(
                        overlay_texture_.Get(), nullptr, overlay_view_.GetAddressOf())))
                    return false;
                overlay_texture_width_ = output_width;
                overlay_texture_height_ = output_height;
            }

            D3D11_MAPPED_SUBRESOURCE overlay_map{};
            if (FAILED(context_->Map(
                    overlay_texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &overlay_map)))
                return false;
            for (UINT row = 0; row < output_height; ++row)
                std::memcpy(
                    static_cast<uint8_t*>(overlay_map.pData) +
                        static_cast<size_t>(row) * overlay_map.RowPitch,
                    overlay + static_cast<size_t>(row) * overlay_stride,
                    static_cast<size_t>(output_width) * 4);
            context_->Unmap(overlay_texture_.Get(), 0);

            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(output_width);
            viewport.Height = static_cast<float>(output_height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context_->OMSetRenderTargets(
                1, hardware_output_render_target_.GetAddressOf(), nullptr);
            context_->OMSetBlendState(blend_state_.Get(), nullptr, 0xffffffff);
            context_->RSSetViewports(1, &viewport);
            context_->RSSetState(rasterizer_.Get());
            context_->IASetInputLayout(nullptr);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
            context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
            update_filter_constants(static_cast<int>(output_width), static_cast<int>(output_height));
            ID3D11ShaderResourceView* overlay_views[] = {overlay_view_.Get()};
            context_->PSSetShaderResources(0, 1, overlay_views);
            ID3D11SamplerState* overlay_samplers[] = {sampler_.Get()};
            context_->PSSetSamplers(0, 1, overlay_samplers);
            context_->Draw(3, 0);
            ID3D11ShaderResourceView* empty_views[] = {nullptr};
            context_->PSSetShaderResources(0, 1, empty_views);
            context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        }

        context_->CopyResource(back_buffer.Get(), hardware_output_texture_.Get());
        const HRESULT present_result = swap_chain_->Present(1, 0);
        if (present_result == DXGI_ERROR_DEVICE_REMOVED ||
            present_result == DXGI_ERROR_DEVICE_RESET ||
            present_result == DXGI_ERROR_DEVICE_HUNG ||
            present_result == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
        {
            const HWND target = window_;
            release_resources();
            lock.unlock();
            initialize(target);
            return false;
        }
        return SUCCEEDED(present_result);
    }

    bool capture_bgra(std::vector<uint8_t>& pixels, int& width, int& height)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!device_ || !context_ || !hardware_output_texture_)
            return false;

        D3D11_TEXTURE2D_DESC source_desc{};
        hardware_output_texture_->GetDesc(&source_desc);
        D3D11_TEXTURE2D_DESC staging_desc{};
        staging_desc.Width = source_desc.Width;
        staging_desc.Height = source_desc.Height;
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.Format = source_desc.Format;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(
                &staging_desc, nullptr, staging.GetAddressOf())))
            return false;

        context_->CopyResource(staging.Get(), hardware_output_texture_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;

        width = static_cast<int>(source_desc.Width);
        height = static_cast<int>(source_desc.Height);
        const size_t row_bytes = static_cast<size_t>(width) * 4;
        pixels.resize(row_bytes * static_cast<size_t>(height));
        for (int row = 0; row < height; ++row)
            std::memcpy(
                pixels.data() + static_cast<size_t>(row) * row_bytes,
                static_cast<const uint8_t*>(mapped.pData) +
                    static_cast<size_t>(row) * mapped.RowPitch,
                row_bytes);
        context_->Unmap(staging.Get(), 0);
        return true;
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
    void recover_device_if_removed(std::unique_lock<std::mutex>& lock)
    {
        if (!device_ || SUCCEEDED(device_->GetDeviceRemovedReason()))
            return;

        const HWND target = window_;
        release_resources();
        lock.unlock();
        if (target)
            initialize(target);
    }

    struct PendingHardwareFrame
    {
        ComPtr<ID3D11Query> completion;
        std::shared_ptr<void> owner;
    };

    void reap_completed_hardware_frames()
    {
        while (!pending_hardware_frames_.empty())
        {
            const HRESULT result = context_->GetData(
                pending_hardware_frames_.front().completion.Get(), nullptr, 0, 0);
            if (result == S_FALSE)
                break;
            pending_hardware_frames_.pop_front();
        }
    }

    bool create_render_target()
    {
        ComPtr<ID3D11Texture2D> back_buffer;
        if (FAILED(swap_chain_->GetBuffer(
            0, IID_PPV_ARGS(back_buffer.GetAddressOf()))))
            return false;
        return SUCCEEDED(device_->CreateRenderTargetView(
            back_buffer.Get(), nullptr, render_target_.GetAddressOf()));
    }

    struct FilterConstants
    {
        float brightness;
        float contrast;
        float saturation;
        float hue_degrees;
        float sharpness;
        float denoise;
        float texel_x;
        float texel_y;
        float hdr_transfer;
        float color_primaries;
        float hdr_peak_luminance;
        float padding;
        float zoom;
        float padding2[3];
    };

    void update_filter_constants(int width, int height)
    {
        if (!filter_constants_buffer_)
            return;
        const FilterConstants values{
            brightness_, contrast_, saturation_, hue_degrees_,
            sharpness_, denoise_,
            width > 0 ? 1.0f / width : 0.0f,
            height > 0 ? 1.0f / height : 0.0f,
            static_cast<float>(hdr_transfer_),
            static_cast<float>(color_primaries_),
            hdr_peak_luminance_,
            0.0f,
            zoom_,
            {0.0f, 0.0f, 0.0f}
        };
        context_->UpdateSubresource(filter_constants_buffer_.Get(), 0, nullptr, &values, 0, 0);
        ID3D11Buffer* buffers[] = {filter_constants_buffer_.Get()};
        context_->PSSetConstantBuffers(0, 1, buffers);
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

        D3D11_BUFFER_DESC filter_buffer_desc{};
        filter_buffer_desc.ByteWidth = static_cast<UINT>(sizeof(FilterConstants));
        filter_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
        filter_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(
                &filter_buffer_desc, nullptr, filter_constants_buffer_.GetAddressOf())))
            return false;

        D3D11_RASTERIZER_DESC rasterizer_desc{};
        rasterizer_desc.FillMode = D3D11_FILL_SOLID;
        rasterizer_desc.CullMode = D3D11_CULL_NONE;
        rasterizer_desc.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rasterizer_desc, rasterizer_.GetAddressOf())))
            return false;

        D3D11_BLEND_DESC blend_desc{};
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device_->CreateBlendState(&blend_desc, blend_state_.GetAddressOf())))
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
        if (context_ && !pending_hardware_frames_.empty())
        {
            context_->Flush();
            for (auto& frame : pending_hardware_frames_)
            {
                HRESULT result = S_FALSE;
                while (result == S_FALSE)
                {
                    result = context_->GetData(frame.completion.Get(), nullptr, 0, 0);
                    if (result == S_FALSE)
                        Sleep(1);
                }
            }
            pending_hardware_frames_.clear();
        }
        if (context_)
            context_->ClearState();
        video_view_.Reset();
        video_texture_.Reset();
        hardware_output_texture_.Reset();
        hardware_output_render_target_.Reset();
        overlay_texture_.Reset();
        overlay_view_.Reset();
        overlay_texture_width_ = 0;
        overlay_texture_height_ = 0;
        hardware_output_width_ = 0;
        hardware_output_height_ = 0;
        video_processor_.Reset();
        video_processor_enumerator_.Reset();
        video_context_.Reset();
        video_device_.Reset();
        render_target_.Reset();
        sampler_.Reset();
        blend_state_.Reset();
        rasterizer_.Reset();
        filter_constants_buffer_.Reset();
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
    std::deque<PendingHardwareFrame> pending_hardware_frames_;
    HWND window_ = nullptr;
    UINT texture_width_ = 0;
    UINT texture_height_ = 0;
    bool stretch_ = false;
    float aspect_ratio_ = 0.0f;
    float zoom_ = 1.0f;
    float brightness_ = 0.0f;
    float contrast_ = 1.0f;
    float saturation_ = 1.0f;
    float hue_degrees_ = 0.0f;
    float sharpness_ = 0.0f;
    float denoise_ = 0.0f;
    bool grayscale_ = false;
    int hdr_transfer_ = 0;
    int color_primaries_ = 0;
    float hdr_peak_luminance_ = 1000.0f;
    int ycbcr_matrix_ = 0;
    int ycbcr_full_range_ = 0;
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
    ComPtr<ID3D11Texture2D> hardware_output_texture_;
    ComPtr<ID3D11RenderTargetView> hardware_output_render_target_;
    ComPtr<ID3D11Texture2D> overlay_texture_;
    ComPtr<ID3D11ShaderResourceView> overlay_view_;
    UINT hardware_output_width_ = 0;
    UINT hardware_output_height_ = 0;
    UINT overlay_texture_width_ = 0;
    UINT overlay_texture_height_ = 0;
    ComPtr<ID3D11ShaderResourceView> video_view_;
    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11Buffer> filter_constants_buffer_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blend_state_;
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

void D3D11VideoRenderer::set_zoom(float zoom)
{
    impl_->set_zoom(zoom);
}

void D3D11VideoRenderer::set_video_filter(
    float brightness, float contrast, float saturation, float hue_degrees,
    float sharpness, float denoise, bool grayscale)
{
    impl_->set_video_filter(
        brightness, contrast, saturation, hue_degrees, sharpness, denoise, grayscale);
}

void D3D11VideoRenderer::set_hdr_metadata(int transfer, int color_primaries, float peak_luminance)
{
    impl_->set_hdr_metadata(transfer, color_primaries, peak_luminance);
}

void D3D11VideoRenderer::set_video_color_space(int ycbcr_matrix, int full_range)
{
    impl_->set_video_color_space(ycbcr_matrix, full_range);
}

bool D3D11VideoRenderer::present(const uint8_t* bgra, int width, int height, int stride)
{
    return impl_->present(bgra, width, height, stride);
}

bool D3D11VideoRenderer::present_hardware(
    void* texture, uint32_t array_slice, uint32_t format,
    int width, int height, std::shared_ptr<void> frame_owner,
    const uint8_t* overlay, int overlay_stride)
{
    return impl_->present_hardware(
        texture, array_slice, format, width, height,
        std::move(frame_owner), overlay, overlay_stride);
}

bool D3D11VideoRenderer::capture_bgra(
    std::vector<uint8_t>& pixels, int& width, int& height)
{
    return impl_->capture_bgra(pixels, width, height);
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
