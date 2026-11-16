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
	// 2.5 V already reaches 0.65 V per sample while plugged): a smooth
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
	CHECK(maxStep < 0.5, "unplug: largest sample step %g V, want < 0.5", maxStep);
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

void limiterIdleAt5v() {
	// True noon: every main knob at 0.5, Mix 1, HAAR. The limiter must not
	// act on any +/-5 V drone (wet peak <= 8 V, the knee), and the wet RMS
	// stays within 10 dB of the input's.
	const char* names[] = {"saw", "square", "sine", "noise"};
	const double hzs[] = {55.0, 110.0, 220.0};
	double worstPeak = 0, worstDb = 1e9;
	for (int shape = 0; shape < 4; shape++) {
		for (int h = 0; h < (shape == 3 ? 1 : 3); h++) {
			Rig r;
			r.eng.limiterEnabled = false;
			r.f.params[FogFrame::MIX] = 1.f;
			for (int p = FogFrame::VISIBILITY; p <= FogFrame::DRIFT; p++)
				r.f.params[p] = 0.5f;
			std::vector<float> in, wet;
			double peak = 0;
			const int N = (int) (15 * RATE);
			for (int i = 0; i < N; i++) {
				const float x = drone(shape, hzs[h], i);
				const FogResult o = r.step(x);
				if (i >= N / 3) {
					in.push_back(x);
					wet.push_back(o.outL);
					wet.push_back(o.outR);
					peak = std::fmax(peak, std::fmax(std::fabs(o.outL), std::fabs(o.outR)));
				}
			}
			const double db = 20.0 * std::log10(rms(wet) / rms(in));
			CHECK(peak <= FogEngine::limitKnee(), "%s %g Hz: wet peak %g V exceeds the %g V knee", names[shape], hzs[h], peak,
			      FogEngine::limitKnee());
			CHECK(db >= -10.0, "%s %g Hz: wet RMS %g dB re input, want >= -10", names[shape], hzs[h], db);
			worstPeak = std::fmax(worstPeak, peak);
			worstDb = std::fmin(worstDb, db);
			std::printf("  limiter_idle_at_5v %-6s %3.0f Hz: wet peak %5.2f V, RMS %+.2f dB re input\n", names[shape], hzs[h], peak, db);
		}
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
	std::printf("  limiter_idle_at_5v: worst peak %.2f V, lowest RMS %+.2f dB; 80 V saw limited to %.3f V\n", worstPeak, worstDb, hp);
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
	limiterIdleAt5v();
	return rkatest::finish("test_engine");
}
