// Counts the C maths library calls Sway's and Fathom's engines make per
// sample, with every libm entry point they could reach wrapped by the
// linker (-Wl,--wrap=<name>, GNU ld, so Linux only: `make libm-calls`, run
// by `make test`, skips it elsewhere with a message). With the knobs still,
// after a warm-up, the count must be zero in every case: idle, outputs
// patched, and Sway with a still mono FM cable; with a knob moving, calls
// are allowed (some coefficients are worked out again on a change) and only
// reported. MinGW's libm is slow, so a libm call creeping back into a
// per-sample path is the regression issue #8 was about, and timing on macOS
// or Linux cannot see it. (Not named test_*.cpp: tests.mk's generic test
// rule would build it without the wrap flags.)
#include "fathom/FathomEngine.hpp"
#include "sway/SwayEngine.hpp"
#include "testing.hpp"

#include <cstring>

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

const float FS = 48000.f;
const long WARM = 48000, RUN = 48000;

long total() {
	long n = 0;
	for (long c : calls)
		n += c;
	return n;
}

void report(const char* name, bool mustBeZero) {
	const long n = total();
	char list[512] = "";
	for (int i = 0; i < CALLS_LEN; i++)
		if (calls[i]) {
			char one[48];
			std::snprintf(one, sizeof(one), " %s x%ld", callName[i], calls[i]);
			std::strncat(list, one, sizeof(list) - std::strlen(list) - 1);
		}
	if (mustBeZero)
		CHECK(n == 0, "%s: %ld libm calls in %ld samples after warm-up:%s", name, n, RUN, list);
	std::printf("%-32s %ld libm calls in %ld samples%s%s\n", name, n, RUN, n ? ":" : "", list);
}

void fathomDefaults(rka::FathomFrame& f, bool patched) {
	using F = rka::FathomFrame;
	float* p = f.params;
	p[F::PITCH] = 0.f;
	p[F::SUB] = 0.5f;
	p[F::DETUNE] = 0.25f;
	p[F::SPREAD] = 0.6f;
	p[F::DRIFT] = 0.25f;
	p[F::CUTOFF] = 5.9068906f;
	p[F::RES] = 0.2f;
	p[F::DRIVE] = 0.25f;
	for (bool& c : f.outputConnected)
		c = patched;
}

// moving: CUTOFF and DETUNE sweep (calls allowed, reported).
void fathom(const char* name, bool patched, bool moving) {
	rka::FathomEngine e;
	rka::FathomFrame f;
	fathomDefaults(f, patched);
	float out[rka::FathomFrame::OUTPUTS_LEN];
	for (long i = 0; i < WARM + RUN; i++) {
		if (i == WARM)
			std::memset(calls, 0, sizeof(calls));
		if (moving) {
			f.params[rka::FathomFrame::CUTOFF] = 2.f + 6.f * float(i % 48000) / 48000.f;
			f.params[rka::FathomFrame::DETUNE] = float(i % 24000) / 24000.f;
		}
		e.process(f, FS, out);
	}
	report(name, !moving);
}

// fm: a mono FM cable at a still 1.3 V; moving: RATE sweeps.
void sway(const char* name, bool patched, bool fm, bool moving) {
	using F = rka::SwayFrame;
	static float volts[F::INPUTS_LEN][16];
	std::memset(volts, 0, sizeof(volts));
	rka::SwayEngine e;
	rka::SwayFrame f;
	rka::SwayResult r;
	float* p = f.params;
	p[F::RATE] = 1.f;
	p[F::PW] = 0.5f;
	p[F::SKEW] = 0.5f;
	p[F::CHANNELS] = 1.f;
	for (int i = 0; i < F::INPUTS_LEN; i++)
		f.inputs[i].v = volts[i];
	for (bool& c : f.outputConnected)
		c = patched;
	if (fm) {
		f.inputs[F::FM].channels = 1;
		volts[F::FM][0] = 1.3f;
		p[F::FM_ATT] = 0.5f;
	}
	for (long i = 0; i < WARM + RUN; i++) {
		if (i == WARM)
			std::memset(calls, 0, sizeof(calls));
		if (moving)
			p[F::RATE] = -2.f + 4.f * float(i % 48000) / 48000.f;
		e.process(f, 1.f / FS, FS, r);
	}
	report(name, !moving);
}

} // namespace

int main() {
	fathom("fathom idle", false, false);
	fathom("fathom outputs", true, false);
	fathom("fathom outputs, knobs moving", true, true);
	sway("sway idle", false, false, false);
	sway("sway outputs", true, false, false);
	sway("sway outputs, mono FM", true, true, false);
	sway("sway outputs, RATE moving", true, false, true);
	return rkatest::finish("libm_calls");
}
