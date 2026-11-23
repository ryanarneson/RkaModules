// Spindrift: the one translation unit that compiles the vendored Mutable
// Instruments Beads DSP (src/spindrift/beads/) and the parts of stmlib it
// uses (src/spindrift/stmlib/). Built with SPINDRIFT_FW_FLAGS (tests.mk):
// -DTEST selects stmlib's portable code for its ARM assembly, and the
// floating-point flags are the same in the plugin and the tests. (-DTEST
// also guarded a printf in wavetable_granular_player.h, now marked out.)
// Local changes to the vendored files: src/spindrift/CHANGES.md.

#include "BeadsContext.hpp"
#include "GrainView.hpp"

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

// GrainView.hpp: the synth's wave for the window, from Beads' const tables
// (UI thread; no state).
int synthWave(int bank, float wave, float* out, int n) {
	if (!out || n <= 0)
		return 0;
	bank = bank < 0 ? 0 : (bank >= int(beads::kNumBanks) ? int(beads::kNumBanks) - 1 : bank);
	const float position = viewUnit(wave) * float(beads::kNumWavesPerTable - 1);
	int w = int(position);
	if (w > int(beads::kNumWavesPerTable) - 2)
		w = int(beads::kNumWavesPerTable) - 2;
	const float blend = position - float(w);
	const size_t stride = beads::kTableSize + 4;
	const int16_t* a = beads::wav_integrated_waves + (size_t(w) + size_t(bank) * beads::kNumWavesPerTable) * stride;
	const int16_t* b = a + stride;
	float peak = 0.f;
	for (int i = 0; i < n; i++) {
		// The slope at table index p, read between the samples around it.
		const float p = float(i) * float(beads::kTableSize) / float(n);
		const int k = int(p);
		const float f = p - float(k);
		const float da0 = float(a[k + 1]) - float(a[k]), da1 = float(a[k + 2]) - float(a[k + 1]);
		const float db0 = float(b[k + 1]) - float(b[k]), db1 = float(b[k + 2]) - float(b[k + 1]);
		const float da = da0 + (da1 - da0) * f, db = db0 + (db1 - db0) * f;
		out[i] = da + (db - da) * blend;
		peak = std::max(peak, std::fabs(out[i]));
	}
	if (peak > 0.f)
		for (int i = 0; i < n; i++)
			out[i] /= peak;
	return n;
}

}  // namespace spindrift
