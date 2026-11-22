# SoundStepOS

A lightweight, low-latency, open-source Windows desktop audio processor for
competitive tactical gaming (PUBG: Battlegrounds). It captures **only**
`TslGame.exe`'s audio via the Windows Application Loopback API, applies a
PUBG-tuned DSP chain, and routes the result to your default playback device.

It is **pure audio signal processing** — no game-memory reads, no DLL
injection/hooks, no interaction with BattlEye/ACE, and no virtual audio cable.
It operates entirely at the OS audio-engine level.

## Features

- **Process-isolated capture** (`AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`)
  of `TslGame.exe` at 48 kHz float32 stereo.
- **Auto-engage / auto-release:** starts when the game launches, restores normal
  routing when it closes.
- **PUBG-tuned DSP chain:**
  - 4th-order high-pass (default 100 Hz) to strip red-zone/vehicle rumble.
  - 3-band minimum-phase footstep EQ (thud / stomp / scuff).
  - Upward compressor to lift distant/crouch footsteps (20–50 m).
  - Transient shaper to add attack punch.
  - Mid/Side self-footstep suppressor (ducks center-panned own steps).
  - Lookahead brickwall limiter to cap gunshots/flashbangs/vehicles.
- **Phase-coherent processing** — linked stereo gain and identical L/R filters
  preserve HRTF/azimuth/elevation cues.
- **Low latency & low CPU** — lock-free SPSC ring buffers, MMCSS *Pro Audio*
  threads, `IAudioClient3` low-latency render path, <1% CPU.
- **Live control surface** — system-tray icon + window with:
  - master on/off toggle (also `F8` hotkey and a tray item);
  - per-feature toggles (EQ, upward compressor, transient shaper,
    self-footstep suppressor, limiter);
  - adjustable tuning sliders (high-pass freq, 3-band EQ gains, distant-step
    lift, transient punch, self-step depth, gunshot ceiling);
  - presets (**Footstep** / **Balanced** / **Gunshot-safe**).

## No ban risk

SoundStepOS operates **only on the Windows audio engine**, exactly like your
gaming headset's software or a system-wide equalizer:

- It **does not read PUBG's memory**, open its process, or attach a debugger.
- It **does not modify any game files** and **does not inject/hook any DLL**
  into the game.
- It **sends nothing to the game** — it only reads the audio the OS has already
  mixed for `TslGame.exe` and re-renders it to your output device.

BattlEye/ACE watch *inside* the game process; SoundStepOS never goes inside it.
The game is not even aware the capture exists.

## Requirements

- **Windows 10 build 20348 (Server 2022) or Windows 11** — process loopback
  capture is not available on earlier Windows 10 consumer builds.
- Visual Studio 2022/2026 (MSVC) with the C++ workload, or CMake + MSVC.
- A default playback device running at **48 kHz stereo** (the Windows default).

## Build

```powershell
# From a "x64 Native Tools" (vcvars64) prompt:
cmake -S . -B build
cmake --build build --config Release
```

The executable is written to `build\Release\soundstepos.exe`.

## Run

1. Launch `soundstepos.exe`. A control window and a system-tray icon appear.
2. Launch PUBG. The tool detects `TslGame.exe` and engages processing
   automatically (status line updates to show the PID).
3. Toggle features or switch presets live in the window, or right-click the tray
   icon. Press `F8` to toggle processing on/off.
4. Closing the window hides it to the tray; use the tray **Exit** to quit
   (or close the console).

> **Tip:** for the lowest render latency, ensure the game is not running and no
> other app is holding the default device in exclusive mode when the tool
> engages. If you hear no output, confirm your default device is 48 kHz stereo.

## Tuning

Presets live in `src/gui/presets.h`; every underlying DSP parameter is in
`src/config.h` (`struct Config`). See [`docs/TUNING_GUIDE.md`](docs/TUNING_GUIDE.md)
for a full calibration workflow using Training Mode / Replay recordings and a
spectrum analyzer.

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the thread model,
ring-buffer design, WASAPI details, and the DSP pipeline description.

## Notes & limitations

- **Format:** capture is limited to 48 kHz float32 stereo (the Windows shared
  mix format). Multi-channel or other rates are not currently resampled.
- **Self-footstep suppression** is a time-domain Mid/Side duck; a dead-ahead or
  dead-behind enemy is also center-panned and may be partially ducked (see the
  tuning guide for a proposed spectral-gating extension).
- The preset/toggle UI is live; fine-grained DSP values are still edited in
  `src/config.h` and `src/gui/presets.h` and rebuilt.

## Disclaimer

This project is for audio enhancement and does not modify or read game memory,
does not inject code into any process, and does not interfere with anti-cheat
software. Use in accordance with the game's terms of service.
