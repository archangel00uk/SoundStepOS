#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "dsp_util.h"

namespace sso::dsp {

// Transient shaper: boosts the attack edge of short impulses (footstep
// impacts) using a fast/slow envelope follower. The gain is derived from the
// linked mono peak and applied identically to L/R, keeping directionality and
// phase coherence intact.
struct TransientShaper {
    bool enabled = true;

    float fastC = 0.0f;   // fast envelope (attack)
    float slowC = 0.0f;   // slow envelope (release/body)
    float amountDb = 2.5f;
    float senseLin = 1.41f; // transient sensitivity in linear terms

    float fastEnv = 0.0f;
    float slowEnv = 0.0f;

    void configure(float fs, float attackMs, float releaseMs,
                   float amountDb_, float senseDb_) {
        fastC = msToCoeff(attackMs, fs);
        slowC = msToCoeff(releaseMs, fs);
        amountDb = amountDb_;
        senseLin = dbToLin(senseDb_);
    }

    void process(float* io, std::size_t frames) {
        if (!enabled) return;
        for (std::size_t i = 0; i < frames; ++i) {
            const float l = io[2 * i];
            const float r = io[2 * i + 1];
            const float peak = std::max(std::fabs(l), std::fabs(r));

            fastEnv += fastC * (peak - fastEnv);
            slowEnv += slowC * (peak - slowEnv);

            const float diff = fastEnv - slowEnv;
            float g = 1.0f;
            if (diff > 0.0f) {
                const float strength = std::min(1.0f, diff / senseLin);
                g = dbToLin(amountDb * strength);
            }
            io[2 * i] = l * g;
            io[2 * i + 1] = r * g;
        }
    }
};

} // namespace sso::dsp
