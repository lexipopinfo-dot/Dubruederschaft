// Dubrüderschaft — iPlug2 plugin implementation (Phase 2, 2026-11-05).
// Wires the framework-free DSP core (DubruederschaftDSP.*) to iPlug2
// parameters and a flat option-E IGraphics UI (1200x750, red base).
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

// Option-E palette (SPEC.md §9). IColor is ARGB: IColor(alpha, r, g, b).
#if IPLUG_EDITOR
static const IColor kColRed(255, 178, 34, 48);     // base #B22230
static const IColor kColNavy(255, 10, 47, 94);     // knobs / master track
static const IColor kColBlue(255, 15, 91, 182);    // Trigger #0F5BB8
static const IColor kColYellow(255, 253, 204, 73); // Hold / accents #FDCC49
static const IColor kColWhite(255, 255, 255, 255);
static const IColor kColMuted(255, 245, 200, 200);
static const IColor kColDisplay(255, 10, 30, 70);
static const IColor kColGreen(255, 46, 160, 70);
static const IColor kColOrange(255, 230, 140, 40);
static const IColor kColMeterRed(255, 210, 60, 45);

// Momentary Trigger button: sound while pressed, stops on release.
// IVButtonControl draws the button; we only override the mouse handlers.
class TriggerButtonControl : public IVButtonControl
{
public:
  TriggerButtonControl(const IRECT& bounds, std::function<void(bool)> setTrigger,
                       const char* label, const IVStyle& style)
    : IVButtonControl(bounds, SplashClickActionFunc, label, style)
    , mSetTrigger(setTrigger)
  {
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    IVButtonControl::OnMouseDown(x, y, mod);
    mSetTrigger(true);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    IVButtonControl::OnMouseUp(x, y, mod);
    mSetTrigger(false);
  }

private:
  std::function<void(bool)> mSetTrigger;
};

// Segmented level meter, green (bottom) -> yellow -> red (top), bottom-up.
// Reads the peak level from the plugin via a lambda; no IGraphics sender
// needed. The plugin updates the atomic level from ProcessBlock.
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
    const IRECT b = mRECT;
    g.DrawRect(COLOR_WHITE, b, nullptr, 2.f);
    const float v = mGetLevel(); // 0..~1 (post-master, post-limiter peak)
    const int nSegs = 6;
    const IColor segCols[6] = {kColGreen, kColGreen, kColYellow,
                               kColYellow, kColOrange, kColMeterRed};
    const float segH = b.H() / static_cast<float>(nSegs);
    for (int i = 0; i < nSegs; ++i)
    {
      if (v >= (i + 1) / static_cast<float>(nSegs) - 0.001f)
      {
        IRECT seg(b.L + 3.f, b.B - segH * (i + 1) + 2.f,
                  b.R - 3.f, b.B - segH * i - 2.f);
        g.FillRect(segCols[i], seg);
      }
    }
  }

private:
  std::function<float()> mGetLevel;
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

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    if (pGraphics->NControls())
      return; // fixed-size panel: create once, no resize handling

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->AttachPanelBackground(kColRed);

    const IVStyle knobStyle = DEFAULT_STYLE
      .WithColor(kFG, kColNavy)
      .WithColor(kPR, kColYellow)
      .WithColor(kFR, kColYellow)
      .WithColor(kBG, kColRed)
      .WithLabelText(IText(17, COLOR_WHITE))
      .WithValueText(IText(14, COLOR_WHITE));
    const IVStyle buttonStyle = DEFAULT_STYLE
      .WithColor(kFG, kColWhite)
      .WithColor(kPR, kColYellow)
      .WithColor(kFR, kColYellow)
      .WithColor(kBG, kColRed)
      .WithLabelText(IText(17, IColor(255, 41, 45, 108)))
      .WithValueText(IText(14, IColor(255, 41, 45, 108)));
    const IVStyle saveStyle = DEFAULT_STYLE
      .WithColor(kFG, kColYellow)
      .WithColor(kPR, kColYellow)
      .WithColor(kFR, kColYellow)
      .WithColor(kBG, kColRed)
      .WithLabelText(IText(19, IColor(255, 41, 45, 108)));

    // Header — plugin name must show DUBRÜDERSCHAFT (SPEC.md §9, LOCKED).
    pGraphics->AttachControl(new ITextControl(IRECT(40, 28, 560, 62),
                                              "DUBRÜDERSCHAFT", IText(28, COLOR_WHITE)));
    WDL_String buildInfoStr;
    GetBuildInfoStr(buildInfoStr, __DATE__, __TIME__);
    pGraphics->AttachControl(new ITextControl(IRECT(860, 36, 1160, 56),
                                              buildInfoStr.Get(),
                                              DEFAULT_TEXT.WithSize(14).WithFGColor(COLOR_WHITE).WithAlign(EAlign::Far)),
                             kCtrlTagVersionNumber);

    // Section labels.
    pGraphics->AttachControl(new ITextControl(IRECT(40, 76, 200, 96), "SIREN", IText(14, kColMuted)));
    pGraphics->AttachControl(new ITextControl(IRECT(40, 240, 200, 260), "ECHO", IText(14, kColMuted)));
    pGraphics->AttachControl(new ITextControl(IRECT(40, 405, 200, 425), "REVERB", IText(14, kColMuted)));
    pGraphics->AttachControl(new ITextControl(IRECT(680, 76, 870, 96), "VOICE", IText(19, COLOR_WHITE)));
    pGraphics->AttachControl(new ITextControl(IRECT(945, 76, 1005, 96), "MASTER", IText(19, COLOR_WHITE)));
    pGraphics->AttachControl(new ITextControl(IRECT(1065, 76, 1125, 96), "LEVEL", IText(19, COLOR_WHITE)));
    pGraphics->AttachControl(new ITextControl(IRECT(40, 618, 160, 638), "PRESETS", IText(19, COLOR_WHITE)));
    pGraphics->AttachControl(new ITextControl(IRECT(40, 648, 160, 668), "8 x 2  •  16 slots", IText(14, kColMuted)));

    // Siren row: Trigger (momentary, blue) + Hold (latch, yellow) + Pitch.
    pGraphics->AttachControl(new TriggerButtonControl(
        IRECT(102, 102, 198, 198),
        [this](bool on) { SetTrigger(on); },
        "TRIGGER",
        DEFAULT_STYLE.WithColor(kFG, kColBlue).WithColor(kPR, kColBlue)
                      .WithColor(kFR, kColYellow).WithColor(kBG, kColRed)
                      .WithLabelText(IText(17, COLOR_WHITE))));
    pGraphics->AttachControl(new IVToggleControl(
        IRECT(282, 102, 378, 198),
        [this](IControl* pControl) { SetHold(pControl->GetValue() > 0.5); },
        "HOLD",
        DEFAULT_STYLE.WithColor(kFG, kColYellow).WithColor(kPR, kColYellow)
                      .WithColor(kFR, COLOR_WHITE).WithColor(kBG, kColRed)
                      .WithLabelText(IText(17, COLOR_WHITE)),
        "HOLD", "HOLD", false));
    pGraphics->AttachControl(new IVKnobControl(IRECT(466, 106, 554, 194), kParamPitch, "PITCH", knobStyle));

    // Echo row.
    pGraphics->AttachControl(new IVKnobControl(IRECT(106, 271, 194, 359), kParamEchoTime, "TIME", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(IRECT(286, 271, 374, 359), kParamEchoFeedback, "FEEDBACK", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(IRECT(466, 271, 554, 359), kParamEchoMix, "ECHO MIX", knobStyle));

    // Reverb row.
    pGraphics->AttachControl(new IVKnobControl(IRECT(106, 436, 194, 524), kParamReverbSize, "SIZE / DECAY", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(IRECT(286, 436, 374, 524), kParamReverbDamp, "DAMP", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(IRECT(466, 436, 554, 524), kParamReverbMix, "REVERB MIX", knobStyle));

    // VOICE column: Wave + Mode (vertical tab switches) + Glide.
    pGraphics->AttachControl(new IVTabSwitchControl(
        IRECT(680, 124, 870, 256), kParamWave,
        std::vector<const char*>{"SINE", "SAW", "SQUARE"}, "WAVE", buttonStyle,
        EVShape::Rectangle, EDirection::Vertical));
    pGraphics->AttachControl(new IVTabSwitchControl(
        IRECT(680, 290, 870, 422), kParamMode,
        std::vector<const char*>{"MANUAL", "AUTO WAIL", "TWO-TONE"}, "MODE", buttonStyle,
        EVShape::Rectangle, EDirection::Vertical));
    pGraphics->AttachControl(new IVKnobControl(IRECT(739, 444, 811, 516), kParamGlide, "GLIDE", knobStyle));

    // Master fader (vertical) + segmented level meter, far right.
    pGraphics->AttachControl(new IVSliderControl(
        IRECT(945, 110, 1005, 540), kParamMaster, "MASTER",
        DEFAULT_STYLE.WithColor(kFG, kColYellow).WithColor(kPR, kColYellow)
                      .WithColor(kFR, kColYellow).WithColor(kBG, kColRed)
                      .WithLabelText(IText(17, COLOR_WHITE)),
        false, EDirection::Vertical, DEFAULT_GEARING, 30.f, 6.f));
    pGraphics->AttachControl(new LevelMeterControl(
        IRECT(1065, 110, 1125, 540),
        [this]() { return GetMeterLevel(); }));
    pGraphics->AttachControl(new ITextControl(IRECT(1065, 552, 1125, 572), "green low", IText(14, kColMuted)));
    pGraphics->AttachControl(new ITextControl(IRECT(1065, 572, 1125, 592), "red high", IText(14, kColMuted)));

    // Presets strip: 16 slot buttons, SAVE, preset-name display.
    static const char* kSlotLabels[16] = {"1", "2", "3", "4", "5", "6", "7", "8",
                                          "9", "10", "11", "12", "13", "14", "15", "16"};
    for (int slot = 0; slot < 16; ++slot)
    {
      const int col = slot % 8, row = slot / 8;
      const float x0 = 180.f + col * 56.f, y0 = 618.f + row * 56.f;
      pGraphics->AttachControl(new IVButtonControl(
          IRECT(x0, y0, x0 + 44.f, y0 + 44.f),
          [this, slot](IControl*) {
            if (mSaveArmed)
            {
              SavePreset(slot);
              mSaveArmed = false;
            }
            else
            {
              LoadPreset(slot);
            }
          },
          kSlotLabels[slot], buttonStyle));
    }
    pGraphics->AttachControl(new IVButtonControl(
        IRECT(680, 630, 820, 700),
        [this](IControl*) {
          mSaveArmed = true;
          std::snprintf(mPresetDisplay, sizeof(mPresetDisplay), "Tap a slot to save");
          UpdatePresetDisplay();
        },
        "SAVE", saveStyle));
    pGraphics->AttachControl(new ITextControl(IRECT(850, 618, 1160, 706),
                                              mPresetDisplay,
                                              IText(22, kColYellow), kColDisplay),
                             kCtrlTagPresetDisplay);
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
  p.pitchHz = GetParam(kParamPitch)->Value();
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
void Dubruederschaft::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void)inputs; // siren generates its own sound; host input is ignored
  SyncDSPFromParams();
  mDSP.setTrigger(mTrigger.load() || mHold.load());

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

void Dubruederschaft::LoadPreset(int slot)
{
  if (slot < 0 || slot >= 16 || !mPresets[slot].filled)
    return;
  const PresetSlot& pr = mPresets[slot];
  for (int i = 0; i < kNumParams; ++i)
  {
    if (i == kParamMaster)
      continue; // Master is NOT stored in presets (SPEC.md §10)
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
    if (i == kParamMaster)
      continue; // Master is NOT stored in presets (SPEC.md §10)
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
