// Counts the C maths library calls every module's engine makes per sample,
// with every libm entry point it could reach wrapped by the linker
// (-Wl,--wrap=<name>, GNU ld, so Linux only: `make libm-calls`, run by
// `make test`, skips it elsewhere with a message). It renders the cases
// each module's declaration file (src/<module>/bench.inc, found by
// tests.mk) lists for it: with the knobs still, after a warm-up, the count
// must be zero; with a knob moving, calls are allowed (some coefficients
// are worked out again on a change) and only reported. MinGW's libm is
// slow, so a libm call creeping back into a per-sample path is the
// regression issue #8 was about, and timing on macOS or Linux cannot see
// it. (Not named test_*.cpp: tests.mk's generic test rule would build it
// without the wrap flags.)
#include "modules.inc"
#include "testing.hpp"

#include <cstring>
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
#define X(ret, name, params, args) \
	ret __real_##name params; \
	ret __wrap_##name params { \
		calls[CALL_##name]++; \
		return __real_##name args; \
	}
RKA_LIBM_FUNCS(X)
#undef X
// sincos/sincosf: GCC may fuse a sin and a cos of one argument.
void __real_sincosf(float, float*, float*);
void __wrap_sincosf(float a, float* s, float* c) {
	calls[CALL_sinf]++;
	__real_sincosf(a, s, c);
}
void __real_sincos(double, double*, double*);
void __wrap_sincos(double a, double* s, double* c) {
	calls[CALL_sin]++;
	__real_sincos(a, s, c);
}
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

void report(const std::string& name, bool mustBeZero) {
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
