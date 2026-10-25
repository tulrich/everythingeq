#define PLUG_NAME "Everything EQ"
#define PLUG_MFR "tulrich"
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'EvEQ'
#define PLUG_MFR_ID 'Tulr'
#define PLUG_URL_STR "https://github.com/tulrich/everythingeq"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Thatcher Ulrich"
#define PLUG_CLASS_NAME EverythingEQ

#define BUNDLE_NAME "EverythingEQ"
#define BUNDLE_MFR "tulrich"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "EverythingEQ"

// Main input - output, with an optional mono or stereo sidechain bus
// (ABOUT.md D10): mono -> mono, mono -> stereo, stereo -> stereo.
#define PLUG_CHANNEL_IO "\
1-1 \
1.1-1 \
1.2-1 \
1-2 \
1.1-2 \
1.2-2 \
2-2 \
2.1-2 \
2.2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 960
#define PLUG_HEIGHT 560
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY EverythingEQ_Entry
#define AUV2_ENTRY_STR "EverythingEQ_Entry"
#define AUV2_FACTORY EverythingEQ_Factory
#define AUV2_VIEW_CLASS EverythingEQ_View
#define AUV2_VIEW_CLASS_STR "EverythingEQ_View"

#define AAX_TYPE_IDS 'EEQ1', 'EEQ2'
#define AAX_TYPE_IDS_AUDIOSUITE 'EEA1', 'EEA2'
#define AAX_PLUG_MFR_STR "tulrich"
#define AAX_PLUG_NAME_STR "Everything EQ\nEvEQ"
#define AAX_PLUG_CATEGORY_STR "EQ"
#define AAX_DOES_AUDIOSUITE 0

#define VST3_SUBCATEGORY "Fx|EQ"
#define CLAP_MANUAL_URL "https://github.com/tulrich/everythingeq"
#define CLAP_SUPPORT_URL "https://github.com/tulrich/everythingeq/issues"
#define CLAP_DESCRIPTION "Dynamic tone and routing EQ"
#define CLAP_FEATURES "audio-effect", "equalizer", "filter"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
