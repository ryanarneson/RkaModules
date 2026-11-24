// Neap's declaration for the CPU bench, the libm check and the globals check
// (src/bench/BenchModule.hpp says what each part is for; TESTING.md rule 10
// has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "neap/NeapEngine.hpp"
#include "spring/SpringCore.hpp"

namespace rkabench {

struct neap {
	typedef rka::NeapFrame F;

	struct Instance {
		rka::NeapEngine engine;
		F frame;
		rka::NeapResult result;
		// Spring at Neap's right, fed from the engine's shaped cycles (as the
		// module's fromNeap call does) when a case sets `linked`.
		rka::SpringCore spring;
		bool linked = false;
		Cv::Cursor rateCur, weightCur;

		// The module's defaults: Drift on, Triangle, +-5 V, nothing patched.
		Instance() : rateCur(Cv::cursor(0, 0)), weightCur(Cv::cursor(1, 0)) {}
		void process(float) {
			engine.process(frame, result);
			if (linked)
				spring.step(result.shaped, frame.sampleTime);
		}
		void reset() {
			engine.reset();
			spring.reset();
		}
		float probe() const {
			return result.cycle[0] + result.cycle[7] + result.tide + result.high + result.light[3] +
			       spring.out[rka::SpringCore::F1] + spring.out[rka::SpringCore::MAX23] + spring.out[rka::SpringCore::T4] +
			       spring.out[rka::SpringCore::SEVEN];
		}
	};

	// Defaults, nothing patched.
	static void idle(Instance&, long, const Cv&) {}

	// Every output patched costs the engine nothing more; the same knobs
	// still, Sine and 0-10 V.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.frame.wave = rka::neap::WAVE_SINE;
			i.frame.unipolar = true;
		}
	}

	// RATE CV and WEIGHT CV moving, RESET pulsing, Sine, the mark swept.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.frame.wave = rka::neap::WAVE_SINE;
			i.frame.rateAtt = 1.f;
		}
		i.frame.rateCv = cv.next(i.rateCur);
		i.frame.weightCv = cv.next(i.weightCur);
		i.frame.reset = (t % 48000) < 48 ? 10.f : 0.f;
		i.frame.mark = float(t % 9600) * (10.f / 9600.f) - 5.f;
	}

	// Spring linked, all four T outputs patched, the knobs still, Sine.
	static void springStill(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.frame.wave = rka::neap::WAVE_SINE;
			i.linked = true;
			for (int k = 0; k < 4; k++)
				i.spring.setPatched(k, true);
		}
	}

	// As busy, with Spring linked and every T output patched.
	static void springBusy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.linked = true;
			for (int k = 0; k < 4; k++)
				i.spring.setPatched(k, true);
		}
		busy(i, t, cv);
	}

	static const int BENCH_CASES = 4;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured, rounded up to
		// the next 0.05 (idle 0.18, outputs 0.25, busy 0.33, spring 0.37); elsewhere three times
		// as much, provisional (not yet measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.4, 1.2},
			{"outputs", outputs, 0.5, 1.5},
			{"busy", busy, 0.7, 2.1},
			{"spring", springBusy, 0.75, 2.25},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 4;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"busy", busy, false},
			{"spring", springStill, true},
		};
		return cases[k];
	}
};

} // namespace rkabench
