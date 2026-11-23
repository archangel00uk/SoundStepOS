#pragma once
#include <cstdint>

namespace sso {

// --- Global audio format (shared by capture, DSP, and render) ---
constexpr uint32_t kSampleRate  = 48000;
constexpr uint32_t kChannels    = 2;
constexpr uint32_t kBlockFrames = 256;   // DSP block size (5.33 ms @ 48 kHz)

// --- Target game executable ---
constexpr wchar_t kTargetProcessName[] = L"TslGame.exe";

// --- Runtime configuration -------------------------------------------------
// Every real-time DSP parameter lives here so the pipeline can be retuned
// without touching DSP code. All EQ gains are in dB, times in ms.
//
struct Config {
    // --- Steep high-pass (strip red-zone / vehicle rumble / wind below ~100 Hz) ---
    float hpFreqHz = 100.0f;

    // --- PUBG footstep emphasis EQ (peaking filters) ---
    struct Band { float freqHz; float gainDb; float q; };
    Band eqBands[3] = {
        {  250.0f, +3.0f, 1.0f },   // low-mid "thud" / body (150-400 Hz)
        { 1200.0f, +4.0f, 1.2f },   // mid "stomp" / contact (600 Hz-1.6 kHz)
        { 3000.0f, +5.0f, 1.4f },   // high "scuff/crunch" / surface detail (2-4.5 kHz)
    };

    // --- Upward compressor (lift distant/crouch footsteps, ~20-50 m) ---
    bool  upCompEnabled     = true;
    float upCompThresholdDb = -48.0f;   // lift levels below this
    float upCompRatio       = 1.5f;     // > 1 = more lift (upward)
    float upCompAttackMs    = 8.0f;     // level detector attack (avoid pumping)
    float upCompReleaseMs   = 120.0f;
    float upCompMaxGainDb   = 14.0f;    // cap lift so the noise floor stays sane

    // --- Transient shaper (add punch to footstep attack edges) ---
    bool  transEnabled   = true;
    float transAmount    = 2.5f;        // dB boost on detected attacks
    float transAttackMs  = 1.0f;        // fast envelope
    float transReleaseMs = 40.0f;       // slow envelope
    float transSenseDb   = 3.0f;        // min level rise to count as a transient

    // --- Self-footstep suppression (duck center-panned transients) ---
    bool  selfSuppressEnabled    = true;
    float selfSuppressDepthDb    = 9.0f;   // attenuation of own steps
    float selfSuppressAttackMs   = 2.0f;
    float selfSuppressReleaseMs  = 90.0f;
    float selfSuppressMonoRatio  = 3.0f;   // mid/side ratio above which = "self"

    // --- Brickwall limiter (gunshot / flashbang / vehicle ceiling) ---
    float limiterCeilingDb    = -1.0f;   // output ceiling (dBFS)
    float limiterLookaheadMs  = 1.5f;    // lookahead to catch gunshot leading edge
    float limiterReleaseMs    = 60.0f;

    // --- Downward compressor (reduce loud gunshots, leave footsteps alone) ---
    float gunshotReduceAmountDb    = 0.0f;    // 0 = off; max loud-signal reduction
    float gunshotReduceThresholdDb = -20.0f;  // reduce signals above this level
    float gunshotReduceRatio       = 3.0f;    // > 1 = more reduction above threshold
    float gunshotReduceAttackMs    = 2.0f;
    float gunshotReduceReleaseMs   = 100.0f;
};

} // namespace sso
