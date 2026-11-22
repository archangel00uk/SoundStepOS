#include "wasapi_renderer.h"

#include <windows.h>
#include <objbase.h>
#include <ksmedia.h>
#include <mmreg.h>

#include <cstring>

#include "../config.h"

namespace sso::audio {

namespace {

void BuildFloat32StereoFormat(WAVEFORMATEXTENSIBLE& wfx, uint32_t sampleRate) {
    std::memset(&wfx, 0, sizeof(wfx));
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = 2;
    wfx.Format.nSamplesPerSec = sampleRate;
    wfx.Format.wBitsPerSample = 32;
    wfx.Format.nBlockAlign = 2 * 4;                         // channels * bytes/sample
    wfx.Format.nAvgBytesPerSec = sampleRate * 2 * 4;
    wfx.Format.cbSize = 22;
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

} // namespace

WasapiRenderer::~WasapiRenderer() {
    Stop();
}

bool WasapiRenderer::Start(uint32_t sampleRate) {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator_));
    if (FAILED(hr)) return false;

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device_);
    if (FAILED(hr)) {
        Stop();
        return false;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&client_));
    if (FAILED(hr)) {
        Stop();
        return false;
    }

    WAVEFORMATEXTENSIBLE wfx;
    BuildFloat32StereoFormat(wfx, sampleRate);
    const WAVEFORMATEX* fmt = reinterpret_cast<const WAVEFORMATEX*>(&wfx);

    // Prefer low-latency shared streaming (IAudioClient3) with the engine's
    // minimum period; fall back to classic event-driven Initialize.
    bool initialized = false;
    IAudioClient3* client3 = nullptr;
    if (SUCCEEDED(client_->QueryInterface(__uuidof(IAudioClient3),
                                          reinterpret_cast<void**>(&client3)))) {
        UINT32 defaultFrames = 0, minFrames = 0, baseFrames = 0, maxFrames = 0;
        if (SUCCEEDED(client3->GetSharedModeEnginePeriod(fmt, &defaultFrames, &minFrames,
                                                         &baseFrames, &maxFrames))) {
            hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                      minFrames, fmt, nullptr);
            initialized = SUCCEEDED(hr);
        }
        client3->Release();
    }

    if (!initialized) {
        const REFERENCE_TIME bufferDuration = 200000; // 20 ms fallback
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 bufferDuration, 0, fmt, nullptr);
        if (FAILED(hr)) {
            Stop();
            return false;
        }
    }

    hr = client_->GetService(__uuidof(IAudioRenderClient),
                             reinterpret_cast<void**>(&render_));
    if (FAILED(hr)) {
        Stop();
        return false;
    }

    hr = client_->GetBufferSize(&bufferFrames_);
    if (FAILED(hr)) {
        Stop();
        return false;
    }

    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) {
        Stop();
        return false;
    }

    hr = client_->SetEventHandle(event_);
    if (FAILED(hr)) {
        Stop();
        return false;
    }

    hr = client_->Start();
    if (FAILED(hr)) {
        Stop();
        return false;
    }

    return true;
}

void WasapiRenderer::Stop() {
    if (client_) client_->Stop();
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
    if (render_) {
        render_->Release();
        render_ = nullptr;
    }
    if (client_) {
        client_->Release();
        client_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    if (enumerator_) {
        enumerator_->Release();
        enumerator_ = nullptr;
    }
    bufferFrames_ = 0;
}

bool WasapiRenderer::WaitForBuffer(DWORD timeoutMs) {
    if (!event_) return false;
    return WaitForSingleObject(event_, timeoutMs) == WAIT_OBJECT_0;
}

UINT32 WasapiRenderer::GetPadding() {
    if (!client_) return 0;
    UINT32 padding = 0;
    if (FAILED(client_->GetCurrentPadding(&padding))) return 0;
    return padding;
}

UINT32 WasapiRenderer::Write(const float* src, UINT32 frames) {
    if (!render_) return 0;

    UINT32 padding = 0;
    if (FAILED(client_->GetCurrentPadding(&padding))) return 0;

    const UINT32 free = (padding < bufferFrames_) ? (bufferFrames_ - padding) : 0;
    if (free == 0) return 0;

    const UINT32 n = (frames < free) ? frames : free;

    float* dst = nullptr;
    if (FAILED(render_->GetBuffer(n, reinterpret_cast<BYTE**>(&dst))) || !dst) return 0;

    std::memcpy(dst, src, n * channels_ * sizeof(float));
    render_->ReleaseBuffer(n, 0);
    return n;
}

} // namespace sso::audio
