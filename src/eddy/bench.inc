// Eddy's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for). Not part of
// the plugin.
//
// PROVISIONAL (Task 7): `make test`'s bench-modules guard needs this file as
// soon as EddyEngine.hpp exists. Task 8 settles the cases (a full tape in
// busy, the overview's cost) and measures the budgets three times.
#include "bench/BenchModule.hpp"
#include "eddy/EddyEngine.hpp"

#include <cstdint>

namespace rkabench {

struct eddy {
	typedef rka::EddyFrame F;

	struct Instance {
		rka::EddyEngine engine;
		F frame;
		rka::EddyResult result;
		rka::TapeSet* set = nullptr;
		Cv::Cursor cur[2];
		uint32_t noise = 77777;

		// The module's defaults, nothing patched, no tape.
		Instance() : engine(1u) {
			for (int i = 0; i < 2; i++)
				cur[i] = Cv::cursor(i, 0);
		}
		~Instance() {
			delete set;
		}
		Instance(const Instance&) = delete;
		Instance& operator=(const Instance&) = delete;

		void process(float) {
			// The engine starts at 48 kHz, the bench's rate.
			engine.process(frame, result);
		}
		void reset() {
			engine.clear();
		}
		float probe() const {
			return result.l + result.r;
		}
		// Cheap white noise in +-5 V (no libm).
		float hiss() {
			noise = noise * 1664525u + 1013904223u;
			return ((float) (noise >> 8) * (1.f / 8388608.f) - 1.f) * 5.f;
		}
		// A tape of `seconds` of noise, offered and playing.
		void tape(float seconds) {
			set = new rka::TapeSet;
			if (!set->allocate(FS))
				return;
			const size_t n = (size_t) (seconds * FS);
			for (size_t k = 0; k < n; k++)
				for (int c = 0; c < 2; c++)
					set->tape.ch[c][k] = set->original.ch[c][k] = hiss();
			set->tape.length = set->original.length = n;
			set->overview.build(set->tape);
			engine.offer(set);
			frame.playPress = true;
		}
	};

	// Defaults, nothing patched, no tape.
	static void idle(Instance&, long, const Cv&) {}

	// A 10 s tape playing at the defaults.
	static void outputs(Instance& i, long t, const Cv&) {
		i.frame.playPress = false;
		if (t == 0)
			i.tape(10.f);
	}

	// A 10 s tape at Speed 2 by a moving CV, Dub on, Wear and Smear full,
	// noise in.
	static void busy(Instance& i, long t, const Cv& cv) {
		i.frame.playPress = i.frame.dubPress = false;
		if (t == 0) {
			i.tape(10.f);
			i.frame.params[F::SPEED_ATT] = 1.f;
			i.frame.params[F::WEAR] = 1.f;
			i.frame.params[F::SMEAR] = 1.f;
			i.frame.inRPatched = true;
		}
		if (t == 64)
			i.frame.dubPress = true;
		i.frame.speedCv = 2.5f + 0.1f * cv.next(i.cur[0]);
		i.frame.inL = i.hiss();
		i.frame.inR = i.hiss();
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Provisional (Task 8 measures): about twice what an Apple M-series
		// Mac measured on Task 7's build (idle 0.075, outputs 0.125, busy
		// 0.72), rounded up; elsewhere three times as much.
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.2, 0.6},
			{"outputs", outputs, 0.3, 0.9},
			{"busy", busy, 1.5, 4.5},
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
