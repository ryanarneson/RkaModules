// Unit tests for src/narrows/StateCore.hpp: the state-variable core, run as
// the engine runs it, at twice the rate inside the halfband pair, and held
// against Fathom's ladder where the two must match. Run with `make test`.
#include "../fathom/Halfband.hpp"
#include "../fathom/LadderCore.hpp"
#include "Laws.hpp"
#include "StateCore.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>
#include <functional>

using namespace rka;
using namespace rka::narrows;

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float next() {
		s = s * 1664525u + 1013904223u;
		return float(int32_t(s)) * (1.f / 2147483648.f);
	}
};

enum Tap { LP, BP, HP };

// One filter at the engine's rate: the core at 2x between the halfbands.
struct StateRun {
	Upsampler2x up;
	Downsampler2x down;
	StateCore<float> core;
	Lcg rng{3};
	float g = 0.f, res = 0.f;
	Tap tap = LP;
	void set(float hz, float r, float fs, Tap t = LP) {
		g = tan34(float(M_PI) * hz / (2.f * fs));
		res = r;
		tap = t;
	}
	float process(float x) {
		float os[2];
		up.process(x, os);
		for (float& v : os) {
			const StateOut<float> o = core.step(v, g, res, rng.next());
			v = ladderLimit(tap == LP ? o.lp : tap == BP ? o.bp : o.hp);
		}
		return down.process(os);
	}
};

// The ladder the same way, low-pass 24, with the feedback Res gives it.
struct LadderRun {
	Upsampler2x up;
	Downsampler2x down;
	LadderCore<float> core;
	Lcg rng{3};
	float g = 0.f, k = 0.f;
	void set(float hz, float r, float fs) {
		g = tan34(float(M_PI) * hz / (2.f * fs));
		k = ladderFeedback(r);
	}
	float process(float x) {
		float os[2];
		up.process(x, os);
		for (float& v : os)
			v = ladderLimit(core.step(v, g, k, 1.f + k, rng.next()).y3);
		return down.process(os);
	}
};

// Gain in dB for a sine: RMS out over RMS in, second half of 16384 samples.
template <typename F>
static double gainDb(F& f, float sineHz, float fs, float amplitude) {
	double sumIn = 0.0, sumOut = 0.0;
	for (int i = 0; i < 16384; i++) {
		const float x = amplitude * std::sin(2.f * float(M_PI) * sineHz * i / fs);
		const float y = f.process(x);
		if (i >= 8192) {
			sumIn += double(x) * x;
			sumOut += double(y) * y;
		}
	}
	return 10.0 * std::log10(sumOut / sumIn);
}

struct Tone {
	double peak, hz;
};

// A short knock, `settle` seconds, then `measure` seconds: the peak, and the
// pitch from rising zero crossings (interpolated).
template <typename F>
static Tone ring(F& f, float fs, double settle, double measure) {
	const int n0 = int(settle * fs), n1 = n0 + int(measure * fs);
	float prev = 0.f;
	double first = -1.0, last = -1.0;
	int crossings = 0;
	Tone t = {0.0, 0.0};
	for (int i = 0; i < n1; i++) {
		const float y = f.process(i < 48 ? 0.2f : 0.f);
		if (i >= n0) {
			t.peak = std::fmax(t.peak, std::fabs(double(y)));
			if (prev < 0.f && y >= 0.f) {
				const double at = i - 1 + double(-prev) / (double(y) - prev);
				if (first < 0.0)
					first = at;
				last = at;
				crossings++;
			}
		}
		prev = y;
	}
	if (crossings >= 2)
		t.hz = double(crossings - 1) * fs / (last - first);
	return t;
}

static double cents(double hz, double want) {
	return 1200.0 * std::log2(hz / want);
}

static const float FS = 48000.f;

static void state_lowpass_shape() {
	const float a = 0.05f;
	StateRun f;
	f.set(1000.f, 0.f, FS);
	const double pass = gainDb(f, 100.f, FS, a), corner = gainDb(f, 1000.f, FS, a), at4 = gainDb(f, 4000.f, FS, a),
	             at8 = gainDb(f, 8000.f, FS, a);
	CHECK(std::fabs(pass) < 0.5, "passband %f dB", pass);
	CHECK(std::fabs(corner + 3.0) < 1.0, "corner %f dB", corner);
	CHECK(std::fabs((at4 - at8) - 12.0) < 1.5, "slope %f dB per octave (4 kHz %f, 8 kHz %f)", at4 - at8, at4, at8);
}

static void state_bandpass_peaks_at_cutoff() {
	const float a = 0.05f;
	StateRun f;
	f.set(1000.f, 0.8f, FS, BP);
	double best = -1e9, bestHz = 0.0;
	for (float hz = 900.f; hz <= 1100.f; hz += 5.f) {
		const double g = gainDb(f, hz, FS, a);
		if (g > best) {
			best = g;
			bestHz = hz;
		}
	}
	CHECK(std::fabs(bestHz - 1000.0) <= 20.0, "band-pass peak at %f Hz", bestHz);
	f.set(1000.f, 0.f, FS, BP);
	const double lo = gainDb(f, 250.f, FS, a), lo2 = gainDb(f, 125.f, FS, a), hi = gainDb(f, 4000.f, FS, a),
	             hi2 = gainDb(f, 8000.f, FS, a);
	CHECK(std::fabs((lo - lo2) - 6.0) < 1.0 && std::fabs((hi - hi2) - 6.0) < 1.5, "skirts %f and %f dB per octave", lo - lo2,
	      hi - hi2);
}

static void state_highpass_mirrors() {
	const float a = 0.05f;
	StateRun f;
	f.set(1000.f, 0.f, FS, HP);
	const double pass = gainDb(f, 10000.f, FS, a), corner = gainDb(f, 1000.f, FS, a), at250 = gainDb(f, 250.f, FS, a),
	             at125 = gainDb(f, 125.f, FS, a);
	CHECK(std::fabs(pass) < 0.5, "passband %f dB", pass);
	CHECK(std::fabs(corner + 3.0) < 1.0, "corner %f dB", corner);
	CHECK(std::fabs((at250 - at125) - 12.0) < 1.5, "slope %f dB per octave", at250 - at125);
}

static void state_passband_holds_with_res() {
	for (float r : {0.f, 0.5f, 0.9f}) {
		StateRun f;
		f.set(1000.f, r, FS);
		const double pass = gainDb(f, 100.f, FS, 0.05f);
		CHECK(std::fabs(pass) < 1.0, "passband at Res %g: %f dB", r, pass);
	}
}

// The damping follows the ladder's square law below the onset: at half of
// RES_ONSET it is three quarters of its Res 0 value, and the low-pass gain
// at the cutoff is one over the damping.
static void state_res_law() {
	StateRun f;
	f.set(1000.f, 0.5f * RES_ONSET, FS);
	const double corner = gainDb(f, 1000.f, FS, 0.02f), want = 20.0 * std::log10(1.0 / (0.75 * STATE_K0));
	CHECK(std::fabs(corner - want) < 0.5, "at half the onset: %f dB at the cutoff, want %f", corner, want);
}

static void state_self_oscillates_at_the_onset() {
	// From silence, at the top of Res: its own noise starts it.
	StateRun top;
	top.set(440.f, 1.f, FS);
	double peak = 0.0;
	for (int i = 0; i < 4 * 48000; i++) {
		const float y = top.process(0.f);
		if (i > 3 * 48000)
			peak = std::fmax(peak, std::fabs(double(y)));
	}
	CHECK(peak > 0.05, "from silence at Res 1: peak %g", peak);
	// Either side of the onset, after a knock: the ladder's onset is at
	// RES_ONSET, and this core's is within 0.01 of it.
	StateRun below, above;
	below.set(440.f, RES_ONSET - 0.01f, FS);
	above.set(440.f, RES_ONSET + 0.01f, FS);
	const Tone b = ring(below, FS, 3.0, 0.5), a = ring(above, FS, 3.0, 0.5);
	CHECK(b.peak < 1e-3, "just below the onset the ring dies: peak %g", b.peak);
	CHECK(a.peak > 0.01, "just above it the ring holds: peak %g", a.peak);
	LadderRun lb, la;
	lb.set(440.f, RES_ONSET - 0.01f, FS);
	la.set(440.f, RES_ONSET + 0.01f, FS);
	const Tone lbt = ring(lb, FS, 3.0, 0.5), lat = ring(la, FS, 3.0, 0.5);
	CHECK(lbt.peak < 1e-3 && lat.peak > 0.01, "the ladder's onset is in the same place: %g, %g", lbt.peak, lat.peak);
}

static void state_tuned() {
	double worstOnset = 0.0, worstTop = 0.0;
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (float hz : {30.f, 110.f, 440.f, 1760.f, 4000.f}) {
			StateRun onset, top;
			onset.set(hz, RES_ONSET + 0.005f, fs);
			top.set(hz, 1.f, fs);
			const Tone a = ring(onset, fs, 3.0, 2.0), b = ring(top, fs, 3.0, 2.0);
			CHECK(a.hz > 0.0 && std::fabs(cents(a.hz, hz)) < 5.0, "onset, %g Hz at %g: %f cents", hz, fs, cents(a.hz, hz));
			CHECK(b.hz > 0.0 && std::fabs(cents(b.hz, hz)) < 15.0, "Res 1, %g Hz at %g: %f cents", hz, fs, cents(b.hz, hz));
			worstOnset = std::fmax(worstOnset, std::fabs(cents(a.hz, hz)));
			worstTop = std::fmax(worstTop, std::fabs(cents(b.hz, hz)));
		}
	std::printf("  state core tuning: worst %.2f cents at the onset, %.2f at Res 1\n", worstOnset, worstTop);
}

static void state_level_matches_ladder() {
	for (float hz : {110.f, 440.f, 1760.f}) {
		StateRun s;
		LadderRun l;
		s.set(hz, 1.f, FS);
		l.set(hz, 1.f, FS);
		const Tone a = ring(s, FS, 4.0, 1.0), b = ring(l, FS, 4.0, 1.0);
		const double db = 20.0 * std::log10(a.peak / b.peak);
		CHECK(std::fabs(db) < 3.0, "at %g Hz: state %g, ladder %g (%f dB)", hz, a.peak, b.peak, db);
		CHECK(a.peak * VOLTS > 4.0 && a.peak * VOLTS < 9.0, "state at %g Hz: %f V", hz, a.peak * VOLTS);
		CHECK(b.peak * VOLTS > 4.0 && b.peak * VOLTS < 9.0, "ladder at %g Hz: %f V", hz, b.peak * VOLTS);
	}
}

// A 5 V sine in the passband: nearly the small-signal gain, little distortion.
static void state_clean_at_five_volts() {
	const float a = 5.f / VOLTS, hz = 100.f;
	StateRun f, small;
	f.set(1000.f, 0.f, FS);
	small.set(1000.f, 0.f, FS);
	const double smallDb = gainDb(small, hz, FS, 0.001f);
	double fund1 = 0.0, fund2 = 0.0, total = 0.0;
	const int n0 = 9600, n1 = n0 + 48000;   // whole cycles of 100 Hz
	double in2 = 0.0;
	for (int i = 0; i < n1; i++) {
		const double ph = 2.0 * M_PI * hz * i / FS;
		const float x = a * float(std::sin(ph));
		const float y = f.process(x);
		if (i >= n0) {
			fund1 += y * std::sin(ph);
			fund2 += y * std::cos(ph);
			total += double(y) * y;
			in2 += double(x) * x;
		}
	}
	const int n = n1 - n0;
	const double fundPower = 2.0 * (fund1 * fund1 + fund2 * fund2) / n;
	const double thd = std::sqrt(std::fmax(0.0, 1.0 - fundPower / total));
	const double db = 10.0 * std::log10(total / in2);
	CHECK(std::fabs(db - smallDb) < 1.0, "gain %f dB against %f small-signal", db, smallDb);
	CHECK(thd < 0.03, "distortion %f", thd);
	// And it does clip when pushed: the input soft clip is there.
	StateRun hot;
	hot.set(1000.f, 0.f, FS);
	const double hotDb = gainDb(hot, hz, FS, 3.f);
	CHECK(hotDb < smallDb - 6.0, "a 3.0 sine is held back: %f dB", hotDb);
}

static void state_four_wide_equals_one_wide() {
	StateCore<Float4> wide;
	StateCore<float> narrow[4];
	const float g[4] = {0.004f, 0.03f, 0.2f, 0.6f};
	const float res[4] = {0.f, 0.5f, 0.94f, 1.f};
	Lcg rng(99);
	double worst = 0.0;
	for (int i = 0; i < 96000; i++) {
		if (i % 4096 == 0) {
			wide.reset();
			for (auto& n : narrow)
				n.reset();
		}
		float x[4], nz[4];
		for (int l = 0; l < 4; l++) {
			x[l] = (l + 1) * 0.3f * rng.next();
			nz[l] = rng.next();
		}
		const StateOut<Float4> w = wide.step(Float4{x[0], x[1], x[2], x[3]}, Float4{g[0], g[1], g[2], g[3]},
		                                     Float4{res[0], res[1], res[2], res[3]}, Float4{nz[0], nz[1], nz[2], nz[3]});
		for (int l = 0; l < 4; l++) {
			const StateOut<float> n = narrow[l].step(x[l], g[l], res[l], nz[l]);
			const float pairs[3][2] = {{w.lp[l], n.lp}, {w.bp[l], n.bp}, {w.hp[l], n.hp}};
			for (auto& p : pairs)
				worst = std::fmax(worst, std::fabs(double(p[0]) - p[1]) / (1.0 + std::fabs(double(p[1]))));
		}
	}
	std::printf("  state four-wide vs one-wide: worst %.3g\n", worst);
	CHECK(worst < 1e-5, "worst %g", worst);
}

static void state_bounded() {
	// Res at the top, loud noise, then a nudge of DC: stays finite and small.
	StateRun f;
	f.set(300.f, 1.f, FS);
	Lcg rng(8);
	double peak = 0.0;
	for (int i = 0; i < 5 * 48000; i++) {
		const float y = f.process(4.f * rng.next() + 0.5f);
		peak = std::fmax(peak, std::fabs(double(y)));
		if (!(y == y))
			peak = 1e9;
	}
	CHECK(peak < 2.6, "peak %g with everything loud", peak);
}

int main() {
	state_lowpass_shape();
	state_bandpass_peaks_at_cutoff();
	state_highpass_mirrors();
	state_passband_holds_with_res();
	state_res_law();
	state_self_oscillates_at_the_onset();
	state_tuned();
	state_level_matches_ladder();
	state_clean_at_five_volts();
	state_four_wide_equals_one_wide();
	state_bounded();
	return rkatest::finish("narrows/test_state");
}
