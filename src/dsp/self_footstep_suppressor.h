#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "dsp_util.h"

namespace sso::dsp {

// Self-footstep suppressor (baseline): ducks *center-panned* transients.
//
// The player's own footsteps and weapon-handling foley are (a) dead-center in
// the stereo field (Mid >> Side) and (b) loud/proximate. Enemy steps from an
// azimuth are off-center and therefore carry significant Side energy.
//
// We decompose to Mid/Side, detect a transient in the Mid channel whose
// Mid/Side energy ratio exceeds a threshold (=> center-panned => likely self),
// and duck only the Mid while leaving Side untouched. L/R are reconstructed
// afterward so enemy (off-center) impulses pass through unchanged.
//
// Known limitation (documented in TUNING_GUIDE.md): an enemy dead ahead or
// directly behind is also center-panned and will be ducked. A full solution
// adds a footstep-periodicity estimator (see docs for the proposed extension).
struct SelfFootstepSuppressor {
    bool enabled = true;

    float fastC = 0.0f;     // fast mid envelope
    float slowC = 0.0f;     // slow mid + side envelope
    float releaseC = 0.0f;  // duck recovery

    float monoRatioThresh = 3.0f; // mid/side ratio above which = "self"
    float duckLin = 0.3548f;      // 10^(-depth/20)
    float sensLin = 2.0f;         // transient detection sensitivity

    float fastM = 0.0f, slowM = 0.0f, slowS = 0.0f;
    float g = 1.0f;               // current duck gain (applied to Mid)

    void configure(float fs, float attackMs, float releaseMs, float depthDb,
                   float monoRatio, float senseDb) {
        fastC = msToCoeff(attackMs, fs);
        slowC = msToCoeff(releaseMs, fs);
        releaseC = msToCoeff(releaseMs, fs);
        monoRatioThresh = monoRatio;
        duckLin = dbToLin(-depthDb);
        sensLin = dbToLin(senseDb);
    }

    void process(float* io, std::size_t frames) {
        if (!enabled) return;
        for (std::size_t i = 0; i < frames; ++i) {
            const float l = io[2 * i];
            const float r = io[2 * i + 1];

            const float m = 0.5f * (l + r);
            const float s = 0.5f * (l - r);

            const float am = std::fabs(m);
            const float as = std::fabs(s);

            fastM += fastC * (am - fastM);
            slowM += slowC * (am - slowM);
            slowS += slowC * (as - slowS);

            const bool transient = (fastM > slowM * sensLin) && (am > 1e-4f);
            const bool center = (slowS * monoRatioThresh) < slowM;

            const float target = (transient && center) ? duckLin : 1.0f;
            g += releaseC * (target - g);

            const float mOut = m * g;
            io[2 * i] = mOut + s;
            io[2 * i + 1] = mOut - s;
        }
    }
};

} // namespace sso::dsp
