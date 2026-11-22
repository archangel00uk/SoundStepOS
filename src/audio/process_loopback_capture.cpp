#include "process_loopback_capture.h"

#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclientactivationparams.h>
#include <ksmedia.h>
#include <mmreg.h>
#include <propidl.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "../config.h"

namespace sso::audio {

namespace {

// Virtual device id used for process-based loopback activation.
constexpr wchar_t kProcessLoopbackDeviceId[] = L"VAD\\Process_Loopback";

void LogHr(const char* stage, HRESULT hr) {
    std::fprintf(stderr, "[capture] %s failed: 0x%08lX\n", stage,
                 static_cast<unsigned long>(hr));
}

// The process-loopback IAudioClient does NOT implement GetMixFormat (returns
// E_NOTIMPL), so we obtain the mix format from the default render endpoint via
// a normal (non-loopback) IAudioClient instead.
bool GetDefaultRenderMixFormat(uint32_t& sampleRate, uint32_t& channels) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    WAVEFORMATEX* mix = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                             reinterpret_cast<void**>(&client));
    if (SUCCEEDED(hr)) hr = client->GetMixFormat(&mix);
    if (SUCCEEDED(hr) && mix) {
        sampleRate = mix->nSamplesPerSec;
        channels = mix->nChannels;
        ok = true;
    }

    if (mix) CoTaskMemFree(mix);
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    return ok;
}

void BuildFloat32StereoFormat(WAVEFORMATEXTENSIBLE& wfx, uint32_t sampleRate) {
    std::memset(&wfx, 0, sizeof(wfx));
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = 2;
    wfx.Format.nSamplesPerSec = sampleRate;
    wfx.Format.wBitsPerSample = 32;
    wfx.Format.nBlockAlign = 2 * 4;
    wfx.Format.nAvgBytesPerSec = sampleRate * 2 * 4;
    wfx.Format.cbSize = 22;
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

// Completion handler for ActivateAudioInterfaceAsync. Implements IAgileObject
// so it can be invoked from any COM apartment.
class ActivationCompletionHandler final
    : public IActivateAudioInterfaceCompletionHandler,
      public IAgileObject {
public:
    explicit ActivationCompletionHandler(HANDLE doneEvent) : doneEvent_(doneEvent) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        } else if (riid == __uuidof(IAgileObject)) {
            *ppv = static_cast<IAgileObject*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hr = E_FAIL;
        IUnknown* unk = nullptr;
        if (op) op->GetActivateResult(&hr, &unk);
        if (SUCCEEDED(hr) && unk) {
            hr = unk->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client_));
            unk->Release();
        }
        result_ = hr;
        SetEvent(doneEvent_);
        return S_OK;
    }

    IAudioClient* DetachClient() {
        IAudioClient* c = client_;
        client_ = nullptr;
        return c;
    }
    HRESULT result() const { return result_; }

private:
    std::atomic<ULONG> ref_{1};
    HANDLE doneEvent_;
    IAudioClient* client_ = nullptr;
    HRESULT result_ = E_FAIL;
};

} // namespace

ProcessLoopbackCapture::~ProcessLoopbackCapture() {
    Stop();
}

bool ProcessLoopbackCapture::Start(DWORD targetPid) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = targetPid;
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!done) return false;

    auto* handler = new ActivationCompletionHandler(done);
    IActivateAudioInterfaceAsyncOperation* op = nullptr;

    HRESULT hr = ActivateAudioInterfaceAsync(kProcessLoopbackDeviceId, __uuidof(IAudioClient),
                                             &pv, handler, &op);
    if (SUCCEEDED(hr)) {
        const DWORD wait = WaitForSingleObject(done, 10000);
        if (wait == WAIT_OBJECT_0) {
            hr = handler->result();
        } else {
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            std::fprintf(stderr, "[capture] ActivateAudioInterfaceAsync timed out\n");
        }
    } else {
        LogHr("ActivateAudioInterfaceAsync", hr);
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            std::fprintf(stderr,
                "[capture] Process-loopback is unavailable on this OS build.\n"
                "[capture] It requires Windows 10 build 20348 (Server 2022) or Windows 11.\n");
        }
    }

    if (SUCCEEDED(hr)) client_ = handler->DetachClient();
    handler->Release();
    if (op) op->Release();
    CloseHandle(done);

    if (FAILED(hr) || !client_) return false;

    // The loopback client does not implement GetMixFormat; query the default
    // render endpoint directly and request a float32 stereo stream at its rate.
    uint32_t deviceRate = 48000;
    uint32_t deviceChannels = 2;
    if (!GetDefaultRenderMixFormat(deviceRate, deviceChannels)) {
        std::fprintf(stderr, "[capture] could not query the default render device format\n");
        Stop();
        return false;
    }

    sampleRate_ = deviceRate;
    mixChannels_ = 2;   // we request stereo; the engine down/upmixes for us
    std::fprintf(stderr, "[capture] default render device: %u Hz, %u ch -> requesting float32 stereo\n",
                 deviceRate, deviceChannels);

    WAVEFORMATEXTENSIBLE wfx;
    BuildFloat32StereoFormat(wfx, sampleRate_);

    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                             0, 0, reinterpret_cast<WAVEFORMATEX*>(&wfx), nullptr);
    if (FAILED(hr)) {
        LogHr("Initialize", hr);
        Stop();
        return false;
    }

    hr = client_->GetService(__uuidof(IAudioCaptureClient),
                             reinterpret_cast<void**>(&capture_));
    if (FAILED(hr)) {
        LogHr("GetService", hr);
        Stop();
        return false;
    }

    hr = client_->GetBufferSize(&bufferFrames_);
    if (FAILED(hr)) {
        LogHr("GetBufferSize", hr);
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
        LogHr("SetEventHandle", hr);
        Stop();
        return false;
    }

    hr = client_->Start();
    if (FAILED(hr)) {
        LogHr("Start", hr);
        Stop();
        return false;
    }

    return true;
}

void ProcessLoopbackCapture::Stop() {
    if (client_) client_->Stop();
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
    if (capture_) {
        capture_->Release();
        capture_ = nullptr;
    }
    if (client_) {
        client_->Release();
        client_ = nullptr;
    }
    bufferFrames_ = 0;
}

bool ProcessLoopbackCapture::WaitForData(DWORD timeoutMs) {
    if (!event_) return false;
    return WaitForSingleObject(event_, timeoutMs) == WAIT_OBJECT_0;
}

UINT32 ProcessLoopbackCapture::ReadAvailable(float* dst, UINT32 maxFrames) {
    if (!capture_) return 0;

    // Drains whole packets into `dst` (interleaved stereo float32). `maxFrames`
    // is the capacity of `dst` in frames; a packet is only read if it fits
    // completely, so no audio is dropped. Returns frames written.
    UINT32 total = 0;
    UINT32 packetFrames = 0;
    HRESULT hr = capture_->GetNextPacketSize(&packetFrames);

    while (SUCCEEDED(hr) && packetFrames > 0 && (total + packetFrames) <= maxFrames) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        const float* src = reinterpret_cast<const float*>(data);
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            std::memset(dst + total * 2, 0, frames * 2 * sizeof(float));
        } else if (mixChannels_ == 1) {
            for (UINT32 i = 0; i < frames; ++i) {
                dst[(total + i) * 2] = src[i];
                dst[(total + i) * 2 + 1] = src[i];
            }
        } else {
            // mixChannels_ == 2 (we requested stereo via AUTOCONVERTPCM).
            for (UINT32 i = 0; i < frames; ++i) {
                dst[(total + i) * 2] = src[i * mixChannels_];
                dst[(total + i) * 2 + 1] = src[i * mixChannels_ + 1];
            }
        }

        total += frames;
        capture_->ReleaseBuffer(frames);
        hr = capture_->GetNextPacketSize(&packetFrames);
    }
    return total;
}

} // namespace sso::audio
