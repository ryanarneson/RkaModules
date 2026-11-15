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

// The module's default filter settings.
static OutputStage::Params defaults() {
	OutputStage::Params p;
	p.cutoffHz = 1200.f;
	p.resonance = 0.2f;
	p.drive = 0.25f;
	p.type = LadderType::LP24;
	p.subThroughFilter = false;
	return p;
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

// which: 0 left, 1 right, 2 sub, 3 subMix
static double rmsOf(DroneVoice& v, float f0, const Settings& s, int n, int which) {
	double sum = 0.0;
	for (int i = 0; i < n; i++) {
		const DroneVoice::Out o = run(v, f0, s);
		const float x = which == 0 ? o.left : which == 1 ? o.right : which == 2 ? o.sub : o.subMix;
		sum += double(x) * x;
	}
	return std::sqrt(sum / n);
}

// SUB OUT ignores the SUB knob; the sub's share of the mix follows it.
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
	const double quiet = rmsOf(c, 110.f, off, 48000, 3);
	const double loud = rmsOf(d, 110.f, on, 48000, 3);
	CHECK(quiet == 0.0, "sub mix %f with SUB at 0", quiet);
	CHECK(std::fabs(loud - SUB_GAIN * M_SQRT1_2) < 0.01, "sub mix rms %f with SUB at 1", loud);
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
		const double ma = oa.left + oa.right + 2.0 * oa.subMix, mb = ob.left + ob.right + 2.0 * ob.subMix;
		sa += ma * ma;
		sb += mb * mb;
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
	for (int i = 0; i < 2 * 48000; i++) {
		const DroneVoice::Out o = run(v, C2, s);
		peak = std::fmax(peak, std::fabs(o.left + o.subMix));
	}
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

// RMS of the stage's left output for a sine at `hz` with `subMix` zero,
// over the second half of 2 s.
static double stageRms(const OutputStage::Params& p, float hz, float amplitude = 1.f) {
	OutputStage st;
	double sum = 0.0;
	int n = 0;
	for (int i = 0; i < 2 * 48000; i++) {
		float l = amplitude * std::sin(2.f * float(M_PI) * hz * i / FS), r = l;
		st.process(p, 0.f, FS, l, r);
		if (i >= 48000) {
			sum += double(l) * l;
			n++;
		}
	}
	return std::sqrt(sum / n);
}

static void testStageClosedCutoffAttenuates() {
	OutputStage::Params open = defaults(), closed = defaults();
	open.cutoffHz = 20000.f;
	closed.cutoffHz = 20.f;
	const double dB = 20.0 * std::log10(stageRms(closed, 100.f) / stageRms(open, 100.f));
	CHECK(dB < -20.0, "cutoff at 20 Hz passes 100 Hz at %f dB", dB);
}

static void testStageBlocksDc() {
	OutputStage st;
	OutputStage::Params p = defaults();
	p.cutoffHz = 20000.f;
	float last = 0.f;
	for (int i = 0; i < 2 * 48000; i++) {
		float l = 1.f, r = 1.f;
		st.process(p, 0.f, FS, l, r);
		last = l;
	}
	CHECK(std::fabs(last) < 0.01f, "DC not blocked: %f after 2 s", last);
}

// DRIVE changes the tone far more than the level.
static void testDriveMakeup() {
	OutputStage::Params p = defaults();
	p.cutoffHz = 20000.f;
	p.resonance = 0.f;
	const double ref = stageRms(p, 100.f, 0.5f);
	p.drive = 1.f;
	const double hot = 20.0 * std::log10(stageRms(p, 100.f, 0.5f) / ref);
	p.drive = 0.f;
	const double cold = 20.0 * std::log10(stageRms(p, 100.f, 0.5f) / ref);
	CHECK(std::fabs(hot) < 2.0, "full drive is %f dB from the default", hot);
	CHECK(std::fabs(cold) < 1.0, "zero drive is %f dB from the default", cold);
}

// RMS of the left output with only the sub mix feeding the stage.
static double subOnlyRms(const OutputStage::Params& p) {
	OutputStage st;
	double sum = 0.0;
	int n = 0;
	for (int i = 0; i < 2 * 48000; i++) {
		float l = 0.f, r = 0.f;
		const float sub = 0.5f * std::sin(2.f * float(M_PI) * 65.f * i / FS);
		st.process(p, sub, FS, l, r);
		if (i >= 48000) {
			sum += double(l) * l;
			n++;
		}
	}
	return std::sqrt(sum / n);
}

static void testSubRouting() {
	OutputStage::Params open = defaults(), closed = defaults();
	open.cutoffHz = 20000.f;
	closed.cutoffHz = 20.f;
	const double around = 20.0 * std::log10(subOnlyRms(closed) / subOnlyRms(open));
	CHECK(std::fabs(around) < 1.0, "sub around the filter changes %f dB with the cutoff", around);
	open.subThroughFilter = closed.subThroughFilter = true;
	const double through = 20.0 * std::log10(subOnlyRms(closed) / subOnlyRms(open));
	CHECK(through < -15.0, "sub through a closed filter is only %f dB down", through);
}

// A parameter step must not step the output: the stage smooths it.
// `which`: 0 cutoff, 1 resonance, 2 drive.
static void checkSmoothed(int which, const char* name) {
	OutputStage st;
	float prev = 0.f, steady = 0.f, during = 0.f;
	for (int i = 0; i < 2 * 48000; i++) {
		float l = std::sin(2.f * float(M_PI) * 100.f * i / FS), r = l;
		OutputStage::Params p = defaults();
		const bool after = i >= 48000;
		if (which == 0)
			p.cutoffHz = after ? 20000.f : 20.f;
		else if (which == 1)
			p.resonance = after ? 1.f : 0.f;
		else
			p.drive = after ? 1.f : 0.f;
		st.process(p, 0.f, FS, l, r);
		const float jump = std::fabs(l - prev);
		prev = l;
		if (i >= 48000 + 2400 && i < 2 * 48000)
			steady = std::fmax(steady, jump);
		if (i >= 48000 && i < 48000 + 2400)
			during = std::fmax(during, jump);
	}
	CHECK(during < 1.5f * steady, "%s step jumped %f vs %f steady", name, during, steady);
}

static void testCutoffIsSmoothed() {
	checkSmoothed(0, "cutoff");
}

static void testResAndDriveAreSmoothed() {
	checkSmoothed(1, "resonance");
	checkSmoothed(2, "drive");
}

// Changing the sample rate mid-stream recomputes everything without a burst.
static void testSampleRateChangeNoBurst() {
	OutputStage st;
	OutputStage::Params p = defaults();
	float steady = 0.f, after = 0.f;
	for (int i = 0; i < 48000; i++) {
		float l = std::sin(2.f * float(M_PI) * 100.f * i / 48000.f), r = l;
		st.process(p, 0.f, 48000.f, l, r);
		if (i >= 24000)
			steady = std::fmax(steady, std::fabs(l));
	}
	for (int i = 0; i < 960; i++) {
		float l = std::sin(2.f * float(M_PI) * 100.f * i / 96000.f), r = l;
		st.process(p, 0.f, 96000.f, l, r);
		after = std::fmax(after, std::fabs(l));
	}
	CHECK(after < 1.5f * steady, "sample-rate change burst %f vs %f steady", after, steady);
}

// The whole chain as the module runs it: voice, stage, x5 V. At the
// default settings the output should sit near other +/-5 V voices.
static void testModuleLevelAtDefaults() {
	Settings s;
	DroneVoice v;
	OutputStage st;
	v.reset(C2);
	float peak = 0.f;
	for (int i = 0; i < 2 * 48000; i++) {
		const DroneVoice::Out o = run(v, C2, s);
		float l = o.left, r = o.right;
		st.process(defaults(), o.subMix, FS, l, r);
		if (i > 4800)
			peak = std::fmax(peak, std::fabs(5.f * l));
	}
	CHECK(peak >= 3.5f && peak <= 5.0f, "module peak at defaults %f V", peak);
}

int main() {
	testModuleLevelAtDefaults();
	testCutoffIsSmoothed();
	testResAndDriveAreSmoothed();
	testSampleRateChangeNoBurst();
	testStageClosedCutoffAttenuates();
	testStageBlocksDc();
	testDriveMakeup();
	testSubRouting();
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
	return rkatest::finish("test_voice");
}
