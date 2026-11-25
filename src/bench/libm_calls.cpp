// Counts the C maths library calls every module's engine makes per sample,
// with every libm entry point it could reach intercepted at link time
// (`make libm-calls`, run by `make test`, on Linux and on macOS). On Linux,
// GNU ld's -Wl,--wrap=<name> sends every call from the executable's own
// objects to __wrap_<name>, which counts it and calls __real_<name>.
// macOS's ld has no --wrap, so there this file defines <name> itself: ld
// binds the objects' references to a definition in an object before it
// looks in libSystem, and the definition counts the call and forwards to
// libSystem's through dlsym(RTLD_NEXT). On both, that is every call from
// the engine code and BeadsFirmware.o, and none made inside the C or C++
// library. It renders the cases each module's declaration file
// (src/<module>/bench.inc, found by tests.mk) lists for it: with the knobs
// still, after a warm-up, the count must be zero; with a knob moving, calls
// are allowed (some coefficients are worked out again on a change) and only
// reported. MinGW's libm is slow, so a libm call creeping back into a
// per-sample path is the regression issue #8 was about, and timing on
// macOS or Linux cannot see it. (Not named test_*.cpp: tests.mk's generic
// test rule would build it without the wrap flags.)
#include "bench/BenchModule.hpp"
#include "modules.inc"
#include "testing.hpp"

#include <cmath>
#include <cstring>
#if defined(__APPLE__)
#include <dlfcn.h>
#endif
#include <memory>
#include <string>

#define RKA_LIBM_FUNCS(X) \
	X(float, exp2f, (float a), (a)) X(double, exp2, (double a), (a)) \
	X(float, expf, (float a), (a)) X(double, exp, (double a), (a)) \
	X(float, log2f, (float a), (a)) X(double, log2, (double a), (a)) \
	X(float, logf, (float a), (a)) X(double, log, (double a), (a)) \
	X(float, powf, (float a, float b), (a, b)) X(double, pow, (double a, double b), (a, b)) \
	X(float, sinf, (float a), (a)) X(double, sin, (double a), (a)) \
	X(float, cosf, (float a), (a)) X(double, cos, (double a), (a)) \
	X(float, tanf, (float a), (a)) X(double, tan, (double a), (a)) \
	X(float, tanhf, (float a), (a)) X(double, tanh, (double a), (a)) \
	X(float, fmodf, (float a, float b), (a, b)) X(double, fmod, (double a, double b), (a, b)) \
	X(long, lroundf, (float a), (a)) X(long, lround, (double a), (a)) \
	X(float, floorf, (float a), (a)) X(double, floor, (double a), (a)) \
	X(float, fminf, (float a, float b), (a, b)) X(double, fmin, (double a, double b), (a, b)) \
	X(float, fmaxf, (float a, float b), (a, b)) X(double, fmax, (double a, double b), (a, b))

enum {
#define X(ret, name, params, args) CALL_##name,
	RKA_LIBM_FUNCS(X)
#undef X
	CALLS_LEN
};
static const char* callName[CALLS_LEN] = {
#define X(ret, name, params, args) #name,
	RKA_LIBM_FUNCS(X)
#undef X
};
static long calls[CALLS_LEN];

extern "C" {
#if defined(__APPLE__)
// <name> itself, forwarding to the next image's (libSystem's) <name>.
#define RKA_LIBM_WRAP(ret, name, params, args, counter) \
	ret name params { \
		static ret(*real) params = reinterpret_cast<ret(*) params>(dlsym(RTLD_NEXT, #name)); \
		calls[counter]++; \
		return real args; \
	}
#else
#define RKA_LIBM_WRAP(ret, name, params, args, counter) \
	ret __real_##name params; \
	ret __wrap_##name params { \
		calls[counter]++; \
		return __real_##name args; \
	}
#endif
#define X(ret, name, params, args) RKA_LIBM_WRAP(ret, name, params, args, CALL_##name)
RKA_LIBM_FUNCS(X)
#undef X
#if defined(__APPLE__)
// Clang fuses a sin and a cos of one argument into __sincos(f)_stret.
RKA_LIBM_WRAP(__float2, __sincosf_stret, (float a), (a), CALL_sinf)
RKA_LIBM_WRAP(__double2, __sincos_stret, (double a), (a), CALL_sin)
#else
// sincos/sincosf: GCC may fuse a sin and a cos of one argument.
RKA_LIBM_WRAP(void, sincosf, (float a, float* s, float* c), (a, s, c), CALL_sinf)
RKA_LIBM_WRAP(void, sincos, (double a, double* s, double* c), (a, s, c), CALL_sin)
#endif
#undef RKA_LIBM_WRAP
}

namespace {

using rkabench::FS;
const long WARM = 48000, RUN = 48000;

long total() {
	long n = 0;
	for (long c : calls)
		n += c;
	return n;
}

[[maybe_unused]] void report(const std::string& name, bool mustBeZero) {
	const long n = total();
	char list[512] = "";
	for (int i = 0; i < CALLS_LEN; i++)
		if (calls[i]) {
			char one[48];
			std::snprintf(one, sizeof(one), " %s x%ld", callName[i], calls[i]);
			std::strncat(list, one, sizeof(list) - std::strlen(list) - 1);
		}
	if (mustBeZero)
		CHECK(n == 0, "%s: %ld libm calls in %ld samples after warm-up:%s", name.c_str(), n, RUN, list);
	std::printf("%-32s %ld libm calls in %ld samples%s%s\n", name.c_str(), n, RUN, n ? ":" : "", list);
}

template <typename M>
int module(const char* name, const rkabench::Cv& cv) {
	int still = 0;
	for (int k = 0; k < M::LIBM_CASES; k++) {
		const rkabench::LibmCase<typename M::Instance> c = M::libmCase(k);
		std::unique_ptr<typename M::Instance> i(new typename M::Instance);
		for (long t = 0; t < WARM + RUN; t++) {
			if (t == WARM)
				std::memset(calls, 0, sizeof(calls));
			c.sample(*i, t, cv);
			i->process(FS);
		}
		report(std::string(name) + " " + c.name, c.knobsStill);
		still += c.knobsStill;
	}
	CHECK(still > 0, "%s: no libm case with the knobs still (src/%s/bench.inc)", name, name);
	return 1;
}

} // namespace

int main() {
	const rkabench::Cv cv;
	int modules = 0;
#define X(m) modules += module<rkabench::m>(#m, cv);
	RKA_BENCH_MODULES(X)
#undef X
	CHECK(modules > 0, "no modules (tests.mk found no src/*/bench.inc)");
	return rkatest::finish("libm_calls");
}
