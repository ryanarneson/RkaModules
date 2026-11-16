// Fog's declaration for the CPU bench, the libm check and the globals check
// (src/bench/BenchModule.hpp says what each part is for; TESTING.md rule 10
// has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "fog/FogEngine.hpp"

#include <cstdint>

namespace rkabench {

struct fog {
	typedef rka::FogFrame F;

	struct Instance {
		rka::FogEngine engine;
		F frame;
		rka::FogResult result;
		float inV[2][16];
		float cvV[5][16];
		Cv::Cursor cvCur[5];
		uint32_t noise;

		// The module's defaults: Visibility, Size, Damp, Bloom, Drift and Mix
		// at noon, attenuverters 0, HAAR, low cut 50 Hz, nothing patched.
		Instance() : noise(12345u) {
			for (int c = 0; c < 2; c++)
				for (int k = 0; k < 16; k++)
					inV[c][k] = 0.f;
			for (int i = 0; i < 5; i++) {
				for (int k = 0; k < 16; k++)
					cvV[i][k] = 0.f;
				cvCur[i] = Cv::cursor(i, 0);
			}
			for (int p = 0; p < F::PARAMS_LEN; p++)
				frame.params[p] = 0.5f;
			for (int p = F::SIZE_ATT; p <= F::MIX_ATT; p++)
				frame.params[p] = 0.f;
			frame.inL.v = inV[0];
			frame.inL.channels = 0;
			frame.inR.v = inV[1];
			frame.inR.channels = 0;
			for (int i = 0; i < 5; i++) {
				frame.cv[i].v = cvV[i];
				frame.cv[i].channels = 0;
			}
			frame.mode = 0;
			frame.driftRate = 0;
			frame.lowCut = 1;
			frame.dryKill = false;
			result.outL = result.outR = 0.f;
		}

		void process(float) {
			// The engine takes its rate at construction (48 kHz, the bench's).
			engine.process(frame, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.outL + result.outR;
		}
		// Uniform noise in +/-5 V (an LCG: no libm).
		float next() {
			noise = noise * 1664525u + 1013904223u;
			return (float) (noise >> 8) * (10.f / 16777216.f) - 5.f;
		}
	};

	// No cables, knobs at their defaults.
	static void idle(Instance&, long, const Cv&) {}

	// Fog has no output jacks the engine reads: as idle.
	static void outputs(Instance&, long, const Cv&) {}

	// Low noise (+/-3 mV: over the engine's sleep threshold, 1e-5 V, and over
	// Bloom's 1 mV presence gate, so the feed is not gated off) into both
	// inputs, knobs at their defaults, no CV: the engine stays awake, so this
	// is the guard for the cache-aliased delay lines that idle used to be.
	static void quiet(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.frame.inL.channels = 1;
			i.frame.inR.channels = 1;
		}
		i.inV[0][0] = i.next() * 6e-4f;
		i.inV[1][0] = i.next() * 6e-4f;
	}

	// Stereo noise in, every CV moving at full attenuverter, Drift and Bloom
	// up, Mix 0.5, in the mode's voicing. The knobs stay still.
	static void setBusy(Instance& i, long t, const Cv& cv, int mode) {
		if (t == 0) {
			i.frame.mode = mode;
			i.frame.inL.channels = 1;
			i.frame.inR.channels = 1;
			for (int k = 0; k < 5; k++)
				i.frame.cv[k].channels = 1;
			i.frame.params[F::VISIBILITY] = 0.8f;
			i.frame.params[F::DRIFT] = 0.8f;
			i.frame.params[F::BLOOM] = 0.5f;
			i.frame.params[F::MIX] = 0.5f;
			for (int p = F::SIZE_ATT; p <= F::MIX_ATT; p++)
				i.frame.params[p] = 1.f;
		}
		i.inV[0][0] = i.next();
		i.inV[1][0] = i.next();
		for (int k = 0; k < 5; k++)
			i.cvV[k][0] = cv.next(i.cvCur[k]);
	}
	static void busy(Instance& i, long t, const Cv& cv) {
		setBusy(i, t, cv, rka::fog::HAAR);
	}
	// ICE: its shifter runs in the loop (one projection, a band-pass and two
	// integer reads a sample); with two diffuser stages to HAAR's four, it
	// costs a little less than HAAR. SMOKE (8 lines) and FRET (HAAR's shape)
	// cost no more than HAAR.
	static void busyIce(Instance& i, long t, const Cv& cv) {
		setBusy(i, t, cv, rka::fog::ICE);
	}

	static const int BENCH_CASES = 5;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.059 and outputs 0.059: the engine sleeps
		// when nothing is patched; quiet 0.57-0.59, after the delay lines'
		// write positions were staggered; before that, 1.09, 1.09 and 1.09).
		// Quiet keeps the old idle budget, 0.95, so the old, cache-aliased
		// lines (1.03-1.11) fail it. Busy 0.75 (0.81 at 16) and busyIce 0.71
		// (0.74 at 16) with the four voicings tuned (plan Task 7; were 0.72
		// each, and 1.32 before the stagger): 1.55 and 1.45. Elsewhere: three
		// times as much, provisional (not yet measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.15, 0.45},
			{"outputs", outputs, 0.15, 0.45},
			{"quiet", quiet, 0.95, 2.85},
			{"busy", busy, 1.55, 4.65},
			{"busyIce", busyIce, 1.45, 4.35},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 3;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"busy", busy, true},
		};
		return cases[k];
	}
};

} // namespace rkabench
