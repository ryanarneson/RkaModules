// Narrows' declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "narrows/NarrowsEngine.hpp"

#include <cstdint>

namespace rkabench {

struct narrows {
	typedef rka::NarrowsFrame F;

	struct Instance {
		rka::NarrowsEngine engine;
		F frame;
		rka::NarrowsResult result;
		Cv::Cursor cur[4][F::MAX_VOICES];
		uint32_t noise = 44444;

		// The module's defaults, nothing patched: one voice, the ladder,
		// low-pass at 1 kHz, silent.
		Instance() {
			engine.setSampleRate(FS);
			frame.params[F::CUTOFF] = rka::narrows::CUTOFF_DEFAULT;
			frame.params[F::CORE] = 1.f;
			for (int k = 0; k < 4; k++)
				for (int c = 0; c < F::MAX_VOICES; c++)
					cur[k][c] = Cv::cursor(k, c);
		}

		void process(float) {
			engine.process(frame, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.l[0] + result.r[0];
		}
		// Cheap white noise in +-5 V (no libm).
		float hiss() {
			noise = noise * 1664525u + 1013904223u;
			return ((float) (noise >> 8) * (1.f / 8388608.f) - 1.f) * 5.f;
		}
	};

	// Defaults, nothing patched. The filter always runs: it can
	// self-oscillate and be pinged with no input.
	static void idle(Instance&, long, const Cv&) {}

	// One stereo voice through the ladder: noise into both inputs, both
	// outputs, the knobs still.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.frame.inL.channels = i.frame.inR.channels = 1;
			i.frame.outRConnected = true;
			i.frame.params[F::RES] = 0.5f;
		}
		i.frame.inL.v[0] = i.hiss();
		i.frame.inR.v[0] = i.hiss();
	}

	// The same through the state core.
	static void state(Instance& i, long t, const Cv& cv) {
		if (t == 0)
			i.frame.params[F::CORE] = 0.f;
		outputs(i, t, cv);
	}

	// Sixteen voices through the ladder, everything engaged: Drive, Res
	// high, the comb in, Spread, and V/OCT, FM, Mode and Comb CVs moving on
	// every voice.
	static void busy(Instance& i, long t, const Cv& cv) {
		const int n = F::MAX_VOICES;
		if (t == 0) {
			i.frame.inL.channels = i.frame.inR.channels = n;
			i.frame.voct.channels = i.frame.fm.channels = i.frame.mode.channels = i.frame.comb.channels = n;
			i.frame.outRConnected = true;
			i.frame.params[F::RES] = 0.9f;
			i.frame.params[F::DRIVE] = 0.5f;
			i.frame.params[F::FM] = 0.3f;
			i.frame.params[F::COMB] = 0.5f;
			i.frame.params[F::SPREAD] = 0.5f;
		}
		for (int c = 0; c < n; c++) {
			i.frame.inL.v[c] = i.hiss();
			i.frame.inR.v[c] = i.hiss();
			i.frame.voct.v[c] = cv.next(i.cur[0][c]) * 0.4f;
			i.frame.fm.v[c] = cv.next(i.cur[1][c]);
			i.frame.mode.v[c] = cv.next(i.cur[2][c]) + 5.f;
			i.frame.comb.v[c] = cv.next(i.cur[3][c]) * 0.4f;
		}
	}

	static const int BENCH_CASES = 4;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.33, outputs 0.33, state 0.24, busy
		// 2.43 at 16 instances). Elsewhere: three times as much,
		// provisional (not yet measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.7, 2.1},
			{"outputs", outputs, 0.7, 2.1},
			{"state", state, 0.5, 1.5},
			{"busy", busy, 4.9, 14.7},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 4;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"state", state, true},
			{"busy", busy, false},
		};
		return cases[k];
	}
};

} // namespace rkabench
