// Fathom's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "fathom/FathomEngine.hpp"

#include <cmath>

namespace rkabench {

struct fathom {
	typedef rka::FathomFrame F;

	struct Instance {
		rka::FathomEngine engine;
		F frame;
		float out[F::OUTPUTS_LEN];
		Cv::Cursor cv[F::INPUTS_LEN];

		// The module's defaults: knobs at their defaults, nothing patched.
		Instance() {
			for (int i = 0; i < F::INPUTS_LEN; i++)
				cv[i] = Cv::cursor(i, 0);
			float* p = frame.params;
			p[F::PITCH] = 0.f;
			p[F::SUB] = 0.5f;
			p[F::DETUNE] = 0.25f;
			p[F::SPREAD] = 0.6f;
			p[F::DRIFT] = 0.25f;
			p[F::CUTOFF] = std::log2(1200.f / 20.f);
			p[F::DETUNE_ATT] = p[F::DRIFT_ATT] = p[F::CUTOFF_ATT] = 0.f;
			p[F::RES] = 0.2f;
			p[F::DRIVE] = 0.25f;
			p[F::RES_ATT] = p[F::DRIVE_ATT] = 0.f;
			for (int i = 0; i < F::INPUTS_LEN; i++)
				frame.inputs[i] = 0.f;
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				frame.outputConnected[o] = false;
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				out[o] = 0.f;
		}

		void process(float sampleRate) {
			engine.process(frame, sampleRate, out);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return out[0] + out[1] + out[2];
		}
		void patchOutputs() {
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				frame.outputConnected[o] = true;
		}
	};

	// No cables, knobs at their defaults.
	static void idle(Instance&, long, const Cv&) {}

	// Every output patched, no inputs, knobs at their defaults.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0)
			i.patchOutputs();
	}

	// Full detune, drift and spread, a moving CV on every input (a slow
	// vibrato of a semitone on V/OCT, the rest +/-5 V), every output
	// patched.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.patchOutputs();
			float* p = i.frame.params;
			p[F::DETUNE] = 1.f;
			p[F::DRIFT] = 1.f;
			p[F::SPREAD] = 1.f;
			p[F::DETUNE_ATT] = p[F::DRIFT_ATT] = p[F::CUTOFF_ATT] = 0.5f;
			p[F::RES_ATT] = p[F::DRIVE_ATT] = 0.5f;
		}
		i.frame.inputs[F::VOCT] = cv.next(i.cv[0]) / 60.f;
		for (int k = F::DETUNE_IN; k <= F::DRIVE_IN; k++)
			i.frame.inputs[k] = cv.next(i.cv[k]);
	}

	// Outputs patched, CUTOFF and DETUNE sweeping.
	static void knobsMoving(Instance& i, long t, const Cv&) {
		if (t == 0)
			i.patchOutputs();
		i.frame.params[F::CUTOFF] = 2.f + 6.f * float(t % 48000) / 48000.f;
		i.frame.params[F::DETUNE] = float(t % 24000) / 24000.f;
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets set on an Apple M-series Mac so the pre-issue-#8 engine
		// fails idle (0.58 before); outputs and busy cost the same before
		// and after there, so their budgets only catch a gross regression.
		// Elsewhere: three times as much, provisional (the only x86 runs so
		// far were emulated).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.1, 0.3},
			{"outputs", outputs, 1.2, 3.6},
			{"busy", busy, 1.2, 3.6},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 3;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"outputs, knobs moving", knobsMoving, false},
		};
		return cases[k];
	}
};

} // namespace rkabench
