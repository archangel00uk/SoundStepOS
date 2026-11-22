#pragma once
#include <cstdint>
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

namespace sso::audio {

// Renders processed audio to the user's default playback device in shared
// mode. Uses low-latency shared-mode streaming (IAudioClient3) when available
// with a fallback to classic event-driven Initialize.
class WasapiRenderer {
public:
    ~WasapiRenderer();

    bool Start(uint32_t sampleRate);
    void Stop();

    UINT32 bufferFrames() const { return bufferFrames_; }

    // Number of frames currently queued (unplayed) in the render buffer.
    UINT32 GetPadding();

    // Blocks until render buffer space is available or the timeout elapses.
    bool WaitForBuffer(DWORD timeoutMs);

    // Writes up to `frames` interleaved float32 frames into the render buffer.
    // Returns the number of frames actually written (may be fewer than asked).
    UINT32 Write(const float* src, UINT32 frames);

private:
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    HANDLE event_ = nullptr;
    UINT32 bufferFrames_ = 0;
    UINT32 channels_ = 2;
};

} // namespace sso::audio
