// Dubruderschaft — iPlug2 plugin implementation (v0.3.2, 2026-11-07).
// Wires the framework-free DSP core (DubruderschaftDSP.*) to iPlug2
// parameters and a single-panel IGraphics UI (600x500).
//
// v0.3.2 UI: ONE flat panel PNG ("panel.png", 600x500) is the whole
// background — header, labels, knob rings/faces, blank pills, blister,
// Master track, Level frame, KILL THE REGIME circle, SAVE pill, display
// sticker are all baked into it. The code draws ONLY what moves or
// changes on top: knob dots + labels (18pt Patrick Hand), pill text
// (12pt) + active red fill, preset numbers (10pt), SAVE/YES text,
// NAME DISPLAY text, Master handle + value, Level segments, blister
// slot fills (selected red, rest yellow) + page outline, Chopper
// toggle fill, Trigger/Hold/Panic rings, MIDI readout.
// The panel lives in the repo as base64 text (resources/art/panel_*.json)
// because the GitHub connector can only push text files;
// decode_assets.py decodes it at CMake configure time (see CMakeLists.txt).
// NOTE: the repo's panel art is still the v0.3 600x375 PNG until the new
// 600x500 art (panel_v031_600x500c.png, 3 base64 chunks) is pushed —
// until then the 600x375 art will be stretched into the 600x500 frame.
// PLUG_NAME is ASCII "Dubruderschaft" (Ableton sidebar shows it; the ü
// lives on in the header artwork). Do NOT "fix" it to "Dubruederschaft".
#include "Dubruderschaft.h"
#include "IPlug_include_in_plug_src.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

#if IPLUG_EDITOR
#include "IControls.h"
#endif

using namespace dubruderschaft;

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
static const IColor kColPillRed(255, 209, 32, 32);  // active pill / slot fill
static const IColor kColSlotYellow(255, 253, 252, 1); // unselected slot fill
static const char* kFontHand = "PatrickHand-Regular"; // Lexi's handwritten font
static constexpr double kPi = 3.14159265358979323846;

// Knob: the ring + navy face are baked into the panel; the code draws
// ONLY the rotating indicator dot + the label below (18pt Patrick Hand,
// v0.3.2 font deal) + an optional sub-label (10pt). Drag behaviour
// comes from IKnobControlBase.
// GetValue() is NORMALISED (0..1) — use it directly, never as raw units
// (that was the v0.2.0 bug).
class KnobDotControl : public IKnobControlBase
{
public:
  // ringBox: the ring's own box (44px, or 60px for Glide). The control
  // bounds are the ring box + 3px outline margin + label space below.
  // The label is drawn in a 90px-wide rect centred on the knob so an
  // 18pt label ("Feedback", "Sine Rate") never clips at the edges.
  KnobDotControl(const IRECT& ringBox, int paramIdx,
                 const char* label, const char* subLabel = nullptr)
    : IKnobControlBase(IRECT(ringBox.L - 3.f, ringBox.T - 3.f,
                         ringBox.R + 3.f,
                         ringBox.B + 3.f + 5.f + 18.f + (subLabel ? 12.f : 0.f)),
                   paramIdx)
    , mRingBox(ringBox)
    , mLabelText(label)
    , mSubLabel(subLabel)
  {
  }

  void Draw(IGraphics& g) override
  {
    const float ringSize = mRingBox.W(); // 44, or 60 for Glide
    const float cx = (mRingBox.L + mRingBox.R) / 2.f;
    const float cy = (mRingBox.T + mRingBox.B) / 2.f;
    // Indicator dot: 7:30 at min, sweeping 270 deg clockwise to 4:30.
    const double norm = GetValue();
    const double theta = (-135.0 + 270.0 * norm) * kPi / 180.0;
    const float dr = ringSize / 3.f - 3.f;
    const float dx = cx + dr * static_cast<float>(std::sin(theta));
    const float dy = cy - dr * static_cast<float>(std::cos(theta));
    g.FillEllipse(kColYellow, IRECT(dx - 3.f, dy - 3.f, dx + 3.f, dy + 3.f));
    const float labelTop = mRingBox.B + 3.f + 5.f;
    g.DrawText(IText(18, COLOR_WHITE, kFontHand, EAlign::Center), mLabelText,
               IRECT(cx - 45.f, labelTop, cx + 45.f, labelTop + 18.f));
    if (mSubLabel)
      g.DrawText(IText(10, kColMuted, kFontHand, EAlign::Center), mSubLabel,
                 IRECT(cx - 45.f, labelTop + 18.f, cx + 45.f, labelTop + 30.f));
  }

private:
  IRECT mRingBox;
  const char* mLabelText;
  const char* mSubLabel;
};

// Wave pill: one option of an enum param. The yellow pill shape is
// baked into the panel; the code draws the centred label (12pt), and a
// red fill (inset, so Lexi's wobbly yellow outline still shows) when
// active. Pills are ALWAYS filled: active = red, inactive = yellow.
// Option values are NORMALISED (0, 0.5, 1) — never raw indices.
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

// Chopper toggle (v0.3.2): a 20x20 square centred at (395,210) — the
// midpoint between Glide (350,250) and Chopper Ramp (440,170). The panel
// art has a white square baked there; the code covers it with the state
// fill: ON = red, OFF = yellow (same active/inactive rule as the pills).
class ChopperToggleControl : public IControl
{
public:
  ChopperToggleControl(const IRECT& bounds, int paramIdx)
    : IControl(bounds, paramIdx)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool on = GetValue() > 0.5;
    g.FillRoundRect(on ? kColPillRed : kColSlotYellow, mRECT, 3.f);
    g.DrawRoundRect(COLOR_WHITE, mRECT, 3.f, nullptr, 2.f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    SetValue(GetValue() > 0.5 ? 0.0 : 1.0);
    SetDirty(true);
  }
};

// Panic button, ONE-SHOT (v0.3.2, Lexi 2026-11-06): the red circle +
// Lexi's hand-drawn "KILL THE REGIME" text are baked into the panel —
// the code draws NO text here. One click kills the siren voice and
// resets to idle (routed to the audio thread); the button immediately
// returns to off, and Trigger/Hold/MIDI work normally afterwards. The
// echo/reverb tails are NOT cleared. A white ring shows only while the
// mouse is held down. (v0.3 bug, fixed: Panic used to latch muted
// until reload.)
class PanicButtonControl : public IControl
{
public:
  PanicButtonControl(const IRECT& bounds, std::function<void()> onPanic)
    : IControl(bounds)
    , mOnPanic(onPanic)
  {
  }

  void Draw(IGraphics& g) override
  {
    IRECT circle(mRECT.L, mRECT.T, mRECT.R, mRECT.T + mRECT.W());
    if (mPressed)
      g.DrawEllipse(COLOR_WHITE, circle, nullptr, 2.f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mPressed = true;
    mOnPanic();
    SetDirty(true);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    mPressed = false;
    SetDirty(true);
  }

private:
  std::function<void()> mOnPanic;
  bool mPressed = false;
};

// Master fader: the vertical pill track is baked into the panel; the
// code draws ONLY the yellow handle (a circle) at the value position.
// Drag behaviour comes from ISliderControlBase.
// GetValue() is NORMALISED (0..1) — use it directly.
class MasterFaderControl : public ISliderControlBase
{
public:
  MasterFaderControl(const IRECT& bounds, int paramIdx)
    : ISliderControlBase(bounds, paramIdx, EDirection::Vertical)
  {
  }

  void Draw(IGraphics& g) override
  {
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
// sounding from MIDI (e.g. "MIDI: C4 • 261 Hz"), or "MIDI: —" when the
// siren is played from Trigger/Hold instead. Reads the atomic note
// number; redraws every frame like the level meter.
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

// Preset blister (v0.3.2): a separate strip below the main panel,
// 560x90 (x20-580, y390-480), 16 slots, radius 15, x = 55 + c*70,
// rows cy = 415 and 455. The code draws the page outline (white =
// Page A factory, red = Page B user) over the panel's baked outline,
// fills EVERY slot (selected = red, rest = yellow — never a bare white
// outline), and the preset numbers (10pt Patrick Hand: navy on yellow,
// white on red).
// NOTE: OnMouseDown receives GLOBAL (panel) coordinates — iPlug2 passes
// the raw mouse position straight through (IGraphics::OnMouseDown calls
// pCapturedControl->OnMouseDown(x, y, mod) with no mRECT subtraction),
// so x/y are compared directly against the global slot centres, exactly
// as Draw() does. Do NOT add mRECT.L/T: that double-offset was the v0.3.1
// bug — every click mapped far below the strip, so no slot was ever hit.
// Tapping the blister frame (not on a slot) switches Page A <-> Page B.
class PresetBlisterControl : public IControl
{
public:
  PresetBlisterControl(const IRECT& bounds,
                       std::function<int()> getPage,
                       std::function<int()> getActivePreset,
                       std::function<void(int)> onSlot,
                       std::function<void()> onFrameTap)
    : IControl(bounds)
    , mGetPage(getPage)
    , mGetActivePreset(getActivePreset)
    , mOnSlot(onSlot)
    , mOnFrameTap(onFrameTap)
  {
  }

  static float SlotCX(int col) { return 55.f + col * 70.f; }
  static float SlotCY(int row) { return 415.f + row * 40.f; }

  void Draw(IGraphics& g) override
  {
    const int page = mGetPage();
    const int active = mGetActivePreset();
    // Page outline over the panel's baked white frame.
    g.DrawRoundRect(page == 1 ? kColPillRed : COLOR_WHITE,
                    IRECT(20.f, 390.f, 580.f, 480.f), 10.f, nullptr, 2.5f);
    for (int slot = 0; slot < 16; ++slot)
    {
      const int col = slot % 8, row = slot / 8;
      const float cx = SlotCX(col), cy = SlotCY(row);
      const bool selected = (slot == active);
      g.FillEllipse(selected ? kColPillRed : kColSlotYellow,
                    IRECT(cx - 15.f, cy - 15.f, cx + 15.f, cy + 15.f));
      g.DrawEllipse(COLOR_WHITE,
                    IRECT(cx - 15.f, cy - 15.f, cx + 15.f, cy + 15.f),
                    nullptr, 2.f);
      char num[4];
      std::snprintf(num, sizeof(num), "%d", slot + 1);
      g.DrawText(IText(10, selected ? COLOR_WHITE : kColNavy, kFontHand,
                       EAlign::Center),
                 num, IRECT(cx - 15.f, cy - 9.f, cx + 15.f, cy + 9.f));
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    // x/y are GLOBAL (panel) coordinates — same space as SlotCX/SlotCY
    // and Draw(). Compare directly; do not offset by mRECT.
    for (int slot = 0; slot < 16; ++slot)
    {
      const int col = slot % 8, row = slot / 8;
      const float dx = x - SlotCX(col), dy = y - SlotCY(row);
      if (dx * dx + dy * dy < 17.f * 17.f)
      {
        mOnSlot(slot);
        return;
      }
    }
    // Not on a slot: tapped the frame -> switch pages.
    mOnFrameTap();
  }

private:
  std::function<int()> mGetPage;
  std::function<int()> mGetActivePreset;
  std::function<void(int)> mOnSlot;
  std::function<void()> mOnFrameTap;
};

// SAVE pill (v0.3.2): Lexi's yellow blank pill asset (final_art/
// save_pill.png), rotated vertical, at (315,303)-(343,358) — 28x55,
// centred at cy=330. The panel art has "SAVE" baked on it reading
// top-to-bottom (S-A-V-E stacked, 12pt Patrick Hand, navy), so the code
// draws nothing in the normal state. While naming (after SAVE -> tap a
// slot), the pill shows "YES": the code covers the baked SAVE with a
// red fill over the FULL pill rect and draws YES stacked vertically
// (12pt Patrick Hand). The control bounds match the pill exactly, so
// the red YES fill leaves no yellow tips.
// Click behaviour lives in the plugin (OnSaveClicked): arm the save,
// confirm the name (YES), or cancel the arm.
class SaveButtonControl : public IControl
{
public:
  SaveButtonControl(const IRECT& bounds, std::function<bool()> isNaming,
                    std::function<void()> onSave)
    : IControl(bounds)
    , mIsNaming(isNaming)
    , mOnSave(onSave)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (!mIsNaming())
      return; // panel's baked "SAVE" shows
    g.FillRoundRect(kColPillRed, mRECT, 6.f);
    // "YES" stacked vertically, 12pt, centred in the pill.
    static const char* kYes[3] = {"Y", "E", "S"};
    for (int i = 0; i < 3; ++i)
    {
      const float y0 = mRECT.T + 2.f + i * 12.f;
      g.DrawText(IText(12, COLOR_WHITE, kFontHand, EAlign::Center), kYes[i],
                 IRECT(mRECT.L, y0, mRECT.R, y0 + 12.f));
    }
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
  std::function<bool()> mIsNaming;
  std::function<void()> mOnSave;
  bool mPressed = false;
};

// NAME DISPLAY (v0.3.2): the sticker at (348,310)-(480,350) is blank in
// the panel art; this control draws the text in Patrick Hand ("07 •
// MADGAKKUM" — slot number + name, NO page letter). It also hosts the
// preset-naming text entry: BeginNaming() opens a native text field
// over the display; OnTextEntryCompletion forwards the typed name to
// the plugin. Max name length 20 chars. Display font is 14pt (v0.3.2):
// the longest factory name ("01 • WAIL IN MI KITCHEN") measures
// 132.6px at 15pt — just past the 132px sticker — but 123.7px at 14pt,
// so 14pt fits with margin.
class NameDisplayControl : public ITextControl
{
public:
  NameDisplayControl(const IRECT& bounds, std::function<void(const char*)> onName)
    : ITextControl(bounds, "-- • No preset",
                   IText(14, kColYellow, kFontHand, EAlign::Center))
    , mOnName(onName)
  {
    SetTextEntryLength(20);
  }

  void BeginNaming()
  {
    if (IGraphics* pGraphics = GetUI())
    {
      pGraphics->CreateTextEntry(*this,
                                 IText(14, kColYellow, kFontHand, EAlign::Center),
                                 mRECT, "", 0);
      SetDirty(true);
    }
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    (void)valIdx;
    mOnName(str ? str : "");
  }

private:
  std::function<void(const char*)> mOnName;
};
#endif // IPLUG_EDITOR

// ---- Factory presets (v0.3.2) -------------------------------------------
// Translated from AudioLemon's 19 .amxd presets ("modify, bukan contek").
// Fields: wave, pitchHz, glideMs, sineRateHz, sineAmtHz, pulseRateHz,
// pulseAmtHz, chopperOn, chopperRamp, echoTimeMs, echoFb%, echoMix%,
// revSize%, revMix%, revDamp%, LFO phase, name.
// Translation rules (documented, see the build report):
//  - Wave = Saw for all 16 (AudioLemon's rect~ + tanh~ reads as a
//    saturated saw-ish tone). Echo/Reverb stay at the plugin defaults
//    (AudioLemon has no FX). Master is NOT stored.
//  - Glide = 0 where the Pulse LFO jumps (hard tone steps, like the
//    original), 120 ms on the pure sine-wail presets (smooth).
//  - Pitch clamped to 20-2000 Hz (AL 2/3: 8.08 -> 20; AL 18: 2233.3 ->
//    2000). Sine Rate clamped to 0.01-1000 Hz (AL 4: 0.0098 -> 0.01).
//  - Slots follow AudioLemon order, except AL 11 ("DEEP WAIL") is
//    promoted to slot 07 (per the agreed example) and AL 8 moves to
//    slot 10.
//  - DROPPED 3 of 19 (the most extreme audio-rate lasers): AL 6 (Sine
//    Rate 2326.5 Hz AND Pitch 2823 Hz both exceed the caps — double
//    clamped, character lost), AL 17 (Pulse Rate 5648.5 Hz + Chopper;
//    redundant with AL 15's laser character), AL 19 (the most extreme:
//    Sine Amt 10000 + Pulse 9234 Hz + Chopper + Ramp 2). AL 15 is kept
//    as the single laser representative (both LFO rates in range, no
//    clamping needed).
namespace {
struct FactoryPresetDef
{
  int wave;
  double pitch, glide;
  double sineRate, sineAmt, pulseRate, pulseAmt;
  bool chopper;
  double chopperRamp;
  double echoTime, echoFb, echoMix, revSize, revMix, revDamp;
  double phase;
  const char* name;
};
static const FactoryPresetDef kFactoryPresets[16] = {
  // AL 1
  {1, 64.7, 120, 1.47, 271, 5.0, 0, false, 32, 375, 45, 30, 40, 25, 50, 0.252, "WAIL IN MI KITCHEN"},
  // AL 2 (pitch 8.08 clamped to 20)
  {1, 20.0, 120, 2.09, 578, 5.0, 0, false, 32, 375, 45, 30, 40, 25, 50, 0.362, "BAD SAMBO"},
  // AL 3 (pitch 8.08 clamped to 20)
  {1, 20.0, 120, 0.208, 578, 5.0, 0, false, 32, 375, 45, 30, 40, 25, 50, 0.819, "NOT THAT LSP!"},
  // AL 4 (sine rate 0.0098 clamped to 0.01)
  {1, 24.8, 120, 0.01, 578, 5.0, 0, false, 32, 375, 45, 30, 40, 25, 50, 0.780, "LOW WAIL"},
  // AL 5
  {1, 24.8, 120, 3.84, 271, 5.0, 0, false, 32, 375, 45, 30, 40, 25, 50, 0.780, "RAAADD"},
  // AL 7
  {1, 195.4, 0, 1.0, 0, 5.02, 364, false, 32, 375, 45, 30, 40, 25, 50, 0.252, "SHAKING LALIN"},
  // AL 11 — the classic slow deep dub wail (Lexi's screenshot)
  {1, 24.8, 120, 0.637, 868, 5.0, 0, false, 32, 375, 45, 30, 40, 25, 50, 0.780, "MADGAKKUM"},
  // AL 9
  {1, 447.3, 0, 1.0, 0, 8.64, 1652, true, 32, 375, 45, 30, 40, 25, 50, 0.252, "KING OPHAY"},
  // AL 10
  {1, 776.5, 0, 1.0, 0, 8.64, 1018, true, 35, 375, 45, 30, 40, 25, 50, 0.252, "LOWCUT BOOTS"},
  // AL 8
  {1, 477.8, 0, 0.208, 692, 10.88, 81, false, 0, 375, 45, 30, 40, 25, 50, 0.252, "LAZERHALF"},
  // AL 12
  {1, 24.8, 120, 0.0378, 1652, 5.0, 0, false, 32, 375, 45, 30, 40, 25, 50, 0.780, "THEM A THIEF"},
  // AL 13
  {1, 24.8, 0, 0.0378, 1652, 5.02, 966, false, 32, 375, 45, 30, 40, 25, 50, 0.780, "FAUX CALL"},
  // AL 14
  {1, 24.8, 0, 0.0378, 1652, 14.84, 966, false, 32, 375, 45, 30, 40, 25, 50, 0.677, "BOUNCER"},
  // AL 15 — the laser representative (both LFO rates in range)
  {1, 24.8, 0, 652.6, 1652, 1308.6, 966, false, 32, 375, 45, 30, 40, 25, 50, 0.677, "ROLLING HAZE"},
  // AL 16
  {1, 24.8, 0, 0.0987, 7183, 2.87, 966, false, 32, 375, 45, 30, 40, 25, 50, 0.677, "LKJAB"},
  // AL 18 (pitch 2233.3 clamped to 2000)
  {1, 2000.0, 0, 101.0, 21, 3.84, 2326, true, 32, 375, 45, 30, 40, 25, 50, 0.063, "FLYING BOBBY"},
};
} // namespace

Dubruderschaft::Dubruderschaft(const InstanceInfo& info)
  : iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamWave)->InitEnum("Wave", 0, {"Sine", "Saw", "Square"});
  GetParam(kParamPitch)->InitFrequency("Pitch", 600., 20., 2000., 1.);
  GetParam(kParamGlide)->InitMilliseconds("Glide", 120., 0., 500.);
  // LFO rates use an exponential shape so the musically useful low range
  // (0.01-10 Hz) is not crammed into the first 1% of knob travel.
  // Amt = 0 turns that LFO off.
  GetParam(kParamSineRate)->InitDouble("Sine Rate", 1.0, 0.01, 1000., 0.01,
                                       "Hz", 0, "", IParam::ShapeExp());
  GetParam(kParamSineAmt)->InitDouble("Sine Amt", 0., 0., 10000., 1., "Hz");
  GetParam(kParamPulseRate)->InitDouble("Pulse Rate", 5.0, 0.01, 10000., 0.01,
                                        "Hz", 0, "", IParam::ShapeExp());
  GetParam(kParamPulseAmt)->InitDouble("Pulse Amt", 0., 0., 10000., 1., "Hz");
  GetParam(kParamChopper)->InitBool("Chopper", false);
  GetParam(kParamChopperRamp)->InitDouble("Chopper Ramp", 32., 0., 127., 1.);
  GetParam(kParamEchoTime)->InitMilliseconds("Echo Time", 375., 0., 1000.);
  GetParam(kParamEchoFeedback)->InitPercentage("Echo Feedback", 45., 0., 85.);
  GetParam(kParamEchoMix)->InitPercentage("Echo Mix", 30., 0., 100.);
  GetParam(kParamReverbSize)->InitPercentage("Reverb Size", 40., 0., 100.);
  GetParam(kParamReverbMix)->InitPercentage("Reverb Mix", 25., 0., 100.);
  GetParam(kParamReverbDamp)->InitPercentage("Reverb Damp", 50., 0., 100.);
  GetParam(kParamMaster)->InitPercentage("Master", 70., 0., 100.);

  LoadFactoryPresets(); // Page A; the plugin still opens on a blank canvas

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };

  // Layout = approved v0.3.2 design (600x500 panel). All coordinates are
  // direct panel coordinates — no scaling from the old 780 draft.
  // The panel PNG holds all static art; controls only draw what moves.
  mLayoutFunc = [&](IGraphics* pGraphics) {
    if (pGraphics->NControls())
      return; // fixed-size panel: create once, no resize handling

    pGraphics->LoadFont("PatrickHand-Regular", PATRICK_HAND_FN);

    // The single background: panel.png (decoded from panel_*.json at
    // configure time). LoadBitmap looks it up by bare filename.
    IBitmap bmpPanel = pGraphics->LoadBitmap("panel.png");
    pGraphics->AttachControl(new IBitmapControl(IRECT(0, 0, 600, 500), bmpPanel));
    // NOTE: version text, Master/Level labels and the blister hint are
    // baked into the panel art — the code does NOT draw them.

    // Siren row: Trigger (momentary) + Hold (latch) + Pitch.
    // Ring boxes are 44x44 centred on the spec knob centres.
    pGraphics->AttachControl(new TriggerButtonControl(
        IRECT(52, 62, 108, 136),
        [this](bool on) { SetTrigger(on); }, "Trigger"));
    pGraphics->AttachControl(new HoldButtonControl(
        IRECT(142, 62, 198, 136),
        [this]() { return GetHold(); },
        [this](bool on) { SetHold(on); }, "Hold"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(238, 68, 282, 112), kParamPitch, "Pitch"));
    // MIDI note readout under Pitch.
    pGraphics->AttachControl(new MidiNoteControl(
        IRECT(120, 138, 290, 150),
        [this]() { return GetMidiNote(); }));

    // LFO row: Sine Rate / Sine Amt / Pulse Rate / Pulse Amt / Ramp.
    // (No "LFO" label drawn by code — Lexi's hand-drawn asset is in
    // the panel art.)
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(58, 148, 102, 192), kParamSineRate, "Sine Rate"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(148, 148, 192, 192), kParamSineAmt, "Sine Amt"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(238, 148, 282, 192), kParamPulseRate, "Pulse Rate"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(328, 148, 372, 192), kParamPulseAmt, "Pulse Amt"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(418, 148, 462, 192), kParamChopperRamp, "Ramp"));
    // Chopper toggle: 20x20 square centred at (395,210), the midpoint
    // between Glide (350,250) and Chopper Ramp (440,170).
    pGraphics->AttachControl(new ChopperToggleControl(
        IRECT(385, 200, 405, 220), kParamChopper));

    // Echo row.
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(58, 228, 102, 272), kParamEchoTime, "Time"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(148, 228, 192, 272), kParamEchoFeedback, "Feedback"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(238, 228, 282, 272), kParamEchoMix, "Mix"));

    // Reverb row.
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(58, 308, 102, 352), kParamReverbSize, "Size"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(148, 308, 192, 352), kParamReverbDamp, "Damp"));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(238, 308, 282, 352), kParamReverbMix, "Mix"));

    // Middle column: Wave pills (Sine/Saw/Square), Glide (big, r=30)
    // + Panic (one-shot, "KILL THE REGIME" art in the panel).
    static const char* kWaveNames[3] = {"Sine", "Saw", "Square"};
    for (int i = 0; i < 3; ++i)
      pGraphics->AttachControl(new PillButtonControl(
          IRECT(320.f + i * 53.f, 68.f, 368.f + i * 53.f, 96.f),
          kParamWave, (double)i / 2.0, kWaveNames[i]));
    pGraphics->AttachControl(new KnobDotControl(
        IRECT(320, 220, 380, 280), kParamGlide, "Glide", "0-500 ms"));
    pGraphics->AttachControl(new PanicButtonControl(
        IRECT(409, 219, 472, 282),
        [this]() { Panic(); }));

    // Master fader (vertical) + value readout + segmented level meter.
    pGraphics->AttachControl(new MasterFaderControl(
        IRECT(508, 70, 538, 368), kParamMaster));
    pGraphics->AttachControl(new MasterValueControl(
        IRECT(505, 370, 540, 384),
        [this]() { return GetParam(kParamMaster)->Value(); }));
    pGraphics->AttachControl(new LevelMeterControl(
        IRECT(550, 70, 578, 368),
        [this]() { return GetMeterLevel(); }));

    // Presets strip: blister (16 slots), SAVE, NAME DISPLAY.
    pGraphics->AttachControl(new PresetBlisterControl(
        IRECT(20, 390, 580, 480),
        [this]() { return GetPage(); },
        [this]() { return GetActivePreset(); },
        [this](int slot) { OnSlotTapped(slot); },
        [this]() { OnBlisterFrameTapped(); }),
        kCtrlTagBlister);
    pGraphics->AttachControl(new SaveButtonControl(
        IRECT(315, 303, 343, 358),
        [this]() { return IsNaming(); },
        [this]() { OnSaveClicked(); }),
        kCtrlTagSaveButton);
    pGraphics->AttachControl(new NameDisplayControl(
        IRECT(348, 310, 480, 350),
        [this](const char* name) { OnNameEntered(name); }),
        kCtrlTagNameDisplay);
  };
#endif // IPLUG_EDITOR
}

void Dubruderschaft::OnReset()
{
  mDSP.setSampleRate(GetSampleRate());
  mScratch.assign(8192, 0.f);
  mDSP.reset();
}

void Dubruderschaft::SyncDSPFromParams()
{
  Params p;
  p.wave = static_cast<Wave>(GetParam(kParamWave)->Int());
  // MIDI note overrides the Pitch knob while a note is held.
  // With no MIDI note, the knob controls pitch as before.
  p.pitchHz = mMidiActive.load()
                ? std::max(20.0, std::min(8000.0, (double)mMidiPitchHz.load()))
                : GetParam(kParamPitch)->Value();
  p.glideMs = GetParam(kParamGlide)->Value();
  p.sineRateHz = GetParam(kParamSineRate)->Value();
  p.sineAmtHz = GetParam(kParamSineAmt)->Value();
  p.pulseRateHz = GetParam(kParamPulseRate)->Value();
  p.pulseAmtHz = GetParam(kParamPulseAmt)->Value();
  p.chopperOn = GetParam(kParamChopper)->Bool();
  p.chopperRamp = GetParam(kParamChopperRamp)->Value();
  p.echoTimeMs = GetParam(kParamEchoTime)->Value();
  p.echoFeedback = GetParam(kParamEchoFeedback)->Value() / 100.0;
  p.echoMix = GetParam(kParamEchoMix)->Value() / 100.0;
  p.reverbSize = GetParam(kParamReverbSize)->Value() / 100.0;
  p.reverbMix = GetParam(kParamReverbMix)->Value() / 100.0;
  p.reverbDamp = GetParam(kParamReverbDamp)->Value() / 100.0;
  p.master = GetParam(kParamMaster)->Value() / 100.0;
  mDSP.setParams(p);
  // A preset load may have requested an LFO start phase; apply it here
  // on the audio thread.
  const double pendingPhase = mPendingPhase.exchange(-1.0);
  if (pendingPhase >= 0.0)
    mDSP.setLFOPhase(pendingPhase);
}

#if IPLUG_DSP
void Dubruderschaft::ProcessMidiMsg(const IMidiMsg& msg)
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

void Dubruderschaft::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void)inputs; // siren generates its own sound; host input is ignored
  // One-shot Panic, routed from the UI thread: kill the voice now.
  // (No muting here — v0.3's latched mute is gone.)
  if (mPanicRequested.exchange(false))
    mDSP.panic();
  SyncDSPFromParams();
  mDSP.setTrigger(mTrigger.load() || mHold.load() || mMidiActive.load());

  if (static_cast<int>(mScratch.size()) < nFrames)
    mScratch.assign(nFrames, 0.f);
  mDSP.processBlock(mScratch.data(), nFrames);

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

// ---- Presets -------------------------------------------------------------

void Dubruderschaft::LoadFactoryPresets()
{
  for (int i = 0; i < 16; ++i)
  {
    const FactoryPresetDef& d = kFactoryPresets[i];
    PresetSlot& pr = mFactoryPresets[i];
    pr.filled = true;
    pr.values[kParamWave] = d.wave;
    pr.values[kParamPitch] = d.pitch;
    pr.values[kParamGlide] = d.glide;
    pr.values[kParamSineRate] = d.sineRate;
    pr.values[kParamSineAmt] = d.sineAmt;
    pr.values[kParamPulseRate] = d.pulseRate;
    pr.values[kParamPulseAmt] = d.pulseAmt;
    pr.values[kParamChopper] = d.chopper ? 1.0 : 0.0;
    pr.values[kParamChopperRamp] = d.chopperRamp;
    pr.values[kParamEchoTime] = d.echoTime;
    pr.values[kParamEchoFeedback] = d.echoFb;
    pr.values[kParamEchoMix] = d.echoMix;
    pr.values[kParamReverbSize] = d.revSize;
    pr.values[kParamReverbMix] = d.revMix;
    pr.values[kParamReverbDamp] = d.revDamp;
    pr.values[kParamMaster] = 70.0; // placeholder only — Master is NOT stored
    pr.phase = d.phase;
    std::snprintf(pr.name, sizeof(pr.name), "%s", d.name);
  }
}

int Dubruderschaft::GetActivePreset() const
{
  return mActivePreset[mPage];
}

bool Dubruderschaft::IsSlotFilled(int page, int slot) const
{
  if (slot < 0 || slot >= 16)
    return false;
  return (page == 0 ? mFactoryPresets[slot] : mUserPresets[slot]).filled;
}

void Dubruderschaft::SetPage(int page)
{
  mPage = (page == 1) ? 1 : 0;
  UpdatePresetDisplay();
  MarkUIDirty();
}

void Dubruderschaft::LoadPreset(int slot)
{
  if (slot < 0 || slot >= 16)
    return;
  const PresetSlot& pr = (mPage == 0) ? mFactoryPresets[slot] : mUserPresets[slot];
  if (!pr.filled)
    return;
  for (int i = 0; i < kNumParams; ++i)
  {
    // Master is NOT stored in presets (SPEC.md §10), so loading a
    // preset cannot cause a sudden volume jump.
    if (i == kParamMaster)
      continue;
    GetParam(i)->Set(pr.values[i]);
  }
  mPendingPhase.store(pr.phase);
  mActivePreset[mPage] = slot;
  UpdatePresetDisplay();
  MarkUIDirty();
}

void Dubruderschaft::SavePreset(int slot, const char* name)
{
  if (slot < 0 || slot >= 16)
    return;
  // Saves ALWAYS go to Page B (user presets).
  PresetSlot& pr = mUserPresets[slot];
  pr.filled = true;
  for (int i = 0; i < kNumParams; ++i)
  {
    if (i == kParamMaster)
      continue; // Master is NOT stored in presets
    pr.values[i] = GetParam(i)->Value();
  }
  pr.phase = 0.0; // user presets start the LFOs at phase 0
  if (name && name[0] != '\0')
    std::snprintf(pr.name, sizeof(pr.name), "%s", name);
  else
    pr.name[0] = '\0'; // blank -> the display shows the slot number only
  mPage = 1;
  mActivePreset[1] = slot;
  UpdatePresetDisplay();
  MarkUIDirty();
}

void Dubruderschaft::OnSaveClicked()
{
  if (mNaming)
  {
    // SAVE is showing "YES": confirm with the pending name (or the
    // slot-ID default if no name was typed).
    ConfirmNaming();
    return;
  }
  if (mSaveArmed)
  {
    // Second SAVE click while armed (before any slot was tapped):
    // cancel the save and go back to Page A.
    mSaveArmed = false;
    mPage = 0;
    UpdatePresetDisplay();
    MarkUIDirty();
    return;
  }
  // Arm the save: switch to Page B and wait for a slot tap.
  mPage = 1;
  mSaveArmed = true;
  mNaming = false;
  mActivePreset[1] = -1;
  UpdatePresetDisplay();
  MarkUIDirty();
}

void Dubruderschaft::OnSlotTapped(int slot)
{
  if (slot < 0 || slot >= 16)
    return;
  if (mSaveArmed)
  {
    // (Re)start naming for this slot: open the text entry over the
    // NAME DISPLAY. The SAVE button shows "YES" while naming.
    mNaming = true;
    mNamingSlot = slot;
    mPendingName[0] = '\0';
    UpdatePresetDisplay();
    MarkUIDirty();
#if IPLUG_EDITOR
    if (IGraphics* pGraphics = GetUI())
    {
      if (IControl* pControl = pGraphics->GetControlWithTag(kCtrlTagNameDisplay))
      {
        if (auto* pDisplay = dynamic_cast<NameDisplayControl*>(pControl))
          pDisplay->BeginNaming();
      }
    }
#endif
    return;
  }
  LoadPreset(slot);
}

void Dubruderschaft::OnBlisterFrameTapped()
{
  // Frame tap does nothing while a save is in progress; otherwise it
  // switches Page A <-> Page B (the page-switch control — the panel
  // has no A/B toggle by design).
  if (mSaveArmed || mNaming)
    return;
  SetPage(mPage == 0 ? 1 : 0);
}

void Dubruderschaft::OnNameEntered(const char* name)
{
  if (!mNaming)
    return;
  // Clean the typed name: strip control characters, trim spaces.
  // An empty result means "no name" -> the display shows the slot
  // number only.
  char clean[32] = "";
  if (name)
  {
    std::size_t j = 0;
    for (std::size_t i = 0; name[i] != '\0' && j < sizeof(clean) - 1; ++i)
    {
      const char c = name[i];
      if (c >= ' ' && c != 127)
        clean[j++] = c;
    }
    clean[j] = '\0';
    // Trim leading/trailing spaces.
    std::size_t first = 0;
    while (clean[first] == ' ' && clean[first] != '\0')
      ++first;
    std::size_t last = std::strlen(clean);
    while (last > first && clean[last - 1] == ' ')
      --last;
    if (first > 0 || last < std::strlen(clean))
    {
      std::memmove(clean, clean + first, last - first);
      clean[last - first] = '\0';
    }
  }
  std::snprintf(mPendingName, sizeof(mPendingName), "%s", clean);
  ConfirmNaming();
}

void Dubruderschaft::ConfirmNaming()
{
  if (!mNaming || mNamingSlot < 0 || mNamingSlot >= 16)
    return;
  const int slot = mNamingSlot;
  mNaming = false;
  mNamingSlot = -1;
  mSaveArmed = false;
  SavePreset(slot, mPendingName);
  mPendingName[0] = '\0';
}

void Dubruderschaft::UpdatePresetDisplay()
{
  if (mNaming)
    std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "Type a name...");
  else if (mSaveArmed)
    std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "Tap a slot to save");
  else if (mActivePreset[mPage] >= 0)
  {
    const PresetSlot& pr = (mPage == 0)
      ? mFactoryPresets[mActivePreset[mPage]]
      : mUserPresets[mActivePreset[mPage]];
    if (pr.filled && pr.name[0] != '\0')
      std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "%02d • %s",
                    mActivePreset[mPage] + 1, pr.name);
    else if (pr.filled)
      std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "%02d",
                    mActivePreset[mPage] + 1);
    else
      std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "-- • No preset");
  }
  else
    std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "-- • No preset");

#if IPLUG_EDITOR
  if (IGraphics* pGraphics = GetUI())
  {
    if (IControl* pControl = pGraphics->GetControlWithTag(kCtrlTagNameDisplay))
    {
      if (auto* pDisplay = dynamic_cast<NameDisplayControl*>(pControl))
      {
        pDisplay->SetStr(mPresetDisplay);
        pDisplay->SetDirty(true);
      }
    }
  }
#endif
}

void Dubruderschaft::MarkUIDirty()
{
#if IPLUG_EDITOR
  if (IGraphics* pGraphics = GetUI())
    pGraphics->SetAllControlsDirty();
#endif
}
