# PUBG Calibration & Tuning Guide

This guide explains how to record reference audio from PUBG's **Training Mode**
or a **Replay**, inspect its spectrum, and dial in every DSP parameter in
`src/config.h` for maximum footstep clarity and positional accuracy.

Everything you tune lives in **`src/config.h`** (struct `Config`). Change a
value, rebuild, relaunch. No game memory is ever touched.

---

## 0. Prerequisites

- A spectrum analyzer / audio editor with a spectrogram view:
  - **Audacity** (free) — use *Analyze → Plot Spectrum* and the spectrogram view.
  - **REAPER** (free trial) — *ReaEQ* + *ReaSpect* + *ReaFIR* are ideal.
  - **spek** (free) — quick spectral overview.
- A quiet room and headphones or stereo speakers set to **stereo** (disable any
  Windows spatial audio / virtualization first — see §5).

---

## 1. Record a reference sample

### Option A — Training Mode (recommended)
1. Launch PUBG → **Training Mode**.
2. Enable Windows stereo mix / your own recorder, or record with
   **OBS** (set audio to capture the game, 48 kHz, stereo, no filters).
3. Record each surface in isolation for ~20 s each while you *walk, run, and
   crouch-walk*:
   - **Concrete / building interiors** (Pochinki, Yasnaya) — indoor "thud".
   - **Wooden floorboards** — hollow knock.
   - **Metal catwalks / shipping containers** — metallic ring/scrape.
   - **Dirt / grass** — soft crunch.
   - **Sand** — gritty scuff.
4. Also record a **gunshot burst** (Beryl M762, SLR, AWM), a **flashbang**, and a
   **vehicle engine** idling — these are your limiter test signals.

### Option B — Replay
1. Play a **Replay**, position the free camera near a squad of bots/players
   moving on the surface you care about, and record their footsteps at
   **known distances** (10 m / 30 m / 50 m).
2. This gives you calibrated distance-vs-loudness data for the compressor.

> Record at **48 kHz stereo float/PCM**. The capture pipeline requires
> 48 kHz stereo.

---

## 2. Analyze the spectrum (find the real footstep bands)

Open a footstep clip in Audacity/REAPER and look at the spectrum/spectrogram of
a *single isolated step*:

1. Zoom to one step transient (≈50–150 ms long).
2. Note the **dominant frequency components** and their decay:
   - **Concrete:** energy concentrated ~200 Hz–1 kHz, strong 300–600 Hz thud.
   - **Wood:** resonant peaks often ~250–500 Hz (thud) plus a 1–3 kHz knock.
   - **Metal:** strong 2–4.5 kHz ring, sometimes up to 8 kHz.
   - **Dirt/grass:** broadband 400 Hz–2 kHz, weaker high end.
   - **Sand:** 1–3 kHz gritty band.
3. Note the **sub-100 Hz rumble** during red zones / vehicles — this is what the
   high-pass removes.

### Map results to `eqBands`
The default three bands (250 Hz / 1.2 kHz / 3 kHz) are a starting point. If your
analysis shows a dominant metal ring at 3.8 kHz, move the third band up. If
indoor concrete "thud" is strongest at 350 Hz, move the first band there.

```cpp
Band eqBands[3] = {
    {  250.0f, +3.0f, 1.0f },   // low-mid "thud"
    { 1200.0f, +4.0f, 1.2f },   // mid "stomp"
    { 3000.0f, +5.0f, 1.4f },   // high "scuff/crunch"
};
```

- `gainDb` > 0 emphasizes; keep total boost ≤ ~+12 dB to avoid masking.
- Lower `q` = wider bandwidth (more forgiving); raise `q` to notch a specific
  resonant ring.

### High-pass cutoff
Set `hpFreqHz` just below the lowest useful footstep component. PUBG footsteps
start around 150 Hz, so **100 Hz** is a good default. If you hear your own
voice/breathing artifacts or too much red-zone thump leaking through, raise it
to 120 Hz; if you lose the low "thud" of a nearby enemy, lower to 90 Hz.

---

## 3. Tune the upward compressor (distant footsteps)

The upward compressor lifts *quiet* signals. In `Config`:

```cpp
float upCompThresholdDb = -48.0f;   // lift below this level
float upCompRatio       = 1.5f;     // > 1 = more lift
float upCompAttackMs    = 8.0f;
float upCompReleaseMs   = 120.0f;
float upCompMaxGainDb   = 14.0f;    // cap the lift
```

**Procedure:**
1. Set `upCompEnabled = true`, `upCompMaxGainDb = 0` (effectively off) and play a
   match. Note the **quietest enemy footstep you can barely hear** at 30–50 m.
2. Set `upCompThresholdDb` to *just above* that barely-audible level. You can
   estimate it by watching the level meter: distant crouch-walk steps in PUBG
   typically sit around **−45 to −55 dBFS**.
3. Raise `upCompMaxGainDb` until distant steps are clearly audible but the
   **noise floor does not pump**. Stop if you hear a "breathing" background.
4. `upCompRatio` controls how aggressively levels below threshold are pulled up:
   - `1.2` subtle, `2.0` aggressive. Start at `1.5`.
5. **Attack** (`upCompAttackMs`): too fast → the compressor chases gunshots and
   ducks footsteps; too slow → slow-peek steps get missed. 5–15 ms is typical.
6. **Release** (`upCompReleaseMs`): how long the lift persists after a loud
   sound. 100–200 ms feels natural; shorter makes it pumpier.

> **Distance-cue rule:** the upward compressor must stay *below* the gunshot
> threshold. Because it only boosts *below* `upCompThresholdDb`, loud content is
> untouched, so relative loudness (and therefore distance) is preserved for
> anything above threshold. Keep `upCompThresholdDb` below your limiter's
> operating range.

---

## 4. Tune the transient shaper (footstep "punch")

```cpp
float transAmount    = 2.5f;   // dB boost on attacks
float transAttackMs  = 1.0f;   // fast envelope
float transReleaseMs = 40.0f;  // slow envelope
float transSenseDb   = 3.0f;   // min rise to count as transient
```

- `transAmount`: how many dB to add to the leading edge. 1–4 dB is the sweet
  spot; too much makes footsteps clicky/artificial.
- `transAttackMs` (fast envelope) sets how quickly a rising edge is recognized.
  0.5–2 ms catches footstep attacks; longer misses the very start.
- `transReleaseMs` (slow envelope) sets the "body" reference. Keep it well above
  the attack so sustained notes aren't boosted.
- `transSenseDb`: the minimum fast/slow ratio to count as a transient. Lower =
  more sensitive (boosts more things); raise it if gunshots start sounding
  over-hyped.

---

## 5. Tune the self-footstep suppressor

```cpp
float selfSuppressDepthDb   = 9.0f;
float selfSuppressAttackMs  = 2.0f;
float selfSuppressReleaseMs = 90.0f;
float selfSuppressMonoRatio = 3.0f;
```

The suppressor ducks **center-panned** transients (your own steps are mono/dead-
center; enemy steps from an azimuth are off-center and carry side energy).

- `selfSuppressDepthDb`: how much to duck your own steps. Start at 6 dB, raise
  until your own footsteps stop masking enemy audio. Too high removes the
  useful feedback of your own movement.
- `selfSuppressMonoRatio`: the Mid/Side energy ratio above which a transient is
  treated as "self". Lower = more aggressive (ducks more center content),
  higher = only very mono sounds are ducked.
- **Important limitation:** an enemy *dead ahead* or *directly behind* is also
  center-panned and will be ducked. Use the ratio to balance this. When an enemy
  is dead-center, rely on the un-ducked side residual and the compressor.

> **Proposed extension (full spectral-gating):** the current implementation is a
> time-domain Mid/Side duck. A more precise method is a **short-time FFT
> spectral gate** that (a) detects footstep transients in the Mid spectrum,
> (b) estimates their cadence (~1.5–2.5 Hz walking, ~3–4 Hz running), and
> (c) subtracts only periodic, center-panned, high-proximity impacts. This
> preserves enemy steps even when they are momentarily centered, because enemy
> cadence is uncorrelated with yours. See §7 for how to validate it.

---

## 6. Tune the brickwall limiter (gunshots)

```cpp
float limiterCeilingDb   = -1.0f;   // output ceiling
float limiterLookaheadMs = 1.5f;    // lookahead
float limiterReleaseMs   = 60.0f;
```

Play back your gunshot/flashbang/vehicle clips through the pipeline:

- `limiterCeilingDb`: hard output ceiling in dBFS. `-1.0` prevents intersample
  clipping; use `-3.0` to `-6.0` if you want aggressive hearing protection on
  the AWM/SLR.
- `limiterLookaheadMs`: how early gain reduction starts before a peak. 1–2 ms is
  enough to catch a gunshot leading edge without audible distortion. Longer
  smooths the catch but adds latency.
- `limiterReleaseMs`: recovery after a gunshot. 40–100 ms keeps the limiter from
  "pumping" between shots. If footsteps right after a shot are too quiet,
  shorten it; if the limiter flutters, lengthen it.

**Verify no clipping:** record the processed output and confirm the peak never
exceeds `limiterCeilingDb` and that a gunshot immediately followed by a footstep
still reveals the footstep.

---

## 7. Validation checklist

1. **Directional test:** in Training Mode, have a bot circle you. Confirm
   azimuth tracking is stable (no L/R image wandering, no phantom center
   pulling). If it smears, you have a stereo-incoherent stage — check that all
   dynamics use linked gain (they do by construction).
2. **Elevation test:** stand under/over a wooden building while a bot runs
   upstairs/downstairs. Upstairs steps should keep their "hollow above" cue
   (high-freq ceiling reflection) vs. downstairs "thud below". The high shelf of
   the EQ band at 3 kHz is the main lever here.
3. **Distance test:** replay at 10/30/50 m and confirm relative loudness scales
   naturally — the upward compressor should only lift the *quiet* far steps,
   never compress the near ones.
4. **Latency test:** fire a gun and confirm the audible result feels immediate.
   If it lags, reduce `kBlockFrames` in `config.h` and ensure the renderer is
   taking the `IAudioClient3` low-latency path (see README).
5. **CPU test:** Task Manager → confirm the process stays < 1% CPU and audio
   never glitches (no dropouts/clicks).

---

## 8. Recommended starting point (defaults)

The shipped `Config` defaults are a conservative, competitive-ready baseline:

| Parameter | Default | Rationale |
|---|---|---|
| `hpFreqHz` | 100 Hz | remove rumble, keep footstep thud |
| EQ bands | 250 / 1.2k / 3k Hz | PUBG thud/stomp/scuff |
| `upCompThresholdDb` | −48 dB | lift distant crouch steps |
| `upCompMaxGainDb` | 14 dB | audible lift, no noise pump |
| `limiterCeilingDb` | −1 dBFS | no clipping |
| `selfSuppressDepthDb` | 9 dB | tame own footsteps |

Start here, follow §2–§6 in order (EQ → compressor → transient → suppressor →
limiter), and make **one change at a time** while listening in a live match.
