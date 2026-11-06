// Dubruederschaft — DSP core implementation. See header for design notes.
#include "DubruederschaftDSP.h"

#include <algorithm>

namespace dubruederschaft {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;

double clamp(double x, double lo, double hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
// One-pole smoothing coefficient for a time constant in seconds.
double smoothCoeff(double timeSec, double sr) {
  if (timeSec <= 0.0) return 1.0;
  return 1.0 - std::exp(-1.0 / (timeSec * sr));
}
}  // namespace

void DSP::setSampleRate(double sr) {
  sr_ = sr > 0.0 ? sr : 44100.0;
  // Echo buffer: 1000 ms + margin.
  echoBuf_.assign(static_cast<std::size_t>(sr_ * 1.05) + 2, 0.0f);
  echoWrite_ = 0;
  // Comb lengths, scaled from the classic 44.1 kHz values.
  const double scale = sr_ / 44100.0;
  const int combLens[4] = {1116, 1188, 1277, 1356};
  combs_.clear();
  for (int len : combLens) {
    Comb c;
    c.buf.assign(static_cast<std::size_t>(len * scale) + 2, 0.0f);
    combs_.push_back(std::move(c));
  }
  // Allpass lengths, scaled the same way.
  const int apLens[2] = {556, 441};
  allpasses_.clear();
  for (int len : apLens) {
    Allpass a;
    a.buf.assign(static_cast<std::size_t>(len * scale) + 2, 0.0f);
    allpasses_.push_back(std::move(a));
  }
  reset();
}

void DSP::reset() {
  phase_ = 0.0;
  smoothedPitchHz_ = params_.pitchHz;
  wailPhase_ = 0.0;
  twoTonePhase_ = 0.0;
  envelope_ = 0.0;
  std::fill(echoBuf_.begin(), echoBuf_.end(), 0.0f);
  echoWrite_ = 0;
  smoothedEchoDelay_ = -1.0;
  for (auto& c : combs_) {
    std::fill(c.buf.begin(), c.buf.end(), 0.0f);
    c.idx = 0;
    c.lp = 0.0;
  }
  for (auto& a : allpasses_) {
    std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    a.idx = 0;
  }
}

double DSP::targetPitchHz() {
  const double centre = clamp(params_.pitchHz, 20.0, 8000.0);
  switch (params_.mode) {
    case Mode::Manual:
      return centre;
    case Mode::AutoWail: {
      // Sine LFO, +/-kWailDepthSemitones around the centre.
      const double lfo = std::sin(kTwoPi * wailPhase_);
      return centre * std::pow(2.0, (kWailDepthSemitones * lfo) / 12.0);
    }
    case Mode::TwoTone: {
      const double high =
          centre * std::pow(2.0, kTwoToneIntervalSemitones / 12.0);
      return twoTonePhase_ < 0.5 ? centre : high;
    }
  }
  return centre;
}

// PolyBLEP correction (Välimäki / Huovilainen). Returns the correction to
// apply at a discontinuity at phase t, given phase increment dt.
// Verified in /tmp/polyblep_check (2026-11-05): at 3000 Hz the
// non-harmonic (alias) energy ratio fell from 0.0154 to 0.0030 (Saw) and
// 0.0187 to 0.0037 (Square) vs the naive waveforms.
double polyBLEP(double t, double dt) {
  if (dt <= 0.0) return 0.0;
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0;
  }
  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return t * t + t + t + 1.0;
  }
  return 0.0;
}

double DSP::oscillator(double phase, double dt) const {
  switch (params_.wave) {
    case Wave::Sine:
      return std::sin(kTwoPi * phase);
    case Wave::Saw:
      // One discontinuity per cycle, at the phase wrap.
      return 2.0 * phase - 1.0 - polyBLEP(phase, dt);
    case Wave::Square: {
      // Two discontinuities: rising at phase 0, falling at phase 0.5.
      double y = phase < 0.5 ? 1.0 : -1.0;
      y += polyBLEP(phase, dt);
      y -= polyBLEP(std::fmod(phase + 0.5, 1.0), dt);
      return y;
    }
  }
  return 0.0;
}

void DSP::processBlock(float* out, int n) {
  if (echoBuf_.empty() || combs_.empty()) setSampleRate(sr_);

  const double glideCoeff = smoothCoeff(params_.glideMs / 1000.0, sr_);
  const double envAttack = smoothCoeff(0.005, sr_);  // 5 ms on
  const double envRelease = smoothCoeff(0.020, sr_); // 20 ms off
  const double echoTargetDelay =
      clamp(params_.echoTimeMs, 0.0, 1000.0) / 1000.0 * sr_;
  // Slew-limit the delay length (~30 ms): turning Time glides the read
  // position instead of jumping it (see header). Snap on the first
  // block, while the echo buffer is still silent.
  const double echoDelayCoeff = smoothCoeff(0.030, sr_);
  if (smoothedEchoDelay_ < 0.0)
    smoothedEchoDelay_ = echoTargetDelay;
  const double echoFb = clamp(params_.echoFeedback, 0.0, 0.85);
  const double echoMix = clamp(params_.echoMix, 0.0, 1.0);
  const double revSize = clamp(params_.reverbSize, 0.0, 1.0);
  const double revMix = clamp(params_.reverbMix, 0.0, 1.0);
  const double revDamp = clamp(params_.reverbDamp, 0.0, 1.0);
  const double master = clamp(params_.master, 0.0, 1.0);
  // Comb feedback grows with Size; Damp darkens the feedback via the
  // one-pole lowpass (coefficient 1 = no filtering / bright, 0.1 = dark).
  const double combFb = 0.20 + revSize * 0.60;  // 0.20 .. 0.80
  const double dampAlpha = 1.0 - revDamp * 0.9; // 1.0 .. 0.10

  for (int i = 0; i < n; ++i) {
    // --- Pitch: mode target, then glide smoothing (all modes) ---
    wailPhase_ += kWailRateHz / sr_;
    if (wailPhase_ >= 1.0) wailPhase_ -= 1.0;
    twoTonePhase_ += kTwoToneRateHz / sr_;
    if (twoTonePhase_ >= 1.0) twoTonePhase_ -= 1.0;
    const double target = targetPitchHz();
    smoothedPitchHz_ += (target - smoothedPitchHz_) * glideCoeff;

    // --- Oscillator + per-wave levelling + on/off envelope ---
    const double freq = clamp(smoothedPitchHz_, 20.0, 8000.0);
    const double dt = freq / sr_;  // phase increment, for PolyBLEP
    phase_ += dt;
    if (phase_ >= 1.0) phase_ -= 1.0;
    const double envTarget = isSounding() ? 1.0 : 0.0;
    const double envCoeff = envTarget > envelope_ ? envAttack : envRelease;
    envelope_ += (envTarget - envelope_) * envCoeff;
    double x = oscillator(phase_, dt) * WaveGain(params_.wave) * envelope_;

    // --- Echo (feedback delay) ---
    double echoOut = 0.0;
    smoothedEchoDelay_ += (echoTargetDelay - smoothedEchoDelay_) * echoDelayCoeff;
    if (!echoBuf_.empty() && smoothedEchoDelay_ >= 1.0) {
      const std::size_t delay =
          static_cast<std::size_t>(smoothedEchoDelay_ + 0.5);
      const std::size_t read =
          (echoWrite_ + echoBuf_.size() - delay) % echoBuf_.size();
      echoOut = echoBuf_[read];
      echoBuf_[echoWrite_] =
          static_cast<float>(x + echoOut * echoFb);
      echoWrite_ = (echoWrite_ + 1) % echoBuf_.size();
    } else if (!echoBuf_.empty()) {
      echoBuf_[echoWrite_] = static_cast<float>(x);
      echoWrite_ = (echoWrite_ + 1) % echoBuf_.size();
    }
    const double echoed = x * (1.0 - echoMix) + echoOut * echoMix;

    // --- Reverb (4 parallel combs -> 2 series allpasses) ---
    double wet = 0.0;
    if (!combs_.empty()) {
      double combSum = 0.0;
      for (auto& c : combs_) {
        const double y = c.buf[c.idx];
        // Damp: lowpass the feedback signal.
        c.lp += dampAlpha * (y - c.lp);
        c.buf[c.idx] = static_cast<float>(echoed + combFb * c.lp);
        c.idx = (c.idx + 1) % c.buf.size();
        combSum += y;
      }
      wet = combSum / static_cast<double>(combs_.size());
      for (auto& a : allpasses_) {
        const double y = a.buf[a.idx];
        // Allpass: out = -0.5*in + buf; buf = in + 0.5*out
        const double apOut = -0.5 * wet + y;
        a.buf[a.idx] = static_cast<float>(wet + 0.5 * apOut);
        a.idx = (a.idx + 1) % a.buf.size();
        wet = apOut;
      }
    }
    const double reverbed = echoed * (1.0 - revMix) + wet * revMix;

    // --- Master + soft limiter (-1 dB ceiling) ---
    double y = reverbed * master;
    y = kLimiterCeiling * std::tanh(y / kLimiterCeiling);
    out[i] = static_cast<float>(y);
  }
}

}  // namespace dubruederschaft
