// Unit tests for src/fathom/DroneVoice.hpp. Run with `make test`.
#include "DroneVoice.hpp"
#include "testing.hpp"
#include <cmath>

using namespace rka;

static const float FS = 48000.f;
static const float C2 = 65.4064f;

struct Settings {
	float detune = 0.25f;
	float spread = 0.6f;
	float drift = 0.f;
	float subLevel = 0.5f;
	int subOctaves = 1;
};

static DroneVoice::Out run(DroneVoice& v, float f0, const Settings& s, float fs = FS) {
	return v.process(f0, s.detune, s.spread, s.drift, s.subLevel, s.subOctaves, fs);
}

// Frequency of the sub output from its rising zero crossings, after a
// 0.1 s settle.
static double measuredSubHz(DroneVoice& v, float f0, double seconds, const Settings& s, float fs = FS) {
	const int settle = int(0.1 * fs), n = int(seconds * fs);
	float prev = 0.f;
	int first = -1, last = -1, crossings = 0;
	for (int i = 0; i < settle + n; i++) {
		const float sub = run(v, f0, s, fs).sub;
		if (i >= settle && prev < 0.f && sub >= 0.f) {
			if (first < 0)
				first = i;
			last = i;
			crossings++;
		}
		prev = sub;
	}
	if (crossings < 2)
		return 0.0;
	return double(crossings - 1) * fs / double(last - first);
}

static double cents(double a, double b) {
	return 1200.0 * std::log2(a / b);
}

static void testSubTracks() {
	Settings s;
	for (int volts = -3; volts <= 5; volts++) {
		DroneVoice v;
		const float f0 = C2 * std::exp2(float(volts));
		v.reset(f0);
		const double hz = measuredSubHz(v, f0, 4.0, s);
		const double err = cents(hz, f0 / 2);
		CHECK(std::fabs(err) < 1.0, "at %d V sub is %f Hz, %f cents off", volts, hz, err);
	}
}

static void testClampLow() {
	Settings s;
	DroneVoice v;
	v.reset(0.5f);
	bool finite = true;
	for (int i = 0; i < 2 * 48000 && finite; i++) {
		const DroneVoice::Out o = run(v, 0.5f, s);
		finite = std::isfinite(o.left) && std::isfinite(o.right) && std::isfinite(o.sub);
	}
	CHECK(finite, "outputs not finite at f0 below the clamp");
	const double hz = measuredSubHz(v, 0.5f, 4.0, s);
	CHECK(std::fabs(cents(hz, 4.0)) < 1.0, "sub at clamp floor is %f Hz, want 4", hz);
}

static void testClampHigh() {
	Settings s;
	DroneVoice v;
	v.reset(20000.f);
	const double hz = measuredSubHz(v, 20000.f, 2.0, s);
	CHECK(std::fabs(cents(hz, 2000.0)) < 1.0, "sub at clamp ceiling is %f Hz, want 2000", hz);
}

static void testSubOctaves() {
	Settings s;
	s.subOctaves = 2;
	DroneVoice v;
	v.reset(110.f);
	const double hz = measuredSubHz(v, 110.f, 4.0, s);
	CHECK(std::fabs(cents(hz, 27.5)) < 1.0, "two-octave sub is %f Hz, want 27.5", hz);
}

static double rmsOf(DroneVoice& v, float f0, const Settings& s, int n, int which) {
	double sum = 0.0;
	for (int i = 0; i < n; i++) {
		const DroneVoice::Out o = run(v, f0, s);
		const float x = which == 0 ? o.left : which == 1 ? o.right : o.sub;
		sum += double(x) * x;
	}
	return std::sqrt(sum / n);
}

static void testSubOutIgnoresSubLevel() {
	Settings off, on;
	off.subLevel = 0.f;
	on.subLevel = 1.f;
	DroneVoice a, b;
	a.reset(110.f);
	b.reset(110.f);
	const double subRms = rmsOf(a, 110.f, off, 48000, 2);
	CHECK(std::fabs(subRms - M_SQRT1_2) < 0.01, "sub out rms %f with SUB at 0", subRms);
	DroneVoice c, d;
	c.reset(110.f);
	d.reset(110.f);
	const double quiet = rmsOf(c, 110.f, off, 48000, 0);
	const double loud = rmsOf(d, 110.f, on, 48000, 0);
	CHECK(quiet < loud, "SUB knob has no effect on left: %f vs %f", quiet, loud);
}

static void testSpreadZeroIsMono() {
	Settings s;
	s.spread = 0.f;
	DroneVoice v;
	v.reset(110.f);
	float maxDiff = 0.f;
	for (int i = 0; i < 48000; i++) {
		const DroneVoice::Out o = run(v, 110.f, s);
		maxDiff = std::fmax(maxDiff, std::fabs(o.left - o.right));
	}
	CHECK(maxDiff < 1e-6f, "spread 0 differs L/R by %f", maxDiff);
}

static void testSpreadKeepsMonoSum() {
	Settings narrow, wide;
	narrow.spread = 0.f;
	wide.spread = 1.f;
	DroneVoice a, b;
	a.reset(110.f);
	b.reset(110.f);
	double sa = 0.0, sb = 0.0;
	for (int i = 0; i < 2 * 48000; i++) {
		const DroneVoice::Out oa = run(a, 110.f, narrow), ob = run(b, 110.f, wide);
		sa += double(oa.left + oa.right) * (oa.left + oa.right);
		sb += double(ob.left + ob.right) * (ob.left + ob.right);
	}
	const double dB = 10.0 * std::log10(sb / sa);
	CHECK(std::fabs(dB) < 1.5, "mono sum changes by %f dB with spread", dB);
}

static void testNoClickOnPitchStep() {
	Settings s;
	DroneVoice v;
	v.reset(65.4f);
	float maxBefore = 0.f, maxAfter = 0.f, prev = 0.f;
	for (int i = 0; i < 48000; i++) {
		const float l = run(v, 65.4f, s).left;
		if (i > 0)
			maxBefore = std::fmax(maxBefore, std::fabs(l - prev));
		prev = l;
	}
	for (int i = 0; i < 2400; i++) {
		const float l = run(v, 130.8f, s).left;
		maxAfter = std::fmax(maxAfter, std::fabs(l - prev));
		prev = l;
	}
	CHECK(maxAfter < 1.5f * maxBefore, "pitch step jumped %f vs %f steady", maxAfter, maxBefore);
}

static void testDefaultLevel() {
	Settings s;
	DroneVoice v;
	v.reset(C2);
	float peak = 0.f;
	for (int i = 0; i < 2 * 48000; i++)
		peak = std::fmax(peak, std::fabs(run(v, C2, s).left));
	CHECK(peak >= 0.5f && peak <= 1.0f, "default peak %f", peak);
}

static void testSampleRateIndependence() {
	Settings s;
	DroneVoice a, b;
	a.reset(110.f);
	b.reset(110.f);
	const double h48 = measuredSubHz(a, 110.f, 4.0, s, 48000.f);
	const double h96 = measuredSubHz(b, 110.f, 4.0, s, 96000.f);
	CHECK(std::fabs(cents(h48, h96)) < 0.1, "48k %f Hz vs 96k %f Hz", h48, h96);
}

static void testOutputStage() {
	OutputStage st;
	float peak = 0.f;
	for (int i = 0; i < 48000; i++) {
		float l = std::sin(2.f * float(M_PI) * 100.f * i / FS), r = l;
		st.process(1200.f, FS, l, r);
		if (i > 4800)
			peak = std::fmax(peak, std::fabs(l));
	}
	CHECK(std::fabs(peak - std::tanh(1.f)) < 0.05f, "open cutoff peak %f, want tanh(1)", peak);

	OutputStage closed;
	double sumIn = 0.0, sumOut = 0.0;
	for (int i = 0; i < 48000; i++) {
		float l = std::sin(2.f * float(M_PI) * 100.f * i / FS), r = l;
		const float x = l;
		closed.process(20.f, FS, l, r);
		if (i > 4800) {
			sumIn += double(x) * x;
			sumOut += double(l) * l;
		}
	}
	const double dB = 10.0 * std::log10(sumOut / sumIn);
	CHECK(dB < -20.0, "cutoff at 20 Hz passes 100 Hz at %f dB", dB);

	OutputStage dc;
	float last = 0.f;
	for (int i = 0; i < 2 * 48000; i++) {
		float l = 1.f, r = 1.f;
		dc.process(20000.f, FS, l, r);
		last = l;
	}
	CHECK(std::fabs(last) < 0.01f, "DC not blocked: %f after 2 s", last);
}

int main() {
	testSubTracks();
	testClampLow();
	testClampHigh();
	testSubOctaves();
	testSubOutIgnoresSubLevel();
	testSpreadZeroIsMono();
	testSpreadKeepsMonoSum();
	testNoClickOnPitchStep();
	testDefaultLevel();
	testSampleRateIndependence();
	testOutputStage();
	return rkatest::finish("test_voice");
}
