// Unit tests for src/fathom/SuperSaw.hpp. Run with `make test`.
#include "SuperSaw.hpp"
#include "sway/Waves.hpp"
#include "testing.hpp"
#include <cmath>

using namespace rka;

static const float FS = 48000.f;
static const float DT = 1.f / FS;

static void testDetuneTable() {
	SuperSaw s;
	s.setDetune(0.f);
	for (int i = 0; i < SuperSaw::N; i++)
		CHECK(s.ratio[i] == 1.f, "ratio[%d] at detune 0 is %f", i, s.ratio[i]);
	s.setDetune(1.f);
	CHECK(std::fabs(s.ratio[0] - 0.955990748f) < 1e-6f, "ratio[0] %f", s.ratio[0]);
	CHECK(std::fabs(s.ratio[6] - 1.042980968f) < 1e-6f, "ratio[6] %f", s.ratio[6]);
	CHECK(s.ratio[3] == 1.f, "centre ratio %f", s.ratio[3]);
	CHECK(std::fabs(s.volume[3] - 0.72102f) < 1e-5f, "centre volume %f", s.volume[3]);
	CHECK(std::fabs(s.volume[0] - 0.502012f) < 1e-5f, "side volume %f", s.volume[0]);
	CHECK(std::fabs(s.gain - 0.3f) < 1e-6f, "gain %f", s.gain);
}

static bool sameTables(const SuperSaw& a, const SuperSaw& b) {
	for (int i = 0; i < SuperSaw::N; i++)
		if (a.ratio[i] != b.ratio[i] || a.volume[i] != b.volume[i])
			return false;
	return a.gain == b.gain;
}

static void testDetuneClamps() {
	SuperSaw lo, zero, hi, one;
	lo.setDetune(-1.f);
	zero.setDetune(0.f);
	hi.setDetune(2.f);
	one.setDetune(1.f);
	CHECK(sameTables(lo, zero), "detune -1 differs from 0");
	CHECK(sameTables(hi, one), "detune 2 differs from 1");
}

static void testZeroDetuneIsOneSaw() {
	SuperSaw s;
	s.reset();
	s.setDetune(0.f);
	const float f = 110.f, dt = f * DT;
	const float mix = (s.volume[3] + 6.f * s.volume[0]) * s.gain * float(M_SQRT1_2);
	double phase = 0.0;
	float maxErr = 0.f, maxDiff = 0.f;
	for (int i = 0; i < 4800; i++) {
		float l, r;
		s.process(f, nullptr, nullptr, 0.f, DT, l, r);
		phase += dt;
		phase -= std::floor(phase);
		const float expected = sawBlep(float(phase), dt) * mix;
		maxErr = std::fmax(maxErr, std::fabs(l - expected));
		maxDiff = std::fmax(maxDiff, std::fabs(l - r));
	}
	CHECK(maxErr < 1e-5f, "seven coherent saws differ from one saw by %f", maxErr);
	CHECK(maxDiff < 1e-6f, "spread 0 is not mono: %f", maxDiff);
}

static double rms(const float* v, int n) {
	double sum = 0.0;
	for (int i = 0; i < n; i++)
		sum += double(v[i]) * v[i];
	return std::sqrt(sum / n);
}

static void testSpreadMonoSum() {
	const int n = 48000;
	static float sum0[n], sum1[n], diff1[n];
	SuperSaw a, b;
	a.setDetune(1.f);
	b.setDetune(1.f);
	for (int i = 0; i < n; i++) {
		float l, r;
		a.process(110.f, nullptr, nullptr, 0.f, DT, l, r);
		sum0[i] = l + r;
		b.process(110.f, nullptr, nullptr, 1.f, DT, l, r);
		sum1[i] = l + r;
		diff1[i] = l - r;
	}
	// Equal-power panning lowers L + R by up to 3 dB for a hard-panned
	// source; seven saws spread evenly lose about 1.1 dB. Anything past
	// 1.5 dB would be phase cancellation, which is what this guards.
	const double dB = 20.0 * std::log10(rms(sum1, n) / rms(sum0, n));
	CHECK(std::fabs(dB) < 1.5, "mono sum changes by %f dB with spread", dB);
	const double side = 20.0 * std::log10(rms(diff1, n) / rms(sum1, n));
	CHECK(side > -12.0, "spread 1 is barely stereo: side %f dB", side);
}

// Sum of squared first differences: a cheap proxy for energy near Nyquist.
static double roughness(const float* v, int n) {
	double sum = 0.0;
	for (int i = 1; i < n; i++)
		sum += double(v[i] - v[i - 1]) * (v[i] - v[i - 1]);
	return sum;
}

static void testAliasingBelowNaive() {
	const int n = 4096;
	static float blep[n], naive[n];
	SuperSaw s;
	s.setDetune(0.f);
	const float f = 1000.f;
	const float mix = (s.volume[3] + 6.f * s.volume[0]) * s.gain * float(M_SQRT1_2);
	double phase = 0.0;
	for (int i = 0; i < n; i++) {
		float l, r;
		s.process(f, nullptr, nullptr, 0.f, DT, l, r);
		blep[i] = l;
		phase += f * DT;
		phase -= std::floor(phase);
		naive[i] = waveSaw(float(phase)) * mix;
	}
	const double rb = roughness(blep, n), rn = roughness(naive, n);
	CHECK(rb < rn, "supersaw %f not smoother than naive saw %f", rb, rn);
}

int main() {
	testDetuneTable();
	testDetuneClamps();
	testZeroDetuneIsOneSaw();
	testSpreadMonoSum();
	testAliasingBelowNaive();
	return rkatest::finish("test_supersaw");
}
