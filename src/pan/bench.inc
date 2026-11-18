// Pan's declaration for the CPU bench, the libm check and the globals check
// (src/bench/BenchModule.hpp says what each part is for). Pan runs inside
// Ballast's process(), so the instance is a Ballast and Pan pair; Pan's own
// cost is this bench less Ballast's. Not part of the plugin.
#include "bench/BenchModule.hpp"
#include "ballast/BallastEngine.hpp"
#include "pan/PanCore.hpp"

namespace rkabench {

struct pan {
	typedef rka::BallastFrame F;

	struct Core : rka::PanCore {
		rka::PanFrame in;
		void readFrame(rka::PanFrame& f) override {
			f = in;
		}
	};

	struct Instance {
		rka::BallastEngine engine;
		Core core;
		F frame;
		rka::BallastResult result;
		float in[8][16];
		float panCv[4][16];
		Cv::Cursor inCur[8], cvCur[4];

		// Ballast at unity, Pan's knobs spread (one per law), attenuverters
		// 0, nothing patched.
		Instance() {
			for (int k = 0; k < 16; k++) {
				for (int i = 0; i < 8; i++)
					in[i][k] = 0.f;
				for (int i = 0; i < 4; i++)
					panCv[i][k] = 0.f;
			}
			for (int i = 0; i < 8; i++)
				inCur[i] = Cv::cursor(i, 0);
			for (int i = 0; i < 4; i++)
				cvCur[i] = Cv::cursor(8 + i, 0);
			for (int c = 0; c < 4; c++) {
				frame.inL[c].v = in[2 * c];
				frame.inR[c].v = in[2 * c + 1];
				core.in.cv[c].v = panCv[c];
			}
			for (int p = 0; p < F::PARAMS_LEN; p++)
				frame.params[p] = 0.75f;
			const float knobs[4] = {0.2f, 0.4f, 0.7f, 0.9f};
			for (int c = 0; c < 4; c++)
				core.in.knob[c] = knobs[c];
			core.setSampleRate(48000.f);
		}

		void process(float) {
			rka::BallastExpander* chain[1] = {&core};
			core.linked();
			engine.process(frame, result, chain, 1);
		}
		void reset() {
			engine.reset();
		}
		float probe() const {
			return result.l + result.r;
		}
	};

	// Ballast's outputs unpatched: Pan still smooths on every link.
	static void idle(Instance&, long, const Cv&) {}

	// Both outputs; channels 1 and 3 stereo, 2 and 4 mono; Pan's knobs
	// fixed off centre.
	static void outputs(Instance& i, long t, const Cv& cv) {
		if (t == 0) {
			i.frame.outL = i.frame.outR = true;
			for (int c = 0; c < 4; c++) {
				i.frame.inL[c].channels = 1;
				i.frame.inR[c].channels = c % 2 == 0 ? 1 : 0;
			}
		}
		for (int k = 0; k < 8; k++)
			i.in[k][0] = cv.next(i.inCur[k]);
	}

	// As outputs, with every Pan CV moving at full attenuverter.
	static void busy(Instance& i, long t, const Cv& cv) {
		outputs(i, t, cv);
		if (t == 0)
			for (int c = 0; c < 4; c++) {
				i.core.in.att[c] = 1.f;
				i.core.in.cv[c].channels = 1;
			}
		for (int c = 0; c < 4; c++)
			i.panCv[c][0] = cv.next(i.cvCur[c]);
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: about twice what an Apple M-series Mac measured for the
		// pair, rounded up to the next 0.05 (idle 0.034, outputs 0.187,
		// busy 0.211; Ballast alone measures 0.019, 0.097 and 0.104, so Pan
		// adds about 0.1). Elsewhere: three times as much, provisional.
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.1, 0.3},
			{"outputs", outputs, 0.4, 1.2},
			{"busy", busy, 0.45, 1.35},
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
