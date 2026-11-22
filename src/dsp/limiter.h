#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "dsp_util.h"

namespace sso::dsp {

// Brickwall peak limiter with lookahead. A short delay line lets the limiter
// start reducing gain *before* a loud transient (gunshot / flashbang / vehicle)
// reaches the output, so peaks never exceed the ceiling and never clip.
//
// Gain is derived from the maximum |sample| over the lookahead window and
// applied identically to L and R (equal gain + equal delay => no smearing of
// directional cues).
struct Limiter {
    float ceilingLin = 0.89125094f; // -1 dBFS
    float release = 0.0f;
    float gain = 1.0f;              // current applied gain

    std::vector<float> bufL;
    std::vector<float> bufR;
    std::vector<float> bufPeak;
    std::size_t pos = 0;

    void configure(float fs, float ceilingDb, float lookaheadMs, float releaseMs) {
        ceilingLin = dbToLin(ceilingDb);
        release = msToCoeff(releaseMs, fs);

        std::size_t look = static_cast<std::size_t>(lookaheadMs * 0.001f * fs) + 1;
        if (look < 1) look = 1;
        bufL.assign(look + 1, 0.0f);
        bufR.assign(look + 1, 0.0f);
        bufPeak.assign(look + 1, 0.0f);
        pos = 0;
        gain = 1.0f;
    }

    void process(float* io, std::size_t frames) {
        if (bufL.empty()) return;   // not configured -> passthrough

        const std::size_t size = bufL.size();
        for (std::size_t i = 0; i < frames; ++i) {
            const float l = io[2 * i];
            const float r = io[2 * i + 1];

            bufL[pos] = l;
            bufR[pos] = r;
            bufPeak[pos] = std::max(std::fabs(l), std::fabs(r));

            float m = 0.0f;
            for (float p : bufPeak) m = std::max(m, p);

            const float target = (m > ceilingLin) ? ceilingLin / m : 1.0f;
            if (target < gain) {
                gain = target;                 // instantaneous attack (lookahead applied)
            } else {
                gain += release * (target - gain); // smooth release
            }

            const std::size_t out = (pos + 1) % size;
            io[2 * i] = bufL[out] * gain;
            io[2 * i + 1] = bufR[out] * gain;
            pos = out;
        }
    }
};

} // namespace sso::dsp
