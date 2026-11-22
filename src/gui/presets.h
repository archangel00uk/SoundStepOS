#pragma once
#include "../config.h"

namespace sso::presets {

enum class Preset { Footstep, Balanced, GunshotSafe };

// Builds a Config for a named tuning preset. "Balanced" returns the shipped
// defaults; the other two are competitive-oriented starting points that you can
// refine in TUNING_GUIDE.md.
inline Config make(Preset p) {
    Config c;

    switch (p) {
        case Preset::Footstep: {
            c.eqBands[0] = { 250.0f, +4.0f, 1.0f };
            c.eqBands[1] = { 1200.0f, +6.0f, 1.2f };
            c.eqBands[2] = { 3000.0f, +7.0f, 1.4f };
            c.upCompThresholdDb = -46.0f;
            c.upCompMaxGainDb = 16.0f;
            c.transAmount = 3.0f;
            c.selfSuppressDepthDb = 8.0f;
            break;
        }
        case Preset::GunshotSafe: {
            c.eqBands[0] = { 250.0f, +2.0f, 1.0f };
            c.eqBands[1] = { 1200.0f, +3.0f, 1.2f };
            c.eqBands[2] = { 3000.0f, +3.0f, 1.4f };
            c.upCompMaxGainDb = 8.0f;
            c.limiterCeilingDb = -4.0f;
            c.limiterLookaheadMs = 2.0f;
            c.limiterReleaseMs = 80.0f;
            break;
        }
        case Preset::Balanced:
        default:
            break;   // defaults
    }
    return c;
}

} // namespace sso::presets
