#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include <windows.h>

#include "../dsp/pubg_pipeline.h"
#include "process_loopback_capture.h"
#include "ring_buffer.h"
#include "wasapi_renderer.h"

namespace sso::audio {

// Identifies a togglable DSP stage.
enum class Feature { Eq, UpComp, Transient, SelfSuppress, Limiter };

// Orchestrates the real-time audio path:
//
//   capture thread -- WASAPI process loopback -> capToDsp_ (lock-free SPSC ring)
//   render thread  -- capToDsp_ -> PubgPipeline -> WASAPI render client
//
// The render thread is the master clock: on each render event it pulls exactly
// the number of frames the device needs, processes them, and writes them. This
// couples DSP consumption to the device clock (eliminating capture/render
// drift) and keeps the render buffer full to avoid underruns.
//
// Also exposes a thread-safe control surface (bypass / per-feature toggles /
// live parameter updates) for the UI. Toggle state persists across
// engage/disengage cycles.
class AudioPipeline {
public:
    AudioPipeline() = default;
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    bool Start(DWORD targetPid);
    void Stop();
    bool running() const { return running_.load(); }

    // --- Live control (callable from the UI thread) ---
    void SetBypass(bool bypass);
    bool bypass() const;

    void SetFeatureEnabled(Feature f, bool enabled);
    bool featureEnabled(Feature f) const;

    void SetConfig(const Config& cfg);

    // Current output peak level in dB (read by the UI meter).
    float outputPeakDb() const { return outputPeakDb_.load(std::memory_order_relaxed); }

private:
    void captureThread();
    void renderThread();

    std::atomic<bool> running_{false};
    std::thread captureThread_;
    std::thread renderThread_;

    std::atomic<float> outputPeakDb_{-100.0f};

    std::mutex lifecycleMutex_;   // serializes Start()/Stop()
    std::mutex configMutex_;      // guards config_
    Config config_{};
    dsp::PipelineControl control_;

    ProcessLoopbackCapture capture_;
    WasapiRenderer renderer_;
    dsp::PubgPipeline dsp_;

    static constexpr std::size_t kRingCapacityFrames = 4096; // ~85 ms @ 48 kHz
    SPSCRingBuffer<float> capToDsp_{kRingCapacityFrames * kChannels};
};

} // namespace sso::audio
