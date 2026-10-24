// Unit tests for src/fathom/Ladder.hpp. Run with `make test`.
#include "Ladder.hpp"
#include "testing.hpp"
#include <cmath>
#include <vector>

using namespace rka;

static const float FS = 48000.f;

// Gain of the ladder for a sine of the given amplitude, in dB: 8192
// samples, the second half measured, RMS out over RMS in.
static double gainDb(LadderType type, float sineHz, float fc, float k, float fs, float amplitude = 1.f) {
	Ladder f;
	f.reset();
	f.setCoefficients(fc, k, type, fs);
	double sumIn = 0.0, sumOut = 0.0;
	for (int i = 0; i < 8192; i++) {
		const float x = amplitude * std::sin(2.f * float(M_PI) * sineHz * i / fs);
		const float y = f.process(x);
		if (i >= 4096) {
			sumIn += double(x) * x;
			sumOut += double(y) * y;
		}
	}
	return 10.0 * std::log10(sumOut / sumIn);
}

// Run `settle` seconds of silence, then `measure` seconds: peak and the
// frequency from rising zero crossings.
struct Tone {
	double peak;
	double hz;
};

static Tone silentTone(float fc, float k, float fs, double settle, double measure) {
	Ladder f;
	f.reset();
	f.setCoefficients(fc, k, LadderType::LP24, fs);
	const int n0 = int(settle * fs), n1 = n0 + int(measure * fs);
	float prev = 0.f;
	int first = -1, last = -1, crossings = 0;
	Tone t = {0.0, 0.0};
	for (int i = 0; i < n1; i++) {
		const float y = f.process(0.f);
		if (i >= n0) {
			t.peak = std::fmax(t.peak, std::fabs(double(y)));
			if (prev < 0.f && y >= 0.f) {
				if (first < 0)
					first = i;
				last = i;
				crossings++;
			}
		}
		prev = y;
	}
	if (crossings >= 2)
		t.hz = double(crossings - 1) * fs / double(last - first);
	return t;
}

static void testLp24Shape() {
	const double below = gainDb(LadderType::LP24, 100.f, 1000.f, 0.f, FS, 0.5f);
	const double above = gainDb(LadderType::LP24, 4000.f, 1000.f, 0.f, FS, 0.5f);
	CHECK(below > -0.5, "lp24 a decade below fc: %f dB", below);
	CHECK(above < -40.0, "lp24 two octaves above fc: %f dB", above);
}

static void testLp12Shape() {
	const double above = gainDb(LadderType::LP12, 4000.f, 1000.f, 0.f, FS, 0.5f);
	CHECK(above < -20.0 && above > -30.0, "lp12 two octaves above fc: %f dB", above);
}

// The low end holds as resonance rises in the low-pass types (compensated),
// and the top end holds in HP (not compensated, not needed).
static void testSteadyLowEnd() {
	const float k90 = LADDER_K_MAX * 0.9f * 0.9f;
	for (LadderType t : {LadderType::LP24, LadderType::LP12}) {
		const double flat = gainDb(t, 40.f, 1000.f, 0.f, FS, 0.5f);
		const double res = gainDb(t, 40.f, 1000.f, k90, FS, 0.5f);
		CHECK(std::fabs(res - flat) < 1.5, "type %d: 40 Hz moves %f dB with resonance", int(t), res - flat);
	}
	const double flat = gainDb(LadderType::HP, 5000.f, 100.f, 0.f, FS, 0.5f);
	const double res = gainDb(LadderType::HP, 5000.f, 100.f, k90, FS, 0.5f);
	CHECK(std::fabs(res - flat) < 1.5, "hp: 5 kHz moves %f dB with resonance", res - flat);
}

static void testHpAndBp() {
	const double hpPass = gainDb(LadderType::HP, 5000.f, 100.f, 0.f, FS, 0.5f);
	const double hpStop = gainDb(LadderType::HP, 20.f, 100.f, 0.f, FS, 0.5f);
	CHECK(std::fabs(hpPass) < 1.0, "hp passes 5 kHz at %f dB", hpPass);
	CHECK(hpStop < -40.0, "hp passes 20 Hz at %f dB", hpStop);
	const double at = gainDb(LadderType::BP, 1000.f, 1000.f, 0.f, FS, 0.5f);
	const double lo = gainDb(LadderType::BP, 100.f, 1000.f, 0.f, FS, 0.5f);
	const double hi = gainDb(LadderType::BP, 10000.f, 1000.f, 0.f, FS, 0.5f);
	CHECK(at - lo > 20.0 && at - hi > 20.0, "bp at fc %f dB, 100 Hz %f, 10 kHz %f", at, lo, hi);
}

static void testSelfOscillation() {
	const Tone a = silentTone(220.f, LADDER_K_MAX, 48000.f, 1.0, 1.0);
	CHECK(a.peak >= 0.05 && a.peak <= 0.5, "self-oscillation peak %f", a.peak);
	CHECK(std::fabs(a.hz - 220.0) / 220.0 < 0.05, "self-oscillation at %f Hz, want 220", a.hz);
	const Tone b = silentTone(220.f, LADDER_K_MAX, 96000.f, 1.0, 1.0);
	CHECK(std::fabs(b.hz - a.hz) / a.hz < 0.005, "96k oscillates at %f Hz vs %f at 48k", b.hz, a.hz);
	const Tone c = silentTone(440.f, LADDER_K_MAX, 48000.f, 1.0, 1.0);
	// The residual tuning error is about +/-0.6 % over the range, so an
	// octave can be off by up to about 1 %.
	CHECK(std::fabs(c.hz - 2.0 * a.hz) / (2.0 * a.hz) < 0.01, "octave up oscillates at %f Hz vs %f", c.hz, a.hz);
}

static void testNoiseFloorAtZeroRes() {
	const Tone t = silentTone(1000.f, 0.f, FS, 1.0, 0.5);
	CHECK(t.peak < 1e-4, "noise floor %g at zero resonance", t.peak);
}

static void testResonanceTuningSmallSignal() {
	const float k95 = LADDER_K_MAX * 0.95f * 0.95f;
	const float hz[5] = {900.f, 950.f, 1000.f, 1050.f, 1100.f};
	int best = 0;
	double bestDb = -1e9;
	for (int i = 0; i < 5; i++) {
		const double g = gainDb(LadderType::LP24, hz[i], 1000.f, k95, FS, 0.001f);
		if (g > bestDb) {
			bestDb = g;
			best = i;
		}
	}
	CHECK(best >= 1 && best <= 3, "small-signal peak at %f Hz for fc 1 kHz", hz[best]);
}

// Energy at `hz` relative to the fundamental, in dB (Goertzel over the
// second half of the run).
static double harmonicDb(float amplitude, double* peakOut) {
	Ladder f;
	f.reset();
	f.setCoefficients(10000.f, 0.f, LadderType::LP24, FS);
	const int n = 9600;
	std::vector<float> y(n);
	double peak = 0.0;
	for (int i = 0; i < 2 * n; i++) {
		const float v = f.process(amplitude * std::sin(2.f * float(M_PI) * 100.f * i / FS));
		if (i >= n) {
			y[i - n] = v;
			peak = std::fmax(peak, std::fabs(double(v)));
		}
	}
	auto goertzel = [&](double hz) {
		const double w = 2.0 * M_PI * hz / FS, c = 2.0 * std::cos(w);
		double s0 = 0, s1 = 0, s2 = 0;
		for (int i = 0; i < n; i++) {
			s0 = y[i] + c * s1 - s2;
			s2 = s1;
			s1 = s0;
		}
		return s1 * s1 + s2 * s2 - c * s1 * s2;
	};
	*peakOut = peak;
	return 10.0 * std::log10(goertzel(300.0) / goertzel(100.0));
}

static void testDriveAddsHarmonics() {
	double peakHot = 0.0, peakCold = 0.0;
	const double hot = harmonicDb(8.f, &peakHot);
	const double cold = harmonicDb(0.5f, &peakCold);
	CHECK(hot - cold > 20.0, "third harmonic at 8x is %f dB vs %f dB at 0.5x", hot, cold);
	CHECK(peakHot <= 2.0, "driven peak %f", peakHot);
}

static bool runStepped(float kA, float kB, float fcA, float fcB, float fs, double* peak) {
	Ladder f;
	f.reset();
	bool ok = true;
	*peak = 0.0;
	const int n = int(5.0 * fs), hop = int(0.1 * fs);
	for (int i = 0; i < n && ok; i++) {
		if (i % hop == 0) {
			const bool a = (i / hop) % 2 == 0;
			f.setCoefficients(a ? fcA : fcB, a ? kA : kB, LadderType::LP24, fs);
		}
		const float x = 8.f * (std::fmod(110.f * i / fs, 1.f) < 0.5f ? 1.f : -1.f);
		const float y = f.process(x);
		ok = std::isfinite(y) && std::fabs(y) < 2.5f;
		*peak = std::fmax(*peak, std::fabs(double(y)));
	}
	return ok;
}

static void testStabilityUnderSteps() {
	double peak;
	CHECK(runStepped(LADDER_K_MAX, LADDER_K_MAX, 20.f, 20000.f, FS, &peak), "cutoff steps: peak %f or non-finite", peak);
	CHECK(runStepped(0.f, LADDER_K_MAX, 1000.f, 1000.f, FS, &peak), "resonance steps: peak %f or non-finite", peak);
}

static void testCutoffClamp() {
	Ladder f;
	f.reset();
	f.setCoefficients(30000.f, LADDER_K_MAX, LadderType::LP24, 44100.f);
	bool ok = true;
	double peak = 0.0;
	for (int i = 0; i < 44100 && ok; i++) {
		const float x = std::fmod(110.f * i / 44100.f, 1.f) < 0.5f ? 1.f : -1.f;
		const float y = f.process(x);
		ok = std::isfinite(y) && std::fabs(y) < 2.5f;
		peak = std::fmax(peak, std::fabs(double(y)));
	}
	CHECK(ok, "cutoff above Nyquist/2: peak %f or non-finite", peak);
}

static void testTypeSwitchMidSound() {
	Ladder f, fresh;
	f.reset();
	fresh.reset();
	f.setCoefficients(500.f, LADDER_K_MAX, LadderType::LP24, FS);
	fresh.setCoefficients(500.f, LADDER_K_MAX, LadderType::HP, FS);
	bool finite = true;
	double sumA = 0.0, sumB = 0.0;
	const int half = 24000, settle = 2400, n = 9600;
	for (int i = 0; i < half + settle + n; i++) {
		const float x = 0.5f * std::sin(2.f * float(M_PI) * 500.f * i / FS);
		if (i == half)
			f.setCoefficients(500.f, LADDER_K_MAX, LadderType::HP, FS);
		const float ya = f.process(x);
		const float yb = fresh.process(x);
		if (i >= half && i < half + settle)
			finite = finite && std::isfinite(ya);
		if (i >= half + settle) {
			sumA += double(ya) * ya;
			sumB += double(yb) * yb;
		}
	}
	CHECK(finite, "type switch produced non-finite output");
	const double dB = 10.0 * std::log10(sumA / sumB);
	CHECK(std::fabs(dB) < 1.0, "after a type switch the gain differs by %f dB from a fresh HP", dB);
}

int main() {
	testLp24Shape();
	testLp12Shape();
	testSteadyLowEnd();
	testHpAndBp();
	testSelfOscillation();
	testNoiseFloorAtZeroRes();
	testResonanceTuningSmallSignal();
	testDriveAddsHarmonics();
	testStabilityUnderSteps();
	testCutoffClamp();
	testTypeSwitchMidSound();
	return rkatest::finish("test_ladder");
}
