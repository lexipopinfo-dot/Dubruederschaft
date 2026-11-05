// Dubruederschaft — standalone DSP core (Phase 1 prototype)
// NOT the plugin yet. Framework-free: no iPlug2, no VST3 SDK, no UI.
// Intended to be dropped into an iPlug2 plugin class later with minimal changes.
//
// Signal flow (locked, see ../SPEC.md):
//   Trigger -> Wave -> Pitch -> per-wave levelling (auto)
//           -> Echo (dub) -> Reverb -> Master -> Limiter -> Output
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
//  - Auto Wail depth (+/-7 semitones) and rate (0.7 Hz), and Two-Tone
//    switch rate (2 Hz), are PROPOSAL values from SPEC.md, to be tuned by ear.
//  - Per-wave levelling gains are RMS-equalised starting values, not final.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace dubruederschaft {

enum class Wave { Sine = 0, Saw = 1, Square = 2 };
enum class Mode { Manual = 0, AutoWail = 1, TwoTone = 2 };

struct Params {
  Wave wave = Wave::Sine;
  Mode mode = Mode::Manual;
  double pitchHz = 600.0;      // PROPOSAL range 200-2000, default 600
  double glideMs = 120.0;      // 0-500, default ~120
  double echoTimeMs = 375.0;   // PROPOSAL 0-1000, default 375
  double echoFeedback = 0.45;  // PROPOSAL 0-0.85, default 0.45
  double echoMix = 0.30;       // PROPOSAL 0-1, default 0.30
  double reverbSize = 0.40;    // PROPOSAL 0-1, default 0.40 (Size/Decay)
  double reverbMix = 0.25;     // PROPOSAL 0-1, default 0.25
  double reverbDamp = 0.50;    // PROPOSAL 0-1, default 0.50
  double master = 0.70;        // PROPOSAL 0-1, default ~0.70
};

// Agreed / proposal constants (see SPEC.md §5)
inline constexpr double kWailRateHz = 0.7;        // ~1.43 s per cycle, in the agreed 1-2 s range
inline constexpr double kWailDepthSemitones = 7.0; // PROPOSAL
inline constexpr double kTwoToneIntervalSemitones = 5.0; // AGREED
inline constexpr double kTwoToneRateHz = 2.0;     // PROPOSAL: each tone 250 ms
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
  double wailPhase_ = 0.0;         // [0,1)
  double twoTonePhase_ = 0.0;      // [0,1)
  double envelope_ = 0.0;          // click-free on/off ramp

  // Echo: one feedback delay line, max 1000 ms.
  std::vector<float> echoBuf_;
  std::size_t echoWrite_ = 0;

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

}  // namespace dubruederschaft
