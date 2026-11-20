// Hadal's declaration for the CPU bench, the libm check and the globals check
// (src/bench/BenchModule.hpp says what each part is for; TESTING.md rule 10
// has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "hadal/HadalEngine.hpp"

#include <cstdint>

namespace rkabench {

struct hadal {
	typedef rka::HadalFrame F;

	struct Instance {
		rka::HadalEngine engine;
		F frame;
		rka::HadalResult result;
		float cvV[5][16];
		Cv::Cursor cvCur[5];

		// The module's defaults: Depth at noon, Rumble 0.8, Hiss 0.3,
		// Crackle 0, Drift 0.25, attenuverters 0, nothing patched.
		Instance() : engine(1u) {
			for (int i = 0; i < 5; i++) {
				for (int k = 0; k < 16; k++)
					cvV[i][k] = 0.f;
				cvCur[i] = Cv::cursor(i, 0);
				frame.cv[i].v = cvV[i];
				frame.cv[i].channels = 0;
			}
			for (int p = 0; p < F::PARAMS_LEN; p++)
				frame.params[p] = 0.f;
			frame.params[F::DEPTH] = 0.5f;
			frame.params[F::RUMBLE] = 0.8f;
			frame.params[F::HISS] = 0.3f;
			frame.params[F::DRIFT] = 0.25f;
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				frame.patched[o] = false;
		}

		void process(float) {
			// The engine takes its rate at construction (48 kHz, the bench's).
			engine.process(frame, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.l + result.r + result.rumble + result.hiss + result.crackle;
		}
	};

	// No cables: Hadal does no work.
	static void idle(Instance&, long, const Cv&) {}

	// Every output patched, knobs at their defaults.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0)
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				i.frame.patched[o] = true;
	}

	// Every output patched, every layer at full, Drift full, and every CV
	// moving at full attenuverter.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				i.frame.patched[o] = true;
			i.frame.params[F::RUMBLE] = i.frame.params[F::HISS] = i.frame.params[F::CRACKLE] = 1.f;
			i.frame.params[F::DRIFT] = 1.f;
			for (int k = 0; k < 5; k++) {
				i.frame.params[F::DEPTH_ATT + k] = 1.f;
				i.frame.cv[k].channels = 1;
			}
		}
		for (int k = 0; k < 5; k++)
			i.cvV[k][0] = cv.next(i.cvCur[k]);
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.028: nothing patched, nothing computed;
		// outputs 0.201 and busy 0.214 with the crackle shaper and Rumble's
		// soft clip, flat from 1 to 16 instances).
		// Elsewhere: three times as much, provisional (not yet measured on
		// x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.1, 0.3},
			{"outputs", outputs, 0.45, 1.35},
			{"busy", busy, 0.45, 1.35},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 3;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"busy", busy, false},
		};
		return cases[k];
	}
};

} // namespace rkabench
