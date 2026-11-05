// Dubrüderschaft — iPlug2 plugin class (Phase 2, 2026-11-05).
// VST3 mono dub siren. DSP core is framework-free (DubruederschaftDSP.*);
// this class wires iPlug2 parameters + IGraphics UI to it.
// See ../SPEC.md for the locked spec. This is a smoke-test build: no human
// has heard it in Ableton yet, and the 16 preset slots are in-memory only
// (not persisted to DAW state yet — see README / Phase 3 notes).
#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "DubruederschaftDSP.h"

#include <array>
#include <atomic>
#include <vector>

const int kNumPresets = 1;

enum EParams
{
  kParamWave = 0,        // enum: Sine / Saw / Square
  kParamMode,            // enum: Manual / Auto Wail / Two-Tone
  kParamPitch,           // Hz, 200-2000, default 600
  kParamGlide,           // ms, 0-500, default 120
  kParamEchoTime,        // ms, 0-1000, default 375
  kParamEchoFeedback,    // %, 0-85, default 45
  kParamEchoMix,         // %, 0-100, default 30
  kParamReverbSize,      // %, 0-100, default 40 (Size/Decay)
  kParamReverbMix,       // %, 0-100, default 25 (Mix Dry/Wet)
  kParamReverbDamp,      // %, 0-100, default 50
  kParamMaster,          // %, 0-100, default 70
  kParamPanic,           // bool, default off — total mute (v0.2.0, Lexi's
                         // "kill the regime" button): while on, ALL sound
                         // dies, in case the output suddenly peaks.
                         // NOT stored in presets (like Master).
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagPresetDisplay = 0,
  kCtrlTagVersionNumber
};

using namespace iplug;
using namespace igraphics;

class Dubruederschaft final : public Plugin
{
public:
  Dubruederschaft(const InstanceInfo& info);

  void OnReset() override;

#if IPLUG_EDITOR
  // Fixed panel size: 780x488 (was 1200x750, scaled to 65% 2026-11-05), no host resize.
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override
  {
    return width == PLUG_WIDTH && height == PLUG_HEIGHT;
  }
#endif

#if IPLUG_DSP // http://bit.ly/2S64BDd
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  // MIDI input (v0.2.0: plugin is now a VSTi / instrument). Note On
  // triggers the siren and sets its pitch from the note; Note Off
  // stops it (unless Hold is latched). Mono: last note wins.
  void ProcessMidiMsg(const IMidiMsg& msg) override;
#endif

  // UI-driven state, not automatable params: momentary Trigger + Hold latch.
  void SetTrigger(bool on) { mTrigger.store(on); }
  void SetHold(bool latched) { mHold.store(latched); }
  bool GetHold() const { return mHold.load(); }
  int GetMidiNote() const { return mMidiNote.load(); }

  // 16 preset slots (8x2). In-memory only in this build. Master is NOT
  // stored in presets (SPEC.md §10, proposal), so loading a preset cannot
  // cause a sudden volume jump.
  void LoadPreset(int slot);
  void SavePreset(int slot);

  float GetMeterLevel() const { return mMeterLevel.load(); }

private:
  void SyncDSPFromParams();
  void UpdatePresetDisplay();

  dubruederschaft::DSP mDSP;
  std::vector<float> mScratch; // mono render buffer, sized in OnReset

  std::atomic<bool> mTrigger{false};
  std::atomic<bool> mHold{false};
  std::atomic<float> mMeterLevel{0.f};

  // MIDI state (audio thread writes, UI reads): -1 = no note held.
  std::atomic<int> mMidiNote{-1};
  std::atomic<float> mMidiPitchHz{0.f};
  std::atomic<bool> mMidiActive{false};

  struct PresetSlot
  {
    bool filled = false;
    std::array<double, kNumParams> values{};
    char name[32] = "";
  };
  std::array<PresetSlot, 16> mPresets{};
  int mActivePreset = -1;
  bool mSaveArmed = false;
  char mPresetDisplay[48] = "-- • No preset";
};
