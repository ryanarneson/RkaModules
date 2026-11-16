// CPU cost of Sway's and Fathom's per-sample paths (`make bench`): the
// Rack-free engines the modules run (src/sway/SwayEngine.hpp,
// src/fathom/FathomEngine.hpp), fed the frames the modules would build, in
// three cases:
//   idle     no cables, knobs at their defaults
//   outputs  every output patched, no inputs, knobs at their defaults
//   busy     Sway: 16 channels, a moving CV on every input, every output
//            patched. Fathom: full detune, drift and spread, a moving CV on
//            every input, every output patched.
// Each case runs 1, 4 and 16 instances processed in turn each sample, as
// Rack's engine does on one thread, each with its own state. Prints ns per
// sample and the share of one core at 48 kHz, total and per instance, and
// exits non-zero if an instance's share is over its module's budget (at 1
// and at 16 instances) or if 16 instances cost more each than one does.
// Built with the plugin's optimisation and architecture flags (tests.mk).
//
//   bench                          every case
//   bench <module> <case> <count> [seconds]
//                                  one case, for a profiler
#include "fathom/FathomEngine.hpp"
#include "sway/SwayEngine.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const float FS = 48000.f;
const double SAMPLE_NS = 1e9 / 48000.0;

// Per-instance budgets, % of one core at 48 kHz, per case (idle, outputs,
// busy), checked at 1 and at 16 instances. Set on an Apple M-series Mac
// (TESTING.md rule 10 has the numbers) so the pre-issue-#8 engines fail
// every case the work improved there (Sway idle and outputs, Fathom idle:
// 0.50, 0.50 and 0.58 before) while the current ones pass with about 1.5x
// room or more. Sway busy and Fathom outputs and busy measured the same
// before and after on the Mac (its libm is fast), so their budgets only
// catch a gross regression; libm_calls is what catches the libm calls
// that made Windows slow. Other machines (x86-64, Linux arm64) get three
// times the room, provisionally: the only x86 runs so far were emulated
// (Rosetta under Docker, about three times slower than the Mac), so these
// budgets still need measuring on native hardware.
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
const double ARCH_ROOM = 1.0;
#else
const double ARCH_ROOM = 3.0;
#endif
const double SWAY_BUDGET[3] = {0.3, 0.4, 3.0};
const double FATHOM_BUDGET[3] = {0.1, 1.2, 1.2};

// A slow, cheap CV in +/-5 V: a sine table read at a per-input rate, so the
// CVs move every sample without libm calls inside the timed loop.
struct CvTable {
	static const int SIZE = 4096;
	float v[SIZE];
	CvTable() {
		for (int i = 0; i < SIZE; i++)
			v[i] = 5.f * float(std::sin(2.0 * M_PI * i / SIZE));
	}
	// A read position in eighths of a table step, and its speed: input k,
	// channel c moves at its own rate from its own start.
	struct Cursor {
		uint32_t pos;
		uint32_t step;
	};
	static Cursor cursor(int k, int c) {
		Cursor r;
		r.pos = uint32_t(c * 97 + k * 389) * 8;
		r.step = uint32_t(1 + k * 3 + c);
		return r;
	}
	float next(Cursor& r) const {
		r.pos += r.step;
		return v[(r.pos >> 3) & (SIZE - 1)];
	}
};
const CvTable cvTable;

enum Case { IDLE, OUTPUTS, BUSY, CASES };
const char* caseName[CASES] = {"idle", "outputs", "busy"};

using Clock = std::chrono::steady_clock;

// Every instance's outputs end up here, so none of the work is optimised
// away.
volatile float sink = 0.f;

// --- Sway -------------------------------------------------------------

struct SwayInstance {
	rka::SwayEngine engine;
	rka::SwayFrame frame;
	rka::SwayResult result;
	float volts[rka::SwayFrame::INPUTS_LEN][16];
	Case kind = IDLE;

	CvTable::Cursor cv[rka::SwayFrame::INPUTS_LEN][16];
	int clock[16], reset[16], hold[16];

	// 10 V for the first `width` of every `period` samples.
	static float pulse(int& t, int period, int width) {
		if (++t >= period)
			t = 0;
		return t < width ? 10.f : 0.f;
	}

	void setup(Case k) {
		kind = k;
		using F = rka::SwayFrame;
		std::memset(volts, 0, sizeof(volts));
		for (int i = 0; i < F::INPUTS_LEN; i++)
			for (int c = 0; c < 16; c++)
				cv[i][c] = CvTable::cursor(i, c);
		for (int c = 0; c < 16; c++) {
			clock[c] = c;
			reset[c] = 31 * c;
			hold[c] = 1000 * c;
		}
		// The module's defaults.
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
			frame.outputConnected[o] = k != IDLE;
		if (k == BUSY) {
			p[F::CHANNELS] = 16.f;
			p[F::RATE] = 3.f;
			p[F::SPREAD] = 0.5f;
			p[F::MORPH] = 0.4f;
			for (int a = F::FM_ATT; a <= F::MORPH_ATT; a++)
				p[a] = 0.5f;
			for (int i = 0; i < F::INPUTS_LEN; i++)
				frame.inputs[i].channels = 16;
		}
	}

	void step(long n) {
		using F = rka::SwayFrame;
		if (kind == BUSY) {
			for (int k = F::FM; k <= F::MORPH_IN; k++)
				for (int c = 0; c < 16; c++)
					volts[k][c] = cvTable.next(cv[k][c]);
			// A 4 Hz clock, a reset every 1.5 s and a hold now and then, with
			// the channels a little apart.
			for (int c = 0; c < 16; c++) {
				volts[F::CLOCK][c] = pulse(clock[c], 12000, 600);
				volts[F::RESET][c] = pulse(reset[c], 72000, 48);
				volts[F::HOLD][c] = pulse(hold[c], 96000, 4800);
			}
		}
		(void) n;
		engine.process(frame, 1.f / FS, FS, result);
	}

	float probe() const {
		float s = 0.f;
		for (int o = 0; o < rka::SwayFrame::OUTPUTS_LEN; o++)
			s += result.out[o][0];
		return s;
	}
};

// --- Fathom -----------------------------------------------------------

struct FathomInstance {
	rka::FathomEngine engine;
	rka::FathomFrame frame;
	float out[rka::FathomFrame::OUTPUTS_LEN];
	Case kind = IDLE;

	CvTable::Cursor cv[rka::FathomFrame::INPUTS_LEN];

	void setup(Case k) {
		kind = k;
		using F = rka::FathomFrame;
		for (int i = 0; i < F::INPUTS_LEN; i++)
			cv[i] = CvTable::cursor(i, 0);
		float* p = frame.params;
		p[F::PITCH] = 0.f;
		p[F::SUB] = 0.5f;
		p[F::DETUNE] = 0.25f;
		p[F::SPREAD] = 0.6f;
		p[F::DRIFT] = 0.25f;
		p[F::CUTOFF] = std::log2(1200.f / 20.f);
		p[F::DETUNE_ATT] = p[F::DRIFT_ATT] = p[F::CUTOFF_ATT] = 0.f;
		p[F::RES] = 0.2f;
		p[F::DRIVE] = 0.25f;
		p[F::RES_ATT] = p[F::DRIVE_ATT] = 0.f;
		for (int i = 0; i < F::INPUTS_LEN; i++)
			frame.inputs[i] = 0.f;
		for (int o = 0; o < F::OUTPUTS_LEN; o++)
			frame.outputConnected[o] = k != IDLE;
		if (k == BUSY) {
			p[F::DETUNE] = 1.f;
			p[F::DRIFT] = 1.f;
			p[F::SPREAD] = 1.f;
			p[F::DETUNE_ATT] = p[F::DRIFT_ATT] = p[F::CUTOFF_ATT] = 0.5f;
			p[F::RES_ATT] = p[F::DRIVE_ATT] = 0.5f;
		}
	}

	void step(long n) {
		using F = rka::FathomFrame;
		if (kind == BUSY) {
			// A slow vibrato of a semitone on V/OCT, the rest +/-5 V.
			frame.inputs[F::VOCT] = cvTable.next(cv[0]) / 60.f;
			for (int k = F::DETUNE_IN; k <= F::DRIVE_IN; k++)
				frame.inputs[k] = cvTable.next(cv[k]);
		}
		(void) n;
		engine.process(frame, FS, out);
	}

	float probe() const {
		return out[0] + out[1] + out[2];
	}
};

// --- Timing -----------------------------------------------------------

struct Result {
	double nsPerSample;  // all instances
	double perInstancePct;
};

// Best of three runs of `seconds` of audio, after a 0.25 s warm-up.
template <typename Instance>
Result run(Case k, int count, double seconds) {
	std::vector<Instance> inst(count);
	for (Instance& i : inst)
		i.setup(k);
	const long warm = long(0.25 * FS), n = long(seconds * FS);
	long t = 0;
	for (; t < warm; t++)
		for (Instance& i : inst)
			i.step(t);
	double best = 1e300;
	for (int rep = 0; rep < 3; rep++) {
		const Clock::time_point t0 = Clock::now();
		for (long e = t + n; t < e; t++)
			for (Instance& i : inst)
				i.step(t);
		const double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n;
		if (ns < best)
			best = ns;
	}
	for (Instance& i : inst)
		sink += i.probe();
	Result r;
	r.nsPerSample = best;
	r.perInstancePct = 100.0 * best / count / SAMPLE_NS;
	return r;
}

template <typename Instance>
bool bench(const char* name, const double* budgets, double seconds) {
	bool ok = true;
	std::printf("%-7s %-8s %5s %12s %12s %10s\n", name, "case", "count", "total ns", "ns/inst", "% core/inst");
	for (int k = 0; k < CASES; k++) {
		const double budget = budgets[k] * ARCH_ROOM;
		double pct1 = 0.0;
		for (int count : {1, 4, 16}) {
			const Result r = run<Instance>(Case(k), count, seconds / count < 2.0 ? 2.0 : seconds / count);
			if (count == 1)
				pct1 = r.perInstancePct;
			const bool over = (count == 1 || count == 16) && r.perInstancePct > budget;
			std::printf("%-7s %-8s %5d %12.1f %12.1f %10.3f%s\n", name, caseName[k], count, r.nsPerSample,
			            r.nsPerSample / count, r.perInstancePct, over ? "  OVER BUDGET" : "");
			ok = ok && !over;
			// Instances share nothing, so 16 must cost no more each than one
			// (with 0.01 % of slack for the near-free cases' timer noise).
			if (count == 16) {
				const bool grows = r.perInstancePct > 1.5 * pct1 + 0.01;
				std::printf("%-7s %-8s 16 vs 1: per-instance cost x%.2f%s\n", name, caseName[k], r.perInstancePct / pct1,
				            grows ? "  GROWS WITH COUNT" : "");
				ok = ok && !grows;
			}
		}
		std::printf("%-7s %-8s budget %.2f %% of a core per instance\n", name, caseName[k], budget);
	}
	return ok;
}

} // namespace

int main(int argc, char** argv) {
	if (argc >= 4) {
		const std::string module = argv[1], kase = argv[2];
		const int count = std::atoi(argv[3]);
		const double seconds = argc >= 5 ? std::atof(argv[4]) : 10.0;
		int k = 0;
		while (k < CASES && kase != caseName[k])
			k++;
		if (k == CASES || count < 1 || (module != "sway" && module != "fathom")) {
			std::fprintf(stderr, "usage: bench [sway|fathom idle|outputs|busy count [seconds]]\n");
			return 2;
		}
		const Result r = module == "sway" ? run<SwayInstance>(Case(k), count, seconds) : run<FathomInstance>(Case(k), count, seconds);
		std::printf("%s %s x%d: %.1f ns per sample, %.3f %% of a core per instance\n", module.c_str(), caseName[k], count,
		            r.nsPerSample, r.perInstancePct);
		return 0;
	}
	bool ok = bench<SwayInstance>("sway", SWAY_BUDGET, 20.0);
	ok = bench<FathomInstance>("fathom", FATHOM_BUDGET, 20.0) && ok;
	std::printf(ok ? "bench: within budget\n" : "bench: FAILED (over budget, or cost grows with the count)\n");
	return ok ? 0 : 1;
}
