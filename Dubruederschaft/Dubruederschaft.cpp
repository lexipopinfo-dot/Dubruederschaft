// Dubrüderschaft — iPlug2 plugin implementation (Phase 2, 2026-11-05).
// Wires the framework-free DSP core (DubruederschaftDSP.*) to iPlug2
// parameters and a crayon-sticker IGraphics UI (780x488) built from
// Lexi's own hand-drawn assets (final art v1, 2026-11-05, draft 05d).
// The art lives in the repo as base64 text (resources/art/*.png.b64 and
// resources/fonts/*.ttf.b64) because the GitHub connector can only push
// text files; decode_assets.py decodes them at CMake configure time
// (see CMakeLists.txt). Bitmaps are drawn by code where they must move
// (knob dot, Master handle, Level segments, pill text, preset numbers).
// Smoke-test build: the code has NOT been compiled yet — the first
// GitHub Actions run is the compile test. No human has heard it in Ableton.
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
// IColor is ARGB: IColor(alpha, r, g, b). The panel itself is a bitmap
// (panel_background.png), so these colours are only for code-drawn bits.
#if IPLUG_EDITOR
static const IColor kColNavy(255, 10, 30, 70);      // knob faces / display
static const IColor kColYellow(255, 253, 252, 1);   // dots / display text
static const IColor kColWhite(255, 255, 255, 255);
static const IColor kColMuted(255, 200, 215, 240);
static const IColor kColGreen(255, 46, 160, 70);
static const IColor kColSegYellow(255, 220, 200, 40);
static const IColor kColOrange(255, 230, 140, 40);
static const IColor kColMeterRed(255, 210, 60, 45);
static const char* kFontHand = "PatrickHand-Regular"; // Lexi's handwritten font
static constexpr double kPi = 3.14159265358979323846;

// Knob: navy crayon face + Lexi's yellow ring (static, with the white
// wobbly outline baked in) + a code-drawn rotating indicator dot.
// The ring must NOT rotate (its -/+ would spin with it), so only the dot
// moves. Drag behaviour comes from IKnobControlBase.
class CrayonKnobControl : public IKnobControlBase
{
public:
  // ringBox: the ring's own box (57px, or 76px for Glide). The control
  // bounds are the ring box + 3px outline margin + label space below.
  CrayonKnobControl(const IRECT& ringBox, int paramIdx, double min, double max,
                    const IBitmap& face, const IBitmap& ring,
                    const char* label, const char* subLabel = nullptr)
    : IKnobControlBase(IRECT(ringBox.L - 3.f, ringBox.T - 3.f,
                         ringBox.R + 3.f,
                         ringBox.B + 3.f + 22.f + (subLabel ? 16.f : 0.f)),
                   paramIdx)
    , mMin(min)
    , mMax(max)
    , mFace(face)
    , mRing(ring)
    , mLabelText(label)
    , mSubLabel(subLabel)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT b = mRECT;
    const float ringSize = b.W() - 6.f; // 57, or 76 for Glide
    const float cx = b.L + b.W() / 2.f;
    const float cy = b.T + b.W() / 2.f;
    const float faceD = ringSize * 2.f / 3.f;
    g.DrawBitmap(mFace, IRECT(cx - faceD / 2.f, cy - faceD / 2.f,
                              cx + faceD / 2.f, cy + faceD / 2.f));
    g.DrawBitmap(mRing, IRECT(b.L, b.T, b.L + b.W(), b.T + b.W()));
    // Indicator dot: 7:30 at min, sweeping 270 deg clockwise to 4:30.
    const double norm = (GetValue() - mMin) / (mMax - mMin);
    const double theta = (-135.0 + 270.0 * norm) * kPi / 180.0;
    const float dr = ringSize / 3.f - 4.f;
    const float dx = cx + dr * static_cast<float>(std::sin(theta));
    const float dy = cy - dr * static_cast<float>(std::cos(theta));
    g.FillEllipse(kColYellow, IRECT(dx - 3.5f, dy - 3.5f, dx + 3.5f, dy + 3.5f));
    const float labelTop = b.T + b.W() + 7.f;
    g.DrawText(IText(15, COLOR_WHITE, kFontHand, EAlign::Center), mLabelText,
               IRECT(b.L, labelTop, b.R, labelTop + 15.f));
    if (mSubLabel)
      g.DrawText(IText(11, kColMuted, kFontHand, EAlign::Center), mSubLabel,
                 IRECT(b.L, labelTop + 16.f, b.R, labelTop + 31.f));
  }

private:
  double mMin, mMax;
  IBitmap mFace, mRing;
  const char* mLabelText;
  const char* mSubLabel;
};

// Wave / Mode pill: one option of an enum param. The pill bitmap is
// blank (no text) — the code draws the centred label, so the same two
// bitmaps serve all six pills. Active = red pill, inactive = yellow.
class PillButtonControl : public IControl
{
public:
  PillButtonControl(const IRECT& bounds, int paramIdx, double optionValue,
                    const IBitmap& yellow, const IBitmap& red, const char* text)
    : IControl(bounds, paramIdx)
    , mOption(optionValue)
    , mYellow(yellow)
    , mRed(red)
    , mText(text)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool active = GetValue() == mOption;
    g.DrawBitmap(active ? mRed : mYellow, mRECT);
    g.DrawText(IText(12, active ? COLOR_WHITE : kColNavy, kFontHand, EAlign::Center),
               mText, mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    SetValue(mOption);
    SetDirty(true);
  }

private:
  double mOption;
  IBitmap mYellow, mRed;
  const char* mText;
};

// Momentary Trigger: Lexi's yellow circle. Sound while pressed.
class BitmapTriggerControl : public IControl
{
public:
  BitmapTriggerControl(const IRECT& bounds, const IBitmap& bitmap,
                       std::function<void(bool)> setTrigger, const char* label)
    : IControl(bounds)
    , mBitmap(bitmap)
    , mSetTrigger(setTrigger)
    , mLabel(label)
  {
  }

  void Draw(IGraphics& g) override
  {
    IRECT circleRect(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 68.f);
    if (mPressed)
    {
      const float ix = circleRect.W() * 0.03f, iy = circleRect.H() * 0.03f;
      circleRect = IRECT(circleRect.L + ix, circleRect.T + iy,
                         circleRect.R - ix, circleRect.B - iy);
    }
    g.DrawBitmap(mBitmap, circleRect);
    g.DrawText(IText(15, COLOR_WHITE, kFontHand, EAlign::Center), mLabel,
               IRECT(mRECT.L, mRECT.T + 69.f, mRECT.R, mRECT.T + 84.f));
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
  IBitmap mBitmap;
  std::function<void(bool)> mSetTrigger;
  const char* mLabel;
  bool mPressed = false;
};

// Hold latch: Lexi's red circle. A white ring shows when latched.
class BitmapHoldControl : public IControl
{
public:
  BitmapHoldControl(const IRECT& bounds, const IBitmap& bitmap,
                    std::function<bool()> getHold, std::function<void(bool)> setHold,
                    const char* label)
    : IControl(bounds)
    , mBitmap(bitmap)
    , mGetHold(getHold)
    , mSetHold(setHold)
    , mLabel(label)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT circleRect(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 62.f);
    g.DrawBitmap(mBitmap, circleRect);
    if (mGetHold())
      g.DrawEllipse(COLOR_WHITE, circleRect, nullptr, 3.f);
    g.DrawText(IText(15, COLOR_WHITE, kFontHand, EAlign::Center), mLabel,
               IRECT(mRECT.L, mRECT.T + 66.f, mRECT.R, mRECT.T + 81.f));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mSetHold(!mGetHold());
    SetDirty(true);
  }

private:
  IBitmap mBitmap;
  std::function<bool()> mGetHold;
  std::function<void(bool)> mSetHold;
  const char* mLabel;
};

// Panic button (Lexi, 2026-11-05): red circle + her "KILL THE REGIME"
// text, attached to kParamPanic. ON = total mute. A white ring shows
// while it is on, so a latched panic is visible at a glance.
class PanicButtonControl : public IControl
{
public:
  PanicButtonControl(const IRECT& bounds, int paramIdx,
                     const IBitmap& circle, const IBitmap& text)
    : IControl(bounds, paramIdx)
    , mCircle(circle)
    , mText(text)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT circleRect(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 82.f);
    g.DrawBitmap(mCircle, circleRect);
    const float cx = (circleRect.L + circleRect.R) / 2.f;
    const float cy = (circleRect.T + circleRect.B) / 2.f;
    g.DrawBitmap(mText, IRECT(cx - 27.f, cy - 24.f, cx + 27.f, cy + 24.f));
    if (GetValue() > 0.5)
      g.DrawEllipse(COLOR_WHITE, circleRect, nullptr, 3.f);
    g.DrawText(IText(11, COLOR_WHITE, kFontHand, EAlign::Center), "Panic button",
               IRECT(mRECT.L, mRECT.T + 85.f, mRECT.R, mRECT.T + 98.f));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    SetValue(GetValue() > 0.5 ? 0.0 : 1.0);
    SetDirty(true);
  }

private:
  IBitmap mCircle, mText;
};

// Master fader: Lexi's vertical pill track + yellow handle bitmaps.
// Drag behaviour comes from ISliderControlBase; only the drawing is custom.
class MasterFaderControl : public ISliderControlBase
{
public:
  MasterFaderControl(const IRECT& bounds, int paramIdx,
                     const IBitmap& track, const IBitmap& handle)
    : ISliderControlBase(bounds, paramIdx, EDirection::Vertical)
    , mTrack(track)
    , mHandle(handle)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.DrawBitmap(mTrack, mRECT);
    // Master is a 0..100 percentage param. The handle centre runs from
    // the bottom cap centre to the top cap centre (19px in from each end).
    const double v = GetValue() / 100.0;
    const float top = mRECT.T + 19.f;
    const float bottom = mRECT.B - 19.f;
    const float cy = bottom - static_cast<float>(v) * (bottom - top);
    const float cx = (mRECT.L + mRECT.R) / 2.f;
    g.DrawBitmap(mHandle, IRECT(cx - 21.f, cy - 21.f, cx + 21.f, cy + 21.f));
  }

private:
  IBitmap mTrack, mHandle;
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
    g.DrawText(IText(13, COLOR_WHITE, kFontHand, EAlign::Center), text, mRECT);
    SetDirty(true);
  }

private:
  std::function<double()> mGetValue;
};

// Segmented level meter, green (bottom) -> yellow -> red (top), bottom-up.
// The wobbly frame is Lexi's bitmap; the 6 segments are drawn by code
// because they light up in real time. Reads the peak level from the
// plugin via a lambda; no IGraphics sender needed.
class LevelMeterControl : public IControl
{
public:
  LevelMeterControl(const IRECT& bounds, const IBitmap& frame,
                    std::function<float()> getLevel)
    : IControl(bounds)
    , mFrame(frame)
    , mGetLevel(getLevel)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.DrawBitmap(mFrame, mRECT);
    const float v = mGetLevel(); // 0..~1 (post-master, post-limiter peak)
    // sqrt() stretches low levels so a quiet-but-audible siren still
    // lights the bottom segment; without it the first segment only
    // lit above ~0.17 peak and the meter looked dead.
    const float vd = std::sqrt(std::max(0.f, std::min(1.f, v)));
    const int nSegs = 6;
    const IColor segCols[6] = {kColGreen, kColGreen, kColSegYellow,
                               kColSegYellow, kColOrange, kColMeterRed};
    const IRECT segArea(mRECT.L + 5.f, mRECT.T + 5.f, mRECT.R - 5.f, mRECT.B - 5.f);
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
  IBitmap mFrame;
  std::function<float()> mGetLevel;
};

// MIDI note readout under the Pitch knob: shows the note currently
// sounding from MIDI (e.g. "C4 • 261 Hz"), or "MIDI: —" when the siren
// is played from Trigger/Hold instead. Reads the atomic note number;
// redraws every frame like the level meter.
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
    g.DrawText(IText(11, COLOR_WHITE, kFontHand, EAlign::Center), text, mRECT);
    SetDirty(true); // keep updating while notes change
  }

private:
  std::function<int()> mGetNote;
};

// Preset blister: Lexi's 8x2 frame + 16 circles (numbers drawn by code,
// so the same bitmap serves every slot). Tap a circle to load; with
// SAVE armed, tap to save into that slot.
class PresetBlisterControl : public IControl
{
public:
  PresetBlisterControl(const IRECT& bounds, const IBitmap& blister,
                       std::function<void(int)> onSlot)
    : IControl(bounds)
    , mBlister(blister)
    , mOnSlot(onSlot)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.DrawBitmap(mBlister, mRECT);
    for (int slot = 0; slot < 16; ++slot)
    {
      const int col = slot % 8, row = slot / 8;
      const float cx = 66.f + 291.f / 8.f * (col + 0.5f);
      const float cy = 392.f + 74.f / 2.f * (row + 0.5f);
      char num[4];
      std::snprintf(num, sizeof(num), "%d", slot + 1);
      g.DrawText(IText(9, COLOR_WHITE, kFontHand, EAlign::Center), num,
                 IRECT(cx - 12.f, cy - 10.f, cx + 12.f, cy + 2.f));
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    for (int slot = 0; slot < 16; ++slot)
    {
      const int col = slot % 8, row = slot / 8;
      const float cx = 66.f + 291.f / 8.f * (col + 0.5f);
      const float cy = 392.f + 74.f / 2.f * (row + 0.5f);
      const float dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy < 14.f * 14.f)
      {
        mOnSlot(slot);
        break;
      }
    }
  }

private:
  IBitmap mBlister;
  std::function<void(int)> mOnSlot;
};

// SAVE pill: Lexi's yellow capsule + "Save" text. Arms the save flow;
// the next tapped preset slot receives the current sound.
class SaveButtonControl : public IControl
{
public:
  SaveButtonControl(const IRECT& bounds, const IBitmap& pill,
                    std::function<void()> onSave)
    : IControl(bounds)
    , mPill(pill)
    , mOnSave(onSave)
  {
  }

  void Draw(IGraphics& g) override
  {
    IRECT r = mRECT;
    if (mPressed)
    {
      const float ix = r.W() * 0.03f, iy = r.H() * 0.03f;
      r = IRECT(r.L + ix, r.T + iy, r.R - ix, r.B - iy);
    }
    g.DrawBitmap(mPill, r);
    g.DrawText(IText(18, kColNavy, kFontHand, EAlign::Center), "Save", mRECT);
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
  IBitmap mPill;
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

  // Layout = approved draft 05d / final art v1 (780x488). Coordinates
  // are the draft's: knob rows at x 66/183/300, Voice pills at x 412,
  // Glide + Panic in the Reverb row, Master + Level far right, blister
  // strip at the bottom aligned with the knob rows (66-357).
  mLayoutFunc = [&](IGraphics* pGraphics) {
    if (pGraphics->NControls())
      return; // fixed-size panel: create once, no resize handling

    pGraphics->LoadFont("PatrickHand-Regular", PATRICK_HAND_FN);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

    // All bitmaps live in resources/art/ (decoded from .b64 at configure
    // time). LoadBitmap looks them up by bare filename in the bundle.
    IBitmap bmpBackground = pGraphics->LoadBitmap("panel_background.png");
    IBitmap bmpHeader = pGraphics->LoadBitmap("header_dubruederschaft.png");
    IBitmap bmpTrigger = pGraphics->LoadBitmap("trigger_circle.png");
    IBitmap bmpHold = pGraphics->LoadBitmap("hold_circle.png");
    IBitmap bmpFace = pGraphics->LoadBitmap("knob_face.png");
    IBitmap bmpRing = pGraphics->LoadBitmap("knob_ring_outlined.png");
    IBitmap bmpPillYellow = pGraphics->LoadBitmap("pill_yellow.png");
    IBitmap bmpPillRed = pGraphics->LoadBitmap("pill_red.png");
    IBitmap bmpPanicCircle = pGraphics->LoadBitmap("panic_circle.png");
    IBitmap bmpPanicText = pGraphics->LoadBitmap("panic_text.png");
    IBitmap bmpMasterTrack = pGraphics->LoadBitmap("master_track.png");
    IBitmap bmpMasterHandle = pGraphics->LoadBitmap("master_handle.png");
    IBitmap bmpLevelFrame = pGraphics->LoadBitmap("level_frame.png");
    IBitmap bmpBlister = pGraphics->LoadBitmap("preset_blister.png");
    IBitmap bmpSave = pGraphics->LoadBitmap("save_pill.png");
    IBitmap bmpDisplay = pGraphics->LoadBitmap("display_sticker.png");
    IBitmap bmpLabelSiren = pGraphics->LoadBitmap("label_siren_vertical.png");
    IBitmap bmpLabelEcho = pGraphics->LoadBitmap("label_echo_vertical.png");
    IBitmap bmpLabelReverb = pGraphics->LoadBitmap("label_reverb_vertical.png");
    IBitmap bmpLabelVoice = pGraphics->LoadBitmap("label_voice_vertical.png");
    IBitmap bmpLabelMaster = pGraphics->LoadBitmap("label_master.png");
    IBitmap bmpLabelLevel = pGraphics->LoadBitmap("label_level.png");
    IBitmap bmpLabelPresets = pGraphics->LoadBitmap("label_presets_vertical.png");

    // Background first: Lexi's blue crayon panel, no controls on it.
    pGraphics->AttachControl(new IBitmapControl(IRECT(0, 0, 780, 488), bmpBackground));

    // Header — Lexi's wobbly "Dubrüderschaft" bitmap (Title Case, v0.2.0:
    // all-caps was hard to read, per her first Ableton feedback).
    pGraphics->AttachControl(new IBitmapControl(IRECT(26, 12, 364, 54), bmpHeader));
    pGraphics->AttachControl(new ITextControl(IRECT(640, 20, 754, 31),
                                              "v0.2.0 • VST3 Instrument",
                                              IText(11, kColMuted, kFontHand, EAlign::Far)),
                             kCtrlTagVersionNumber);

    // Section labels (Lexi's vertical/horizontal assets).
    pGraphics->AttachControl(new IBitmapControl(IRECT(30, 73, 48, 121), bmpLabelSiren));
    pGraphics->AttachControl(new IBitmapControl(IRECT(30, 183, 49, 224), bmpLabelEcho));
    pGraphics->AttachControl(new IBitmapControl(IRECT(30, 282, 48, 342), bmpLabelReverb));
    pGraphics->AttachControl(new IBitmapControl(IRECT(366, 101, 399, 199), bmpLabelVoice));
    pGraphics->AttachControl(new IBitmapControl(IRECT(628, 44, 692, 62), bmpLabelMaster));
    pGraphics->AttachControl(new IBitmapControl(IRECT(712, 44, 764, 62), bmpLabelLevel));
    pGraphics->AttachControl(new IBitmapControl(IRECT(28, 389, 50, 469), bmpLabelPresets));

    // Siren row: Trigger (momentary) + Hold (latch) + Pitch.
    pGraphics->AttachControl(new BitmapTriggerControl(
        IRECT(63, 63, 131, 150), bmpTrigger,
        [this](bool on) { SetTrigger(on); }, "Trigger"));
    pGraphics->AttachControl(new BitmapHoldControl(
        IRECT(183, 66, 245, 150), bmpHold,
        [this]() { return GetHold(); },
        [this](bool on) { SetHold(on); }, "Hold"));
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(300, 69, 357, 126), kParamPitch, 200., 2000., bmpFace, bmpRing, "Pitch"));
    // MIDI note readout under Pitch (v0.2.0): which note is sounding.
    pGraphics->AttachControl(new MidiNoteControl(
        IRECT(286, 147, 377, 162),
        [this]() { return GetMidiNote(); }));

    // Echo row.
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(66, 176, 123, 233), kParamEchoTime, 0., 1000., bmpFace, bmpRing, "Time"));
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(183, 176, 240, 233), kParamEchoFeedback, 0., 85., bmpFace, bmpRing, "Feedback"));
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(300, 176, 357, 233), kParamEchoMix, 0., 100., bmpFace, bmpRing, "Mix"));

    // Reverb row.
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(66, 283, 123, 340), kParamReverbSize, 0., 100., bmpFace, bmpRing, "Size / Decay"));
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(183, 283, 240, 340), kParamReverbDamp, 0., 100., bmpFace, bmpRing, "Damp"));
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(300, 283, 357, 340), kParamReverbMix, 0., 100., bmpFace, bmpRing, "Mix"));

    // VOICE column: Wave + Mode pills, Glide (big, Reverb row) + Panic.
    static const char* kWaveNames[3] = {"Sine", "Saw", "Square"};
    for (int i = 0; i < 3; ++i)
      pGraphics->AttachControl(new PillButtonControl(
          IRECT(409.f + i * 72.f, 78.f, 475.f + i * 72.f, 116.f),
          kParamWave, (double)i, bmpPillYellow, bmpPillRed, kWaveNames[i]));
    static const char* kModeNames[3] = {"Manual", "Auto Wail", "Two-Tone"};
    for (int i = 0; i < 3; ++i)
      pGraphics->AttachControl(new PillButtonControl(
          IRECT(409.f + i * 72.f, 185.f, 475.f + i * 72.f, 223.f),
          kParamMode, (double)i, bmpPillYellow, bmpPillRed, kModeNames[i]));
    pGraphics->AttachControl(new CrayonKnobControl(
        IRECT(412, 273, 488, 349), kParamGlide, 0., 500., bmpFace, bmpRing,
        "Glide", "0-500 ms"));
    pGraphics->AttachControl(new PanicButtonControl(
        IRECT(539, 270, 621, 368), kParamPanic, bmpPanicCircle, bmpPanicText));

    // Master fader (vertical) + value readout + segmented level meter.
    pGraphics->AttachControl(new MasterFaderControl(
        IRECT(653, 69, 697, 354), kParamMaster, bmpMasterTrack, bmpMasterHandle));
    pGraphics->AttachControl(new MasterValueControl(
        IRECT(653, 355, 697, 372),
        [this]() { return GetParam(kParamMaster)->Value(); }));
    pGraphics->AttachControl(new LevelMeterControl(
        IRECT(710, 70, 752, 353), bmpLevelFrame,
        [this]() { return GetMeterLevel(); }));
    pGraphics->AttachControl(new ITextControl(IRECT(712, 355, 764, 368), "green low",
                                              IText(10, kColMuted, kFontHand, EAlign::Near)));
    pGraphics->AttachControl(new ITextControl(IRECT(712, 368, 764, 381), "red high",
                                              IText(10, kColMuted, kFontHand, EAlign::Near)));

    // Presets strip: blister (16 slots), SAVE, preset-name display.
    pGraphics->AttachControl(new PresetBlisterControl(
        IRECT(64, 390, 359, 468), bmpBlister,
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
        IRECT(439, 407, 536, 456), bmpSave,
        [this]() {
          mSaveArmed = true;
          std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "Tap a slot to save");
          UpdatePresetDisplay();
        }));
    pGraphics->AttachControl(new IBitmapControl(IRECT(552, 412, 754, 446), bmpDisplay));
    pGraphics->AttachControl(new ITextControl(IRECT(552, 412, 754, 446),
                                              mPresetDisplay,
                                              IText(17, kColYellow, kFontHand, EAlign::Center),
                                              COLOR_TRANSPARENT),
                             kCtrlTagPresetDisplay);
    // Preset hint, centred exactly below the blister (draft 05, Lexi).
    pGraphics->AttachControl(new ITextControl(IRECT(66, 469, 357, 482),
                                              "Tap a slot = load   •   SAVE + slot = save",
                                              IText(12, kColMuted, kFontHand, EAlign::Center)));
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
