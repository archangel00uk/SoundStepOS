# SoundStepOS — System Architecture

A lightweight, low-latency Windows desktop audio processor for competitive
tactical gaming (PUBG / `TslGame.exe`). It captures *only* the game's rendered
audio via the Windows Application Loopback API, applies a PUBG-tuned DSP chain,
and routes the result to your default playback device. It never reads game
memory, never injects/hooks a DLL, and never touches BattlEye/ACE — it operates
purely on the audio signal at the OS audio-engine level.

---

## 1. Text-based Architecture Diagram

```
                          ┌─────────────────────────────────────────────────────────────┐
                          │                         main thread                         │
                          │  CoInitializeEx(MTA) · polls CreateToolhelp32Snapshot      │
                          │  for TslGame.exe every 1 s · starts/stops AudioPipeline    │
                          └───────────────▲───────────────────────────┬────────────────┘
                                          │ found PID                  │ process gone
                          ┌───────────────┴───────────────────────────▼────────────────┐
                          │                    AudioPipeline (orchestrator)            │
                          └──┬───────────────────────────────────────────┬─────────────┘
                             │                                           │
        ┌────────────────────▼──────────────────┐   ┌────────────────────▼─────────────────┐
        │   CAPTURE THREAD (MMCSS "Pro Audio") │   │   RENDER/DSP THREAD (MMCSS "Pro      │
        │                                      │   │   Audio")  = the master clock         │
        │  ActivateAudioInterfaceAsync         │   │                                       │
        │  (VAD\Process_Loopback,              │   │  WaitForBuffer(event)                 │
        │   PID = TslGame.exe)                 │   │  GetCurrentPadding → free frames      │
        │  IAudioCaptureClient                 │   │  pop `free` frames from ring          │
        │  WaitForData(event)                  │   │  PubgPipeline::process                │
        │  GetBuffer/ReleaseBuffer             │   │  IAudioRenderClient Write             │
        └──────────────┬───────────────────────┘   └───────────────────▲───────────────────┘
                       │ push() (SPSC)                                 │ pop() (SPSC)
                       ▼                                                │
              ┌──────────────────────────────┐                        │
              │  capToDsp_ lock-free ring    │────────────────────────┘
              │  (float32 interleaved)       │
              └──────────────────────────────┘
                       ▲
                       └───────── WASAPI shared-mode engine (mix format)
```

### Data formats
- **Sample format:** IEEE float32, interleaved stereo (`L R L R …`).
- **Sample rate:** the default render device's mix rate (queried at start,
  typically 48 kHz); both capture and render are configured to it.
- **Block size:** one render-engine period per iteration (typically 480 frames /
  10 ms).

---

## 2. Threading model & latency budget

| Component | Thread | Role | Real-time class |
|---|---|---|---|
| Process monitor | `main` | Poll for `TslGame.exe`, start/stop pipeline | — |
| Capture | `captureThread_` | WASAPI loopback → `capToDsp_` ring | MMCSS *Pro Audio* |
| DSP + Render | `renderThread_` | `capToDsp_` → `PubgPipeline` → render client | MMCSS *Pro Audio* |

The **render thread is the master clock**. On every render event it computes how
many frames the device buffer can accept, pulls exactly that many from the ring
(silence if none are ready), runs them through the DSP, and writes them. Because
capture and render share the same endpoint (and thus the same audio-engine
clock), coupling DSP consumption to the render clock eliminates the
capture/render drift that previously caused the audio to degrade ("go fuzzy")
over time. The single lock-free ring is the only cross-thread handoff.

### Latency budget (typical)
| Stage | Latency |
|---|---|
| WASAPI shared-mode engine period (capture) | ~10 ms |
| Ring buffer residency (`capToDsp_`, ~1 period) | ~10 ms |
| DSP block processing (1 engine period) | <1 ms |
| Limiter lookahead | ~1.5 ms |
| WASAPI render engine period | ~10 ms (lower with `IAudioClient3` min period) |
| **Total** | **~20–30 ms** |

To push toward the low end, the renderer uses `IAudioClient3::
InitializeSharedAudioStream` with the engine's *minimum* period when the device
supports it (falling back to classic 20 ms event-driven `Initialize`). CPU cost
of the DSP chain is a handful of multiply-adds per sample; it is far below 1%
of a single core on any modern CPU.

### Lock-free SPSC ring buffer
`SPSCRingBuffer<T>` (see `src/audio/ring_buffer.h`) uses two monotonically
increasing 64-bit indices (producer `write_`, consumer `read_`) on separate
cache lines to avoid false sharing. Capacity is a power of two so indexing is a
single AND-mask. Each buffer has exactly one producer and one consumer, so no
CAS loops are required.

---

## 3. WASAPI process-loopback capture (zero-interference)

1. `main` locates `TslGame.exe`'s PID via `CreateToolhelp32Snapshot`.
2. `ProcessLoopbackCapture::Start(pid)` builds an
   `AUDIOCLIENT_ACTIVATION_PARAMS`:
   ```cpp
   params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
   params.ProcessLoopbackParams.TargetProcessId   = pid;
   params.ProcessLoopbackParams.ProcessLoopbackMode =
       PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
   ```
3. It calls `ActivateAudioInterfaceAsync(L"VAD\\Process_Loopback", …)` wrapped in
   a `PROPVARIANT` blob. The completion handler implements `IAgileObject` so it
   fires correctly from the MTA.
4. The returned `IAudioClient` is initialized in **shared mode** with
   `AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
   AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` requesting float32 stereo. Note: the
   loopback client does **not** implement `GetMixFormat` (it returns
   `E_NOTIMPL`), so the mix format is obtained from the default render endpoint
   via a separate `IAudioClient`.
5. Capture packets are drained with `IAudioCaptureClient::GetBuffer` /
   `ReleaseBuffer` and copied into the ring buffer.

Because this uses the OS loopback of the *render endpoint*, the game is
completely unaware of the capture — it is neither a virtual audio cable nor a
hook, and it cannot trip anti-cheat integrity checks. Closing the game triggers
`Stop()`, which releases the `IAudioClient` and restores normal routing.

> **Requirement:** Windows 10 build 20348 (Server 2022) or Windows 11 — the
> `VAD\Process_Loopback` virtual device is not available on earlier Windows 10
> consumer builds. (The SDK guard uses `NTDDI_WIN10_FE`, but the runtime feature
> shipped with build 20348.)

---

## 4. DSP pipeline

The chain is assembled in `src/dsp/pubg_pipeline.h` and processes interleaved
stereo blocks in place:

1. **Steep high-pass (100 Hz, 4th order).** Two cascaded 2nd-order Butterworth
   biquads (24 dB/oct) strip red-zone explosions, vehicle engine rumble, and
   wind below the footstep band.
2. **Footstep emphasis EQ.** Three minimum-phase peaking filters at ~250 Hz,
   ~1.2 kHz, and ~3 kHz emphasize the "thud", "stomp", and "scuff/crunch"
   components of footstep signatures (150 Hz–4.5 kHz).
3. **Upward compressor.** Lifts low-amplitude, distant footsteps (crouch-walk /
   slow-peek at 20–50 m) by boosting signals *below* a threshold.
4. **Transient shaper.** A fast/slow envelope follower adds punch to footstep
   attack edges.
5. **Self-footstep suppressor.** Mid/Side decomposition ducks center-panned
   (mono) transients — the player's own steps — while passing off-center enemy
   impulses.
6. **Brickwall limiter (lookahead).** A short delay line lets gain reduction
   begin before gunshot/flashbang/vehicle transients arrive, hard-capping output
   at the ceiling without clipping.

### Phase-coherence rules (directional accuracy)
- The linear filter bank is built from **identical coefficients for L and R**, so
  any phase shift is equal on both channels → inter-channel phase, ITD, and HRTF
  azimuth/elevation cues are preserved.
- All dynamics stages (upward compressor, transient shaper, limiter) derive a
  **single linked gain from the mono peak** and apply it identically to L and R,
  so the relative L/R level and time-of-arrival (the distance/azimuth cues) are
  never smeared.
- The limiter's lookahead delay is applied equally to both channels.

See `src/dsp/*.h` for the full implementations; every tunable is in
`src/config.h`.

---

## 5. Lifecycle

```
[no game] ──poll(1s)──▶ [TslGame.exe detected] ──▶ capture_.Start(pid)
     ▲                                              renderer_.Start(rate)
     │                                              spawn capture + render threads
     │                                                          │
     └────────────────── [process gone] ◀── poll(1s) ◀──────────┘
                            capture_.Stop() / renderer_.Stop() / join threads
```

---

## 6. Live control surface

The UI thread (Win32 window + tray icon, `src/gui/gui_controller.cpp`) never
touches the audio buffers. It writes to a lock-free `PipelineControl` of
atomics and, for parameter changes, publishes a new `Config`:

- **Bypass / per-feature toggles** are `std::atomic<bool>` flags read by the DSP
  stage once per block — zero locking on the real-time path.
- **Tuning sliders / preset changes** mutate a `Config`, then copy it into a
  mutex-guarded slot and set an atomic "dirty" flag; the DSP stage swaps it in at
  the start of the next block via `PubgPipeline::publish`/`configure`.

Toggle/parameter state persists across game engage/disengage cycles because it
lives in `AudioPipeline`, not in the DSP objects.

## 7. File layout

```
src/
  main.cpp                         entry point, process watcher thread, GUI setup
  config.h                         all tunable DSP parameters
  dsp/
    biquad.h                       RBJ cookbook biquads (HP/peaking/shelves)
    dsp_util.h                     dB↔linear, ms→smoothing-coefficient
    upward_compressor.h            distant-footstep lift
    transient_shaper.h             attack-edge punch
    limiter.h                      lookahead brickwall limiter
    self_footstep_suppressor.h     center-panned transient duck
    pubg_pipeline.h                full chain assembler + PipelineControl
  audio/
    ring_buffer.h                  lock-free SPSC ring buffer
    process_loopback_capture.h/.cpp WASAPI process-loopback capture
    wasapi_renderer.h/.cpp         low-latency shared-mode render
    process_monitor.h/.cpp         TslGame.exe PID discovery
    audio_pipeline.h/.cpp          thread orchestration + control surface
  gui/
    presets.h                      Footstep / Balanced / Gunshot-safe presets
    gui_controller.h/.cpp          Win32 window + tray + sliders + F8 hotkey
```

See **TUNING_GUIDE.md** for the PUBG calibration workflow and **README.md** for
build/run instructions.
