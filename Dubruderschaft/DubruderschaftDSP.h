// Dubruderschaft — standalone DSP core (v0.3.2, 2026-11-07)
// NOT the plugin yet. Framework-free: no iPlug2, no VST3 SDK, no UI.
// Intended to be dropped into an iPlug2 plugin class later with minimal changes.
//
// Signal flow (locked, see ../SPEC.md):
//   Trigger -> Wave -> Pitch (+ Sine/Pulse LFOs) -> per-wave levelling (auto)
//           -> Chopper (amplitude gate) -> Echo (dub) -> Reverb
//           -> Master -> Limiter -> Output
//
// v0.3.2 changes: panic() now also clears the latched Hold and
//   Trigger (ear-test fix — the voice re-attacked from a latched Hold).
// v0.3.1 changes (from the AudioLemon .amxd study, "modify, bukan contek"):
//   - Mode (Manual / Auto Wail / Two-Tone) is GONE. In its place, two
//     standalone LFOs, each with its own Rate + Amount knob:
//       Sine LFO  = rectified cosine, UNIPOLAR: the pitch only ever rises
//                   from the base by 0..+Amt Hz, never dips; it pulses at
//                   2x the LFO rate. Amt = 0 turns this LFO off.
//       Pulse LFO = bipolar square wave: the pitch jumps by +/-Amt Hz
//                   (two-tone). Amt = 0 turns this LFO off.
//     Both amounts are in Hz and are ADDED to the pitch. Either LFO can
//     run at audio rate (Sine <= 1000 Hz, Pulse <= 10000 Hz) for the
//     "laser" sounds — that is intentional, not a bug.
//   - Chopper: an amplitude gate at the Pulse rate (gate = 1 in the first
//     half of the pulse cycle, 0 in the second), smoothed by Ramp
//     (line~ style: Ramp 0 = hard chop, 127 = ~100 ms smoothing).
//   - Pitch range extended down to ~20 Hz (was 200-2000).
//   - Panic is a ONE-SHOT kill/reset (see panic()): it kills the voice
//     and resets the oscillator/LFO phases, but does NOT clear the
//     echo/reverb buffers — the tails keep ringing.
//
// CPU-light by design: one mono oscillator, one feedback delay line,
// 4 comb filters + 2 allpasses for reverb, no allocations in processBlock
// after setSampleRate().
//
// Known limitations of this prototype (do not present as final):
//  - Saw/Square are now PolyBLEP band-limited (Phase 2, 2026-11-05; alias
//    ratio at 3000 Hz dropped ~5x vs naive, see test 7 in test_dsp.cpp).
//    PolyBLEP is not perfect anti-aliasing, but it is the standard
//    CPU-light fix for a one-oscillator plugin.
//  - Reverb is a simple Freeverb-inspired algorithmic reverb, not a spring
//    emulation. Spring is a possible v2 upgrade.
//  - Per-wave levelling gains are RMS-equalised starting values, not final.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace dubruderschaft {

enum class Wave { Sine = 0, Saw = 1, Square = 2 };

struct Params {
  Wave wave = Wave::Sine;
  double pitchHz = 600.0;      // range 20-2000, default 600
  double glideMs = 120.0;      // 0-500, default ~120
  // LFOs (v0.3.1): Amt = 0 means that LFO is off ("manual" still possible).
  double sineRateHz = 1.0;     // 0.01-1000, default 1.0
  double sineAmtHz = 0.0;      // 0-10000 Hz, default 0 (off)
  double pulseRateHz = 5.0;    // 0.01-10000, default 5.0
  double pulseAmtHz = 0.0;     // 0-10000 Hz, default 0 (off)
  bool chopperOn = false;      // amplitude gate at the Pulse rate
  double chopperRamp = 32.0;   // 0-127, default 32 (line~ smoothing)
  double echoTimeMs = 375.0;   // 0-1000, default 375
  double echoFeedback = 0.45;  // 0-0.85, default 0.45
  double echoMix = 0.30;       // 0-1, default 0.30
  double reverbSize = 0.40;    // 0-1, default 0.40 (Size/Decay)
  double reverbMix = 0.25;     // 0-1, default 0.25
  double reverbDamp = 0.50;    // 0-1, default 0.50
  double master = 0.70;        // 0-1, default ~0.70
};

inline constexpr double kLimiterCeiling = 0.8912509381337456; // -1 dB

// RMS-equalised per-wave gains (PROPOSAL, see SPEC.md §4).
// Sine RMS 0.707, Saw RMS 0.577, Square RMS 1.0 at peak 1.0.
inline double WaveGain(Wave w) {
  switch (w) {
    case Wave::Sine: return 0.70;
    case Wave::Saw: return 0.85;
    case Wave::Square: return 0.50;
  }
  return 0.70;
}

class DSP {
 public:
  DSP() = default;

  void setSampleRate(double sr);
  void setParams(const Params& p) { params_ = p; }
  void setTrigger(bool on) { trigger_ = on; }
  // Hold is a latch: the UI toggles it. The DSP just reads the latched state.
  void setHold(bool latched) { hold_ = latched; }
  bool isSounding() const { return trigger_ || hold_; }
  double currentPitchHz() const { return smoothedPitchHz_; }
  void reset();

  // One-shot Panic (v0.3.1, Lexi 2026-11-06): kill the voice NOW and
  // reset to idle — envelope to 0, oscillator + LFO phases to 0, glide
  // back to the base pitch. It does NOT clear the echo/reverb buffers,
  // so the tails keep ringing ("biarkan saja kalau tail masih tersisa").
  // If a MOMENTARY Trigger is still held after the click, the voice
  // simply re-attacks through the normal 5 ms envelope — that is
  // correct for a momentary Trigger only. It is NOT correct for a
  // latched Hold: Panic now unlatches Hold (plugin Panic() clears
  // mHold, and panic() itself clears hold_ here), so a latched Hold
  // does not re-attack. A held MIDI note is a separate trigger source
  // and is not cleared by Panic; its Note Off stops it normally.
  // Call this on the AUDIO thread (the plugin routes the UI click there).
  void panic();

  // LFO start phase (0..1), from a factory preset's Phase value.
  // Sets both LFO phases. Called on the audio thread via the plugin.
  void setLFOPhase(double phase) {
    sinePhase_ = phase - std::floor(phase);
    pulsePhase_ = phase - std::floor(phase);
  }

  // Render n mono samples into out. No allocation here.
  void processBlock(float* out, int n);

 private:
  double targetPitchHz();
  // PolyBLEP band-limited oscillator. dt = phase increment per sample
  // (frequency / sample rate). Sine is unchanged (already band-limited).
  double oscillator(double phase, double dt) const;

  double sr_ = 44100.0;
  Params params_;
  bool trigger_ = false;
  bool hold_ = false;

  double phase_ = 0.0;             // oscillator phase [0,1)
  double smoothedPitchHz_ = 600.0; // glide-smoothed pitch
  double sinePhase_ = 0.0;         // Sine LFO phase [0,1)
  double pulsePhase_ = 0.0;        // Pulse LFO phase [0,1)
  double envelope_ = 0.0;          // click-free on/off ramp
  double chopperEnv_ = 1.0;        // smoothed chopper gate (1 = open)

  // Echo: one feedback delay line, max 1000 ms.
  std::vector<float> echoBuf_;
  std::size_t echoWrite_ = 0;
  // Slew-limited echo delay length (samples). Turning the Time knob
  // used to jump the read pointer through old audio -> click, and the
  // feedback recirculated it as a crackle (ear-test 2026-11-06). The
  // length now glides toward its target (~30 ms), like a tape delay.
  // -1 = not set yet (snap on the first block, while the buffer is silent).
  double smoothedEchoDelay_ = -1.0;

  // Reverb: 4 parallel combs + 2 series allpasses (Freeverb-inspired).
  struct Comb {
    std::vector<float> buf;
    std::size_t idx = 0;
    double lp = 0.0; // one-pole lowpass state in the feedback path (Damp)
  };
  struct Allpass {
    std::vector<float> buf;
    std::size_t idx = 0;
  };
  std::vector<Comb> combs_;
  std::vector<Allpass> allpasses_;
};

}  // namespace dubruderschaft
