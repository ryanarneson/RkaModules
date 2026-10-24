// Unit tests for src/fathom/Svf.hpp. Run with `make test`.
#include "Svf.hpp"
#include "sway/Waves.hpp"
#include "testing.hpp"
#include <cmath>

using namespace rka;

enum Mode { LP, BP, HP };

static float pick(const Svf::Out& o, Mode m) {
	return m == LP ? o.lp : m == BP ? o.bp : o.hp;
}

// Gain of one output for a unit sine, in dB: 4096 samples to settle, 4096
// measured, RMS out over RMS in.
static double gainDb(Mode mode, float sineHz, float fc, float fs) {
	Svf f;
	f.setCoefficients(fc, BUTTERWORTH_K, fs);
	double sumIn = 0.0, sumOut = 0.0;
	for (int i = 0; i < 8192; i++) {
		const float x = std::sin(2.f * float(M_PI) * sineHz * i / fs);
		const float y = pick(f.process(x), mode);
		if (i >= 4096) {
			sumIn += double(x) * x;
			sumOut += double(y) * y;
		}
	}
	return 10.0 * std::log10(sumOut / sumIn);
}

static void testLowpassShape() {
	const double below = gainDb(LP, 500.f, 1000.f, 48000.f);
	const double at = gainDb(LP, 1000.f, 1000.f, 48000.f);
	const double above = gainDb(LP, 4000.f, 1000.f, 48000.f);
	CHECK(std::fabs(below) < 0.5, "lp an octave below fc: %f dB", below);
	CHECK(std::fabs(at + 3.0) < 0.5, "lp at fc: %f dB", at);
	CHECK(above < -20.0, "lp two octaves above fc: %f dB", above);
}

static void testHighpassMirrors() {
	const double above = gainDb(HP, 2000.f, 1000.f, 48000.f);
	const double at = gainDb(HP, 1000.f, 1000.f, 48000.f);
	const double below = gainDb(HP, 250.f, 1000.f, 48000.f);
	CHECK(std::fabs(above) < 0.5, "hp an octave above fc: %f dB", above);
	CHECK(std::fabs(at + 3.0) < 0.5, "hp at fc: %f dB", at);
	CHECK(below < -20.0, "hp two octaves below fc: %f dB", below);
}

static void testBandpassPeaksAtCutoff() {
	const double at = gainDb(BP, 1000.f, 1000.f, 48000.f);
	const double lo = gainDb(BP, 250.f, 1000.f, 48000.f);
	const double hi = gainDb(BP, 4000.f, 1000.f, 48000.f);
	CHECK(std::fabs(at + 3.0) < 1.0, "bp at fc: %f dB", at);
	CHECK(lo < at && hi < at, "bp peak at fc: %f vs %f / %f", at, lo, hi);
}

static void testSweepStaysBounded() {
	Svf f;
	const float fs = 48000.f, dt = 1.f / fs;
	double phase = 0.0;
	bool ok = true;
	float peak = 0.f;
	for (int i = 0; i < 48000 && ok; i++) {
		const float t = i / fs;
		const float fc = 20.f * std::exp2(5.f + 5.f * std::sin(2.f * float(M_PI) * 1000.f * t));
		f.setCoefficients(fc, BUTTERWORTH_K, fs);
		const float x = sawBlep(float(phase), 100.f * dt);
		phase += 100.0 * dt;
		phase -= std::floor(phase);
		const Svf::Out o = f.process(x);
		ok = std::isfinite(o.lp) && std::isfinite(o.bp) && std::isfinite(o.hp) && std::fabs(o.lp) < 4.f;
		peak = std::fmax(peak, std::fabs(o.lp));
	}
	CHECK(ok, "swept lp went out of bounds, peak %f", peak);
}

static void testNyquistClamp() {
	Svf f;
	f.setCoefficients(1e6f, BUTTERWORTH_K, 44100.f);
	bool ok = true;
	for (int i = 0; i < 4096 && ok; i++) {
		const Svf::Out o = f.process(std::sin(2.f * float(M_PI) * 1000.f * i / 44100.f));
		ok = std::isfinite(o.lp) && std::isfinite(o.hp) && std::fabs(o.lp) < 2.f;
	}
	CHECK(ok, "cutoff above Nyquist is not clamped");
}

static void testSampleRateIndependence() {
	const double a = gainDb(LP, 500.f, 1000.f, 48000.f);
	const double b = gainDb(LP, 500.f, 1000.f, 96000.f);
	CHECK(std::fabs(a - b) < 0.1, "48k %f dB vs 96k %f dB", a, b);
}

int main() {
	testLowpassShape();
	testHighpassMirrors();
	testBandpassPeaksAtCutoff();
	testSweepStaysBounded();
	testNyquistClamp();
	testSampleRateIndependence();
	return rkatest::finish("test_svf");
}
