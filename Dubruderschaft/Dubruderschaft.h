// Dubruderschaft — iPlug2 plugin class (v0.3.1, 2026-11-06).
// VST3 mono dub siren. DSP core is framework-free (DubruderschaftDSP.*);
// this class wires iPlug2 parameters + IGraphics UI to it.
// Panel: ONE flat PNG (600x500). Functional/sidebar name is ASCII
// "Dubruderschaft" (Lexi's explicit choice; ü -> plain u). The header
// artwork still reads "Dubrüderschaft" (with ü).
//
// v0.3.1 changes:
//  - Mode pills are gone: standalone LFO knobs (Sine Rate/Amt,
//    Pulse Rate/Amt) + Chopper toggle + Ramp knob. Panic is a one-shot
//    kill/reset (no longer a param, no longer latching).
//  - Pitch range 20-2000 Hz. Panel 600x500, blister 560x90.
//  - Presets are two pages sharing 16 slots: Page A = 16 factory presets
//    (translated from AudioLemon's 19), Page B = 16 user presets (start
//    empty). In-memory only (SerializeState persistence is Phase 3).
#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "DubruderschaftDSP.h"

#include <array>
#include <atomic>
#include <vector>

const int kNumPresets = 1;

enum EParams
{
  kParamWave = 0,        // enum: Sine / Saw / Square
  kParamPitch,           // Hz, 20-2000, default 600
  kParamGlide,           // ms, 0-500, default 120
  kParamSineRate,        // Hz, 0.01-1000 (exp), default 1.0
  kParamSineAmt,         // Hz, 0-10000, default 0 (LFO off)
  kParamPulseRate,       // Hz, 0.01-10000 (exp), default 5.0
  kParamPulseAmt,        // Hz, 0-10000, default 0 (LFO off)
  kParamChopper,         // bool, default off
  kParamChopperRamp,     // 0-127, default 32
  kParamEchoTime,        // ms, 0-1000, default 375
  kParamEchoFeedback,    // %, 0-85, default 45
  kParamEchoMix,         // %, 0-100, default 30
  kParamReverbSize,      // %, 0-100, default 40 (Size/Decay)
  kParamReverbMix,       // %, 0-100, default 25 (Mix Dry/Wet)
  kParamReverbDamp,      // %, 0-100, default 50
  kParamMaster,          // %, 0-100, default 70
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagNameDisplay = 0,
  kCtrlTagSaveButton,
  kCtrlTagBlister
};

using namespace iplug;
using namespace igraphics;

class Dubruderschaft final : public Plugin
{
public:
  Dubruderschaft(const InstanceInfo& info);

  void OnReset() override;

#if IPLUG_EDITOR
  // Fixed panel size: 600x500, no host resize.
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

  // Panic, one-shot (v0.3.1): called from the UI thread on click. The
  // request is routed to the audio thread (ProcessBlock calls
  // mDSP.panic()), so no DSP state is touched from the UI thread.
  void Panic() { mPanicRequested.store(true); }

  // --- Presets: two pages x 16 slots (8x2). In-memory only. Master is
  // NOT stored in presets, so loading a preset cannot cause a sudden
  // volume jump. Page A (0) = factory presets, Page B (1) = user.
  int GetPage() const { return mPage; }
  void SetPage(int page);
  int GetActivePreset() const;
  bool IsSaveArmed() const { return mSaveArmed; }
  bool IsNaming() const { return mNaming; }
  bool IsSlotFilled(int page, int slot) const;
  void LoadPreset(int slot);
  void SavePreset(int slot, const char* name);
  // Save flow (called from the UI): SAVE click, blister tap, name entry.
  void OnSaveClicked();
  void OnSlotTapped(int slot);
  void OnBlisterFrameTapped();
  void OnNameEntered(const char* name);
  void ConfirmNaming();

  float GetMeterLevel() const { return mMeterLevel.load(); }

private:
  void SyncDSPFromParams();
  void UpdatePresetDisplay();
  void LoadFactoryPresets();
  void MarkUIDirty();

  dubruderschaft::DSP mDSP;
  std::vector<float> mScratch; // mono render buffer, sized in OnReset

  std::atomic<bool> mTrigger{false};
  std::atomic<bool> mHold{false};
  std::atomic<float> mMeterLevel{0.f};
  std::atomic<bool> mPanicRequested{false};
  // LFO start phase requested by a preset load; -1 = none pending.
  // Applied on the audio thread in SyncDSPFromParams().
  std::atomic<double> mPendingPhase{-1.0};

  // MIDI state (audio thread writes, UI reads): -1 = no note held.
  std::atomic<int> mMidiNote{-1};
  std::atomic<float> mMidiPitchHz{0.f};
  std::atomic<bool> mMidiActive{false};

  struct PresetSlot
  {
    bool filled = false;
    std::array<double, kNumParams> values{};
    double phase = 0.0; // LFO start phase (0..1), from the AudioLemon preset
    char name[32] = "";
  };
  std::array<PresetSlot, 16> mFactoryPresets{};
  std::array<PresetSlot, 16> mUserPresets{};
  int mPage = 0; // 0 = Page A (factory, white blister), 1 = Page B (user, red)
  int mActivePreset[2] = {-1, -1}; // active slot per page, -1 = none
  bool mSaveArmed = false;
  bool mNaming = false;
  int mNamingSlot = -1;
  char mPendingName[32] = "";
  char mPresetDisplay[48] = "-- • No preset";
};
