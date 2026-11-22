#pragma once
#include <cmath>

namespace sso::dsp {

// Convert a dB gain to a linear amplitude.
inline float dbToLin(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Convert a linear amplitude to dB (clamped floor to avoid -inf).
inline float linToDb(float lin) {
    return 20.0f * std::log10(lin + 1e-12f);
}

// Per-sample one-pole smoothing coefficient that reaches ~63% of the target
// in `ms` milliseconds at `sampleRate` Hz.
inline float msToCoeff(float ms, float sampleRate) {
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * sampleRate));
}

} // namespace sso::dsp
