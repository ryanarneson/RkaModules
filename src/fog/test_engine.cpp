// Fog's engine: Bloom, the mix law, dry kill, CV law, normalling, low cut
// and the limiter. Expected values come from the spec (docs/specs/
// 2026-11-15-fog-design.md: Bloom, Mix, Inputs, Safety) or are measured
// here without the engine. Built with the plugin's floating-point flags
// (tests.mk).
#include "Bloom.hpp"
#include "FogEngine.hpp"
#include "test_measure.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using rka::FogEngine;
using rka::FogFrame;
using rka::FogResult;
using fogtest::rms;

const float RATE = 48000.f;
const double PI = 3.14159265358979323846;

uint32_t rng = 12345u;
float uniform() { // [-1, 1)
	rng = rng * 1664525u + 1013904223u;
	return (float) (rng >> 8) / 8388608.f - 1.f;
}

// A frame with one mono signal on L (and optionally R).
struct Rig {
	FogEngine eng;
	FogFrame f;
	float vL = 0.f, vR = 0.f;
	float cv[5] = {};
	Rig(float rate = RATE) {
		eng.setSampleRate(rate);
		f.params[FogFrame::MIX] = 0.5f;
		f.params[FogFrame::VISIBILITY] = 0.5f;
		for (int p = FogFrame::SIZE; p <= FogFrame::DRIFT; p++)
			f.params[p] = 0.5f;
		f.params[FogFrame::BLOOM] = 0.f;
		f.params[FogFrame::DRIFT] = 0.f;
		f.params[FogFrame::DAMP] = 0.f;
		f.inL.v = &vL;
		f.inL.channels = 1;
		f.inR.v = &vR;
		f.inR.channels = 0;
		for (int i = 0; i < 5; i++) {
			f.cv[i].v = &cv[i];
			f.cv[i].channels = 0;
		}
	}
	FogResult step(float l) {
		vL = l;
		FogResult r;
		eng.process(f, r);
		return r;
	}
	FogResult step(float l, float r) {
		vR = r;
		f.inR.channels = 1;
		return step(l);
	}
};

// ---- Bloom ----

double riseTime(float bloom) {
	rka::fog::Bloom b;
	b.setup(RATE);
	b.set(bloom, 0.f);
	const float target = 0.70794578f; // -3 dB
	for (int i = 0; i < (int) (30 * RATE); i++)
		if (b.process(1.f) >= target)
			return (double) i / RATE;
	return -1.0;
}

void bloomRiseTimes() {
	const double t0 = riseTime(0.f), t5 = riseTime(0.5f), t1 = riseTime(1.f);
	CHECK(t0 >= 0 && t0 < 0.05, "Bloom 0 rise %g s, want < 50 ms", t0);
	CHECK(t5 > 0.7 && t5 < 1.3, "Bloom 0.5 rise %g s, want 1 s +/- 30 %%", t5);
	CHECK(t1 > 2.8 && t1 < 5.2, "Bloom 1 rise %g s, want 4 s +/- 30 %%", t1);
	std::printf("  bloom_rise_times: 0 -> %.4f s, 0.5 -> %.3f s, 1 -> %.3f s\n", t0, t5, t1);
}

// ---- mix ----

void dryPathZeroDelay() {
	const float rates[] = {48000.f, 192000.f, 384000.f};
	for (float rate : rates) {
		Rig r(rate);
		const FogResult o = r.step(1.f);
		const double want = std::cos(PI / 4);
		CHECK(std::fabs(o.outL - want) < 1e-6, "rate %g: step sample 0 out %g, want cos(pi/4) = %g", rate, o.outL, want);
		CHECK(std::fabs(o.outR - want) < 1e-6, "rate %g: R normalled, out %g", rate, o.outR);
		std::printf("  dry_path_zero_delay %6.0f Hz: sample 0 = %.7f (want %.7f)\n", rate, o.outL, want);
	}
}

void mixLaw() {
	// Mix 0: bit-exact dry, left and right, whatever the wet does.
	{
		Rig r;
		r.f.params[FogFrame::MIX] = 0.f;
		int bad = 0;
		for (int i = 0; i < 48000; i++) {
			const float a = 5.f * uniform(), b = 5.f * uniform();
			const FogResult o = r.step(a, b);
			bad += (o.outL != a) + (o.outR != b);
		}
		CHECK(bad == 0, "Mix 0: %d samples differ from the dry input", bad);
	}
	// Mix 1: no dry.
	{
		Rig r;
		r.f.params[FogFrame::MIX] = 1.f;
		const FogResult o = r.step(1.f, 1.f);
		CHECK(o.outL == 0.f && o.outR == 0.f, "Mix 1: sample 0 is %g, %g, want 0", o.outL, o.outR);
	}
	// Mix 0.5: the dry gain on an impulse, and the wet gain against the
	// Mix 1 run of the same input.
	{
		Rig a, w;
		a.f.params[FogFrame::MIX] = 0.5f;
		w.f.params[FogFrame::MIX] = 1.f;
		const double c = std::cos(PI / 4), s = std::sin(PI / 4);
		const FogResult o0 = a.step(1.f);
		CHECK(std::fabs(o0.outL - c) < 1e-6, "Mix 0.5 dry gain %g, want %g", o0.outL, c);
		w.step(1.f);
		double worst = 0;
		for (int i = 0; i < 48000; i++) {
			const float x = 3.f * uniform();
			const FogResult o = a.step(x), wet = w.step(x);
			const double e = std::fabs(o.outL - (c * x + s * wet.outL));
			worst = e > worst ? e : worst;
		}
		CHECK(worst < 1e-5, "Mix 0.5: out vs cos x + sin wet, worst error %g", worst);
		std::printf("  mix_law: dry gain error %.2e, 0.5 mix vs cos/sin of dry and wet worst %.2e V\n", std::fabs(o0.outL - c), worst);
	}
}

void dryKillOverridesCv() {
	Rig r;
	r.f.dryKill = true;
	r.f.params[FogFrame::MIX] = 0.f;
	r.f.params[FogFrame::MIX_ATT] = 1.f;
	r.cv[4] = -5.f;
	r.f.cv[4].channels = 1;
	const FogResult o = r.step(1.f, 1.f);
	CHECK(o.outL == 0.f && o.outR == 0.f, "dry kill: sample 0 is %g, %g, want 0", o.outL, o.outR);
	// Without dry kill, the same settings give dry (Mix 0 plus -5 V clamps to 0).
	Rig q;
	q.f.params[FogFrame::MIX] = 0.f;
	q.f.params[FogFrame::MIX_ATT] = 1.f;
	q.cv[4] = -5.f;
	q.f.cv[4].channels = 1;
	const FogResult d = q.step(1.f, 1.f);
	CHECK(d.outL == 1.f, "no dry kill: want the dry sample 1, got %g", d.outL);
}

void cvLaw() {
	Rig r;
	float v = 5.f;
	r.f.cv[0].v = &v;
	r.f.cv[0].channels = 1;
	r.f.params[FogFrame::SIZE] = 0.5f;
	r.f.params[FogFrame::SIZE_ATT] = 1.f;
	const float up = FogEngine::knob(r.f, FogFrame::SIZE, 0, FogFrame::SIZE_ATT);
	r.f.params[FogFrame::SIZE_ATT] = -1.f;
	const float down = FogEngine::knob(r.f, FogFrame::SIZE, 0, FogFrame::SIZE_ATT);
	v = 0.f;
	const float zero = FogEngine::knob(r.f, FogFrame::SIZE, 0, FogFrame::SIZE_ATT);
	v = 5.f;
	r.f.params[FogFrame::SIZE_ATT] = 0.f;
	const float noAtt = FogEngine::knob(r.f, FogFrame::SIZE, 0, FogFrame::SIZE_ATT);
	v = 50.f;
	r.f.params[FogFrame::SIZE_ATT] = 1.f;
	const float hot = FogEngine::knob(r.f, FogFrame::SIZE, 0, FogFrame::SIZE_ATT);
	r.f.cv[0].channels = 0;
	const float unplugged = FogEngine::knob(r.f, FogFrame::SIZE, 0, FogFrame::SIZE_ATT);
	CHECK(up == 1.f, "+5 V att 1: %g, want 1", up);
	CHECK(down == 0.f, "+5 V att -1: %g, want 0", down);
	CHECK(zero == 0.5f, "0 V: %g, want 0.5", zero);
	CHECK(noAtt == 0.5f, "att 0: %g, want 0.5", noAtt);
	CHECK(hot == 1.f, "+50 V clamps: %g", hot);
	CHECK(unplugged == 0.5f, "unplugged CV: %g, want 0.5", unplugged);
}

void monoNormals() {
	Rig r;
	r.f.params[FogFrame::MIX] = 1.f;
	std::vector<float> L, R;
	for (int i = 0; i < 96000; i++) {
		const FogResult o = r.step(5.f * uniform()); // only IN L connected
		L.push_back(o.outL);
		R.push_back(o.outR);
	}
	const double rl = rms(L), rr = rms(R);
	CHECK(rl > 0.5 && rr > 0.5, "mono in: RMS L %g, R %g, want both well above 0", rl, rr);
	Rig d;
	d.f.params[FogFrame::MIX] = 0.f;
	const float x = 3.f;
	const FogResult o = d.step(x);
	CHECK(o.outR == x, "mono dry: R is %g, want L's %g", o.outR, x);
	std::printf("  mono_normals: wet RMS L %.3f, R %.3f\n", rl, rr);
}

void polyInputBounded() {
	Rig r;
	r.f.params[FogFrame::MIX] = 1.f;
	r.f.params[FogFrame::VISIBILITY] = 1.f;
	float vl[16], vr[16];
	r.f.inL.v = vl;
	r.f.inL.channels = 16;
	r.f.inR.v = vr;
	r.f.inR.channels = 16;
	double peak = 0;
	const int N = (int) (20 * RATE);
	for (int i = 0; i < N; i++) {
		for (int c = 0; c < 16; c++) {
			const int period = 40 + 17 * c, period2 = 53 + 11 * c;
			vl[c] = ((i % period) < period / 2) ? 5.f : -5.f;
			vr[c] = ((i % period2) < period2 / 2) ? 5.f : -5.f;
		}
		FogResult o;
		r.eng.process(r.f, o);
		const double m = std::fmax(std::fabs(o.outL), std::fabs(o.outR));
		peak = m > peak ? m : peak;
	}
	CHECK(peak <= 10.5, "16 ch +/-5 V squares, v = 1, 20 s: peak %g V, want <= 10.5", peak);
	std::printf("  poly_input_bounded: peak %.3f V\n", peak);
}

void unplugKeepsTail() {
	Rig r;
	r.f.params[FogFrame::MIX] = 1.f;
	r.f.params[FogFrame::VISIBILITY] = 0.642f;
	// Low-passed noise (one pole near 100 Hz), clipped at +/-1 V (the wet's own slope at
	// 2.5 V already reaches 0.75 V per sample while plugged, with the level-matched wet): a smooth
	// signal, so a step in the output could only come from the unplug.
	float lp = 0.f;
	double preStep = 0, preV = 0;
	// Run 2 s, then until the input is clipped at its peak: the cut falls there.
	for (int i = 0; i < (int) (2 * RATE) || (lp < 1.f && lp > -1.f); i++) {
		lp += 0.013f * (uniform() * 30.f - lp);
		const FogResult o = r.step(lp > 1.f ? 1.f : lp < -1.f ? -1.f : lp);
		if (i > (int) RATE) {
			const double d = std::fabs(o.outL - preV);
			preStep = d > preStep ? d : preStep;
		}
		preV = o.outL;
	}
	r.f.inL.channels = 0;
	std::vector<float> L;
	double maxStep = 0, prev = preV; // the step from the last plugged sample counts
	for (int i = 0; i < (int) (4 * RATE); i++) {
		const FogResult o = r.step(0.f);
		L.push_back(o.outL);
		const double d = std::fabs(o.outL - prev);
		maxStep = d > maxStep ? d : maxStep;
		prev = o.outL;
	}
	const double early = rms(L, 0, 4800), late = rms(L, L.size() - 4800, L.size());
	CHECK(maxStep < 1.0, "unplug: largest sample step %g V, want < 1.0", maxStep);
	CHECK(maxStep <= 1.1 * preStep, "unplug: step %g V exceeds the plugged-in slope %g V", maxStep, preStep);
	CHECK(early > 0.05, "unplug: tail RMS in the first 100 ms is %g, zeroed", early);
	CHECK(late < early, "unplug: the tail does not decay (%g -> %g)", early, late);
	std::printf("  unplug_keeps_tail: max step %.3f V (while plugged: %.3f), tail RMS %.3f -> %.3f\n", maxStep, preStep, early, late);
}

void rateIgnored() {
	FogEngine e;
	const float bad[] = {0.f, -48000.f, 1e6f, 768001.f, INFINITY, NAN};
	for (float b : bad) {
		e.setSampleRate(b);
		CHECK(e.hostRate == 48000.f, "rate %g was accepted (now %g)", b, e.hostRate);
	}
	e.setSampleRate(96000.f);
	e.setSampleRate(1e6f);
	CHECK(e.hostRate == 96000.f, "after 96 kHz, 1e6 changed the rate to %g", e.hostRate);
}

void mixRampNoClick() {
	// A held 5 V DC input, dry kill toggled on and off: the dry path ramps.
	Rig r;
	r.f.params[FogFrame::MIX] = 0.f;
	for (int i = 0; i < (int) (10 * RATE); i++)
		r.step(5.f);
	double maxStep = 0, prev = 5.0;
	for (int k = 0; k < 4; k++) {
		r.f.dryKill = (k & 1) == 0;
		for (int i = 0; i < 4800; i++) {
			const FogResult o = r.step(5.f);
			maxStep = std::fmax(maxStep, std::fabs(o.outL - prev));
			prev = o.outL;
		}
	}
	CHECK(maxStep <= 0.05, "dry kill toggled on 5 V DC: largest step %g V, want <= 0.05", maxStep);
	std::printf("  mix_ramp_no_click: largest step %.4f V\n", maxStep);
}

void lowCut() {
	double wet[2] = {};
	for (int cut = 0; cut < 2; cut++) {
		Rig r;
		r.f.params[FogFrame::MIX] = 1.f;
		r.f.lowCut = cut; // 0 Off, 1 50 Hz
		std::vector<float> L;
		for (int i = 0; i < (int) (4 * RATE); i++)
			L.push_back(r.step(5.f * (float) std::sin(2.0 * PI * 30.0 * i / RATE)).outL);
		wet[cut] = rms(L, (size_t) RATE, L.size());
	}
	const double db = 20.0 * std::log10(wet[1] / wet[0]);
	CHECK(db <= -6.0, "30 Hz wet with the 50 Hz cut is %g dB vs Off, want <= -6", db);
	// The dry signal is unchanged by the cut.
	Rig d;
	d.f.params[FogFrame::MIX] = 0.f;
	d.f.lowCut = 2;
	int bad = 0;
	for (int i = 0; i < 48000; i++) {
		const float x = 5.f * (float) std::sin(2.0 * PI * 30.0 * i / RATE);
		bad += d.step(x).outL != x;
	}
	CHECK(bad == 0, "low cut changed the dry signal in %d samples", bad);
	std::printf("  low_cut: 30 Hz wet %.2f dB with the 50 Hz cut vs Off\n", db);
}

// One +/-5 V drone sample: shape 0 saw, 1 square, 2 sine, 3 white noise.
float drone(int shape, double hz, int i) {
	const double ph = std::fmod(i * hz / RATE, 1.0);
	switch (shape) {
		case 0: return 5.f * (float) (2.0 * ph - 1.0);
		case 1: return ph < 0.5 ? 5.f : -5.f;
		case 2: return 5.f * (float) std::sin(2.0 * PI * ph);
		default: return 5.f * uniform();
	}
}

// Output RMS (both channels) over the last two thirds of `seconds` of +/-5 V
// noise through the engine at the given knobs (Damp, Bloom and Drift at noon
// unless set), against the input's RMS, in dB.
double noiseLevelDb(float vis, float size, float mix, double seconds = 30.0, int mode = 0) {
	Rig r;
	r.f.mode = mode; // from HAAR, a 0.1 s fade: long before the measurement
	r.f.params[FogFrame::MIX] = mix;
	r.f.params[FogFrame::VISIBILITY] = vis;
	r.f.params[FogFrame::SIZE] = size;
	r.f.params[FogFrame::DAMP] = 0.5f;
	r.f.params[FogFrame::BLOOM] = 0.5f;
	r.f.params[FogFrame::DRIFT] = 0.5f;
	std::vector<float> in, out;
	const int N = (int) (seconds * RATE);
	for (int i = 0; i < N; i++) {
		const float x = 5.f * uniform();
		const FogResult o = r.step(x);
		if (i >= N / 3) {
			in.push_back(x);
			out.push_back(o.outL);
			out.push_back(o.outR);
		}
	}
	return 20.0 * std::log10(rms(out) / rms(in));
}

// Full wet is level-matched to dry, in every mode: for noise at Mix 1 the
// output RMS is within 1.5 dB of the input's at Visibility 0 to 1 (Size noon)
// and at Size 0 and 1 (Visibility noon), and at the four corners, the other
// knobs at noon. The modes are equally loud: at Visibility 0, 0.5 and 1 (Size
// noon) each is within 1 dB of HAAR.
void wetMatchedLevel() {
	const char* names[4] = {"HAAR", "FRET", "SMOKE", "ICE"};
	const float pts[][2] = {{0.f, .5f}, {.25f, .5f}, {.5f, .5f}, {.75f, .5f}, {1.f, .5f}, {.5f, 0.f}, {.5f, 1.f},
	                        // the corners, where the Size term matters
	                        {0.f, 0.f}, {0.f, 1.f}, {1.f, 0.f}, {1.f, 1.f}};
	double dbs[4][11];
	for (int m = 0; m < 4; m++) {
		int k = 0;
		for (const auto& p : pts) {
			const double db = noiseLevelDb(p[0], p[1], 1.f, 30.0, m);
			dbs[m][k++] = db;
			CHECK(std::fabs(db) <= 1.5, "%s Visibility %g Size %g: wet RMS %+.2f dB re input, want within 1.5", names[m], p[0], p[1], db);
			std::printf("  wet_matched_level %-5s v %.2f size %.2f: %+.2f dB\n", names[m], p[0], p[1], db);
		}
		// Size does not tilt the level: Size 1 against Size 0 at Visibility
		// 0.5, 0 and 1 within 0.75 dB in HAAR (a make-up with no Size term
		// leaves ~2.4 dB). The other modes share HAAR's Size term and are
		// held only to the 1.5 dB above; their tilt is reported.
		const int lo[3] = {5, 7, 9}, hi[3] = {6, 8, 10};
		for (int i = 0; i < 3; i++) {
			const double tilt = dbs[m][hi[i]] - dbs[m][lo[i]];
			if (m == 0)
				CHECK(std::fabs(tilt) <= 0.75, "Visibility %g: Size 1 is %+.2f dB against Size 0, want within 0.75", pts[lo[i]][0], tilt);
			std::printf("  wet_matched_level %-5s v %.2f: Size 1 vs Size 0 %+.2f dB\n", names[m], pts[lo[i]][0], tilt);
		}
	}
	const int noon[3] = {0, 2, 4};
	for (int m = 1; m < 4; m++)
		for (int i : noon) {
			const double d = dbs[m][i] - dbs[0][i];
			CHECK(std::fabs(d) <= 1.0, "%s Visibility %g: %+.2f dB against HAAR, want within 1", names[m], pts[i][0], d);
			std::printf("  wet_matched_level %-5s v %.2f: %+.2f dB against HAAR\n", names[m], pts[i][0], d);
		}
}

// The make-up is smoothed: a step in Visibility or Size, with a steady tail
// present, moves the applied make-up by at most 0.5 % of unity per sample
// (the 30 ms one-pole at 48 kHz moves it by at most ~0.07 % of the step per
// sample; an unsmoothed step is 100 % in one) and settles on the new target.
void wetMakeupSmooth() {
	for (int which = 0; which < 2; which++) {
		Rig r;
		r.f.params[FogFrame::MIX] = 1.f;
		r.f.params[FogFrame::SIZE] = which ? 0.f : 0.5f;
		r.f.params[FogFrame::VISIBILITY] = which ? 0.5f : 0.f;
		for (int i = 0; i < (int) (3 * RATE); i++)
			r.step(5.f * uniform());
		if (which)
			r.f.params[FogFrame::SIZE] = 1.f;
		else
			r.f.params[FogFrame::VISIBILITY] = 1.f;
		double worst = 0;
		float prev = r.eng.makeup;
		const double jump = std::fabs(rka::FogEngine::wetMakeup(which ? 0.5f : 1.f, which ? 1.f : 0.5f, 1.f) -
		                              rka::FogEngine::wetMakeup(which ? 0.5f : 0.f, which ? 0.f : 0.5f, 1.f));
		for (int i = 0; i < (int) (1 * RATE); i++) {
			r.step(5.f * uniform());
			worst = std::fmax(worst, std::fabs(r.eng.makeup - prev));
			prev = r.eng.makeup;
		}
		const float want = rka::FogEngine::wetMakeup(which ? 0.5f : 1.f, which ? 1.f : 0.5f, 1.f);
		CHECK(worst <= 0.005, "%s step: make-up moved %g in one sample (the step is %g), want <= 0.005", which ? "Size" : "Visibility", worst, jump);
		CHECK(std::fabs(r.eng.makeup - want) < 1e-3, "%s step: make-up %g did not settle on %g", which ? "Size" : "Visibility", r.eng.makeup, want);
		std::printf("  wet_makeup_smooth %s 0->1 step (%.3f): largest per-sample move %.5f\n", which ? "Size" : "Visibility", jump, worst);
	}
}

// ---- Control steps ----

const char* STEP_MODES[4] = {"HAAR", "FRET", "SMOKE", "ICE"};

// The largest sample-to-sample step of either output over `seconds` of a
// 1 V sine at hz, continuing from sample *i.
double largestStep(Rig& r, double hz, double seconds, long* i, float* prevL, float* prevR) {
	double worst = 0;
	for (long k = 0; k < (long) (seconds * RATE); k++, (*i)++) {
		const FogResult o = r.step((float) std::sin(2.0 * PI * hz * (double) *i / RATE));
		worst = std::fmax(worst, std::fmax(std::fabs(o.outL - *prevL), std::fabs(o.outR - *prevR)));
		*prevL = o.outL;
		*prevR = o.outR;
	}
	return worst;
}

// Drift steps do not click: a 1 V sine at 220 Hz, Mix 1, Visibility noon,
// Drift noon, every mode; after 4 s, Drift 0.5 -> 0 in one sample, or the
// Drift rate switched Slow -> Very slow or back. The largest output step in
// the second after it is at most twice the largest in the second before,
// plus 0.05 V. Unsmoothed, the reads jump by up to the whole swing (96
// samples at noon, Slow; 288 Very slow) and the step is 0.5-4.5 V; with the
// swing's 40 ms one-pole it is within the steady one's spread. The rate
// itself needs no smoothing: the phase is continuous, so a new rate bends the
// read's path but never moves it (the swing's slope, at most 0.6 % of a
// sample a sample at noon, is all that changes).
void driftStepNoClick() {
	const char* what[3] = {"Drift 0.5 -> 0", "Slow -> Very slow", "Very slow -> Slow"};
	double worstRatio = 0;
	for (int m = 0; m < 4; m++)
		for (int kind = 0; kind < 3; kind++) {
			Rig* r = new Rig;
			r->f.mode = m;
			r->eng.mode = (rka::fog::Mode) m;
			r->eng.reset();
			r->f.params[FogFrame::MIX] = 1.f;
			r->f.params[FogFrame::DRIFT] = 0.5f;
			r->f.driftRate = kind == 2 ? 1 : 0;
			long i = 0;
			float pl = 0.f, pr = 0.f;
			largestStep(*r, 220.0, 3.0, &i, &pl, &pr);
			const double before = largestStep(*r, 220.0, 1.0, &i, &pl, &pr);
			if (kind == 0)
				r->f.params[FogFrame::DRIFT] = 0.f;
			else
				r->f.driftRate = kind == 1 ? 1 : 0;
			const double after = largestStep(*r, 220.0, 1.0, &i, &pl, &pr);
			delete r;
			CHECK(before > 0.005, "%s %s: no signal before the step (%g V)", STEP_MODES[m], what[kind], before);
			CHECK(after <= 2.0 * before + 0.05, "%s %s: largest step %.4f V after, %.4f V before; want <= %.4f", STEP_MODES[m], what[kind], after, before,
			      2.0 * before + 0.05);
			worstRatio = std::fmax(worstRatio, after / before);
			std::printf("  drift_step_no_click %-5s %-17s: largest step %.4f V before, %.4f V after\n", STEP_MODES[m], what[kind], before, after);
		}
	std::printf("  drift_step_no_click: worst after/before %.2f\n", worstRatio);
}

// A Visibility jump does not step the wet: a 1 V sine at 200 Hz (a period
// of 240 samples, so a 5 ms RMS window holds whole periods and reads a
// steady wet as a flat line), Mix 1, Drift 0, every mode; after 6 s at
// Visibility 0 (the network holding its tail), Visibility 1 in one sample.
// The wet's 5 ms RMS envelope moves by at most 3 dB from one window to the
// next over the second after, and the largest sample step is at most twice
// the steady one plus 0.05 V. Unsmoothed, the wet level (sqrt(1 - g^2) at
// Visibility 0, 0.985, against 0.132 at 1) steps by -17 dB in one sample and
// the make-up climbs back over 30 ms: 9-17 dB in one window. Smoothed (the
// Visibility the network's gains use follows a 30 ms one-pole, and the wet
// level ramps between its 32-sample updates), a 30 ms one-pole moves at
// most 1 - e^(-1/6) = 15 % of its gap a window, -1.3 dB for the 0.134 ratio;
// the bound leaves room for the make-up's own 30 ms climb and the tail
// building under it.
void visibilityStepNoClick() {
	double worstDb = 0;
	for (int m = 0; m < 4; m++) {
		Rig* r = new Rig;
		r->f.mode = m;
		r->eng.mode = (rka::fog::Mode) m;
		r->eng.reset();
		r->f.params[FogFrame::MIX] = 1.f;
		r->f.params[FogFrame::VISIBILITY] = 0.f;
		const int W = 240;
		const long at = (long) (6 * RATE), N = at + (long) RATE;
		std::vector<double> env;
		double acc = 0, steadyStep = 0, step = 0;
		float pl = 0.f, pr = 0.f;
		for (long i = 0; i < N; i++) {
			if (i == at)
				r->f.params[FogFrame::VISIBILITY] = 1.f;
			const FogResult o = r->step((float) std::sin(2.0 * PI * 200.0 * (double) i / RATE));
			acc += 0.5 * ((double) o.outL * o.outL + (double) o.outR * o.outR);
			if (i % W == W - 1) {
				env.push_back(std::sqrt(acc / W));
				acc = 0;
			}
			const double d = std::fmax(std::fabs(o.outL - pl), std::fabs(o.outR - pr));
			if (i >= at - (long) RATE && i < at)
				steadyStep = std::fmax(steadyStep, d);
			else if (i >= at)
				step = std::fmax(step, d);
			pl = o.outL;
			pr = o.outR;
		}
		delete r;
		const size_t k0 = (size_t) (at / W);
		double steadyDb = 0, db = 0;
		for (size_t k = k0 - 200; k < k0 + 200; k++) {
			const double d = std::fabs(20.0 * std::log10(env[k] / env[k - 1]));
			if (k < k0)
				steadyDb = std::fmax(steadyDb, d);
			else
				db = std::fmax(db, d);
		}
		CHECK(env[k0 - 1] > 0.05, "%s: no wet before the step (%g V RMS)", STEP_MODES[m], env[k0 - 1]);
		CHECK(db <= 3.0, "%s Visibility 0 -> 1: the wet's 5 ms envelope moves %.2f dB in one window (%.2f before), want <= 3", STEP_MODES[m], db, steadyDb);
		CHECK(step <= 2.0 * steadyStep + 0.05, "%s Visibility 0 -> 1: largest step %.4f V after, %.4f V before; want <= %.4f", STEP_MODES[m], step, steadyStep,
		      2.0 * steadyStep + 0.05);
		worstDb = std::fmax(worstDb, db);
		std::printf("  visibility_step_no_click %-5s 0 -> 1: envelope %.2f dB a window at most (%.2f before), largest step %.4f V (%.4f before)\n",
		            STEP_MODES[m], db, steadyDb, step, steadyStep);
	}
	std::printf("  visibility_step_no_click: worst %.2f dB a window\n", worstDb);
}

// The loudness does not dip across Mix: noise, noon knobs, Visibility 0.5.
void levelAcrossMix() {
	const float mixes[] = {0.f, .25f, .5f, .75f, 1.f};
	for (float m : mixes) {
		const double db = noiseLevelDb(.5f, .5f, m, 20.0);
		CHECK(std::fabs(db) <= 2.0, "Mix %g: output RMS %+.2f dB re input, want within 2", m, db);
		std::printf("  level_across_mix Mix %.2f: %+.2f dB\n", m, db);
	}
}

// The ceiling: nothing exceeds 10 V, for hot (+/-5 V) saw, square, sine and
// noise through the longest, brightest, most resonant setting. Reports how
// often the limiter engages (output above the 9 V knee).
void outputCeiling() {
	const char* names[] = {"saw", "square", "sine", "noise"};
	for (int shape = 0; shape < 4; shape++) {
		Rig r;
		r.f.params[FogFrame::MIX] = 1.f;
		r.f.params[FogFrame::VISIBILITY] = 1.f;
		r.f.params[FogFrame::SIZE] = 1.f;
		r.f.params[FogFrame::DAMP] = 0.f;
		r.f.params[FogFrame::BLOOM] = 1.f;
		r.f.params[FogFrame::DRIFT] = 0.5f;
		double peak = 0;
		long over = 0, n = 0;
		const int N = (int) (30 * RATE);
		for (int i = 0; i < N; i++) {
			const FogResult o = r.step(drone(shape, 110.0, i));
			const double a = std::fmax(std::fabs(o.outL), std::fabs(o.outR));
			peak = std::fmax(peak, a);
			over += a > FogEngine::limitKnee();
			n++;
		}
		CHECK(peak <= FogEngine::limitCeil(), "%s: output peak %g V exceeds %g V", names[shape], peak, FogEngine::limitCeil());
		std::printf("  output_ceiling %-6s: peak %.3f V, over the knee %.2f%% of samples\n", names[shape], peak, 100.0 * over / n);
	}
	// And it does act on a hot one: a 16x saw.
	Rig hot;
	hot.f.params[FogFrame::MIX] = 1.f;
	double hp = 0;
	for (int i = 0; i < (int) (10 * RATE); i++) {
		const FogResult o = hot.step(16.f * drone(0, 110.0, i));
		hp = std::fmax(hp, std::fmax(std::fabs(o.outL), std::fabs(o.outR)));
	}
	CHECK(hp <= 10.f && hp > 8.f, "hot drone limited to %g V, want in (8, 10]", hp);
	std::printf("  output_ceiling: 80 V saw limited to %.3f V\n", hp);
}

// ---- The leveller ----

// An engine starts in mode m at once (no fade from HAAR).
void startIn(Rig& r, int m) {
	r.f.mode = m;
	r.eng.mode = (rka::fog::Mode) m;
	r.eng.reset();
}

const char* MODE_NAMES[4] = {"HAAR", "FRET", "SMOKE", "ICE"};

const char* SHAPE_NAMES[3] = {"saw", "square", "sine"};

// A rig at the drone conditions: Damp 0, Bloom and Drift at noon, in mode m
// from the start.
void droneRig(Rig& r, int m, float vis, float size, float mix) {
	startIn(r, m);
	r.f.params[FogFrame::VISIBILITY] = vis;
	r.f.params[FogFrame::SIZE] = size;
	r.f.params[FogFrame::DAMP] = 0.f;
	r.f.params[FogFrame::BLOOM] = 0.5f;
	r.f.params[FogFrame::DRIFT] = 0.5f;
	r.f.params[FogFrame::MIX] = mix;
}

// Drones are turned down, not clipped: +/-5 V saw, square and sine at 55,
// 110 and 220 Hz, every mode, Visibility 0.75 and 1, Size 0.5 and 1, Damp 0,
// Mix 1, 10 s. After the first 0.5 s, at most 1 % of samples are over the
// limiter's 9 V knee (without the leveller: up to 51 %), and the peak is at
// most 10 V. The same drones at Mix noon (the output worked out from the
// same wet by the equal-power law): at most 1 % of output samples over 9 V
// (without the leveller: up to 40 %). The share is of the whole run, and the
// onset's overshoot is most of it, so a shorter run reads higher (7 s:
// 1.09 %). The wet's spikes (the
// network's answer to a bright drone's edges, sharpest at Damp 0) still reach
// the limiter now and then, and at Mix noon a spike on top of a +/-5 V
// square's dry can reach 10.6 V: the largest peaks are reported.
void levellerTurnsDronesDown() {
	const int N = (int) (10 * RATE), from = (int) (0.5 * RATE);
	double worstShare = 0, worstPeak = 0, worstMixShare = 0, worstMixPeak = 0, worstPre = 0;
	char where[96] = "", whereMix[96] = "";
	for (int m = 0; m < 4; m++)
		for (float vis : {0.75f, 1.f})
			for (float size : {0.5f, 1.f})
				for (int shape = 0; shape < 3; shape++)
					for (double hz : {55.0, 110.0, 220.0}) {
						Rig* a = new Rig; // large: on the heap
						droneRig(*a, m, vis, size, 1.f);
						long over = 0, overMix = 0;
						double peak = 0, peakMix = 0, pre = 0;
						for (int i = 0; i < N; i++) {
							const float x = drone(shape, hz, i);
							const FogResult o = a->step(x);
							// Mix noon from the same wet: mix_law shows the output there
							// is cos(pi/4) dry + sin(pi/4) wet to 1e-5 V.
							const float h = 0.70710678f;
							const FogResult q = {h * x + h * o.outL, h * x + h * o.outR};
							if (i < from)
								continue;
							const double s = std::fmax(std::fabs(o.outL), std::fabs(o.outR));
							const double t = std::fmax(std::fabs(q.outL), std::fabs(q.outR));
							over += s > FogEngine::limitKnee();
							overMix += t > 9.0;
							peak = std::fmax(peak, s);
							peakMix = std::fmax(peakMix, t);
							// The peak before the limiter, from the knee's inverse.
							if (s > FogEngine::limitKnee()) {
								const double room = FogEngine::limitCeil() - FogEngine::limitKnee();
								const double u = (s - FogEngine::limitKnee()) / room;
								pre = std::fmax(pre, u < 0.999999 ? FogEngine::limitKnee() + room * std::atanh(u) : 99.0);
							}
						}
						const double share = 100.0 * over / (N - from), shareMix = 100.0 * overMix / (N - from);
						CHECK(share <= 1.0 && peak <= FogEngine::limitCeil(), "%s %s %g Hz v %g Size %g: %.2f%% of samples over 9 V, peak %.2f V; want <= 1%%, <= 10 V",
						      MODE_NAMES[m], SHAPE_NAMES[shape], hz, vis, size, share, peak);
						CHECK(shareMix <= 1.0, "%s %s %g Hz v %g Size %g, Mix 0.5: %.2f%% of output samples over 9 V (peak %.2f V), want <= 1%%",
						      MODE_NAMES[m], SHAPE_NAMES[shape], hz, vis, size, shareMix, peakMix);
						if (share > worstShare) {
							worstShare = share;
							std::snprintf(where, sizeof where, "%s %s %g Hz v %g Size %g", MODE_NAMES[m], SHAPE_NAMES[shape], hz, vis, size);
						}
						if (shareMix > worstMixShare) {
							worstMixShare = shareMix;
							std::snprintf(whereMix, sizeof whereMix, "%s %s %g Hz v %g Size %g", MODE_NAMES[m], SHAPE_NAMES[shape], hz, vis, size);
						}
						worstPeak = std::fmax(worstPeak, peak);
						worstMixPeak = std::fmax(worstMixPeak, peakMix);
						worstPre = std::fmax(worstPre, pre);
						delete a;
					}
	std::printf("  leveller_turns_drones_down: 144 drones, Mix 1: worst %.2f%% over 9 V (%s), peak %.2f V (%s%.1f V before the limiter)\n", worstShare,
	            where, worstPeak, worstPre >= 99.0 ? "over " : "", worstPre >= 99.0 ? 20.0 : worstPre);
	std::printf("  leveller_turns_drones_down: Mix 0.5: worst %.3f%% over 9 V (%s), peak %.2f V\n", worstMixShare, whereMix, worstMixPeak);
}

// No pumping: on a steady drone (Drift 0, so the network is time-invariant
// and its wet is periodic), once the network's build-up is over, the
// leveller's gain moves by under 1 dB peak to peak in every 1 s window:
// Size 0.5, Damp 0, 30 s, windows from 20 s, on the 15 drones (of saws,
// squares and sines at 55, 110 and 220 Hz, every mode, Visibility 0.75 and
// 1) that the leveller turns down by more than 3 dB.
//
// A regression guard, not a pumping test: with Drift at noon the drone's
// wet itself wanders by several dB a second (the moving lines sweep the
// network's resonances across its harmonics), and the gain follows that
// wander. It is not pumping: with the limiter off, the output's 100 ms RMS
// spread is the same with and without the leveller (measured in review). A saw and a square at
// 110 Hz, Visibility 1, windows from 10 s, are held to 6.5 dB (measured 5.2;
// 8.0 with no slow follower, 7.2 with its release at 30 ms).
// The worst 1 s spread from fromS; *early gets the worst from 2 s.
double gainSpreadDb(int m, int shape, double hz, float vis, float drift, double fromS, double seconds, float* minGain, double* early) {
	Rig* r = new Rig;
	droneRig(*r, m, vis, 0.5f, 1.f);
	r->f.params[FogFrame::DRIFT] = drift;
	const int N = (int) (seconds * RATE), from = (int) (fromS * RATE), win = (int) RATE;
	double worst = 0, worstEarly = 0;
	float lo = 1.f, hi = 0.f, gmin = 1.f, elo = 1.f, ehi = 0.f;
	const int two = (int) (2 * RATE);
	for (int i = 0; i < N; i++) {
		r->step(drone(shape, hz, i));
		if (i >= two) {
			const float g = r->eng.levelGain;
			elo = g < elo ? g : elo;
			ehi = g > ehi ? g : ehi;
			if ((i - two) % win == win - 1) {
				worstEarly = std::fmax(worstEarly, 20.0 * std::log10(ehi / elo));
				elo = 1.f;
				ehi = 0.f;
			}
		}
		if (i < from)
			continue;
		const float g = r->eng.levelGain;
		lo = g < lo ? g : lo;
		hi = g > hi ? g : hi;
		gmin = g < gmin ? g : gmin;
		if ((i - from) % win == win - 1) {
			worst = std::fmax(worst, 20.0 * std::log10(hi / lo));
			lo = 1.f;
			hi = 0.f;
		}
	}
	delete r;
	*minGain = gmin;
	*early = worstEarly;
	return worst;
}

void levellerNoPumping() {
	// The steady drones (Drift 0) the leveller turns down by over 3 dB, from
	// the full set (saw, square, sine at 55, 110, 220 Hz, Visibility 0.75 and
	// 1, every mode; the rest build up less without Drift): each is checked
	// to be engaged, so a change that idles one fails here. Since the low
	// band (Network.hpp) none is ICE: its 55 Hz drones had built up on one
	// side, and now stay under the leveller's threshold on both.
	struct Case {
		int m, shape;
		double hz;
		float vis;
	};
	const Case cases[] = {{0, 0, 55, 0.75f},  {0, 0, 55, 1.f},    {0, 0, 110, 0.75f}, {0, 0, 110, 1.f},   {0, 1, 55, 0.75f},
	                      {0, 1, 55, 1.f},    {0, 1, 110, 0.75f}, {0, 1, 110, 1.f},   {0, 2, 110, 0.75f}, {0, 2, 110, 1.f},
	                      {1, 0, 220, 0.75f}, {1, 0, 220, 1.f},   {1, 1, 55, 0.75f},  {1, 1, 55, 1.f},    {1, 1, 110, 0.75f},
	                      {1, 1, 220, 0.75f}, {2, 1, 110, 0.75f}};
	double worst = 0, worstEarly = 0, worstNoon = 0, worstNoonEarly = 0;
	char where[96] = "", whereNoon[96] = "";
	for (const Case& c : cases) {
		float g = 1.f;
		double e = 0;
		const double d = gainSpreadDb(c.m, c.shape, c.hz, c.vis, 0.f, 20.0, 30.0, &g, &e);
		CHECK(g < 0.708f, "%s %s %g Hz v %g Drift 0: the leveller is idle (gain %.3f), the test means nothing", MODE_NAMES[c.m], SHAPE_NAMES[c.shape], c.hz,
		      c.vis, g);
		CHECK(d < 1.0, "%s %s %g Hz v %g Drift 0: leveller gain moves %.2f dB in 1 s, want < 1", MODE_NAMES[c.m], SHAPE_NAMES[c.shape], c.hz, c.vis, d);
		worstEarly = std::fmax(worstEarly, e);
		if (d > worst) {
			worst = d;
			std::snprintf(where, sizeof where, "%s %s %g Hz v %g, gain %.1f dB", MODE_NAMES[c.m], SHAPE_NAMES[c.shape], c.hz, c.vis, 20.0 * std::log10(g));
		}
	}
	// Drift noon: a regression guard on how the gain follows the network's own
	// wander (not pumping: see above).
	for (int m = 0; m < 4; m++)
		for (int shape = 0; shape < 2; shape++) {
			float gn = 1.f;
			double e = 0;
			const double dn = gainSpreadDb(m, shape, 110.0, 1.f, 0.5f, 10.0, 20.0, &gn, &e);
			worstNoonEarly = std::fmax(worstNoonEarly, e);
			CHECK(dn <= 6.5, "%s %s 110 Hz v 1 Drift noon (regression guard): leveller gain follows the wander by %.2f dB in 1 s, want <= 6.5", MODE_NAMES[m],
			      SHAPE_NAMES[shape], dn);
			if (dn > worstNoon) {
				worstNoon = dn;
				std::snprintf(whereNoon, sizeof whereNoon, "%s %s 110 Hz v 1", MODE_NAMES[m], SHAPE_NAMES[shape]);
			}
		}
	std::printf("  leveller_no_pumping: Drift 0, settled (20-30 s), %d drones turned down over 3 dB: worst %.2f dB in 1 s (%s); from 2 s, in the build-up: %.2f dB\n",
	            (int) (sizeof cases / sizeof cases[0]), worst, where, worstEarly);
	std::printf("  leveller_no_pumping: Drift noon (saw, square 110 Hz, v 1), 10-20 s, the wander followed (regression guard): worst %.2f dB in 1 s (%s); from 2 s: %.2f dB\n",
	            worstNoon, whereNoon, worstNoonEarly);
}

// Transparent when quiet: a +/-1 V sine (55, 110, 220 Hz) or +/-1 V noise at
// Visibility 0.5, the other knobs at noon, Mix 1, every mode, Size 0.5 and 1:
// the gain is exactly 1 at every sample.
void levellerIdleWhenQuiet() {
	float lowest = 1.f;
	for (int m = 0; m < 4; m++)
		for (float size : {0.5f, 1.f})
			for (int shape : {2, 3})
				for (double hz : {55.0, 110.0, 220.0}) {
					if (shape == 3 && hz != 110.0)
						continue; // noise: once
					Rig* r = new Rig;
					startIn(*r, m);
					r->f.params[FogFrame::MIX] = 1.f;
					r->f.params[FogFrame::SIZE] = size;
					r->f.params[FogFrame::DAMP] = 0.5f;
					r->f.params[FogFrame::BLOOM] = 0.5f;
					r->f.params[FogFrame::DRIFT] = 0.5f;
					float g = 1.f;
					for (int i = 0; i < (int) (10 * RATE); i++) {
						r->step(0.2f * drone(shape, hz, i));
						g = r->eng.levelGain < g ? r->eng.levelGain : g;
					}
					CHECK(g == 1.f, "%s Size %g, +/-1 V %s %g Hz: leveller gain fell to %.6f, want 1", MODE_NAMES[m], size, shape == 3 ? "noise" : "sine", hz, g);
					lowest = g < lowest ? g : lowest;
					delete r;
				}
	std::printf("  leveller_idle_when_quiet: lowest gain %.6f\n", lowest);
}

// The tail decays as it would without the leveller: a +/-5 V saw at 110 Hz
// held 15 s at Visibility 1, Size 0.5, the other knobs at noon, Mix 1, then
// stopped. In every mode, the output in 1 s windows from 1 to 13 s after the
// stop, against the same run with the leveller off, stays within 1.5 dB of
// that ratio in the first second after the stop (where the gain has stopped
// moving: the leveller holds it while the input is absent). 1.5 dB is the spread the two runs' own beating gives (the
// held gain lands on a slightly different point of the wander each time, up
// to 1.3 dB measured); a gain that releases at its 3 s while the tail falls
// lifts the tail back up as fast as it decays, +4 to +7 dB by 12 s. The
// attack is never held: a tail that peaks above the held drone is turned
// down like any other peak, and since the low band (Network.hpp) SMOKE's
// does: its sustained 110 Hz no longer piles up on one side (the louder
// side's peak, which the leveller follows, fell from 18.7 to about 14 V),
// and its first repeats after the stop reach about 16 V. So for SMOKE the
// gain must never rise from 1 s after the stop, and its step down (the window's mean
// gain against the first second's) is taken out before the 1.5 dB bound. And FRET: from 1 to 2 s after
// the stop the gain never rises (it is not released; it may still fall by
// the attack, under 0.01 dB here); a new note (a 1 V sine, 220 Hz) then
// releases it again, at its own 3 s: at least 2 dB within 6 s (measured
// 3.5; it cannot rise faster than the old tail under it falls, which keeps
// the 5 ms follower up).
double windowDb(const std::vector<float>& x, size_t a, size_t b) {
	return 20.0 * std::log10(rms(x, a, b) + 1e-12);
}

void levellerHoldsInTail() {
	const int hold = (int) (15 * RATE), after = (int) (14 * RATE), sec = (int) RATE;
	double worst = 0;
	char where[64] = "";
	for (int m = 0; m < 4; m++) {
		std::vector<float> out[2], gain;
		for (int pass = 0; pass < 2; pass++) {
			Rig* r = new Rig;
			startIn(*r, m);
			r->f.params[FogFrame::VISIBILITY] = 1.f;
			r->f.params[FogFrame::SIZE] = 0.5f;
			r->f.params[FogFrame::DAMP] = 0.5f;
			r->f.params[FogFrame::BLOOM] = 0.5f;
			r->f.params[FogFrame::DRIFT] = 0.5f;
			r->f.params[FogFrame::MIX] = 1.f;
			r->eng.levellerEnabled = pass == 0;
			for (int i = 0; i < hold + after; i++) {
				const FogResult o = r->step(i < hold ? drone(0, 110.0, i) : 0.f);
				if (i >= hold) {
					out[pass].push_back(o.outL);
					if (pass == 0)
						gain.push_back(r->eng.levelGain);
				}
			}
			delete r;
		}
		const double ref = windowDb(out[0], 0, sec) - windowDb(out[1], 0, sec);
		// SMOKE: from 1 s after the stop (the feed's follower has let go by
		// 0.3 s) the gain never rises, and what it fell by (window mean
		// against the first second's) is taken out of d.
		const bool smoke = m == rka::fog::SMOKE;
		float gTop = 0.f;
		for (size_t i = sec; i < gain.size(); i++)
			gTop = gain[i] > gTop ? gain[i] : gTop;
		if (smoke)
			CHECK(gTop <= gain[sec], "SMOKE: the leveller's gain rose in the tail, %.4f at 1 s after the stop, up to %.4f", gain[sec], gTop);
		auto meanGainDb = [&](size_t a, size_t b) {
			double sum = 0;
			for (size_t i = a; i < b; i++)
				sum += gain[i];
			return 20.0 * std::log10(sum / (double) (b - a));
		};
		const double g0 = meanGainDb(0, sec);
		for (int k = 1; k < 13; k++) {
			const double step = smoke ? meanGainDb(k * sec, (k + 1) * sec) - g0 : 0.0;
			const double d = windowDb(out[0], k * sec, (k + 1) * sec) - windowDb(out[1], k * sec, (k + 1) * sec) - ref - step;
			CHECK(std::fabs(d) <= 1.5, "%s: %d-%d s after the stop, the levelled tail is %+.2f dB against the unlevelled one%s, want within 1.5", MODE_NAMES[m],
			      k, k + 1, d, smoke ? " (less the gain's step down)" : "");
			if (smoke && k == 12)
				std::printf("  leveller_holds_in_tail: SMOKE's gain steps down %.2f dB in the tail (its repeats peak over the held drone), never up\n", step);
			if (std::fabs(d) > std::fabs(worst)) {
				worst = d;
				std::snprintf(where, sizeof where, "%s, %d-%d s", MODE_NAMES[m], k, k + 1);
			}
		}
	}
	// A new note in the tail releases the gain.
	Rig* r = new Rig;
	startIn(*r, rka::fog::FRET);
	r->f.params[FogFrame::VISIBILITY] = 1.f;
	r->f.params[FogFrame::DAMP] = 0.5f;
	r->f.params[FogFrame::BLOOM] = 0.5f;
	r->f.params[FogFrame::DRIFT] = 0.5f;
	r->f.params[FogFrame::MIX] = 1.f;
	float first = 0.f, top = 0.f;
	for (int i = 0; i < hold + 2 * sec; i++) {
		r->step(i < hold ? drone(0, 110.0, i) : 0.f);
		if (i == hold + sec)
			first = r->eng.levelGain;
		if (i >= hold + sec)
			top = r->eng.levelGain > top ? r->eng.levelGain : top;
	}
	const float held = r->eng.levelGain;
	for (int i = 0; i < 6 * sec; i++)
		r->step(0.2f * drone(2, 220.0, i));
	const float later = r->eng.levelGain;
	delete r;
	const double rise = 20.0 * std::log10(later / held);
	CHECK(held < 0.6f && top == first, "in the tail: gain %.4f at 1 s after the stop, up to %.4f by 2 s, want held: never above (and under 0.6)", first, top);
	CHECK(rise >= 2.0, "a new note in the tail: gain %.2f dB held, %+.2f dB 6 s later, want it to rise >= 2 dB", 20.0 * std::log10(held), rise);
	std::printf("  leveller_holds_in_tail: worst %+.2f dB against the unlevelled tail (%s); a new note releases it %+.1f dB in 6 s (from %.1f dB)\n", worst,
	            where, rise, 20.0 * std::log10(held));
}

// The leveller starts clean: after a loud drone has pulled its gain down,
// reset(), setSampleRate() and a mode change each leave it at gain 1 with
// both followers at 0 (at the mode change: the moment the network is
// switched, under the faded-out wet), and so does waking from sleep (its
// state, made stale on purpose while asleep, is cleared by the wake).
void levellerResets() {
	auto loud = [](Rig& r) {
		droneRig(r, 0, 1.f, 0.5f, 1.f);
		for (int i = 0; i < (int) (8 * RATE); i++)
			r.step(drone(1, 220.0, i));
		return r.eng.levelGain;
	};
	auto clear = [](const FogEngine& e) { return e.levelGain == 1.f && e.levelEnv == 0.f && e.levelSlow == 0.f && e.levelFeed == 0.f; };
	{
		Rig* r = new Rig;
		const float g = loud(*r);
		r->eng.reset();
		CHECK(g < 0.7f && clear(r->eng), "reset(): gain before %g (want < 0.7), after %g, followers %g %g", g, r->eng.levelGain, r->eng.levelEnv,
		      r->eng.levelSlow);
		delete r;
	}
	{
		Rig* r = new Rig;
		const float g = loud(*r);
		r->eng.setSampleRate(96000.f);
		CHECK(g < 0.7f && clear(r->eng), "setSampleRate(): gain before %g, after %g, followers %g %g", g, r->eng.levelGain, r->eng.levelEnv,
		      r->eng.levelSlow);
		delete r;
	}
	{
		Rig* r = new Rig;
		const float g = loud(*r);
		r->f.mode = rka::fog::FRET;
		bool seen = false, ok = false;
		for (int i = 0; i < (int) (0.2 * RATE) && !seen; i++) {
			const int before = r->eng.fadeState;
			FogResult o;
			r->vL = drone(1, 220.0, i);
			r->eng.process(r->f, o);
			if (before == 1 && r->eng.fadeState == 2) {
				seen = true;
				// The switch's wake() cleared it; this sample's wet (the new,
				// empty network's) has gone through it since.
				ok = r->eng.levelGain == 1.f && r->eng.levelSlow < 1e-3f;
			}
		}
		CHECK(g < 0.7f && seen && ok, "mode change: gain before %g, switch seen %d, after it gain %g, slow follower %g", g, (int) seen, r->eng.levelGain,
		      r->eng.levelSlow);
		delete r;
	}
	{
		Rig* r = new Rig;
		r->f.params[FogFrame::MIX] = 1.f;
		int guard = 0;
		while (!r->eng.asleep() && guard++ < (int) (30 * RATE))
			r->step(0.f);
		CHECK(r->eng.asleep(), "never slept");
		r->eng.levelEnv = r->eng.levelSlow = 30.f; // stale, and held (the feed's follower is 0)
		r->eng.levelGain = 0.2f;
		r->step(1.f);
		CHECK(!r->eng.asleep() && r->eng.levelGain == 1.f && r->eng.levelSlow < 1.f,
		      "wake: gain %g, followers %g %g, want gain 1 and clear followers", r->eng.levelGain, r->eng.levelEnv, r->eng.levelSlow);
		delete r;
	}
	std::printf("  leveller_resets: clean after reset(), setSampleRate(), a mode change and a wake\n");
}

// ---- Sleep ----

// Silence puts the engine to sleep after exactly its hold (at least 0.25 s),
// and not a sample before.
void sleepAfterHold() {
	Rig r;
	r.f.params[FogFrame::MIX] = 1.f;
	int k = 0;
	bool early = false;
	while (!r.eng.asleep() && k < (int) (30 * RATE)) {
		r.step(0.f);
		k++;
		if (!r.eng.asleep() && k >= r.eng.hold)
			early = true;
	}
	CHECK(r.eng.asleep(), "silence for %d samples did not put the engine to sleep", k);
	CHECK(!early, "awake at or past the hold (%d samples)", r.eng.hold);
	CHECK(k == r.eng.hold, "slept after %d samples, want the hold %d", k, r.eng.hold);
	CHECK(k >= (int) (0.25 * RATE), "slept after %d samples, under 0.25 s", k);
	// Asleep: the wet is exactly 0, and a quiet input stays asleep.
	const FogResult o = r.step(0.f);
	CHECK(o.outL == 0.f && o.outR == 0.f, "asleep output %g, %g, want 0", o.outL, o.outR);
	CHECK(r.eng.asleep(), "woke on silence");
	// A sample well under QUIET does not wake it; 1 V (far over, whatever the
	// low cut and Bloom pass) does.
	r.step(0.1f * FogEngine::quiet());
	CHECK(r.eng.asleep(), "woke on a feed under QUIET");
	r.step(1.f);
	CHECK(!r.eng.asleep(), "did not wake on a feed of 1 V");
	std::printf("  sleep_after_hold: slept after %d samples (%.3f s)\n", k, k / RATE);
}

struct SleepRun {
	int sleepAt = -1, lastLoud = -1;
	double maxStepAtSleep = 0;
	double hold = 0, longest = 0;
};

// Feeds `burst` samples of +/-5 V noise (or one 5 V impulse when impulse is
// true), then silence, up to `total` samples; notes when it slept and the
// last output at or above the wet's QUIET.
SleepRun runToSleep(float vis, bool impulse, int burst, int total) {
	Rig r;
	r.f.params[FogFrame::MIX] = 1.f;
	r.f.params[FogFrame::VISIBILITY] = vis;
	r.f.params[FogFrame::SIZE] = 1.f;
	SleepRun s;
	std::vector<float> out, outR;
	for (int i = 0; i < total && s.sleepAt < 0; i++) {
		float x = 0.f;
		if (impulse)
			x = i == 0 ? 5.f : 0.f;
		else if (i < burst)
			x = 5.f * uniform();
		const FogResult o = r.step(x);
		out.push_back(o.outL);
		outR.push_back(o.outR);
		const float q = FogEngine::quiet();
		if (i >= 1 && (std::fabs(o.outL) >= q || std::fabs(o.outR) >= q) && i >= burst)
			s.lastLoud = i;
		if (r.eng.asleep())
			s.sleepAt = i;
	}
	s.hold = r.eng.hold;
	s.longest = r.eng.net.ringSamples() * (float) r.eng.divider.factor();
	// Steps around the sleep transition.
	if (s.sleepAt > 2)
		for (int i = s.sleepAt - 2; i <= s.sleepAt; i++) {
			double d = std::fabs(out[i] - out[i - 1]);
			const double dr = std::fabs(outR[i] - outR[i - 1]);
			d = dr > d ? dr : d;
			s.maxStepAtSleep = d > s.maxStepAtSleep ? d : s.maxStepAtSleep;
		}
	return s;
}

// It does not fall asleep while the tail is audible: DENSE after a 1 s noise
// burst, and CLEAR after one impulse, both at Size 1.
void noSleepInTail() {
	const int total = (int) (400 * RATE);
	const SleepRun d = runToSleep(1.f, false, (int) RATE, total);
	CHECK(d.sleepAt > 0, "DENSE Size 1: never slept in 400 s");
	CHECK(d.sleepAt > d.lastLoud, "DENSE Size 1: slept at %d, but the wet was audible at %d", d.sleepAt, d.lastLoud);
	CHECK(d.sleepAt >= d.lastLoud + (int) d.hold, "DENSE Size 1: slept %d samples after the last audible one, want >= the hold %g",
		d.sleepAt - d.lastLoud, d.hold);
	// The hold is the longer of 0.25 s and twice the longest ring.
	CHECK(d.hold >= 2.0 * d.longest - 1.0 && d.hold > 0.25 * RATE + 1, "Size 1: hold %g samples, want >= 2 x the longest ring (%g) and over 0.25 s", d.hold,
		d.longest);
	const SleepRun c = runToSleep(0.f, true, 0, total);
	CHECK(c.sleepAt > 0, "CLEAR Size 1: never slept in 400 s");
	CHECK(c.sleepAt > c.lastLoud, "CLEAR Size 1: slept at %d, but the wet was audible at %d", c.sleepAt, c.lastLoud);
	CHECK(c.sleepAt >= c.lastLoud + (int) c.hold, "CLEAR Size 1: slept %d samples after the last audible one, want >= the hold %g",
		c.sleepAt - c.lastLoud, c.hold);
	std::printf("  no_sleep_in_tail: DENSE slept at %.2f s (last audible %.2f s, hold %.2f s); CLEAR at %.2f s (last %.2f s, hold %.2f s)\n",
		d.sleepAt / RATE, d.lastLoud / RATE, d.hold / RATE, c.sleepAt / RATE, c.lastLoud / RATE, c.hold / RATE);
}

// No click when it falls asleep: the step in the output there stays under
// 2 QUIET (Mix 1; the sleep tests the wet after the make-up).
void sleepNoClick() {
	const SleepRun d = runToSleep(1.f, false, (int) RATE, (int) (400 * RATE));
	const double lim = 2.0 * FogEngine::quiet();
	CHECK(d.sleepAt > 2, "never slept");
	CHECK(d.maxStepAtSleep < lim, "step at the sleep transition %g V, want < %g", d.maxStepAtSleep, lim);
	std::printf("  sleep_no_click: step at sleep %.3g V (limit %.3g)\n", d.maxStepAtSleep, lim);
}

// Waking is transparent, in every mode: with Drift 0, a noise burst after
// sleeping gives the same output as the same burst on a fresh engine (48 kHz,
// so the divider has no phase: sample for sample).
void wakeTransparent(int m) {
	Rig a, b;
	startIn(a, m);
	startIn(b, m);
	for (Rig* r : {&a, &b}) {
		r->f.params[FogFrame::MIX] = 1.f;
		r->f.params[FogFrame::DRIFT] = 0.f;
		r->f.params[FogFrame::BLOOM] = 0.f;
	}
	// A: noise, then silence until asleep.
	for (int i = 0; i < (int) (0.5 * RATE); i++)
		a.step(3.f * uniform());
	int guard = 0;
	while (!a.eng.asleep() && guard++ < (int) (400 * RATE))
		a.step(0.f);
	CHECK(a.eng.asleep(), "A never slept");
	// Then a 0.2 s noise burst and a tail, the same on both. Mix 1, so the
	// output is the wet alone (Bloom 0 still passes only a few mV of the
	// first sample, so a burst, not an impulse, shows a lost or late start).
	double worst = 0, peak = 0;
	for (int i = 0; i < (int) (3 * RATE); i++) {
		const float x = i < (int) (0.2 * RATE) ? 3.f * uniform() : 0.f;
		const FogResult oa = a.step(x), ob = b.step(x);
		double d = std::fabs(oa.outL - ob.outL);
		const double dr = std::fabs(oa.outR - ob.outR);
		d = dr > d ? dr : d;
		worst = d > worst ? d : worst;
		peak = std::fabs(ob.outL) > peak ? std::fabs(ob.outL) : peak;
	}
	CHECK(peak > 0.1, "%s: the reference engine's response is silent (peak %g)", MODE_NAMES[m], peak);
	CHECK(worst < 2e-5, "%s: after sleep the output differs from a fresh engine by %g V, want < 2e-5", MODE_NAMES[m], worst);
	std::printf("  wake_transparent %-5s: sample-for-sample, worst difference %.3g V (response peak %.2f V)\n", MODE_NAMES[m], worst, peak);
}


// Sleep at Size 0 after a loud CLEAR burst, turn Size to 1 while asleep, wake
// with a noise burst: the same output as an engine that was awake at Size 1
// and settled (48 kHz, sample for sample). Needs the hold to cover the rings
// (nothing stale to read back at the larger Size) and the Size to snap on
// wake (no swoop from Size 0). In every mode: SMOKE's allpasses and ICE's
// shifter are ring memory too.
void wakeAtNewSize(int m) {
	Rig a, b;
	startIn(a, m);
	startIn(b, m);
	for (Rig* r : {&a, &b}) {
		r->f.params[FogFrame::MIX] = 1.f;
		r->f.params[FogFrame::DRIFT] = 0.f;
		r->f.params[FogFrame::BLOOM] = 0.f;
		r->f.params[FogFrame::VISIBILITY] = 0.f;
	}
	a.f.params[FogFrame::SIZE] = 0.f;
	b.f.params[FogFrame::SIZE] = 1.f;
	for (int i = 0; i < (int) (1.0 * RATE); i++)
		a.step(5.f * uniform());
	int guard = 0;
	while (!a.eng.asleep() && guard++ < (int) (400 * RATE))
		a.step(0.f);
	CHECK(a.eng.asleep(), "A never slept");
	a.f.params[FogFrame::SIZE] = 1.f;
	// Asleep for 5 s more: Bloom (which keeps running) forgets the burst, so
	// the two engines differ only in what sleep skips.
	for (int i = 0; i < (int) (5 * RATE); i++)
		a.step(0.f);
	CHECK(a.eng.asleep(), "A woke on silence");
	for (int i = 0; i < (int) (2 * RATE); i++)
		b.step(0.f); // settled at Size 1
	double worst = 0, peak = 0;
	for (int i = 0; i < (int) (3 * RATE); i++) {
		const float x = i < (int) (0.2 * RATE) ? 3.f * uniform() : 0.f;
		const FogResult oa = a.step(x), ob = b.step(x);
		double d = std::fabs(oa.outL - ob.outL);
		const double dr = std::fabs(oa.outR - ob.outR);
		d = dr > d ? dr : d;
		worst = d > worst ? d : worst;
		peak = std::fabs(ob.outL) > peak ? std::fabs(ob.outL) : peak;
	}
	CHECK(peak > 0.1, "%s: the reference response is silent (peak %g)", MODE_NAMES[m], peak);
	CHECK(worst < 2e-5, "%s: wake at a new Size differs from a settled engine by %g V, want < 2e-5", MODE_NAMES[m], worst);
	std::printf("  wake_at_new_size %-5s: worst difference %.3g V (response peak %.2f V)\n", MODE_NAMES[m], worst, peak);
}

} // namespace

int main() {
	bloomRiseTimes();
	dryPathZeroDelay();
	mixLaw();
	dryKillOverridesCv();
	cvLaw();
	monoNormals();
	polyInputBounded();
	unplugKeepsTail();
	rateIgnored();
	mixRampNoClick();
	lowCut();
	wetMatchedLevel();
	wetMakeupSmooth();
	driftStepNoClick();
	visibilityStepNoClick();
	levelAcrossMix();
	outputCeiling();
	levellerTurnsDronesDown();
	levellerNoPumping();
	levellerIdleWhenQuiet();
	levellerHoldsInTail();
	levellerResets();
	sleepAfterHold();
	noSleepInTail();
	sleepNoClick();
	for (int m = 0; m < 4; m++) {
		wakeTransparent(m);
		wakeAtNewSize(m);
	}
	return rkatest::finish("test_engine");
}
