// Wake's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "wake/WakeEngine.hpp"

#include <cstdint>

namespace rkabench {

struct wake {
	typedef rka::WakeFrame F;

	struct Instance {
		rka::WakeEngine engine;
		F frame;
		rka::WakeResult result;
		Cv::Cursor cur[3];
		uint32_t noise = 33333;
		int clockT = 0;

		// The module's defaults, nothing patched: an empty delay.
		Instance() : engine(1u) {
			for (int i = 0; i < 3; i++)
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

	// Defaults, nothing patched. The delay always runs (an echo in flight
	// must not wait for a cable).
	static void idle(Instance&, long, const Cv&) {}

	// An echo at the defaults: noise into IN L, both outputs, the knobs still.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.frame.inLPatched = true;
			i.frame.outRPatched = true;
		}
		i.frame.inL = i.hiss();
	}

	// Analog, Diffuse and Drift full, Ping Pong, Feedback high, noise in
	// both sides, Time, Feedback and Mix CVs moving, and a 5 Hz clock.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.frame.inLPatched = i.frame.inRPatched = true;
			i.frame.outRPatched = true;
			i.frame.clockPatched = true;
			i.frame.style = 1;
			i.frame.pingPong = true;
			i.frame.params[F::DIFFUSE] = 1.f;
			i.frame.params[F::DRIFT] = 1.f;
			i.frame.params[F::FEEDBACK] = 0.8f;
			i.frame.params[F::TIME_ATT] = 1.f;
		}
		i.frame.inL = i.hiss();
		i.frame.inR = i.hiss();
		i.frame.timeCv = cv.next(i.cur[0]);
		i.frame.feedbackCv = cv.next(i.cur[1]) * 0.2f;
		i.frame.mixCv = cv.next(i.cur[2]);
		i.frame.clock = pulse(i.clockT, 9600, 96);
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded
		// up to the next 0.05 (idle 0.155, outputs 0.157, busy 0.178 at 16
		// instances; one alone costs about 0.02 less).
		// Elsewhere: three times as much, provisional (not yet measured on
		// x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.35, 1.05},
			{"outputs", outputs, 0.35, 1.05},
			{"busy", busy, 0.4, 1.2},
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
