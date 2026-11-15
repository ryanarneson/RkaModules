// Shoal: the one translation unit that compiles the vendored O&C firmware
// (src/shoal/firmware/) together with the fake-hardware shim, and the
// shoal::Core implementation on top of it.
//
// Arduino builds concatenate the .ino files and generate prototypes; this file
// does both by hand: shim headers, then the prototypes header, then the
// firmware .cpp files, then the .ino files in LC_ALL=C name order, then the
// per-instance state (shoal::Context), then the Core implementation.

// Shim headers.
#include "shim/Context.hpp" // path from here: Rack has its own context.hpp
#include "Arduino.h"
#include "EEPROM.h"
#include "dspinst.h"

// Prototypes for functions the .ino files use before defining them.
#include "firmware/shoal_prototypes.h"

// Firmware .cpp files.
#include "firmware/OC_ADC.cpp"
#include "firmware/OC_DAC.cpp"
#include "firmware/OC_autotune.cpp"
#include "firmware/OC_bitmaps.cpp"
#include "firmware/OC_chords.cpp"
#include "firmware/OC_digital_inputs.cpp"
#include "firmware/OC_input_map.cpp"
#include "firmware/OC_menus.cpp"
#include "firmware/OC_patterns.cpp"
#include "firmware/OC_scales.cpp"
#include "firmware/OC_strings.cpp"
#include "firmware/OC_ui.cpp"
#include "firmware/bjorklund.cpp"
#include "firmware/braids_quantizer.cpp"
#include "firmware/extern/stmlib_utils_random.cpp"
#include "firmware/frames_poly_lfo.cpp"
#include "firmware/frames_resources.cpp"
#include "firmware/peaks_bytebeat.cpp"
#include "firmware/peaks_multistage_envelope.cpp"
#include "firmware/peaks_resources.cpp"
#include "firmware/src/drivers/display.cpp"
#include "firmware/src/drivers/weegfx.cpp"
#include "firmware/src/util/util_misc.cpp"
#include "firmware/streams_lorenz_generator.cpp"
#include "firmware/streams_resources.cpp"

// Firmware .ino files, LC_ALL=C name order.
#include "firmware/APP_ASR.ino"
#include "firmware/APP_AUTOMATONNETZ.ino"
#include "firmware/APP_A_SEQ.ino"
#include "firmware/APP_BBGEN.ino"
#include "firmware/APP_BYTEBEATGEN.ino"
#include "firmware/APP_CHORDS.ino"
#include "firmware/APP_DQ.ino"
#include "firmware/APP_ENVGEN.ino"
#include "firmware/APP_H1200.ino"
#include "firmware/APP_LORENZ.ino"
#include "firmware/APP_POLYLFO.ino"
#include "firmware/APP_QQ.ino"
#include "firmware/APP_REFS.ino"
#include "firmware/OC_apps.ino"
#include "firmware/OC_calibration.ino"
#include "firmware/o_c_REV.ino"

// Per-instance firmware state.
#include "FirmwareContext.inc"

// shoal::Core.
#include "CoreImpl.inc"
