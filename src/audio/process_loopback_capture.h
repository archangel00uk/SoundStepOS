#pragma once
#include <cstdint>
#include <windows.h>
#include <audioclient.h>

namespace sso::audio {

// Captures audio rendered by a specific process (TslGame.exe) via Windows
// Application Loopback (AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK).
//
// The capture client delivers interleaved float32 STEREO at the device's mix
// sample rate (downmixed/upmixed from the device channel layout) into a
// caller-supplied buffer. Intended to be driven from a dedicated thread that
// waits on WaitForData() and then drains packets with ReadAvailable().
//
// NOTE: process-loopback capture requires Windows 10 build 20348 / Windows 11.
class ProcessLoopbackCapture {
public:
    ~ProcessLoopbackCapture();

    bool Start(DWORD targetPid);
    void Stop();

    UINT32 bufferFrames() const { return bufferFrames_; }
    uint32_t sampleRate() const { return sampleRate_; }
    uint32_t channels() const { return 2; }   // always outputs stereo

    // Blocks until the capture event signals or the timeout elapses.
    bool WaitForData(DWORD timeoutMs);

    // Drains all pending capture packets into `dst` (interleaved stereo float32),
    // writing at most `maxFrames` frames. Returns frames written.
    UINT32 ReadAvailable(float* dst, UINT32 maxFrames);

private:
    IAudioClient* client_ = nullptr;
    IAudioCaptureClient* capture_ = nullptr;
    HANDLE event_ = nullptr;
    UINT32 bufferFrames_ = 0;
    uint32_t sampleRate_ = 48000;
    uint32_t mixChannels_ = 2;
};

} // namespace sso::audio
