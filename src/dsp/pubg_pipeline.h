#pragma once
#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

#include "../config.h"
#include "biquad.h"
#include "limiter.h"
#include "self_footstep_suppressor.h"
#include "transient_shaper.h"
#include "upward_compressor.h"

namespace sso::dsp {

// Live, lock-free control surface. Written by the UI thread, read by the DSP
// thread once per block. All flags are atomics so no lock is taken on the
// real-time path.
struct PipelineControl {
    std::atomic<bool> bypass{false};          // master on/off (true = passthrough)
    std::atomic<bool> enableEq{true};         // high-pass + footstep EQ
    std::atomic<bool> enableUpComp{true};
    std::atomic<bool> enableTransient{true};
    std::atomic<bool> enableSelfSuppress{true};
    std::atomic<bool> enableLimiter{true};
};

// Per-channel linear filter bank (high-pass + parametric EQ). Both channels
// are built from identical coefficients, so any phase shift they introduce is
// equal on L and R => inter-channel phase / ITD / HRTF cues are preserved.
struct ChannelFilterBank {
    std::vector<Biquad> stages;

    float process(float x) {
        for (auto& b : stages) x = b.process(x);
        return x;
    }
};

// Full PUBG-focused DSP pipeline. Processes interleaved float32 stereo
// buffers in place.
//
// Order:
//   1. Linear filter bank (HP 100 Hz + footstep emphasis EQ)
//   2. Upward compressor  (lift distant footsteps)
//   3. Transient shaper   (add attack punch)
//   4. Self-footstep suppressor (duck center-panned transients)
//   5. Brickwall limiter  (gunshot ceiling, final stage)
//
class PubgPipeline {
public:
    void setControl(PipelineControl* ctl) { control_ = ctl; }

    // Builds filters + configures all modules from a Config. Safe to call from
    // the DSP thread (on the reconfigure path) or before threads start.
    void configure(const Config& cfg, float sampleRate) {
        const float fs = sampleRate;
        sampleRate_ = sampleRate;

        left_.stages.clear();
        right_.stages.clear();

        // Steep high-pass: two cascaded 2nd-order Butterworth sections
        // => 4th order (24 dB/octave) below the cutoff.
        const Biquad hp = Biquad::highPass(fs, cfg.hpFreqHz, 0.70710678f);
        left_.stages.push_back(hp);
        left_.stages.push_back(hp);
        right_.stages.push_back(hp);
        right_.stages.push_back(hp);

        // Footstep emphasis peaking bands.
        for (const auto& band : cfg.eqBands) {
            const Biquad pk = Biquad::peaking(fs, band.freqHz, band.gainDb, band.q);
            left_.stages.push_back(pk);
            right_.stages.push_back(pk);
        }

        up_.configure(fs, cfg.upCompAttackMs, cfg.upCompReleaseMs,
                      cfg.upCompThresholdDb, cfg.upCompRatio, cfg.upCompMaxGainDb);
        trans_.configure(fs, cfg.transAttackMs, cfg.transReleaseMs,
                         cfg.transAmount, cfg.transSenseDb);
        self_.configure(fs, cfg.selfSuppressAttackMs, cfg.selfSuppressReleaseMs,
                        cfg.selfSuppressDepthDb, cfg.selfSuppressMonoRatio, cfg.transSenseDb);
        limiter_.configure(fs, cfg.limiterCeilingDb, cfg.limiterLookaheadMs, cfg.limiterReleaseMs);

        // Remember config-level enables for the runtime gating below.
        eqEnabled_ = true;
        upEnabled_ = cfg.upCompEnabled;
        transEnabled_ = cfg.transEnabled;
        selfEnabled_ = cfg.selfSuppressEnabled;
        limiterEnabled_ = true;
    }

    // Publish a new Config from any thread. The DSP thread picks it up at the
    // start of the next block and reconfigures (glitch-free enough for UI use).
    void publish(const Config& cfg) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_ = cfg;
        }
        pendingDirty_.store(true, std::memory_order_release);
    }

    void process(float* io, std::size_t frames) {
        if (control_ && control_->bypass.load(std::memory_order_relaxed)) return;

        if (pendingDirty_.exchange(false, std::memory_order_acq_rel)) {
            Config cfg;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                cfg = pending_;
            }
            configure(cfg, sampleRate_);
        }

        const bool eq = eqEnabled_ && (!control_ || control_->enableEq.load(std::memory_order_relaxed));
        const bool up = upEnabled_ && (!control_ || control_->enableUpComp.load(std::memory_order_relaxed));
        const bool tr = transEnabled_ && (!control_ || control_->enableTransient.load(std::memory_order_relaxed));
        const bool sf = selfEnabled_ && (!control_ || control_->enableSelfSuppress.load(std::memory_order_relaxed));
        const bool lm = limiterEnabled_ && (!control_ || control_->enableLimiter.load(std::memory_order_relaxed));

        if (eq) {
            for (std::size_t i = 0; i < frames; ++i) {
                io[2 * i] = left_.process(io[2 * i]);
                io[2 * i + 1] = right_.process(io[2 * i + 1]);
            }
        }
        if (up) up_.process(io, frames);
        if (tr) trans_.process(io, frames);
        if (sf) self_.process(io, frames);
        if (lm) limiter_.process(io, frames);
    }

private:
    PipelineControl* control_ = nullptr;

    std::mutex mutex_;
    Config pending_{};
    std::atomic<bool> pendingDirty_{false};
    float sampleRate_ = 48000.0f;

    bool eqEnabled_ = true;
    bool upEnabled_ = true;
    bool transEnabled_ = true;
    bool selfEnabled_ = true;
    bool limiterEnabled_ = true;

    ChannelFilterBank left_;
    ChannelFilterBank right_;
    UpwardCompressor up_;
    TransientShaper trans_;
    SelfFootstepSuppressor self_;
    Limiter limiter_;
};

} // namespace sso::dsp
