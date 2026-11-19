// Squall's declaration for the CPU bench, the libm check and the globals check
// (src/bench/BenchModule.hpp says what each part is for; TESTING.md rule 10
// has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "squall/SquallEngine.hpp"

#include <cstdint>

namespace rkabench {

struct squall {
	typedef rka::SquallFrame F;

	struct Instance {
		rka::SquallEngine engine;
		F frame;
		rka::SquallResult result;
		float cvV[3][16];
		Cv::Cursor cvCur[3];

		// The module's defaults: Weather at noon, Gust 0.3, Memory 0,
		// attenuverters 0, nothing patched, free-running.
		Instance() : engine(1u) {
			for (int i = 0; i < 3; i++) {
				for (int k = 0; k < 16; k++)
					cvV[i][k] = 0.f;
				cvCur[i] = Cv::cursor(i, 0);
				frame.cv[i].v = cvV[i];
				frame.cv[i].channels = 0;
			}
		}

		void process(float) {
			// The engine takes its rate at construction (48 kHz, the bench's).
			engine.process(frame, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.step + result.swell + result.gate;
		}
	};

	// Defaults, nothing patched. Squall's outputs are cheap enough that it
	// always runs (a gate in a minute must not wait for a cable).
	static void idle(Instance&, long, const Cv&) {}

	// The same work as idle: Squall has no per-output cost. Kept as its
	// own case for the bench's shape.
	static void outputs(Instance&, long, const Cv&) {}

	// Stormy, Gust 1, Memory 0.7, every CV moving at full attenuverter,
	// clocked at 100 Hz.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.frame.params[F::WEATHER] = 1.f;
			i.frame.params[F::GUST] = 1.f;
			i.frame.params[F::MEMORY] = 0.7f;
			for (int k = 0; k < 3; k++) {
				i.frame.params[F::WEATHER_ATT + k] = 1.f;
				i.frame.cv[k].channels = 1;
			}
			i.frame.clockPatched = true;
		}
		for (int k = 0; k < 3; k++)
			i.cvV[k][0] = cv.next(i.cvCur[k]);
		i.frame.clock = (t / 240) % 2 ? 0.f : 10.f;
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.023, outputs 0.024, busy 0.029, flat
		// from 1 to 16 instances). Elsewhere: three times as much,
		// provisional (not yet measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.05, 0.15},
			{"outputs", outputs, 0.05, 0.15},
			{"busy", busy, 0.1, 0.3},
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
