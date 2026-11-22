#include "audio_pipeline.h"

#include <avrt.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../config.h"

namespace sso::audio {

namespace {
// Marks the calling thread as a "Pro Audio" MMCSS task and returns a handle to
// revert on exit (or nullptr if unavailable).
HANDLE EnterProAudio() {
    DWORD taskIndex = 0;
    return AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
}
} // namespace

AudioPipeline::~AudioPipeline() {
    Stop();
}

bool AudioPipeline::Start(DWORD pid) {
    std::lock_guard<std::mutex> lk(lifecycleMutex_);
    if (running_) return false;

    Config cfg;
    {
        std::lock_guard<std::mutex> cl(configMutex_);
        cfg = config_;
    }

    // Start capture first so we learn the device's actual mix sample rate,
    // then configure the renderer and DSP to match.
    if (!capture_.Start(pid)) {
        std::fprintf(stderr, "process loopback capture start failed (see [capture] log above)\n");
        return false;
    }
    const uint32_t sampleRate = capture_.sampleRate();

    if (!renderer_.Start(sampleRate)) {
        std::fprintf(stderr, "renderer start failed\n");
        capture_.Stop();
        return false;
    }

    dsp_.setControl(&control_);
    dsp_.configure(cfg, static_cast<float>(sampleRate));

    running_ = true;
    renderThread_ = std::thread(&AudioPipeline::renderThread, this);
    captureThread_ = std::thread(&AudioPipeline::captureThread, this);
    return true;
}

void AudioPipeline::Stop() {
    std::lock_guard<std::mutex> lk(lifecycleMutex_);
    running_.store(false);
    capture_.Stop();
    renderer_.Stop();

    if (captureThread_.joinable()) captureThread_.join();
    if (renderThread_.joinable()) renderThread_.join();
}

void AudioPipeline::SetBypass(bool bypass) {
    control_.bypass.store(bypass, std::memory_order_relaxed);
}

bool AudioPipeline::bypass() const {
    return control_.bypass.load(std::memory_order_relaxed);
}

void AudioPipeline::SetFeatureEnabled(Feature f, bool enabled) {
    switch (f) {
        case Feature::Eq:           control_.enableEq.store(enabled, std::memory_order_relaxed); break;
        case Feature::UpComp:       control_.enableUpComp.store(enabled, std::memory_order_relaxed); break;
        case Feature::Transient:    control_.enableTransient.store(enabled, std::memory_order_relaxed); break;
        case Feature::SelfSuppress: control_.enableSelfSuppress.store(enabled, std::memory_order_relaxed); break;
        case Feature::Limiter:      control_.enableLimiter.store(enabled, std::memory_order_relaxed); break;
    }
}

bool AudioPipeline::featureEnabled(Feature f) const {
    switch (f) {
        case Feature::Eq:           return control_.enableEq.load(std::memory_order_relaxed);
        case Feature::UpComp:       return control_.enableUpComp.load(std::memory_order_relaxed);
        case Feature::Transient:    return control_.enableTransient.load(std::memory_order_relaxed);
        case Feature::SelfSuppress: return control_.enableSelfSuppress.load(std::memory_order_relaxed);
        case Feature::Limiter:      return control_.enableLimiter.load(std::memory_order_relaxed);
    }
    return false;
}

void AudioPipeline::SetConfig(const Config& cfg) {
    {
        std::lock_guard<std::mutex> lk(configMutex_);
        config_ = cfg;
    }
    if (running_) dsp_.publish(cfg);
}

void AudioPipeline::captureThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE mmcss = EnterProAudio();

    // Size the read buffer to the WASAPI engine period so whole packets are
    // copied without truncation.
    const UINT32 readFrames = capture_.bufferFrames() > 0 ? capture_.bufferFrames() : kBlockFrames;
    std::vector<float> buf(readFrames * kChannels);

    while (running_) {
        if (!capture_.WaitForData(500)) continue;
        const UINT32 n = capture_.ReadAvailable(buf.data(), readFrames);
        if (n > 0) {
            // Bounded retry; drop (log) rather than stall the capture and let
            // the WASAPI buffer overflow.
            int tries = 0;
            while (running_ && !capToDsp_.push(buf.data(), n * kChannels)) {
                if (++tries > 1000) {
                    std::fprintf(stderr, "[capture] ring overflow, dropped %u frames\n", n);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

void AudioPipeline::renderThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE mmcss = EnterProAudio();

    const UINT32 periodFrames = renderer_.bufferFrames();
    std::vector<float> buf(periodFrames * kChannels);

    while (running_) {
        if (!renderer_.WaitForBuffer(500)) continue;

        // Fill the device buffer back up to capacity each period.
        const UINT32 padding = renderer_.GetPadding();
        const UINT32 want = (padding < periodFrames) ? (periodFrames - padding) : 0;
        if (want == 0) continue;

        // Pull processed audio if a full block is available, else render silence
        // (keeps the device buffer from underflowing; partial data stays queued).
        const std::size_t availFrames = capToDsp_.size() / kChannels;
        if (availFrames >= want) {
            capToDsp_.pop(buf.data(), want * kChannels);
        } else {
            std::memset(buf.data(), 0, want * kChannels * sizeof(float));
        }

        dsp_.process(buf.data(), want);

        float peak = 0.0f;
        for (std::size_t i = 0; i < want * kChannels; ++i) {
            const float a = std::fabs(buf[i]);
            if (a > peak) peak = a;
        }
        outputPeakDb_.store(20.0f * std::log10(peak + 1e-12f), std::memory_order_relaxed);

        renderer_.Write(buf.data(), want);
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

} // namespace sso::audio
