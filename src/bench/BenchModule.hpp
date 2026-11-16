#pragma once
// What a module's declaration file (src/<module>/bench.inc) gives the CPU
// bench (bench.cpp), the libm check (libm_calls.cpp) and the globals check
// (engine_globals.cpp). tests.mk finds every src/*/bench.inc and writes
// build/bench/modules.inc, which includes each one and defines
// RKA_BENCH_MODULES(X) as X(<folder>) for each folder; the tools include
// that and run the same measurements and checks for every module. Adding a
// module: TESTING.md, "Adding a module".
//
// src/<folder>/bench.inc defines `struct rkabench::<folder>` with:
//
//   struct Instance                one engine and the inputs it is fed, with
//                                  the module's defaults (knobs at their
//                                  defaults, nothing patched) on
//                                  construction, and
//     void process(float sampleRate)   one sample through the engine
//     void reset()                     the module's Initialize
//     float probe() const              something of the last outputs
//   static const int BENCH_CASES;  how many bench cases (3 or more)
//   static BenchCase<Instance> benchCase(int k)
//                                  case k: idle, outputs and busy at least
//   static const int LIBM_CASES;
//   static LibmCase<Instance> libmCase(int k)
//                                  what libm_calls renders; at least one
//                                  with the knobs still
//
// A case's `sample` sets the instance's inputs for sample t (t counts from
// 0, before the first process()); one-time set-up goes under `t == 0`.
// Everything here is types and inline functions, so engine_globals.o holds
// nothing of it that is not used.
#include <cmath>
#include <cstdint>

namespace rkabench {

const float FS = 48000.f;

// A slow, cheap CV in +/-5 V: a sine table read at a per-input rate, so the
// CVs move every sample without libm calls inside the timed loop. The tools
// own the one table and pass it to every case.
struct Cv {
	static const int SIZE = 4096;
	float v[SIZE];
	Cv() {
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

// 10 V for the first `width` of every `period` samples.
inline float pulse(int& t, int period, int width) {
	if (++t >= period)
		t = 0;
	return t < width ? 10.f : 0.f;
}

// One bench case. Budgets: % of one core at 48 kHz per instance, checked at
// 1 and at 16 instances; one for Apple arm64 Macs (where they were
// measured) and one for every other machine (x86-64, Linux arm64).
template <typename Instance>
struct BenchCase {
	const char* name;
	void (*sample)(Instance& i, long t, const Cv& cv);
	double budgetAppleArm64;
	double budgetElsewhere;
};

// One libm_calls case. knobsStill: after a warm-up the engine must make no
// libm call at all; otherwise the calls are only reported.
template <typename Instance>
struct LibmCase {
	const char* name;
	void (*sample)(Instance& i, long t, const Cv& cv);
	bool knobsStill;
};

} // namespace rkabench
