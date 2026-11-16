// Sway's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "sway/SwayEngine.hpp"

#include <cstring>

namespace rkabench {

struct sway {
	typedef rka::SwayFrame F;

	struct Instance {
		rka::SwayEngine engine;
		F frame;
		rka::SwayResult result;
		float volts[F::INPUTS_LEN][16];
		Cv::Cursor cv[F::INPUTS_LEN][16];
		int clock[16], reset_[16], hold[16];

		// The module's defaults: knobs at their defaults, nothing patched.
		Instance() {
			std::memset(volts, 0, sizeof(volts));
			for (int i = 0; i < F::INPUTS_LEN; i++)
				for (int c = 0; c < 16; c++)
					cv[i][c] = Cv::cursor(i, c);
			for (int c = 0; c < 16; c++) {
				clock[c] = c;
				reset_[c] = 31 * c;
				hold[c] = 1000 * c;
			}
			float* p = frame.params;
			p[F::RATE] = 1.f;
			p[F::PW] = 0.5f;
			p[F::PHASE] = 0.f;
			p[F::SKEW] = 0.5f;
			p[F::SMOOTH] = 0.f;
			p[F::MORPH] = 0.f;
			p[F::SPREAD] = 0.f;
			p[F::CHANNELS] = 1.f;
			for (int a = F::FM_ATT; a <= F::MORPH_ATT; a++)
				p[a] = 0.f;
			p[F::OFFSET] = 0.f;
			p[F::ONESHOT] = 0.f;
			frame.range = rka::RANGE_NORMAL;
			for (int i = 0; i < F::INPUTS_LEN; i++) {
				frame.inputs[i].v = volts[i];
				frame.inputs[i].channels = 0;
			}
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				frame.outputConnected[o] = false;
		}

		void process(float sampleRate) {
			engine.process(frame, 1.f / sampleRate, sampleRate, result);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			float s = 0.f;
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				s += result.out[o][0];
			return s;
		}
		void patchOutputs() {
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				frame.outputConnected[o] = true;
		}
	};

	// No cables, knobs at their defaults.
	static void idle(Instance&, long, const Cv&) {}

	// Every output patched, no inputs, knobs at their defaults.
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0)
			i.patchOutputs();
	}

	// 16 channels, a moving CV on every CV input, a 4 Hz clock, a reset
	// every 1.5 s and a hold now and then (the channels a little apart),
	// every output patched.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.patchOutputs();
			float* p = i.frame.params;
			p[F::CHANNELS] = 16.f;
			p[F::RATE] = 3.f;
			p[F::SPREAD] = 0.5f;
			p[F::MORPH] = 0.4f;
			for (int a = F::FM_ATT; a <= F::MORPH_ATT; a++)
				p[a] = 0.5f;
			for (int k = 0; k < F::INPUTS_LEN; k++)
				i.frame.inputs[k].channels = 16;
		}
		for (int k = F::FM; k <= F::MORPH_IN; k++)
			for (int c = 0; c < 16; c++)
				i.volts[k][c] = cv.next(i.cv[k][c]);
		for (int c = 0; c < 16; c++) {
			i.volts[F::CLOCK][c] = pulse(i.clock[c], 12000, 600);
			i.volts[F::RESET][c] = pulse(i.reset_[c], 72000, 48);
			i.volts[F::HOLD][c] = pulse(i.hold[c], 96000, 4800);
		}
	}

	// Outputs patched and a mono FM cable at a still 1.3 V.
	static void monoFm(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.patchOutputs();
			i.frame.inputs[F::FM].channels = 1;
			i.volts[F::FM][0] = 1.3f;
			i.frame.params[F::FM_ATT] = 0.5f;
		}
	}

	// Outputs patched, RATE sweeping.
	static void rateMoving(Instance& i, long t, const Cv&) {
		if (t == 0)
			i.patchOutputs();
		i.frame.params[F::RATE] = -2.f + 4.f * float(t % 48000) / 48000.f;
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets set on an Apple M-series Mac so the pre-issue-#8 engine
		// fails idle and outputs (0.50 and 0.50 before); busy cost the same
		// before and after there, so its budget only catches a gross
		// regression. Elsewhere: three times as much, provisional (the only
		// x86 runs so far were emulated).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.3, 0.9},
			{"outputs", outputs, 0.4, 1.2},
			{"busy", busy, 3.0, 9.0},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 4;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"outputs, mono FM", monoFm, true},
			{"outputs, RATE moving", rateMoving, false},
		};
		return cases[k];
	}
};

} // namespace rkabench
