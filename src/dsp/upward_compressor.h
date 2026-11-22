#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "dsp_util.h"

namespace sso::dsp {

// Upward compressor: lifts signals *below* the threshold (distant / crouch
// footsteps) while leaving loud content untouched. Uses a linked mono level
// detector so the same gain is applied to L and R, preserving the stereo image
// and inter-channel phase (HRTF / binaural cues stay intact).
struct UpwardCompressor {
    bool enabled = true;

    float attack = 0.0f;
    float release = 0.0f;
    float thresholdDb = -48.0f;
    float ratio = 1.5f;      // > 1 = more lift
    float maxGainDb = 14.0f;

    float envDb = -120.0f;   // smoothed linked level in dB

    void configure(float fs, float attackMs, float releaseMs,
                   float thrDb, float r, float maxGainDb_) {
        attack = msToCoeff(attackMs, fs);
        release = msToCoeff(releaseMs, fs);
        thresholdDb = thrDb;
        ratio = r;
        maxGainDb = maxGainDb_;
    }

    void process(float* io, std::size_t frames) {
        if (!enabled) return;
        for (std::size_t i = 0; i < frames; ++i) {
            const float l = io[2 * i];
            const float r = io[2 * i + 1];
            const float peak = std::max(std::fabs(l), std::fabs(r));
            const float levelDb = linToDb(peak);

            const float c = (levelDb > envDb) ? attack : release;
            envDb += c * (levelDb - envDb);

            float g = 1.0f;
            if (envDb < thresholdDb) {
                float boostDb = (thresholdDb - envDb) * (1.0f - 1.0f / ratio);
                if (boostDb > maxGainDb) boostDb = maxGainDb;
                g = dbToLin(boostDb);
            }
            io[2 * i] = l * g;
            io[2 * i + 1] = r * g;
        }
    }
};

} // namespace sso::dsp
