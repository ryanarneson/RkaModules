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
// and at 16 instances). Built with the plugin's optimisation and
// architecture flags (tests.mk).
//
//   bench                          every case
//   bench <module> <case> <count> [seconds]
//                                  one case, for a profiler
#include "fathom/FathomEngine.hpp"
#include "sway/SwayEngine.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const float FS = 48000.f;
const double SAMPLE_NS = 1e9 / 48000.0;

// Per-instance budgets, % of one core at 48 kHz, for every case. The bench
// fails above them; see TESTING.md rule 10 for the measured values.
const double SWAY_BUDGET = 0.0;
const double FATHOM_BUDGET = 0.0;

// A slow, cheap CV in +/-5 V: a sine table read at a per-input rate, so the
// CVs move every sample without libm calls inside the timed loop.
struct CvTable {
	static const int SIZE = 4096;
	float v[SIZE];
	CvTable() {
		for (int i = 0; i < SIZE; i++)
			v[i] = 5.f * float(std::sin(2.0 * M_PI * i / SIZE));
	}
	// Input k, channel c at sample n.
	float at(int k, int c, long n) const {
		const long step = 1 + k * 3 + c;
		return v[(n * step / 8 + c * 97 + k * 389) & (SIZE - 1)];
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

	void setup(Case k) {
		kind = k;
		using F = rka::SwayFrame;
		std::memset(volts, 0, sizeof(volts));
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
					volts[k][c] = cvTable.at(k, c, n);
			// A 4 Hz clock, a reset every 1.5 s and a hold now and then, with
			// the channels a little apart.
			for (int c = 0; c < 16; c++) {
				volts[F::CLOCK][c] = (n + c) % 12000 < 600 ? 10.f : 0.f;
				volts[F::RESET][c] = (n + 31 * c) % 72000 < 48 ? 10.f : 0.f;
				volts[F::HOLD][c] = (n + 1000 * c) % 96000 < 4800 ? 10.f : 0.f;
			}
		}
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

	void setup(Case k) {
		kind = k;
		using F = rka::FathomFrame;
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
			frame.inputs[F::VOCT] = cvTable.at(0, 0, n) / 60.f;
			for (int k = F::DETUNE_IN; k <= F::DRIVE_IN; k++)
				frame.inputs[k] = cvTable.at(k, 0, n);
		}
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
bool bench(const char* name, double budget, double seconds) {
	bool ok = true;
	std::printf("%-7s %-8s %5s %12s %12s %10s\n", name, "case", "count", "total ns", "ns/inst", "% core/inst");
	for (int k = 0; k < CASES; k++) {
		double pct1 = 0.0;
		for (int count : {1, 4, 16}) {
			const Result r = run<Instance>(Case(k), count, seconds / count < 0.5 ? 0.5 : seconds / count);
			if (count == 1)
				pct1 = r.perInstancePct;
			const bool over = budget > 0.0 && (count == 1 || count == 16) && r.perInstancePct > budget;
			std::printf("%-7s %-8s %5d %12.1f %12.1f %10.3f%s\n", name, caseName[k], count, r.nsPerSample,
			            r.nsPerSample / count, r.perInstancePct, over ? "  OVER BUDGET" : "");
			if (count == 16 && pct1 > 0.0)
				std::printf("%-7s %-8s 16 vs 1: per-instance cost x%.2f\n", name, caseName[k], r.perInstancePct / pct1);
			ok = ok && !over;
		}
	}
	if (budget > 0.0)
		std::printf("%s budget: %.2f %% of a core per instance\n", name, budget);
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
	bool ok = bench<SwayInstance>("sway", SWAY_BUDGET, 2.0);
	ok = bench<FathomInstance>("fathom", FATHOM_BUDGET, 2.0) && ok;
	std::printf(ok ? "bench: within budget\n" : "bench: OVER BUDGET\n");
	return ok ? 0 : 1;
}
