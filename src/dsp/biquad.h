#pragma once
#include <cmath>
#include <cstddef>

namespace sso::dsp {

// Transposed Direct Form II biquad (RBJ Audio-EQ Cookbook coefficients).
// a0 is normalized to 1.0. Stateful: one instance per channel.
struct Biquad {
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }

    float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void process(float* io, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) io[i] = process(io[i]);
    }

    static Biquad highPass(float fs, float freq, float q);
    static Biquad peaking(float fs, float freq, float gainDb, float q);
    static Biquad highShelf(float fs, float freq, float gainDb, float q);
    static Biquad lowShelf(float fs, float freq, float gainDb, float q);
};

inline Biquad Biquad::highPass(float fs, float freq, float q) {
    Biquad f;
    const float w0 = 6.283185307179586f * freq / fs;
    const float cosw = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    f.b0 = (1.0f + cosw) / 2.0f / a0;
    f.b1 = -(1.0f + cosw) / a0;
    f.b2 = (1.0f + cosw) / 2.0f / a0;
    f.a1 = -2.0f * cosw / a0;
    f.a2 = (1.0f - alpha) / a0;
    return f;
}

inline Biquad Biquad::peaking(float fs, float freq, float gainDb, float q) {
    Biquad f;
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 6.283185307179586f * freq / fs;
    const float cosw = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha / A;
    f.b0 = (1.0f + alpha * A) / a0;
    f.b1 = -2.0f * cosw / a0;
    f.b2 = (1.0f - alpha * A) / a0;
    f.a1 = -2.0f * cosw / a0;
    f.a2 = (1.0f - alpha / A) / a0;
    return f;
}

inline Biquad Biquad::highShelf(float fs, float freq, float gainDb, float q) {
    Biquad f;
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 6.283185307179586f * freq / fs;
    const float cosw = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;
    const float a0 = (A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAalpha;
    f.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAalpha)) / a0;
    f.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
    f.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw - twoSqrtAalpha)) / a0;
    f.a1 = (-2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
    f.a2 = ((A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAalpha) / a0;
    return f;
}

inline Biquad Biquad::lowShelf(float fs, float freq, float gainDb, float q) {
    Biquad f;
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 6.283185307179586f * freq / fs;
    const float cosw = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;
    const float a0 = (A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAalpha;
    f.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAalpha)) / a0;
    f.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
    f.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAalpha)) / a0;
    f.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
    f.a2 = ((A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAalpha) / a0;
    return f;
}

} // namespace sso::dsp
