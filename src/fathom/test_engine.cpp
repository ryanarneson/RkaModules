// The optimised Fathom engine (FathomEngine.hpp) against the code it
// replaced, kept frozen in testdata/ref/ (namespace rkaref): both render the
// same frames, seconds at a time, across settings, moving CVs and parameter
// steps, and the outputs must agree. Built with the plugin's floating-point
// flags (tests.mk). Run with `make test`.
//
// Full scale is 5 V (audio is +/-5 V). What may differ: the tanh clip and
// the sub sine (FastMath.hpp, within 1.5e-7 and 2.5e-7 of libm's), and the
// drift's gains and pitch ratios (fastExp2, within 1.2e-7 relative of
// libm's exp2f and pow, measured 1.03e-7). That is a pitch error of at
// most 2e-4 cents and a level error of at most 1e-6 dB per saw, which is
// the argument that the change is inaudible: both are thousands of times
// below what an ear resolves (about 1 cent, 0.1 dB).
//
// The sample difference these bounds check is not that small, and it is not
// fixed: a pitch ratio an ulp apart makes a saw's phase creep, so the
// difference is a time shift that grows with the length of the render
// (and the resonant ladder, near self-oscillation, carries it further than
// a linear filter would). Over the 4 to 6 s renders here, measured on
// macOS arm64 and Linux x86-64: every sample within -95 dBFS, the RMS
// within -127 dBFS. Bounds: every sample within -90 dBFS (160 uV on a
// 5 V signal) and the RMS within -120 dBFS; a much longer render would
// need looser ones, though the pitch and level errors stay as above. The
// libm functions themselves differ between macOS, glibc and MinGW by such
// ulps, so the reference moves about as much between platforms.
#include "FathomEngine.hpp"
#include "testdata/ref/FathomEngine.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>

using F = rka::FathomFrame;

namespace {

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float uniform() {  // [0, 1)
		s = s * 1664525u + 1013904223u;
		return float(s >> 8) / 16777216.f;
	}
};

void defaults(F& f) {
	float* p = f.params;
	p[F::PITCH] = 0.f;
	p[F::SUB] = 0.5f;
	p[F::DETUNE] = 0.25f;
	p[F::SPREAD] = 0.6f;
	p[F::DRIFT] = 0.25f;
	p[F::CUTOFF] = std::log2(1200.f / 20.f);
	p[F::DETUNE_ATT] = p[F::DRIFT_ATT] = p[F::CUTOFF_ATT] = 0.f;
	p[F::RES] = 0.2f;
	p[F::DRIVE] = 0.25f;
	p[F::RES_ATT] = p[F::DRIVE_ATT] = 0.f;
	for (float& v : f.inputs)
		v = 0.f;
	for (bool& c : f.outputConnected)
		c = true;
	f.subOctave = 0;
	f.filterType = 0;
	f.subThroughFilter = false;
}

struct Diff {
	double peak[F::OUTPUTS_LEN] = {};
	double sumSq[F::OUTPUTS_LEN] = {};
	double sigSq[F::OUTPUTS_LEN] = {};
	long n = 0;

	void add(const float* a, const float* b) {
		for (int o = 0; o < F::OUTPUTS_LEN; o++) {
			const double d = std::fabs(double(a[o]) - double(b[o]));
			// NaN counts as the worst difference.
			peak[o] = d == d ? std::fmax(peak[o], d) : 1e30;
			sumSq[o] += d * d;
			sigSq[o] += double(b[o]) * b[o];
		}
		n++;
	}
	static double dbfs(double volts) {
		return 20.0 * std::log10(std::fmax(volts, 1e-30) / 5.0);
	}
	double peakDb(int o) const { return dbfs(peak[o]); }
	double rmsDb(int o) const { return dbfs(std::sqrt(sumSq[o] / double(n))); }
	double signalDb(int o) const { return dbfs(std::sqrt(sigSq[o] / double(n))); }
};

const char* outName[F::OUTPUTS_LEN] = {"LEFT", "RIGHT", "SUB"};

// Renders `seconds` (at the starting rate) through both engines;
// `change(f, i, rng, fs)` edits the frame, and may change the sample rate,
// before sample i.
template <typename Change>
Diff render(const char* name, float fs, double seconds, Change change, double peakBound, double rmsBound) {
	rka::FathomEngine opt;
	rkaref::FathomEngine ref;
	F f;
	defaults(f);
	rkaref::FathomFrame g;
	Lcg rng(12345);
	Diff d;
	float rate = fs;
	const long n = long(seconds * fs);
	for (long i = 0; i < n; i++) {
		change(f, i, rng, rate);
		// The reference frame: same params, inputs and menu settings.
		for (int k = 0; k < F::PARAMS_LEN; k++)
			g.params[k] = f.params[k];
		for (int k = 0; k < F::INPUTS_LEN; k++)
			g.inputs[k] = f.inputs[k];
		g.subOctave = f.subOctave;
		g.filterType = f.filterType;
		g.subThroughFilter = f.subThroughFilter;
		float a[F::OUTPUTS_LEN], b[F::OUTPUTS_LEN];
		opt.process(f, rate, a);
		ref.process(g, rate, b);
		d.add(a, b);
	}
	for (int o = 0; o < F::OUTPUTS_LEN; o++) {
		CHECK(d.peakDb(o) < peakBound && d.rmsDb(o) < rmsBound,
		      "%s %s: difference peak %.1f dBFS, RMS %.1f dBFS (bounds %.0f, %.0f)", name, outName[o], d.peakDb(o),
		      d.rmsDb(o), peakBound, rmsBound);
		// The signal must be there, or a silent pair would pass.
		CHECK(d.signalDb(o) > -40.0, "%s %s: signal only %.1f dBFS", name, outName[o], d.signalDb(o));
	}
	std::printf("%-22s peak/RMS difference dBFS: L %.1f/%.1f  R %.1f/%.1f  SUB %.1f/%.1f\n", name, d.peakDb(0), d.rmsDb(0),
	            d.peakDb(1), d.rmsDb(1), d.peakDb(2), d.rmsDb(2));
	return d;
}

const double PEAK_BOUND = -90.0, RMS_BOUND = -120.0;

// A moving CV in +/-5 V.
float cv(long i, double hz, float fs, double phase) {
	return 5.f * float(std::sin(2.0 * M_PI * (hz * double(i) / fs + phase)));
}

void testNoDrift() {
	// Knobs at defaults but drift off.
	render("defaults, no drift", 48000.f, 5.0, [](F& f, long, Lcg&, float&) { f.params[F::DRIFT] = 0.f; }, PEAK_BOUND, RMS_BOUND);
	// Every CV but V/OCT and DRIFT moving, at full attenuverters.
	render("CVs, no drift", 48000.f, 5.0, [](F& f, long i, Lcg&, float&) {
		f.params[F::DRIFT] = 0.f;
		f.params[F::DETUNE_ATT] = f.params[F::CUTOFF_ATT] = f.params[F::RES_ATT] = f.params[F::DRIVE_ATT] = 1.f;
		f.inputs[F::DETUNE_IN] = cv(i, 0.3, 48000.f, 0.0);
		f.inputs[F::CUTOFF_IN] = cv(i, 0.7, 48000.f, 0.2);
		f.inputs[F::RES_IN] = cv(i, 0.11, 48000.f, 0.4);
		f.inputs[F::DRIVE_IN] = cv(i, 0.23, 48000.f, 0.6);
	}, PEAK_BOUND, RMS_BOUND);
	// Steps of every knob and menu setting every 0.25 s, pitch included.
	// SUB and Sub through filter are drawn each time but set only at the
	// start: since issue #87 they glide over 20 ms where the reference
	// jumps (test_output checks the glide).
	render("steps, no drift", 44100.f, 6.0, [](F& f, long i, Lcg& rng, float&) {
		f.params[F::DRIFT] = 0.f;
		if (i % 11025 != 0)
			return;
		f.params[F::PITCH] = std::round(-24.f + 48.f * rng.uniform());
		const float sub = rng.uniform();
		f.params[F::DETUNE] = rng.uniform();
		f.params[F::SPREAD] = rng.uniform();
		f.params[F::CUTOFF] = 10.f * rng.uniform();
		f.params[F::RES] = rng.uniform();
		f.params[F::DRIVE] = rng.uniform();
		f.filterType = int(4.f * rng.uniform());
		f.subOctave = rng.uniform() < 0.5f ? 0 : 1;
		const bool through = rng.uniform() < 0.5f;
		if (i == 0) {
			f.params[F::SUB] = sub;
			f.subThroughFilter = through;
		}
	}, PEAK_BOUND, RMS_BOUND);
}

void testDrift() {
	render("defaults", 48000.f, 5.0, [](F&, long, Lcg&, float&) {}, PEAK_BOUND, RMS_BOUND);
	// Full detune, drift and spread, every CV moving, V/OCT a slow vibrato
	// of a few semitones.
	render("busy", 48000.f, 5.0, [](F& f, long i, Lcg&, float&) {
		f.params[F::DETUNE] = f.params[F::DRIFT] = f.params[F::SPREAD] = 1.f;
		for (int a : {F::DETUNE_ATT, F::DRIFT_ATT, F::CUTOFF_ATT, F::RES_ATT, F::DRIVE_ATT})
			f.params[a] = 0.5f;
		f.inputs[F::VOCT] = cv(i, 0.5, 48000.f, 0.1) / 20.f;
		for (int k = F::DETUNE_IN; k <= F::DRIVE_IN; k++)
			f.inputs[k] = cv(i, 0.1 * k, 48000.f, 0.13 * k);
		f.subThroughFilter = true;
	}, PEAK_BOUND, RMS_BOUND);
	// The sample rate switching between 44.1 and 96 kHz every 0.5 s (at the
	// starting rate) while drift and CVs move.
	render("44.1/96 kHz switching", 44100.f, 5.0, [](F& f, long i, Lcg&, float& fs) {
		f.params[F::DRIFT] = 1.f;
		f.params[F::CUTOFF_ATT] = f.params[F::RES_ATT] = 0.5f;
		f.inputs[F::CUTOFF_IN] = cv(i, 0.4, 44100.f, 0.0);
		f.inputs[F::RES_IN] = cv(i, 0.15, 44100.f, 0.3);
		if (i % 22050 == 0)
			fs = (i / 22050) % 2 ? 96000.f : 44100.f;
	}, PEAK_BOUND, RMS_BOUND);
	render("busy, 96 kHz, LP12", 96000.f, 4.0, [](F& f, long i, Lcg&, float&) {
		f.params[F::DRIFT] = 1.f;
		f.params[F::DRIFT_ATT] = 1.f;
		f.inputs[F::DRIFT_IN] = cv(i, 0.2, 96000.f, 0.0);
		f.inputs[F::VOCT] = cv(i, 3.0, 96000.f, 0.0) / 60.f;
		f.filterType = 1;
	}, PEAK_BOUND, RMS_BOUND);
}

// With LEFT and RIGHT unpatched only the sub runs: SUB OUT must still match
// the reference, which runs everything, through a pitch and sub-octave
// change; and when the cables return, the output stage must start on the
// settings of now (CUTOFF moved while unpatched), not glide from the old
// ones, and the body must come back finite and at the reference's level.
// laterFs: the sample rate from 2 s on (while unpatched).
void testUnpatchedBody(float laterFs) {
	rka::FathomEngine opt;
	rkaref::FathomEngine ref;
	F f;
	defaults(f);
	rkaref::FathomFrame g;
	// Closed far below the fundamental until the change, and no sub in the
	// mix to hide the body (SUB OUT ignores the SUB knob).
	f.params[F::CUTOFF] = 1.f;
	f.params[F::SUB] = 0.f;
	// Sample indices are counted at 48 kHz whatever the rate.
	const float fs = 48000.f;
	float rate = fs;
	double subPeak = 0.0, sumA = 0.0, sumB = 0.0, firstA = 0.0, firstB = 0.0;
	bool finite = true;
	for (long i = 0; i < long(6 * fs); i++) {
		const bool patched = i < long(fs) || i >= long(3 * fs);
		f.outputConnected[F::LEFT] = f.outputConnected[F::RIGHT] = patched;
		if (i == long(2 * fs)) {
			rate = laterFs;
			f.inputs[F::VOCT] = 1.f;
			f.subOctave = 1;
			f.params[F::CUTOFF] = 9.f;
			f.params[F::DRIFT] = 0.f;
		}
		for (int k = 0; k < F::PARAMS_LEN; k++)
			g.params[k] = f.params[k];
		for (int k = 0; k < F::INPUTS_LEN; k++)
			g.inputs[k] = f.inputs[k];
		g.subOctave = f.subOctave;
		float a[F::OUTPUTS_LEN], b[F::OUTPUTS_LEN];
		opt.process(f, rate, a);
		ref.process(g, rate, b);
		subPeak = std::fmax(subPeak, std::fabs(double(a[F::SUB_OUT]) - b[F::SUB_OUT]));
		finite = finite && std::isfinite(a[F::LEFT]) && std::isfinite(a[F::RIGHT]);
		if (!patched)
			CHECK(a[F::LEFT] == 0.f && a[F::RIGHT] == 0.f, "unpatched body still writes %f", a[F::LEFT]);
		// 2 ... 12 ms after re-patching, past the restart's first edge.
		if (i >= long(3.002 * fs) && i < long(3.012 * fs)) {
			firstA += double(a[F::LEFT]) * a[F::LEFT];
			firstB += double(b[F::LEFT]) * b[F::LEFT];
		}
		if (i >= long(4 * fs)) {
			sumA += double(a[F::LEFT]) * a[F::LEFT];
			sumB += double(b[F::LEFT]) * b[F::LEFT];
		}
	}
	CHECK(Diff::dbfs(subPeak) < PEAK_BOUND, "SUB OUT while the body is unpatched differs by %.1f dBFS", Diff::dbfs(subPeak));
	CHECK(finite, "body not finite after re-patching");
	const double firstDb = 10.0 * std::log10(firstA / firstB);
	CHECK(std::fabs(firstDb) < 3.0, "first 10 ms after re-patching %.2f dB from the reference", firstDb);
	const double levelDb = 10.0 * std::log10(sumA / sumB);
	// The saws paused while unpatched, so they beat against each other in
	// another phase than the reference's: levels agree only to about 1 dB.
	CHECK(std::fabs(levelDb) < 1.5, "re-patched body level %.2f dB from the reference", levelDb);
	std::printf("unpatched body, %.0f Hz later: SUB difference %.1f dBFS, re-patched level %.2f dB at once, %.2f dB later\n",
	            laterFs, Diff::dbfs(subPeak), firstDb, levelDb);
}

// Two instances run in turn each sample, as Rack runs them, the second
// with other settings: the first must give exactly what it gives alone.
void testInstancesIndependent() {
	const float fs = 48000.f;
	F a, other;
	defaults(a);
	defaults(other);
	other.params[F::DETUNE] = other.params[F::DRIFT] = 1.f;
	other.params[F::SPREAD] = 0.1f;
	other.filterType = 3;
	rka::FathomEngine solo, first, second;
	long differ = 0;
	for (long i = 0; i < long(2 * fs); i++) {
		a.inputs[F::VOCT] = other.inputs[F::CUTOFF_IN] = float(i % 1000) / 1000.f;
		other.outputConnected[F::LEFT] = other.outputConnected[F::RIGHT] = (i / 10000) % 2 == 0;
		float x[F::OUTPUTS_LEN], y[F::OUTPUTS_LEN], z[F::OUTPUTS_LEN];
		solo.process(a, fs, x);
		first.process(a, fs, y);
		second.process(other, fs, z);
		for (int o = 0; o < F::OUTPUTS_LEN; o++)
			differ += x[o] != y[o];
	}
	CHECK(differ == 0, "an instance's output depends on another instance on %ld samples", differ);
}

} // namespace

int main() {
	testNoDrift();
	testDrift();
	testUnpatchedBody(48000.f);
	// The rate changes while the body is paused.
	testUnpatchedBody(96000.f);
	testInstancesIndependent();
	return rkatest::finish("test_engine");
}
