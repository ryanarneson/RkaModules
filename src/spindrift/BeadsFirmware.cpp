// Spindrift: the one translation unit that compiles the vendored Mutable
// Instruments Beads DSP (src/spindrift/beads/) and the parts of stmlib it
// uses (src/spindrift/stmlib/). Built with SPINDRIFT_FW_FLAGS (tests.mk):
// -DTEST selects stmlib's portable code for its ARM assembly, and the
// floating-point flags are the same in the plugin and the tests.
// Local changes to the vendored files: src/spindrift/CHANGES.md.

// Under TEST, beads/dsp/wavetable_granular_player.h calls printf without
// including <cstdio> (libc++ declares it through other headers, libstdc++
// does not).
#include <cstdio>

#include "beads/dsp/delay_clock.cc"
#include "beads/dsp/delay_controller.cc"
#include "beads/dsp/mu_law.cc"
#include "beads/dsp/processor.cc"
#include "beads/dsp/synth.cc"
#include "beads/dsp/trigger_generator.cc"
#include "beads/resources.cc"
#include "stmlib/dsp/units.cc"
#include "stmlib/utils/random.cc"
