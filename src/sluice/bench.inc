// Sluice's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "sluice/SluiceEngine.hpp"

#include <cstring>

namespace rkabench {

struct sluice {
	typedef rka::SluiceFrame F;

	struct Instance {
		rka::SluiceEngine engine;
		F frame;
		rka::SluiceResult result;
		float inV[F::ROWS][16];
		float cvV[F::ROWS][16];
		Cv::Cursor inCur[F::ROWS][16];
		Cv::Cursor cvCur[F::ROWS][16];

		// The module's defaults: Levels 0, Exp, nothing patched.
		Instance() {
			std::memset(inV, 0, sizeof(inV));
			std::memset(cvV, 0, sizeof(cvV));
			for (int r = 0; r < F::ROWS; r++)
				for (int c = 0; c < 16; c++) {
					inCur[r][c] = Cv::cursor(r, c);
					cvCur[r][c] = Cv::cursor(r + F::ROWS, c);
				}
			for (int p = F::LEVEL_A; p <= F::LEVEL_D; p++)
				frame.params[p] = 0.f;
			frame.params[F::CURVE] = 1.f;
			for (int r = 0; r < F::ROWS; r++) {
				frame.in[r].v = inV[r];
				frame.in[r].channels = 0;
				frame.cv[r].v = cvV[r];
				frame.cv[r].channels = 0;
				frame.outConnected[r] = false;
			}
			std::memset(&result, 0, sizeof(result));
		}

		void process(float sampleRate) {
			engine.process(frame, sampleRate, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			float s = 0.f;
			for (int r = 0; r < F::ROWS; r++)
				s += result.out[r][0];
			return s;
		}
		void patchOutputs() {
			for (int r = 0; r < F::ROWS; r++)
				frame.outConnected[r] = true;
		}
	};

	// No cables, Levels 0.
	static void idle(Instance&, long, const Cv&) {}

	// Every output patched, no inputs.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0)
			i.patchOutputs();
	}

	// 16 voices on every row from Cv, a moving 16-channel CV on every row,
	// Levels 0.5, Exp, A-D connected. The knobs stay still.
	static void setBusy(Instance& i, long t, const Cv& cv, bool allConnected) {
		if (t == 0) {
			if (allConnected)
				i.patchOutputs();
			else
				i.frame.outConnected[3] = true;
			for (int p = F::LEVEL_A; p <= F::LEVEL_D; p++)
				i.frame.params[p] = 0.5f;
			i.frame.params[F::CURVE] = 1.f;
			for (int r = 0; r < F::ROWS; r++) {
				i.frame.in[r].channels = 16;
				i.frame.cv[r].channels = 16;
			}
		}
		for (int r = 0; r < F::ROWS; r++)
			for (int c = 0; c < 16; c++) {
				i.inV[r][c] = cv.next(i.inCur[r][c]);
				i.cvV[r][c] = cv.next(i.cvCur[r][c]);
			}
	}
	static void busy(Instance& i, long t, const Cv& cv) {
		setBusy(i, t, cv, true);
	}

	// As busy, but only D connected: A to C mix down into D.
	static void mixDown(Instance& i, long t, const Cv& cv) {
		setBusy(i, t, cv, false);
	}

	static const int BENCH_CASES = 4;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured (idle 0.077,
		// outputs 0.066, busy 1.07, mixDown 1.11), rounded up to the next
		// 0.05. Elsewhere: three times as much, provisional (not yet measured
		// on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.2, 0.6},
			{"outputs", outputs, 0.15, 0.45},
			{"busy", busy, 2.15, 6.45},
			{"mixDown", mixDown, 2.25, 6.75},
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
