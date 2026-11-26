// Doldrums' declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets' history). Not part of the plugin. The transform
// is the vendored pffft, which tests.mk links into the tools
// (BENCH_VENDOR_OBJS).
//
// Besides the cases, extra() measures the most work done on any one sample
// (the design's "Staging": under 100 us on the development Mac at 96 kHz
// with the Long grain), piece by piece, and fails over that bound.
#include "bench/BenchModule.hpp"
#include "doldrums/DoldrumsEngine.hpp"
#include "doldrums/TapeView.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace rkabench {

struct doldrums {
	typedef ::rka::doldrums::DoldrumsFrame F;
	typedef ::rka::doldrums::DoldrumsEngine Engine;

	struct Instance {
		Engine engine;
		::rka::doldrums::TapeView view;   // fed as the module feeds it: wrote() for each frame written, publish() 60 times a second
		long long viewGap = 0, viewEvery = 800;
		F frame;
		::rka::doldrums::DoldrumsResult result;
		float voct[16];
		Cv::Cursor voctCur[16], stretchCur;
		uint32_t noise = 77777;
		uint32_t phase = 0;

		// The module's defaults: knobs at their defaults, nothing patched,
		// the buffers made for the rate (as the module does off the audio
		// thread). The bench runs at 48 kHz; extra() makes them at 96 kHz too.
		explicit Instance(float rate = FS) : stretchCur(Cv::cursor(16, 0)) {
			engine.allocate(rate);
			engine.setSampleRate(rate);
			engine.seed(0x5eed);
			view.configure(engine.state().capacity, 0);
			viewEvery = (long long) (rate / 60.f);
			for (int c = 0; c < 16; c++) {
				voct[c] = 0.f;
				voctCur[c] = Cv::cursor(c, 0);
			}
		}
		Instance(const Instance&) = delete;
		Instance& operator=(const Instance&) = delete;

		void process(float) {
			::rka::doldrums::Ring& ring = engine.ring();
			const long long before = ring.now();
			engine.process(frame, result);
			if (ring.now() > before) {
				const long long p = ring.physical(ring.now() - 1);
				view.wrote(ring.channel(0)[p], ring.channel(1)[p]);
			}
			if (++viewGap >= viewEvery) {
				viewGap = 0;
				view.publish(engine.state());
			}
		}
		// The module's Initialize: a silent tape, the params and the menu at
		// their defaults.
		void reset() {
			frame = F();
			engine.reset();
			view.clear();
		}
		float probe() const {
			return result.outL + result.outR + result.run;
		}
		// Cheap white noise in +-5 V (no libm).
		float hiss() {
			noise = noise * 1664525u + 1013904223u;
			return ((float) (noise >> 8) * (1.f / 8388608.f) - 1.f) * 5.f;
		}
		// A 5 V sine at hz (at 48 kHz) from the tools' table (no libm).
		float tone(const Cv& cv, double hz) {
			phase += uint32_t(hz / FS * 4294967296.0);
			return cv.v[phase >> 20];
		}
	};

	// Defaults, nothing patched: recording silence, a grain every hop.
	static void idle(Instance&, long, const Cv&) {}

	// Recording a 220 Hz tone into IN L (IN R unpatched takes it), defaults.
	static void outputs(Instance& i, long, const Cv& cv) {
		i.frame.inL = i.tone(cv, 220.0);
	}

	// The busy patch's settings, the CVs where they start.
	static void busySetup(Instance& i) {
		F& f = i.frame;
		f.grainSize = ::rka::doldrums::GRAIN_LONG;
		f.inRPatched = true;
		f.params[F::HARMONICS] = 1.f;
		f.params[F::BLUR] = 1.f;
		f.params[F::SHIFT] = 7.f;
		f.params[F::STRETCH_ATT] = 0.5f;
		f.voct = i.voct;
		f.voctChannels = 16;
	}

	// Recording stereo noise, the Long grain, a 16-note comb (Harmonics 1)
	// from V/OCT moving (each channel within +-1 V), Blur 1, Shift +7, the
	// Stretch CV moving: the comb is rebuilt all the time.
	static void busy(Instance& i, long t, const Cv& cv) {
		if (t == 0)
			busySetup(i);
		i.frame.inL = i.hiss();
		i.frame.inR = i.hiss();
		for (int c = 0; c < 16; c++)
			i.voct[c] = 0.2f * cv.next(i.voctCur[c]);
		i.frame.stretchCv = cv.next(i.stretchCur);
	}

	// As busy with the CVs still: the tables built once, then only applied.
	static void busyStill(Instance& i, long t, const Cv&) {
		if (t == 0) {
			busySetup(i);
			for (int c = 0; c < 16; c++)
				i.voct[c] = 0.0625f * float(c);
		}
		i.frame.inL = i.hiss();
		i.frame.inR = i.hiss();
	}

	static const int BENCH_CASES = 3;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: 1.5 times the most an Apple M-series Mac (M5 Pro) measured
		// in three runs at 1, 4 or 16 instances (2026-11-26, with the window's
		// view fed as the module feeds it, wrote() each frame written and
		// publish() 60 times a second, which costs about 0.008: idle 0.111,
		// outputs 0.112, busy 0.326; before the view 0.100, 0.100, 0.308), rounded up to the next 0.05; elsewhere
		// three times the Mac's, provisional (not measured on x86-64). Idle
		// costs what outputs does: the grains run on silence as on a tone.
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.2, 0.6},
			{"outputs", outputs, 0.2, 0.6},
			{"busy", busy, 0.5, 1.5},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 4;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"busyStill", busyStill, true},
			{"busy", busy, false},
		};
		return cases[k];
	}

	// ---- The worst single sample (the design's "Staging").

	// Each piece of the staging staged() can name, for the report.
	static int pieceOf(int staged) {
		if (staged == 0) return 0;
		for (int b = 0; b < 12; b++)
			if (staged == (1 << b)) return b + 1;
		return 13;   // more than one piece on one sample: never, by the staging
	}
	static const char* pieceName(int p) {
		switch (p) {
		case 0: return "no piece";
		case 1: return "boundary";
		case 2: return "WINDOW";
		case 3: return "FORWARD_L";
		case 4: return "FORWARD_R";
		case 5: return "LOUDNESS";
		case 6: return "SPECTRAL_L";
		case 7: return "SPECTRAL_R";
		case 8: return "PHASES";
		case 9: return "INVERSE_L";
		case 10: return "INVERSE_R";
		case 11: return "add";
		case 12: return "tables";
		default: return "two pieces";
		}
	}

	// The busiest tables (busy, with Tilt and 32 partials: every table in
	// use, Shift up) at `rate`, Long grain, recording noise, V/OCT moving so
	// the tables are always being rebuilt. The same `seconds` run four
	// times, from a new engine each time (the same seed and inputs, so the
	// same work on each sample); the first, untimed, brings the core up to
	// its working clock (from a light load an M-series core starts slow: the
	// first grains' pieces took up to 1.7 times as long). A sample's time is
	// the least of the other three, which drops a preemption that hit it
	// once, and each piece's figure is the most of its samples. Returns the
	// worst piece's time in us.
	static double worstSample(float rate, double seconds, const Cv& cv, bool print) {
		typedef std::chrono::steady_clock Clock;
		const long n = long(seconds * rate);
		std::vector<float> best(size_t(n), 1e30f);
		std::vector<unsigned short> what(size_t(n), 0);
		float sink = 0.f;
		for (int rep = 0; rep < 4; rep++) {
			std::unique_ptr<Instance> i(new Instance(rate));
			for (long t = 0; t < n; t++) {
				busy(*i, t, cv);
				if (t == 0) {
					i->frame.params[F::TILT] = 0.5f;
					i->frame.params[F::PARTIALS] = 32.f;
				}
				const Clock::time_point t0 = Clock::now();
				i->engine.process(i->frame, i->result);
				const float us = std::chrono::duration<float, std::micro>(Clock::now() - t0).count();
				if (rep == 0) continue;
				best[size_t(t)] = std::min(best[size_t(t)], us);
				what[size_t(t)] = (unsigned short) pieceOf(i->engine.staged());
			}
			sink += i->probe();
		}
		double most[14] = {}, sum[14] = {};
		long count[14] = {};
		for (long t = 0; t < n; t++) {
			const int p = what[size_t(t)];
			most[p] = std::max(most[p], double(best[size_t(t)]));
			sum[p] += best[size_t(t)];
			count[p]++;
		}
		double worst = 0.0;
		for (int p = 0; p < 14; p++) worst = std::max(worst, most[p]);
		for (int p = 2; p <= 11; p++)
			if (!count[p]) {
				std::printf("doldrums   no %s piece ran: nothing was measured\n", pieceName(p));
				worst = 1e9;
			}
		if (count[13]) {
			std::printf("doldrums   %ld samples ran two pieces of the staging at once\n", count[13]);
			worst = 1e9;   // fails the bound: the staging is one piece a sample
		}
		if (print) {
			std::printf("doldrums worst single sample at %.0f kHz, Long grain, busiest tables (least of 3 runs of %.0f s):\n",
			            rate / 1000.0, seconds);
			for (int p = 0; p < 14; p++)
				if (count[p])
					std::printf("doldrums   %-11s %8ld samples  most %7.2f us  mean %7.2f us\n", pieceName(p), count[p],
					            most[p], sum[p] / double(count[p]));
		}
		if (sink != sink) std::printf("doldrums: NaN out\n");
		return worst;
	}

	// Run by the bench after the cases: the worst single sample at 96 kHz
	// (the bound's rate) and 48 kHz, Long grain, against the design's bound
	// of 100 us on the development Mac; elsewhere three times that,
	// provisional (as the budgets are). Measured 2026-11-26 on the M5 Pro,
	// three runs of `make bench`: at 96 kHz 68.7 to 70.7 us, the worst piece
	// INVERSE_L or INVERSE_R (a transform and a pass over the frames); then
	// FORWARD 59 to 63, SPECTRAL 50 to 58 (Tables::apply, every table in
	// use), PHASES 33, LOUDNESS 26, add 28, the worst Tables::work call 33,
	// the boundary (swap, pass step, a copy of 512 KB) 11. At 48 kHz 32.4 to
	// 33.3 us. TESTING.md rule 10 has the history.
	static bool extra(const Cv& cv) {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
		const double bound = 100.0;
#else
		const double bound = 300.0;
#endif
		bool ok = true;
		for (float rate : {96000.f, 48000.f}) {
			const double us = worstSample(rate, 10.0, cv, true);
			const bool over = !(us < bound);
			std::printf("doldrums   worst %.2f us at %.0f kHz (bound %.0f us)%s\n", us, rate / 1000.0, bound,
			            over ? "  OVER BOUND" : "");
			ok = ok && !over;
		}
		return ok;
	}
};

} // namespace rkabench
