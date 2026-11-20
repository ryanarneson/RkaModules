// Rigging's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "rigging/RiggingEngine.hpp"

#include <cstdint>

namespace rkabench {

struct rigging {
	typedef rka::RiggingFrame F;

	struct Instance {
		rka::RiggingEngine engine;
		F frame;
		rka::RiggingResult result;
		Cv::Cursor cur[5];
		uint32_t noise = 22222;
		int strumT = 0;

		// The module's defaults, nothing patched: six silent strings.
		Instance() : engine(1u) {
			for (int i = 0; i < 5; i++)
				cur[i] = Cv::cursor(i, 0);
		}

		void process(float) {
			// The engine takes its rate at construction (48 kHz, the bench's).
			engine.process(frame, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.l + result.r;
		}
		// Cheap white noise in +-5 V (no libm).
		float hiss() {
			noise = noise * 1664525u + 1013904223u;
			return ((float) (noise >> 8) * (1.f / 8388608.f) - 1.f) * 5.f;
		}
	};

	// Defaults, nothing patched. The strings always run (a strike or a
	// ringing string must not wait for a cable).
	static void idle(Instance&, long, const Cv&) {}

	// A drone: noise into IN L, the knobs still.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0)
			i.frame.inLPatched = true;
		i.frame.inL = i.hiss();
	}

	// Noise in both sides, V/OCT, Chord, Ring and Damp CVs all moving, and
	// STRUM at 20 Hz.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.frame.inLPatched = i.frame.inRPatched = true;
			i.frame.strumPatched = true;
		}
		i.frame.inL = i.hiss();
		i.frame.inR = i.hiss();
		i.frame.voct = cv.next(i.cur[0]) * 0.4f;
		i.frame.chordCv = cv.next(i.cur[1]);
		i.frame.ringCv = cv.next(i.cur[2]);
		i.frame.dampCv = cv.next(i.cur[3]);
		i.frame.strum = pulse(i.strumT, 2400, 48);
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.13, outputs 0.13, busy 0.27, flat from
		// 1 to 16 instances). Elsewhere: three times as much, provisional
		// (not yet measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.3, 0.9},
			{"outputs", outputs, 0.3, 0.9},
			{"busy", busy, 0.55, 1.65},
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
