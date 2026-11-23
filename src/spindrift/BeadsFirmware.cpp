// Spindrift: the one translation unit that compiles the vendored Mutable
// Instruments Beads DSP (src/spindrift/beads/) and the parts of stmlib it
// uses (src/spindrift/stmlib/). Built with SPINDRIFT_FW_FLAGS (tests.mk):
// -DTEST selects stmlib's portable code for its ARM assembly, and the
// floating-point flags are the same in the plugin and the tests. (-DTEST
// also guarded a printf in wavetable_granular_player.h, now marked out.)
// Local changes to the vendored files: src/spindrift/CHANGES.md.

#include "BeadsContext.hpp"

#include "beads/dsp/delay_clock.cc"
#include "beads/dsp/delay_controller.cc"
#include "beads/dsp/mu_law.cc"
#include "beads/dsp/processor.cc"
#include "beads/dsp/synth.cc"
#include "beads/dsp/trigger_generator.cc"
#include "beads/resources.cc"
#include "stmlib/dsp/units.cc"
#include "stmlib/utils/random.cc"

namespace spindrift {

// The one thread-local: the current BeadsContext, read by stmlib::Random
// (random.cc) and set by BeadsScope (BeadsContext.hpp). As Shoal's
// shoal::current() (FirmwareContext.inc); `make spindrift-globals` allows this
// one and nothing else.
BeadsContext*& current() {
	static thread_local BeadsContext* context = nullptr;
	return context;
}

}  // namespace spindrift
