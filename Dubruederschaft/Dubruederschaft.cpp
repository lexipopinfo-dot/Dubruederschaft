// Dubruederschaft — iPlug2 plugin implementation (v0.3.0, 2026-11-06).
// Wires the framework-free DSP core (DubruederschaftDSP.*) to iPlug2
// parameters and a single-panel IGraphics UI (600x375).
//
// v0.3 UI (Mo is the designer now; Lexi's Canva is down): ONE flat
// panel PNG ("panel.png", 600x375, palette-optimised) is the whole
// background — header, labels, knob rings/faces, blank pills, blister,
// Master track, Level frame, Panic circle+text, SAVE pill, display
// sticker are all baked into it. The code draws ONLY what moves or
// changes on top: knob dots, pill text + active red fill, preset
// numbers, "Save", display text, Master handle, Level segments, knob
// labels and the small readouts. This kills the v0.2.0 scale bug
// (2x art loaded at scale 1, everything rendering 2x too big).
// The panel lives in the repo as base64 text (resources/art/panel_*.json)
// because the GitHub connector can only push text files;
// decode_assets.py decodes it at CMake configure time (see CMakeLists.txt).
// v0.3 fixes from the 2026-11-06 ear-test: knob dots / Master handle /
// pill states were frozen or wrong because the code treated iPlug2's
// NORMALISED control values (0..1) as raw ranges — all three now use
// GetValue() directly. Echo Time crackle: the DSP slew-limits the delay
// length (see DubruederschaftDSP). PLUG_NAME is ASCII "Dubruederschaft"
// (Ableton sidebar shows it; the ü lives on in the header artwork).
#include "Dubruederschaft.h"
#include "IPlug_include_in_plug_src.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

#if IPLUG_EDITOR
#include "IControls.h"
#endif

using namespace dubruederschaft;

// Superman palette, sampled from Lexi's images.png (2026-11-05).
// IColor is ARGB: IColor(alpha, r, g, b). The panel itself is a bitmap,
// so these colours are only for code-drawn bits.
#if IPLUG_EDITOR
static const IColor kColNavy(255, 10, 30, 70);      // pill text / display
static const IColor kColYellow(255, 253, 252, 1);   // dots / handle / display text
static const IColor kColWhite(255, 255, 255, 255);
static const IColor kColMuted(255, 200, 215, 240);
static const IColor kColGreen(255, 46, 160, 70);
static const IColor kColSegYellow(255, 220, 200, 40);
static const IColor kColOrange(255, 230, 140, 40);
static const IColor kColMeterRed(255, 210, 60, 45);
static const IColor kColPillRed(255, 209, 32, 32);  // active pill fill
static const char* kFontHand = "PatrickHand-Regular"; // Lexi's handwritten font
static constexpr double kPi = 3.14159265358979323846;

// Knob: the ring + navy face are baked into the panel; the code draws
// ONLY the rotating indicator dot + the label below. Drag behaviour
// comes from IKnobControlBase.
// FIX (v0.3): GetValue() is already NORMALISED (0..1) — the v0.2.0 code
// treated it as the raw range (e.g. 200..2000 Hz), so the dot never moved.
class KnobDotControl : public IKnobControlBase
{
public:
  // ringBox: the ring's own box (44px, or 58px for Glide). The control
  // bounds are the ring box + 3px outline margin + label space below.
  KnobDotControl(const IRECT& ringBox, int paramIdx,
                 const char* label, const char* subLabel = nullptr)
    : IKnobControlBase(IRECT(ringBox.L - 3.f, ringBox.T - 3.f,
                         ringBox.R + 3.f,
                         ringBox.B + 3.f + 17.f + (subLabel ? 12.f : 0.f)),
                   paramIdx)
    , mRingBox(ringBox)
    , mLabelText(label)
    , mSubLabel(subLabel)
  {
  }

  void Draw(IGraphics& g) override
  {
    const float ringSize = mRingBox.W(); // 44, or 58 for Glide
    const float cx = (mRingBox.L + mRingBox.R) / 2.f;
    const float cy = (mRingBox.T + mRingBox.B) / 2.f;
    // Indicator dot: 7:30 at min, sweeping 270 deg clockwise to 4:30.
    // GetValue() is normalised 0..1 — use it directly.
    const double norm = GetValue();
    const double theta = (-135.0 + 270.0 * norm) * kPi / 180.0;
    const float dr = ringSize / 3.f - 3.f;
    const float dx = cx + dr * static_cast<float>(std::sin(theta));
    const float dy = cy - dr * static_cast<float>(std::cos(theta));
    g.FillEllipse(kColYellow, IRECT(dx - 3.f, dy - 3.f, dx + 3.f, dy + 3.f));
    const float labelTop = mRingBox.B + 3.f + 5.f;
    g.DrawText(IText(12, COLOR_WHITE, kFontHand, EAlign::Center), mLabelText,
               IRECT(mRECT.L, labelTop, mRECT.R, labelTop + 12.f));
    if (mSubLabel)
      g.DrawText(IText(8, kColMuted, kFontHand, EAlign::Center), mSubLabel,
                 IRECT(mRECT.L, labelTop + 12.f, mRECT.R, labelTop + 24.f));
  }

private:
  IRECT mRingBox;
  const char* mLabelText;
  const char* mSubLabel;
};

// Wave / Mode pill: one option of an enum param. The yellow pill shape
// is baked into the panel; the code draws the centred label, and a red
// fill (inset, so Lexi's wobbly yellow outline still shows) when active.
// FIX (v0.3): option values are NORMALISED (0, 0.5, 1) — the v0.2.0 code
// passed raw indices (0, 1, 2), so clicking Saw lit Saw but played
// Square, and Square/Two-Tone could never light up.
class PillButtonControl : public IControl
{
public:
  PillButtonControl(const IRECT& bounds, int paramIdx, double optionValue,
                    const char* text)
    : IControl(bounds, paramIdx)
    , mOption(optionValue)
    , mText(text)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool active = GetValue() == mOption;
    if (active)
    {
      g.FillRoundRect(kColPillRed,
                      IRECT(mRECT.L + 2.f, mRECT.T + 2.f,
                            mRECT.R - 2.f, mRECT.B - 2.f),
                      8.f);
    }
    g.DrawText(IText(9, active ? COLOR_WHITE : kColNavy, kFontHand, EAlign::Center),
               mText, mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    SetValue(mOption);
    SetDirty(true);
  }

private:
  double mOption;
  const char* mText;
};

// Trigger (momentary): the yellow circle is baked into the panel; the
// code draws a white ring while pressed + the label below.
class TriggerButtonControl : public IControl
{
public:
  TriggerButtonControl(const IRECT& bounds,
                       std::function<void(bool)> setTrigger, const char* label)
    : IControl(bounds)
    , mSetTrigger(setTrigger)
    , mLabel(label)
  {
  }

  void Draw(IGraphics& g) override
  {
    // Circle square = top square of the control rect.
    IRECT circle(mRECT.L, mRECT.T, mRECT.R, mRECT.T + mRECT.W());
    if (mPressed)
      g.DrawEllipse(COLOR_WHITE, circle, nullptr, 2.f);
    g.DrawText(IText(12, COLOR_WHITE, kFontHand, EAlign::Center), mLabel,
               IRECT(mRECT.L, mRECT.B - 13.f, mRECT.R, mRECT.B));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mPressed = true;
    mSetTrigger(true);
    SetDirty(true);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    mPressed = false;
    mSetTrigger(false);
    SetDirty(true);
  }

private:
  std::function<void(bool)> mSetTrigger;
  const char* mLabel;
  bool mPressed = false;
};

// Hold latch: the red circle is baked into the panel; a white ring
// shows when latched + the label below.
class HoldButtonControl : public IControl
{
public:
  HoldButtonControl(const IRECT& bounds,
                    std::function<bool()> getHold, std::function<void(bool)> setHold,
                    const char* label)
    : IControl(bounds)
    , mGetHold(getHold)
    , mSetHold(setHold)
    , mLabel(label)
  {
  }

  void Draw(IGraphics& g) override
  {
    IRECT circle(mRECT.L, mRECT.T, mRECT.R, mRECT.T + mRECT.W());
    if (mGetHold())
      g.DrawEllipse(COLOR_WHITE, circle, nullptr, 2.f);
    g.DrawText(IText(12, COLOR_WHITE, kFontHand, EAlign::Center), mLabel,
               IRECT(mRECT.L, mRECT.B - 13.f, mRECT.R, mRECT.B));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mSetHold(!mGetHold());
    SetDirty(true);
  }

private:
  std::function<bool()> mGetHold;
  std::function<void(bool)> mSetHold;
  const char* mLabel;
};

// Panic button (Lexi, 2026-11-05): the red circle + "KILL THE REGIME"
// text are baked into the panel. ON = total mute. A white ring shows
// while it is on, so a latched panic is visible at a glance.
class PanicButtonControl : public IControl
{
public:
  PanicButtonControl(const IRECT& bounds, int paramIdx)
    : IControl(bounds, paramIdx)
  {
  }

  void Draw(IGraphics& g) override
  {
    IRECT circle(mRECT.L, mRECT.T, mRECT.R, mRECT.T + mRECT.W());
    if (GetValue() > 0.5)
      g.DrawEllipse(COLOR_WHITE, circle, nullptr, 2.f);
    g.DrawText(IText(8, COLOR_WHITE, kFontHand, EAlign::Center), "Panic button",
               IRECT(mRECT.L, mRECT.B - 10.f, mRECT.R, mRECT.B));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    SetValue(GetValue() > 0.5 ? 0.0 : 1.0);
    SetDirty(true);
  }
};

// Master fader: the vertical pill track is baked into the panel; the
// code draws ONLY the yellow handle (a circle) at the value position.
// Drag behaviour comes from ISliderControlBase.
// FIX (v0.3): GetValue() is already NORMALISED (0..1) — the v0.2.0 code
// divided it by 100, pinning the handle at the bottom.
class MasterFaderControl : public ISliderControlBase
{
public:
  MasterFaderControl(const IRECT& bounds, int paramIdx)
    : ISliderControlBase(bounds, paramIdx, EDirection::Vertical)
  {
  }

  void Draw(IGraphics& g) override
  {
    // Master is a 0..100 percentage param; GetValue() is normalised.
    const double v = GetValue();
    const float top = mRECT.T + 15.f;
    const float bottom = mRECT.B - 15.f;
    const float cy = bottom - static_cast<float>(v) * (bottom - top);
    const float cx = (mRECT.L + mRECT.R) / 2.f;
    g.FillEllipse(kColYellow, IRECT(cx - 13.f, cy - 13.f, cx + 13.f, cy + 13.f));
    g.DrawEllipse(COLOR_WHITE, IRECT(cx - 13.f, cy - 13.f, cx + 13.f, cy + 13.f),
                  nullptr, 1.5f);
  }
};

// Master value readout under the fader ("70 %"), redrawn every frame.
class MasterValueControl : public IControl
{
public:
  MasterValueControl(const IRECT& bounds, std::function<double()> getValue)
    : IControl(bounds)
    , mGetValue(getValue)
  {
  }

  void Draw(IGraphics& g) override
  {
    char text[16];
    std::snprintf(text, sizeof(text), "%.0f %%", mGetValue());
    g.DrawText(IText(10, COLOR_WHITE, kFontHand, EAlign::Center), text, mRECT);
    SetDirty(true);
  }

private:
  std::function<double()> mGetValue;
};

// Segmented level meter, green (bottom) -> yellow -> red (top), bottom-up.
// The wobbly frame is baked into the panel; the 6 segments are drawn by
// code because they light up in real time. Reads the peak level from the
// plugin via a lambda; no IGraphics sender needed.
class LevelMeterControl : public IControl
{
public:
  LevelMeterControl(const IRECT& bounds, std::function<float()> getLevel)
    : IControl(bounds)
    , mGetLevel(getLevel)
  {
  }

  void Draw(IGraphics& g) override
  {
    const float v = mGetLevel(); // 0..~1 (post-master, post-limiter peak)
    // sqrt() stretches low levels so a quiet-but-audible siren still
    // lights the bottom segment; without it the first segment only
    // lit above ~0.17 peak and the meter looked dead.
    const float vd = std::sqrt(std::max(0.f, std::min(1.f, v)));
    const int nSegs = 6;
    const IColor segCols[6] = {kColGreen, kColGreen, kColSegYellow,
                               kColSegYellow, kColOrange, kColMeterRed};
    const IRECT segArea(mRECT.L + 4.f, mRECT.T + 4.f, mRECT.R - 4.f, mRECT.B - 4.f);
    const float segH = segArea.H() / static_cast<float>(nSegs);
    for (int i = 0; i < nSegs; ++i)
    {
      if (vd >= static_cast<float>(i) / static_cast<float>(nSegs) + 0.001f)
      {
        IRECT seg(segArea.L + 2.f, segArea.B - segH * (i + 1) + 2.f,
                  segArea.R - 2.f, segArea.B - segH * i - 2.f);
        g.FillRect(segCols[i], seg);
      }
    }
    // Keep redrawing every frame: IGraphics only draws dirty controls,
    // and nothing else marks this meter dirty, so without this it drew
    // once (at v = 0, empty) and never updated while sounding.
    SetDirty(true);
  }

private:
  std::function<float()> mGetLevel;
};

// MIDI note readout under the Pitch knob: shows the note currently
// sounding from MIDI (e.g. "C4 • 261 Hz"), or "MIDI: —" when the siren
// is played from Trigger/Hold instead. Reads the atomic note number;
// redraws every frame like the level meter.
// v0.3: moved down (y155-170 at 780 scale) — v0.2.0 overlapped the
// Pitch label.
class MidiNoteControl : public IControl
{
public:
  MidiNoteControl(const IRECT& bounds, std::function<int()> getNote)
    : IControl(bounds)
    , mGetNote(getNote)
  {
  }

  void Draw(IGraphics& g) override
  {
    const int note = mGetNote();
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    char text[32];
    if (note >= 0 && note < 128)
    {
      const double freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
      std::snprintf(text, sizeof(text), "MIDI: %s%d • %.0f Hz",
                    kNames[note % 12], note / 12 - 1, freq);
    }
    else
    {
      std::snprintf(text, sizeof(text), "MIDI: —");
    }
    g.DrawText(IText(8, COLOR_WHITE, kFontHand, EAlign::Center), text, mRECT);
    SetDirty(true); // keep updating while notes change
  }

private:
  std::function<int()> mGetNote;
};

// Preset blister: the 8x2 frame + 16 circles are baked into the panel;
// the code draws ONLY the numbers. Tap a circle to load; with SAVE
// armed, tap to save into that slot.
class PresetBlisterControl : public IControl
{
public:
  PresetBlisterControl(const IRECT& bounds, std::function<void(int)> onSlot)
    : IControl(bounds)
    , mOnSlot(onSlot)
  {
  }

  void Draw(IGraphics& g) override
  {
    for (int slot = 0; slot < 16; ++slot)
    {
      const int col = slot % 8, row = slot / 8;
      // 780-scale centres, scaled to the 600x375 panel.
      const float cx = (66.f + 291.f / 8.f * (col + 0.5f)) * 0.769231f;
      const float cy = (392.f + 37.f * (row + 0.5f)) * 0.768443f;
      char num[4];
      std::snprintf(num, sizeof(num), "%d", slot + 1);
      g.DrawText(IText(7, COLOR_WHITE, kFontHand, EAlign::Center), num,
                 IRECT(cx - 9.f, cy - 8.f, cx + 9.f, cy + 2.f));
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    for (int slot = 0; slot < 16; ++slot)
    {
      const int col = slot % 8, row = slot / 8;
      const float cx = (66.f + 291.f / 8.f * (col + 0.5f)) * 0.769231f;
      const float cy = (392.f + 37.f * (row + 0.5f)) * 0.768443f;
      const float dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy < 11.f * 11.f)
      {
        mOnSlot(slot);
        break;
      }
    }
  }

private:
  std::function<void(int)> mOnSlot;
};

// SAVE pill: the yellow capsule is baked into the panel; the code draws
// ONLY the "Save" text. Arms the save flow; the next tapped preset
// slot receives the current sound.
class SaveButtonControl : public IControl
{
public:
  SaveButtonControl(const IRECT& bounds, std::function<void()> onSave)
    : IControl(bounds)
    , mOnSave(onSave)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.DrawText(IText(14, mPressed ? kColMuted : kColNavy, kFontHand, EAlign::Center),
               "Save", mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mPressed = true;
    SetDirty(true);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    mPressed = false;
    SetDirty(true);
    mOnSave();
  }

private:
  std::function<void()> mOnSave;
  bool mPressed = false;
};
#endif // IPLUG_EDITOR

Dubruederschaft::Dubruederschaft(const InstanceInfo& info)
  : iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamWave)->InitEnum("Wave", 0, {"Sine", "Saw", "Square"});
  GetParam(kParamMode)->InitEnum("Mode", 0, {"Manual", "Auto Wail", "Two-Tone"});
  GetParam(kParamPitch)->InitFrequency("Pitch", 600., 200., 2000., 1.);
  GetParam(kParamGlide)->InitMilliseconds("Glide", 120., 0., 500.);
  GetParam(kParamEchoTime)->InitMilliseconds("Echo Time", 375., 0., 1000.);
  GetParam(kParamEchoFeedback)->InitPercentage("Echo Feedback", 45., 0., 85.);
  GetParam(kParamEchoMix)->InitPercentage("Echo Mix", 30., 0., 100.);
  GetParam(kParamReverbSize)->InitPercentage("Reverb Size", 40., 0., 100.);
  GetParam(kParamReverbMix)->InitPercentage("Reverb Mix", 25., 0., 100.);
  GetParam(kParamReverbDamp)->InitPercentage("Reverb Damp", 50., 0., 100.);
  GetParam(kParamMaster)->InitPercentage("Master", 70., 0., 100.);
  GetParam(kParamPanic)->InitBool("Panic", false);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };

  // Layout = approved draft 05d, scaled to the 600x375 panel
  // (x * 600/780, y * 375/488). The panel PNG holds all static art;
  // controls only draw what moves or changes.
  mLayoutFunc = [&](IGraphics* pGraphics) {
    if (pGraphics->NControls())
      return; // fixed-size panel: create once, no resize handling

    pGraphics->LoadFont("PatrickHand-Regular", PATRICK_HAND_FN);

    // The single background: panel.png (decoded from panel_*.json at
    // configure time). LoadBitmap looks it up by bare filename.
    IBitmap bmpPanel = pGraphics->LoadBitmap("panel.png");
    pGraphics->AttachControl(new IBitmapControl(IRECT(0, 0, 600, 375), bmpPanel));
    pGraphics->AttachControl(new ITextControl(IRECT(492, 15, 580, 24),
                                              "v0.3.0 • VST3 Instrument",
                                              IText(8, kColMuted, kFontHand, EAlign::Far)),
                             kCtrlTagVersionNumber);

    // Siren row: Trigger (momentary) + Hold (latch) + Pitch.
    pGraphics->AttachControl(new TriggerButtonControl(
        IRECT(48, 48, 101, 115),
        [this](bool on) { SetTrigger(on); }, "Trigger"));
    pGraphics->AttachControl(new HoldButtonControl(
        IRECT(141, 51, 188, 115),
        [this]() { return GetHold(); },
        [this](bool on) { SetHold(on); }, "Hold"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(231, 53, 275, 97), kParamPitch, "Pitch"));
    // MIDI note readout under Pitch (v0.3: its own line, no overlap).
    pGraphics->AttachControl(new MidiNoteControl(
        IRECT(220, 119, 290, 131),
        [this]() { return GetMidiNote(); }));

    // Echo row.
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(51, 135, 95, 179), kParamEchoTime, "Time"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(141, 135, 185, 179), kParamEchoFeedback, "Feedback"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(231, 135, 275, 179), kParamEchoMix, "Mix"));

    // Reverb row.
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(51, 218, 95, 262), kParamReverbSize, "Size / Decay"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(141, 218, 185, 262), kParamReverbDamp, "Damp"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(231, 218, 275, 262), kParamReverbMix, "Mix"));

    // VOICE column: Wave + Mode pills (normalised options 0, 0.5, 1),
    // Glide (big, Reverb row) + Panic.
    static const char* kWaveNames[3] = {"Sine", "Saw", "Square"};
    for (int i = 0; i < 3; ++i)
      pGraphics->AttachControl(new PillButtonControl(
          IRECT(315.f + i * 55.f, 60.f, 365.f + i * 55.f, 89.f),
          kParamWave, (double)i / 2.0, kWaveNames[i]));
    static const char* kModeNames[3] = {"Manual", "Auto Wail", "Two-Tone"};
    for (int i = 0; i < 3; ++i)
      pGraphics->AttachControl(new PillButtonControl(
          IRECT(315.f + i * 55.f, 142.f, 365.f + i * 55.f, 171.f),
          kParamMode, (double)i / 2.0, kModeNames[i]));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(317, 210, 375, 268), kParamGlide, "Glide", "0-500 ms"));
    pGraphics->AttachControl(new PanicButtonControl(
        IRECT(415, 208, 478, 283), kParamPanic));

    // Master fader (vertical) + value readout + segmented level meter.
    pGraphics->AttachControl(new MasterFaderControl(
        IRECT(502, 53, 536, 272), kParamMaster));
    pGraphics->AttachControl(new MasterValueControl(
        IRECT(502, 273, 536, 286),
        [this]() { return GetParam(kParamMaster)->Value(); }));
    pGraphics->AttachControl(new LevelMeterControl(
        IRECT(546, 54, 578, 271),
        [this]() { return GetMeterLevel(); }));
    pGraphics->AttachControl(new ITextControl(IRECT(548, 271, 588, 281), "green low",
                                              IText(8, kColMuted, kFontHand, EAlign::Near)));
    pGraphics->AttachControl(new ITextControl(IRECT(548, 284, 588, 294), "red high",
                                              IText(8, kColMuted, kFontHand, EAlign::Near)));

    // Presets strip: blister (16 slots), SAVE, preset-name display.
    pGraphics->AttachControl(new PresetBlisterControl(
        IRECT(49, 300, 276, 360),
        [this](int slot) {
          if (mSaveArmed)
          {
            SavePreset(slot);
            mSaveArmed = false;
          }
          else
          {
            LoadPreset(slot);
          }
        }));
    pGraphics->AttachControl(new SaveButtonControl(
        IRECT(338, 313, 412, 351),
        [this]() {
          mSaveArmed = true;
          std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "Tap a slot to save");
          UpdatePresetDisplay();
        }));
    pGraphics->AttachControl(new ITextControl(IRECT(425, 317, 580, 343),
                                              mPresetDisplay,
                                              IText(13, kColYellow, kFontHand, EAlign::Center)),
                             kCtrlTagPresetDisplay);
    pGraphics->AttachControl(new ITextControl(IRECT(51, 361, 275, 371),
                                              "Tap a slot = load • SAVE + slot = save",
                                              IText(9, kColMuted, kFontHand, EAlign::Center)));
  };
#endif // IPLUG_EDITOR
}

void Dubruederschaft::OnReset()
{
  mDSP.setSampleRate(GetSampleRate());
  mScratch.assign(8192, 0.f);
  mDSP.reset();
}

void Dubruederschaft::SyncDSPFromParams()
{
  Params p;
  p.wave = static_cast<Wave>(GetParam(kParamWave)->Int());
  p.mode = static_cast<Mode>(GetParam(kParamMode)->Int());
  // MIDI note overrides the Pitch knob while a note is held: the note
  // is the centre pitch in Auto Wail and the low tone in Two-Tone.
  // With no MIDI note, the knob controls pitch as before.
  p.pitchHz = mMidiActive.load()
                ? std::max(50.0, std::min(4000.0, (double)mMidiPitchHz.load()))
                : GetParam(kParamPitch)->Value();
  p.glideMs = GetParam(kParamGlide)->Value();
  p.echoTimeMs = GetParam(kParamEchoTime)->Value();
  p.echoFeedback = GetParam(kParamEchoFeedback)->Value() / 100.0;
  p.echoMix = GetParam(kParamEchoMix)->Value() / 100.0;
  p.reverbSize = GetParam(kParamReverbSize)->Value() / 100.0;
  p.reverbMix = GetParam(kParamReverbMix)->Value() / 100.0;
  p.reverbDamp = GetParam(kParamReverbDamp)->Value() / 100.0;
  p.master = GetParam(kParamMaster)->Value() / 100.0;
  mDSP.setParams(p);
}

#if IPLUG_DSP
void Dubruederschaft::ProcessMidiMsg(const IMidiMsg& msg)
{
  const int status = msg.StatusMsg();
  if (status == IMidiMsg::kNoteOn && msg.Velocity() > 0)
  {
    const int note = msg.NoteNumber();
    mMidiNote.store(note);
    mMidiPitchHz.store(static_cast<float>(
        440.0 * std::pow(2.0, (note - 69) / 12.0)));
    mMidiActive.store(true);
  }
  else if (status == IMidiMsg::kNoteOff ||
           (status == IMidiMsg::kNoteOn && msg.Velocity() == 0))
  {
    // Only release if the note going off is the one sounding (mono:
    // last note wins, so an older note's Note Off must not kill a
    // newer held note).
    if (msg.NoteNumber() == mMidiNote.load())
    {
      mMidiActive.store(false);
      mMidiNote.store(-1);
    }
  }
}

void Dubruederschaft::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void)inputs; // siren generates its own sound; host input is ignored
  SyncDSPFromParams();
  mDSP.setTrigger(mTrigger.load() || mHold.load() || mMidiActive.load());

  if (static_cast<int>(mScratch.size()) < nFrames)
    mScratch.assign(nFrames, 0.f);
  mDSP.processBlock(mScratch.data(), nFrames);

  // Panic (v0.2.0, Lexi 2026-11-05): while it is on, ALL sound dies
  // totally — the safeguard against a sudden peak. The DSP keeps
  // running, so when Panic is released the sound (and echo tails)
  // come straight back. The meter reads the muted buffer, so it
  // goes dark while Panic is on, as it should.
  if (GetParam(kParamPanic)->Bool())
    std::memset(mScratch.data(), 0, sizeof(float) * static_cast<size_t>(nFrames));

  float peak = 0.f;
  for (int s = 0; s < nFrames; ++s)
  {
    const float a = std::fabs(mScratch[s]);
    if (a > peak) peak = a;
  }
  const float prev = mMeterLevel.load();
  mMeterLevel.store(peak > prev ? peak : prev * 0.999f);

  const int nChans = NOutChansConnected();
  for (int c = 0; c < nChans; ++c)
    for (int s = 0; s < nFrames; ++s)
      outputs[c][s] = mScratch[s];
}
#endif

void Dubruederschaft::LoadPreset(int slot)
{
  if (slot < 0 || slot >= 16 || !mPresets[slot].filled)
    return;
  const PresetSlot& pr = mPresets[slot];
  for (int i = 0; i < kNumParams; ++i)
  {
    // Master is NOT stored in presets (SPEC.md §10), and neither is
    // Panic (a safety switch, not a sound setting — loading a preset
    // must never be able to mute the plugin).
    if (i == kParamMaster || i == kParamPanic)
      continue;
    GetParam(i)->Set(pr.values[i]);
  }
  mActivePreset = slot;
  UpdatePresetDisplay();
#if IPLUG_EDITOR
  if (IGraphics* pGraphics = GetUI())
    pGraphics->SetAllControlsDirty();
#endif
}

void Dubruederschaft::SavePreset(int slot)
{
  if (slot < 0 || slot >= 16)
    return;
  PresetSlot& pr = mPresets[slot];
  pr.filled = true;
  for (int i = 0; i < kNumParams; ++i)
  {
    if (i == kParamMaster || i == kParamPanic)
      continue; // Master and Panic are NOT stored in presets
    pr.values[i] = GetParam(i)->Value();
  }
  std::snprintf(pr.name, sizeof(pr.name), "Preset %02d", slot + 1);
  mActivePreset = slot;
  UpdatePresetDisplay();
}

void Dubruederschaft::UpdatePresetDisplay()
{
  if (mActivePreset >= 0 && mPresets[mActivePreset].filled)
    std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "%02d • %s",
                  mActivePreset + 1, mPresets[mActivePreset].name);
  else if (std::strncmp(mPresetDisplay, "Tap a slot", 10) == 0)
    return; // keep the SAVE-armed hint until a slot is tapped
  else
    std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "-- • No preset");

#if IPLUG_EDITOR
  if (IGraphics* pGraphics = GetUI())
  {
    if (IControl* pControl = pGraphics->GetControlWithTag(kCtrlTagPresetDisplay))
    {
      if (auto* pText = dynamic_cast<ITextControl*>(pControl))
      {
        pText->SetStr(mPresetDisplay);
        pText->SetDirty(true);
      }
    }
  }
#endif
}
