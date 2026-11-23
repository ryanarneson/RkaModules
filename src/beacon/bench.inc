// Beacon's declaration for the CPU bench, the libm check and the globals check
// (src/bench/BenchModule.hpp says what each part is for; TESTING.md rule 10
// has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "beacon/BeaconEngine.hpp"

#include <cstdint>

namespace rkabench {

struct beacon {
	typedef rka::BeaconFrame F;

	struct Instance {
		rka::BeaconEngine engine;
		F frame;
		rka::BeaconResult result;
		float cvV[4][16];
		float gateV[16], retrigV[16];
		Cv::Cursor cvCur[4][16];

		// The module's defaults: nothing patched, Gate mode, the stage knobs
		// at their starting points.
		Instance() {
			for (int i = 0; i < 4; i++)
				for (int c = 0; c < 16; c++) {
					cvV[i][c] = 0.f;
					cvCur[i][c] = Cv::cursor(i, c);
				}
			for (int c = 0; c < 16; c++)
				gateV[c] = retrigV[c] = 0.f;
		}
		void process(float) {
			engine.process(frame, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.out[0] + result.out[result.channels - 1] + result.trig[0] + result.stageGate[0][0];
		}
	};

	// Defaults, nothing patched.
	static void idle(Instance&, long, const Cv&) {}

	// One channel, a 2 Hz gate (a quarter second high every half second).
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.frame.gate.v = i.gateV;
			i.frame.gate.channels = 1;
		}
		i.gateV[0] = (t % 24000) < 12000 ? 10.f : 0.f;
	}

	// 16 channels, gates staggered a sixteenth of a period apart, RETRIG
	// pulsing on every third channel, every CV moving, Shape 0.8.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.frame.params[F::SHAPE] = 0.8f;
			rka::BeaconPort* cvs[4] = {&i.frame.attackCv, &i.frame.decayCv, &i.frame.sustainCv, &i.frame.releaseCv};
			for (int k = 0; k < 4; k++) {
				cvs[k]->v = i.cvV[k];
				cvs[k]->channels = 16;
			}
			i.frame.gate.v = i.gateV;
			i.frame.gate.channels = 16;
			i.frame.retrig.v = i.retrigV;
			i.frame.retrig.channels = 16;
		}
		for (int k = 0; k < 4; k++)
			for (int c = 0; c < 16; c++)
				i.cvV[k][c] = cv.next(i.cvCur[k][c]);
		for (int c = 0; c < 16; c++) {
			i.gateV[c] = ((t + c * 1500) % 24000) < 12000 ? 10.f : 0.f;
			i.retrigV[c] = (c % 3 == 0 && (t + c * 700) % 9600 < 48) ? 10.f : 0.f;
		}
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.120, outputs 0.18, busy 1.0; 16 voices in
		// busy, flat from 1 to 16 instances). Elsewhere: three times as much,
		// provisional (not yet measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.25, 0.75},
			{"outputs", outputs, 0.4, 1.2},
			{"busy", busy, 2.05, 6.15},
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
