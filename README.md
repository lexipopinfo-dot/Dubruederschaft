# Dubrüderschaft

A dub siren VST3 for Ableton Live 12 Suite — one mono oscillator, dub echo
and reverb, and a big prominent master volume. Copy-only install, no
installer: the deliverable is a single `Dubruederschaft.vst3` bundle copied
to `~/Library/Audio/Plug-Ins/VST3/` on macOS Monterey (Intel).

> **Status (Phase 2, 2026-11-05): smoke-test build, NOT a finished plugin.**
> The DSP core is tested offline on Linux (`../dsp/` — all checks pass,
> including a PolyBLEP alias test). The iPlug2 plugin code and the macOS
> GitHub Actions workflow in this repo have **not been compiled yet** — the
> first Actions run is the compile test. No human has heard this in Ableton;
> Lexi's ear test on her Intel MacBook Pro 2016 is still required.

## Locked spec

See `../SPEC.md` (in `workspace/dub_siren_v1/`) — the coding reference:

- **Name:** Dubrüderschaft (display, with ü). Internal ASCII id: `Dubruederschaft`.
- **Signal flow:** Trigger → Wave → Pitch → per-wave levelling (auto) →
  Echo → Reverb → Master → Limiter (−1 dB) → Output.
- **Wave:** Sine / Saw / Square. **Mode:** Manual / Auto Wail / Two-Tone
  (Auto Wail ~0.7 Hz, ±7 semitones around centre; Two-Tone +5 semitones,
  2 Hz switch). **Glide:** 0–500 ms.
- **Echo:** Time / Feedback / Mix. **Reverb (3 only):** Size/Decay,
  Mix (Dry/Wet), Damp.
- **Master:** vertical fader, end of chain. **Presets:** 16 slots, 8×2,
  SAVE + slot = save, tap slot = load, Master not saved.
- **UI:** flat option E, 1200×750, red base `#B22230`, Trigger blue
  `#0F5BB8`, Hold yellow `#FDCC49`. Header shows `DUBRÜDERSCHAFT`.
- **Target:** macOS Monterey, Intel x86_64, CPU-light.

## Repo layout

```
Dubruederschaft/                 # the plugin (config, plugin class, UI, DSP copy)
  Dubruederschaft.cpp/.h         # iPlug2 plugin + IGraphics UI (option E)
  DubruederschaftDSP.cpp/.h      # framework-free DSP core (same as ../dsp/)
  config.h                       # plugin identity, 1200x750, 1-1, VST3
  CMakeLists.txt                 # FORMATS VST3 only (smoke test)
  resources/                     # Roboto font, VST3 Info.plist
.github/workflows/
  build-macos-vst3.yml           # macOS smoke-test build (see below)
CMakeLists.txt / CMakePresets.json
  macos-xcode-x86_64 preset: cross-compile x86_64, deployment target 10.13
.gitmodules                      # iPlug2 submodule (fetched by Actions)
```

## The macOS build (GitHub Actions)

`build-macos-vst3.yml` runs on `macos-15` (ARM64) and cross-compiles the
Intel x86_64 `.vst3` via the `macos-xcode-x86_64` CMake preset. It:

1. Checks out the repo with submodules (iPlug2).
2. Downloads the iPlug SDKs (VST3 + CLAP, cached).
3. Configures + builds Release with CMake/Xcode.
4. Verifies the bundle exists and `lipo -archs` shows `x86_64`.
5. Ad-hoc code-signs it (`codesign -s -`, no Developer ID needed).
6. Uploads `Dubruederschaft-vst3-x86_64.zip` as a workflow artifact.

**Biggest known risk:** GitHub has no native Intel runner left, so this
cross-compile is unproven for iPlug2/IGraphics. If the first run fails,
the exact error is the deliverable — do not claim success. Fallbacks are
in `../BUILD_NOTES.md` §3.2 (Universal build, paid cloud Mac, or building
on Lexi's own Mac — the last two need Lexi's approval).

## How to push this repo (see PUSH_CHECKLIST.md)

GitHub auth is **not** set up in this environment (`gh auth status`:
not logged in, no credentials). To create the repo and trigger the first
build, follow `PUSH_CHECKLIST.md` — it lists the exact commands and the
one decision needed (who owns the GitHub repo).

## Known limitations (Phase 2)

- Preset slots are **in-memory only**: they work while the plugin is
  loaded, but are not yet persisted to DAW state (needs `SerializeState`
  work — Phase 3).
- PolyBLEP reduces aliasing ~5× at 3000 Hz vs naive oscillators; it is
  not perfect anti-aliasing.
- Reverb is a simple algorithmic (Freeverb-inspired) reverb, not a
  spring emulation (v2 candidate).
- MIDI behaviour in Hold mode and Echo tempo-sync are still open
  (SPEC.md §12).
