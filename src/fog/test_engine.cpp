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
	double maxStep = 0, prev = 0;
	for (int i = 0; i < (int) (4 * RATE); i++) {
		const FogResult o = r.step(0.f);
		L.push_back(o.outL);
		if (i > 0) {
			const double d = std::fabs(o.outL - prev);
			maxStep = d > maxStep ? d : maxStep;
		}
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
double noiseLevelDb(float vis, float size, float mix, double seconds = 30.0) {
	Rig r;
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

// Full wet is level-matched to dry: for noise at Mix 1 the output RMS is
// within 1.5 dB of the input's at Visibility 0 to 1 (Size noon) and at Size 0
// and 1 (Visibility noon), the other knobs at noon.
void wetMatchedLevel() {
	const float pts[][2] = {{0.f, .5f}, {.25f, .5f}, {.5f, .5f}, {.75f, .5f}, {1.f, .5f}, {.5f, 0.f}, {.5f, 1.f}};
	for (const auto& p : pts) {
		const double db = noiseLevelDb(p[0], p[1], 1.f);
		CHECK(std::fabs(db) <= 1.5, "Visibility %g Size %g: wet RMS %+.2f dB re input, want within 1.5", p[0], p[1], db);
		std::printf("  wet_matched_level v %.2f size %.2f: %+.2f dB\n", p[0], p[1], db);
	}
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

// Waking is transparent: with Drift 0, a noise burst after sleeping gives the
// same output as the same burst on a fresh engine (48 kHz, so the divider
// has no phase: sample for sample).
void wakeTransparent() {
	Rig a, b;
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
	CHECK(peak > 0.1, "the reference engine's response is silent (peak %g)", peak);
	CHECK(worst < 2e-5, "after sleep the output differs from a fresh engine by %g V, want < 2e-5", worst);
	std::printf("  wake_transparent: sample-for-sample, worst difference %.3g V (response peak %.2f V)\n", worst, peak);
}


// Sleep at Size 0 after a loud CLEAR burst, turn Size to 1 while asleep, wake
// with a noise burst: the same output as an engine that was awake at Size 1
// and settled (48 kHz, sample for sample). Needs the hold to cover the rings
// (nothing stale to read back at the larger Size) and the Size to snap on
// wake (no swoop from Size 0).
void wakeAtNewSize() {
	Rig a, b;
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
	CHECK(peak > 0.1, "the reference response is silent (peak %g)", peak);
	CHECK(worst < 2e-5, "wake at a new Size differs from a settled engine by %g V, want < 2e-5", worst);
	std::printf("  wake_at_new_size: worst difference %.3g V (response peak %.2f V)\n", worst, peak);
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
	levelAcrossMix();
	outputCeiling();
	sleepAfterHold();
	noSleepInTail();
	sleepNoClick();
	wakeTransparent();
	wakeAtNewSize();
	return rkatest::finish("test_engine");
}
