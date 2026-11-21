// Eddy's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "eddy/EddyEngine.hpp"

#include <cstdint>
#include <cstring>

namespace rkabench {

struct eddy {
	typedef rka::EddyFrame F;

	struct Instance {
		rka::EddyEngine engine;
		F frame;
		rka::EddyResult result;
		rka::TapeSet* set = nullptr;
		Cv::Cursor cur;
		uint32_t noise = 77777;

		// The module's defaults, nothing patched, no tape.
		Instance() : engine(1u), cur(Cv::cursor(0, 0)) {}
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
		// A tape of `seconds` of noise (0: as long as the tape can be),
		// tape and original equal, offered, and Play pressed.
		void tape(float seconds) {
			set = new rka::TapeSet;
			if (!set->allocate(FS))
				return;
			size_t n = (size_t) (seconds * FS);
			if (n == 0 || n > set->tape.capacity())
				n = set->tape.capacity();
			for (int c = 0; c < 2; c++) {
				for (size_t k = 0; k < n; k++)
					set->tape.ch[c][k] = hiss();
				std::memcpy(set->original.ch[c], set->tape.ch[c], n * sizeof(float));
			}
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

	// A full tape (5 minutes) at Speed 2 (the knob at 1 and the Speed CV),
	// Dub on, Wear and Smear at 1, noise in. Length makes a window of about
	// 1 s, so the head turns every half second: at Smear 1 the overlap is
	// half the window, so two voices read, write and wear at once all the
	// time, and the overview is rescanning the columns they write.
	static void busyStill(Instance& i, long t, const Cv&) {
		i.frame.playPress = i.frame.dubPress = false;
		if (t == 0) {
			i.tape(0.f);
			i.frame.params[F::SPEED_ATT] = 1.f;
			i.frame.params[F::WEAR] = 1.f;
			i.frame.params[F::SMEAR] = 1.f;
			i.frame.params[F::LENGTH] = 0.4465f;   // 10 ms x 30000^k = 1 s
			i.frame.inRPatched = true;
			i.frame.speedCv = 2.5f;                // +1
		}
		if (t == 64)
			i.frame.dubPress = true;
		i.frame.inL = i.hiss();
		i.frame.inR = i.hiss();
	}
	// The same with the Speed CV moving about 2 (1.8 to 2.2).
	static void busy(Instance& i, long t, const Cv& cv) {
		busyStill(i, t, cv);
		i.frame.speedCv = 2.5f + 0.1f * cv.next(i.cur);
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: twice the most an Apple M-series Mac measured in three
		// runs (idle 0.089, outputs 0.140, busy 0.845, at 1, 4 or 16
		// instances), rounded up to the next 0.05; elsewhere three times as
		// much, provisional (not measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.2, 0.6},
			{"outputs", outputs, 0.3, 0.9},
			{"busy", busy, 1.7, 5.1},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 3;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"busyStill", busyStill, true},
		};
		return cases[k];
	}
};

} // namespace rkabench
