// Ballast's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "ballast/BallastEngine.hpp"

namespace rkabench {

struct ballast {
	typedef rka::BallastFrame F;

	struct Instance {
		rka::BallastEngine engine;
		F frame;
		rka::BallastResult result;
		float in[8][16];
		float cvV[5][16];
		Cv::Cursor inCur[8], cvCur[5];

		// The module's defaults: every knob at unity, DC blocker on,
		// nothing patched.
		Instance() {
			for (int k = 0; k < 16; k++) {
				for (int i = 0; i < 8; i++)
					in[i][k] = 0.f;
				for (int i = 0; i < 5; i++)
					cvV[i][k] = 0.f;
			}
			for (int i = 0; i < 8; i++)
				inCur[i] = Cv::cursor(i, 0);
			for (int i = 0; i < 5; i++)
				cvCur[i] = Cv::cursor(8 + i, 0);
			for (int c = 0; c < 4; c++) {
				frame.inL[c].v = in[2 * c];
				frame.inR[c].v = in[2 * c + 1];
				frame.cv[c].v = cvV[c];
			}
			frame.masterCv.v = cvV[4];
			for (int p = 0; p < F::PARAMS_LEN; p++)
				frame.params[p] = 0.75f;
		}

		void process(float) {
			engine.process(frame, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.l + result.r;
		}
	};

	// No cables: Ballast does no work.
	static void idle(Instance&, long, const Cv&) {}

	// Both outputs and four stereo inputs patched, knobs at unity; the
	// inputs carry moving signals.
	static void outputs(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.frame.outL = i.frame.outR = true;
			for (int c = 0; c < 4; c++)
				i.frame.inL[c].channels = i.frame.inR[c].channels = 1;
		}
		for (int k = 0; k < 8; k++)
			i.in[k][0] = cv.next(i.inCur[k]);
	}

	// As outputs, plus every level CV and the master CV moving.
	static void busy(Instance& i, long t, const Cv& cv) {
		outputs(i, t, cv);
		if (t == 0)
			for (int k = 0; k < 4; k++)
				i.frame.cv[k].channels = 1;
		if (t == 0)
			i.frame.masterCv.channels = 1;
		for (int k = 0; k < 5; k++)
			i.cvV[k][0] = 5.f + cv.next(i.cvCur[k]);
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.017: nothing patched, nothing
		// computed; outputs 0.105 and busy 0.128, flat from 1 to 16
		// instances). Elsewhere: three times as much, provisional (not yet
		// measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.05, 0.15},
			{"outputs", outputs, 0.25, 0.75},
			{"busy", busy, 0.3, 0.9},
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
