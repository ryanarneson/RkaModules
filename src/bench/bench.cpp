// CPU cost of every module's per-sample path (`make bench`): the Rack-free
// engine each module runs, fed the frames the module would build, in the
// cases its declaration file (src/<module>/bench.inc; BenchModule.hpp)
// names: idle, outputs and busy at least. tests.mk finds the declaration
// files; nothing here names a module.
//
// Each case runs 1, 4 and 16 instances processed in turn each sample, as
// Rack's engine does on one thread, each with its own state. Prints ns per
// sample and the share of one core at 48 kHz, total and per instance, and
// exits non-zero if an instance's share is over the case's budget (at 1
// and at 16 instances), if 16 instances cost more each than one does, or
// if there are no modules. Built with the plugin's optimisation and
// architecture flags (tests.mk).
//
//   bench                          every module, every case
//   bench --check                  only check the declarations (make test):
//                                  idle, outputs and busy present, every
//                                  budget set, a libm case with the knobs
//                                  still
//   bench <module> <case> <count> [seconds]
//                                  one case, for a profiler
#include "bench/BenchModule.hpp"
#include "modules.inc"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using rkabench::Cv;
using rkabench::FS;

const double SAMPLE_NS = 1e9 / 48000.0;

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
const bool APPLE_ARM64 = true;
#else
const bool APPLE_ARM64 = false;
#endif

using Clock = std::chrono::steady_clock;

// Every instance's outputs end up here, so none of the work is optimised
// away.
volatile float sink = 0.f;

struct Result {
	double nsPerSample;  // all instances
	double perInstancePct;
};

// Best of three runs of `seconds` of audio, after a 0.25 s warm-up.
template <typename M>
Result run(int k, int count, double seconds, const Cv& cv) {
	typedef typename M::Instance Instance;
	const rkabench::BenchCase<Instance> c = M::benchCase(k);
	std::vector<Instance> inst(count);
	const long warm = long(0.25 * FS), n = long(seconds * FS);
	long t = 0;
	for (; t < warm; t++)
		for (Instance& i : inst) {
			c.sample(i, t, cv);
			i.process(FS);
		}
	double best = 1e300;
	for (int rep = 0; rep < 3; rep++) {
		const Clock::time_point t0 = Clock::now();
		for (long e = t + n; t < e; t++)
			for (Instance& i : inst) {
				c.sample(i, t, cv);
				i.process(FS);
			}
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

template <typename M>
bool bench(const char* name, double seconds, const Cv& cv) {
	bool ok = true;
	std::printf("%-7s %-8s %5s %12s %12s %10s\n", name, "case", "count", "total ns", "ns/inst", "% core/inst");
	for (int k = 0; k < M::BENCH_CASES; k++) {
		const rkabench::BenchCase<typename M::Instance> c = M::benchCase(k);
		const double budget = APPLE_ARM64 ? c.budgetAppleArm64 : c.budgetElsewhere;
		double pct1 = 0.0;
		for (int count : {1, 4, 16}) {
			const Result r = run<M>(k, count, seconds / count < 2.0 ? 2.0 : seconds / count, cv);
			if (count == 1)
				pct1 = r.perInstancePct;
			const bool over = (count == 1 || count == 16) && r.perInstancePct > budget;
			std::printf("%-7s %-8s %5d %12.1f %12.1f %10.3f%s\n", name, c.name, count, r.nsPerSample,
			            r.nsPerSample / count, r.perInstancePct, over ? "  OVER BUDGET" : "");
			ok = ok && !over;
			// Instances share nothing, so 16 must cost no more each than one
			// (with 0.01 % of slack for the near-free cases' timer noise).
			if (count == 16) {
				const bool grows = r.perInstancePct > 1.5 * pct1 + 0.01;
				std::printf("%-7s %-8s 16 vs 1: per-instance cost x%.2f%s\n", name, c.name, r.perInstancePct / pct1,
				            grows ? "  GROWS WITH COUNT" : "");
				ok = ok && !grows;
			}
		}
		std::printf("%-7s %-8s budget %.2f %% of a core per instance\n", name, c.name, budget);
	}
	return ok;
}

// The declaration's own checks: the three cases every module has, a budget
// for each case on both kinds of machine, a libm case with the knobs still.
template <typename M>
bool check(const char* name) {
	bool ok = true;
	const char* required[] = {"idle", "outputs", "busy"};
	for (const char* want : required) {
		bool found = false;
		for (int k = 0; k < M::BENCH_CASES; k++)
			found = found || std::strcmp(M::benchCase(k).name, want) == 0;
		if (!found) {
			std::fprintf(stderr, "bench: module %s (src/%s/bench.inc) has no '%s' case\n", name, name, want);
			ok = false;
		}
	}
	for (int k = 0; k < M::BENCH_CASES; k++) {
		const rkabench::BenchCase<typename M::Instance> c = M::benchCase(k);
		if (!c.sample || !(c.budgetAppleArm64 > 0.0) || !(c.budgetElsewhere > 0.0)) {
			std::fprintf(stderr,
			             "bench: module %s (src/%s/bench.inc) case '%s' is missing its inputs or a budget "
			             "(Apple arm64 %g, elsewhere %g; both must be > 0)\n",
			             name, name, c.name, c.budgetAppleArm64, c.budgetElsewhere);
			ok = false;
		}
	}
	bool still = false;
	for (int k = 0; k < M::LIBM_CASES; k++)
		still = still || (M::libmCase(k).knobsStill && M::libmCase(k).sample);
	if (!still) {
		std::fprintf(stderr, "bench: module %s (src/%s/bench.inc) has no libm case with the knobs still\n", name, name);
		ok = false;
	}
	return ok;
}

} // namespace

int main(int argc, char** argv) {
	static const Cv cv;
	int modules = 0;
#define X(m) modules++;
	RKA_BENCH_MODULES(X)
#undef X
	if (modules == 0) {
		std::fprintf(stderr, "bench: FAILED: no modules (tests.mk found no src/*/bench.inc)\n");
		return 1;
	}

	if (argc == 2 && std::string(argv[1]) == "--check") {
		bool ok = true;
#define X(m) ok = check<rkabench::m>(#m) && ok;
		RKA_BENCH_MODULES(X)
#undef X
		std::printf(ok ? "bench --check: %d module declaration(s) complete\n" : "bench --check: FAILED (%d modules)\n",
		            modules);
		return ok ? 0 : 1;
	}

	if (argc >= 4) {
		const std::string module = argv[1], kase = argv[2];
		const int count = std::atoi(argv[3]);
		const double seconds = argc >= 5 ? std::atof(argv[4]) : 10.0;
		bool found = false;
#define X(m)                                                                                                   \
	if (module == #m && count >= 1)                                                                           \
		for (int k = 0; k < rkabench::m::BENCH_CASES; k++)                                                    \
			if (kase == rkabench::m::benchCase(k).name) {                                                     \
				const Result r = run<rkabench::m>(k, count, seconds, cv);                                     \
				std::printf("%s %s x%d: %.1f ns per sample, %.3f %% of a core per instance\n", #m, kase.c_str(), \
				            count, r.nsPerSample, r.perInstancePct);                                          \
				found = true;                                                                                 \
			}
		RKA_BENCH_MODULES(X)
#undef X
		if (!found) {
			std::fprintf(stderr, "usage: bench [--check | <module> <case> <count> [seconds]]; modules:");
#define X(m) std::fprintf(stderr, " %s", #m);
			RKA_BENCH_MODULES(X)
#undef X
			std::fprintf(stderr, "\n");
			return 2;
		}
		return 0;
	}

	bool ok = true;
#define X(m) ok = bench<rkabench::m>(#m, 20.0, cv) && ok;
	RKA_BENCH_MODULES(X)
#undef X
	std::printf(ok ? "bench: within budget\n" : "bench: FAILED (over budget, or cost grows with the count)\n");
	return ok ? 0 : 1;
}
