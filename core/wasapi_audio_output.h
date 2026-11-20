#ifndef LNPLAYER_WASAPI_AUDIO_OUTPUT_H
#define LNPLAYER_WASAPI_AUDIO_OUTPUT_H

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <cstdint>

namespace lnplayer
{

class WasapiAudioOutput
{
public:
    WasapiAudioOutput() = default;
    ~WasapiAudioOutput();

    WasapiAudioOutput(const WasapiAudioOutput&) = delete;
    WasapiAudioOutput& operator=(const WasapiAudioOutput&) = delete;

    bool open();
    bool write(const float* interleaved_stereo, uint32_t frames);
    bool flush();
    bool current_padding(uint32_t& frames) const;
    void close();

    int sample_rate() const;
    int channels() const;

private:
    bool initialize_com();

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    WAVEFORMATEX* mix_format_ = nullptr;
    HANDLE audio_event_ = nullptr;
    UINT32 buffer_frames_ = 0;
    bool com_initialized_ = false;
    bool started_ = false;
};

}

#endif

#endif
