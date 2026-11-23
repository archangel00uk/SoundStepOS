#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "dsp_util.h"

namespace sso::dsp {

// Downward compressor ("gunshot reduce"): attenuates signals ABOVE a threshold
// (loud transients such as gunshots, flashbangs, vehicles) while leaving
// quieter content (footsteps) untouched. A linked mono level detector applies
// the same gain to L and R, preserving phase / directional cues.
struct DownwardCompressor {
    float thresholdDb = -20.0f;
    float ratio = 3.0f;
    float maxReductionDb = 0.0f;   // 0 = disabled
    float attack = 0.0f;
    float release = 0.0f;

    float envDb = -120.0f;   // smoothed linked level
    float gainDb = 0.0f;     // current gain reduction (<= 0)

    void configure(float fs, float thresholdDb_, float ratio_, float maxReductionDb_,
                   float attackMs, float releaseMs) {
        thresholdDb = thresholdDb_;
        ratio = ratio_;
        maxReductionDb = maxReductionDb_;
        attack = msToCoeff(attackMs, fs);
        release = msToCoeff(releaseMs, fs);
    }

    void process(float* io, std::size_t frames) {
        if (maxReductionDb <= 0.0f) return;

        for (std::size_t i = 0; i < frames; ++i) {
            const float l = io[2 * i];
            const float r = io[2 * i + 1];
            const float peak = std::max(std::fabs(l), std::fabs(r));
            const float levelDb = linToDb(peak);

            const float lc = (levelDb > envDb) ? attack : release;
            envDb += lc * (levelDb - envDb);

            float target = 0.0f;
            if (envDb > thresholdDb) {
                target = -(envDb - thresholdDb) * (1.0f - 1.0f / ratio);
                if (target < -maxReductionDb) target = -maxReductionDb;
            }

            const float gc = (target < gainDb) ? attack : release;
            gainDb += gc * (target - gainDb);

            const float g = dbToLin(gainDb);
            io[2 * i] = l * g;
            io[2 * i + 1] = r * g;
        }
    }
};

} // namespace sso::dsp
