#include "wasapi_audio_output.h"

#ifdef _WIN32

#include <ksmedia.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lnplayer
{
namespace
{

bool is_float_format(const WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    return false;
}

bool is_pcm_format(const WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_PCM)
        return true;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return extended->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
    }

    return false;
}

void write_sample(uint8_t* destination, float value, bool floating_point, WORD bits)
{
    value = std::clamp(value, -1.0f, 1.0f);

    if (floating_point && bits == 32)
    {
        std::memcpy(destination, &value, sizeof(value));
        return;
    }

    if (floating_point)
        return;

    if (bits == 8)
    {
        const uint8_t sample = static_cast<uint8_t>(
            std::clamp(std::lrintf((value + 1.0f) * 127.5f), 0L, 255L));
        destination[0] = sample;
    }
    else if (bits == 16)
    {
        const int16_t sample = static_cast<int16_t>(std::lrintf(value * 32767.0f));
        std::memcpy(destination, &sample, sizeof(sample));
    }
    else if (bits == 24)
    {
        const int32_t sample = static_cast<int32_t>(std::lrintf(value * 8388607.0f));
        destination[0] = static_cast<uint8_t>(sample & 0xff);
        destination[1] = static_cast<uint8_t>((sample >> 8) & 0xff);
        destination[2] = static_cast<uint8_t>((sample >> 16) & 0xff);
    }
    else if (bits == 32)
    {
        const int32_t sample = static_cast<int32_t>(
            std::clamp(
                static_cast<double>(value) * 2147483647.0,
                -2147483648.0,
                2147483647.0));
        std::memcpy(destination, &sample, sizeof(sample));
    }
}

}

WasapiAudioOutput::~WasapiAudioOutput()
{
    close();
}

bool WasapiAudioOutput::initialize_com()
{
    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result))
        return false;

    com_initialized_ = true;
    return true;
}

bool WasapiAudioOutput::open()
{
    close();

    if (!initialize_com())
        return false;

    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator_));
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device_);
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = device_->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&audio_client_));
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = audio_client_->GetMixFormat(&mix_format_);
    if (FAILED(result))
    {
        close();
        return false;
    }

    if ((!is_float_format(mix_format_) && !is_pcm_format(mix_format_)) ||
        (mix_format_->wBitsPerSample != 8 &&
         mix_format_->wBitsPerSample != 16 &&
         mix_format_->wBitsPerSample != 24 &&
         mix_format_->wBitsPerSample != 32))
    {
        close();
        return false;
    }

    audio_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!audio_event_)
    {
        close();
        return false;
    }

    result = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        1000000,
        0,
        mix_format_,
        nullptr);
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = audio_client_->SetEventHandle(audio_event_);
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = audio_client_->GetBufferSize(&buffer_frames_);
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = audio_client_->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&render_client_));
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = audio_client_->GetService(
        __uuidof(IAudioClock),
        reinterpret_cast<void**>(&audio_clock_));
    if (FAILED(result))
    {
        close();
        return false;
    }

    result = audio_clock_->GetFrequency(&audio_clock_frequency_);
    if (FAILED(result) || audio_clock_frequency_ == 0)
    {
        close();
        return false;
    }

    result = audio_client_->Start();
    if (FAILED(result))
    {
        close();
        return false;
    }

    started_ = true;
    return true;
}

bool WasapiAudioOutput::write(const float* samples, uint32_t frames)
{
    if (!started_ || !samples || !mix_format_ || !render_client_)
        return false;

    const bool floating_point = is_float_format(mix_format_);
    const WORD bits = mix_format_->wBitsPerSample;
    const WORD channels_count = mix_format_->nChannels;
    const WORD bytes_per_sample = static_cast<WORD>(bits / 8);

    if (floating_point && bits != 32)
        return false;

    uint32_t written = 0;
    while (written < frames)
    {
        const DWORD wait_result = WaitForSingleObject(audio_event_, 1000);
        if (wait_result != WAIT_OBJECT_0)
            return false;

        UINT32 padding = 0;
        HRESULT result = audio_client_->GetCurrentPadding(&padding);
        if (FAILED(result))
            return false;

        const UINT32 available = buffer_frames_ - padding;
        if (available == 0)
            continue;

        const UINT32 count = std::min<UINT32>(available, frames - written);
        BYTE* buffer = nullptr;
        result = render_client_->GetBuffer(count, &buffer);
        if (FAILED(result))
            return false;

        for (UINT32 frame = 0; frame < count; ++frame)
        {
            const float left = samples[(written + frame) * 2];
            const float right = samples[(written + frame) * 2 + 1];

            for (WORD channel = 0; channel < channels_count; ++channel)
            {
                float value = 0.0f;
                if (channel == 0)
                    value = left;
                else if (channel == 1)
                    value = right;
                else
                    value = (left + right) * 0.5f;

                uint8_t* destination = buffer +
                    (static_cast<size_t>(frame) * channels_count + channel) * bytes_per_sample;
                write_sample(destination, value, floating_point, bits);
            }
        }

        result = render_client_->ReleaseBuffer(count, 0);
        if (FAILED(result))
            return false;

        written += count;
    }

    return true;
}

bool WasapiAudioOutput::flush()
{
    if (!started_ || !audio_client_)
        return false;

    HRESULT result = audio_client_->Stop();
    if (FAILED(result))
        return false;

    result = audio_client_->Reset();
    if (FAILED(result))
        return false;

    result = audio_client_->Start();
    return SUCCEEDED(result);
}

bool WasapiAudioOutput::current_padding(uint32_t& frames) const
{
    if (!started_ || !audio_client_)
        return false;

    UINT32 padding = 0;
    if (FAILED(audio_client_->GetCurrentPadding(&padding)))
        return false;

    frames = padding;
    return true;
}

bool WasapiAudioOutput::clock_position(uint64_t& frames) const
{
    if (!started_ || !audio_clock_ || audio_clock_frequency_ == 0 || !mix_format_)
        return false;

    UINT64 position = 0;
    if (FAILED(audio_clock_->GetPosition(&position, nullptr)))
        return false;

    const long double device_frames =
        static_cast<long double>(position) * mix_format_->nSamplesPerSec /
        static_cast<long double>(audio_clock_frequency_);
    frames = static_cast<uint64_t>(device_frames);
    return true;
}

bool WasapiAudioOutput::drain()
{
    if (!started_ || !audio_client_ || !audio_event_)
        return false;

    for (;;)
    {
        UINT32 padding = 0;
        if (FAILED(audio_client_->GetCurrentPadding(&padding)))
            return false;
        if (padding == 0)
            return true;

        if (WaitForSingleObject(audio_event_, 1000) != WAIT_OBJECT_0)
            return false;
    }
}

void WasapiAudioOutput::close()
{
    if (started_ && audio_client_)
        audio_client_->Stop();
    started_ = false;

    if (audio_clock_)
    {
        audio_clock_->Release();
        audio_clock_ = nullptr;
    }
    audio_clock_frequency_ = 0;
    if (render_client_)
    {
        render_client_->Release();
        render_client_ = nullptr;
    }
    if (audio_client_)
    {
        audio_client_->Release();
        audio_client_ = nullptr;
    }
    if (device_)
    {
        device_->Release();
        device_ = nullptr;
    }
    if (enumerator_)
    {
        enumerator_->Release();
        enumerator_ = nullptr;
    }
    if (mix_format_)
    {
        CoTaskMemFree(mix_format_);
        mix_format_ = nullptr;
    }
    if (audio_event_)
    {
        CloseHandle(audio_event_);
        audio_event_ = nullptr;
    }
    if (com_initialized_)
    {
        CoUninitialize();
        com_initialized_ = false;
    }
    buffer_frames_ = 0;
}

int WasapiAudioOutput::sample_rate() const
{
    return mix_format_ ? static_cast<int>(mix_format_->nSamplesPerSec) : 0;
}

int WasapiAudioOutput::channels() const
{
    return mix_format_ ? static_cast<int>(mix_format_->nChannels) : 0;
}

}

#endif
