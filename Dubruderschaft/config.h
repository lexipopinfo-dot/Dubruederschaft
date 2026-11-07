#define PLUG_NAME "Dubruderschaft"
#define PLUG_MFR "Lexi"
#define PLUG_VERSION_HEX 0x00030200
#define PLUG_VERSION_STR "0.3.2"
#define PLUG_UNIQUE_ID 'DbR1'
#define PLUG_MFR_ID 'Lexi'
#define PLUG_URL_STR "https://github.com/iPlug2/iPlug2/wiki"
#define PLUG_EMAIL_STR "spam@me.com"
#define PLUG_COPYRIGHT_STR "Copyright 2026 Lexi"
#define PLUG_CLASS_NAME Dubruderschaft

#define BUNDLE_NAME "Dubruderschaft"
#define BUNDLE_MFR "Lexi"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "Dubruderschaft"

#define PLUG_CHANNEL_IO "1-1" // kept (not "0-1"): Ableton dislikes VST3
// instruments with an empty input bus; the audio input is ignored anyway,
// the siren generates its own sound. PLUG_TYPE 1 is what makes it an
// instrument in the host.

#define PLUG_LATENCY 0
#define PLUG_TYPE 1 // 1 = instrument (VSTi), 0 = effect. Was 0 in v0.1.x.
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 600
#define PLUG_HEIGHT 500
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0
#define PLUG_MIN_WIDTH 600
#define PLUG_MIN_HEIGHT 500
#define PLUG_MAX_WIDTH 600
#define PLUG_MAX_HEIGHT 500

#define AUV2_ENTRY Dubruderschaft_Entry
#define AUV2_ENTRY_STR "Dubruderschaft_Entry"
#define AUV2_FACTORY Dubruderschaft_Factory
#define AUV2_VIEW_CLASS Dubruderschaft_View
#define AUV2_VIEW_CLASS_STR "Dubruderschaft_View"

#define AAX_TYPE_IDS 'DRB1'
#define AAX_TYPE_IDS_AUDIOSUITE 'DRA1'
#define AAX_PLUG_MFR_STR "Lexi"
#define AAX_PLUG_NAME_STR "Dubruderschaft\nDBRD"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Instrument"

#define CLAP_MANUAL_URL "https://iplug2.github.io/manuals/example_manual.pdf"
#define CLAP_SUPPORT_URL "https://github.com/iPlug2/iPlug2/wiki"
#define CLAP_DESCRIPTION "A dub siren for Ableton — one oscillator, dub echo and reverb, big master volume."
#define CLAP_FEATURES "audio-effect"//, "utility"

#define APP_NUM_CHANNELS 1
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
#define PATRICK_HAND_FN "PatrickHand-Regular.ttf"
