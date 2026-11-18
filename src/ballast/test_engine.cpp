// BallastEngine, offline (plan Task 2, then Task 3): the knob curve, the VCA
// law, the normals, the mix and Master, the mono out, smoothing and idle;
// then the DC blocker, the soft limit and the CLIP light.
// Spec: docs/specs/2026-11-17-ballast-design.md.
#include "testing.hpp"
#include "BallastEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

typedef rka::BallastFrame F;

namespace {

const float FS = 48000.f;

double db(double g) {
	return 20.0 * std::log10(g);
}

// One engine and the voltages behind every port. A port is patched when
// its channel count is above 0.
struct Rig {
	rka::BallastEngine e;
	F f;
	float inL[4][16] = {}, inR[4][16] = {}, cv[4][16] = {}, mcv[16] = {}, chL[16] = {}, chR[16] = {};
	rka::BallastResult out;

	Rig() {
		e.setSampleRate(FS);
		for (int p = 0; p < F::PARAMS_LEN; p++)
			f.params[p] = 0.75f;
		for (int c = 0; c < 4; c++) {
			f.inL[c].v = inL[c];
			f.inR[c].v = inR[c];
			f.cv[c].v = cv[c];
		}
		f.masterCv.v = mcv;
		f.chainL.v = chL;
		f.chainR.v = chR;
		f.outL = f.outR = true;
		f.dcBlock = false;
	}
	void step() {
		e.process(f, out);
	}
	void run(int n) {
		for (int i = 0; i < n; i++)
			step();
	}
};

// Channel 1 fed DC on IN L, both outputs patched: out.l after settling.
float dcThrough(Rig& g, float volts) {
	g.f.inL[0].channels = 1;
	g.inL[0][0] = volts;
	g.run(4800);
	return g.out.l;
}

void curve_points() {
	rka::BallastEngine e;
	CHECK(e.curve(0.f) == 0.f, "curve(0) is %g, want 0", e.curve(0.f));
	CHECK(std::fabs(db(e.curve(0.75f))) <= 0.01, "curve(0.75) is %+.3f dB, want 0", db(e.curve(0.75f)));
	CHECK(std::fabs(db(e.curve(1.f)) - 6.0206) <= 0.01, "curve(1) is %+.3f dB, want +6.02", db(e.curve(1.f)));
	bool mono = true;
	double worst = 0;
	for (int i = 1; i <= 1000; i++) {
		const float p = i / 1000.f, q = (i - 1) / 1000.f;
		mono = mono && e.curve(p) >= e.curve(q);
		const double x = rka::BallastEngine::curveExact(p);
		if (x > 1e-3)
			worst = std::max(worst, std::fabs(db(e.curve(p) / x)));
	}
	CHECK(mono, "the curve must never fall as the knob turns up");
	CHECK(worst <= 0.05, "the table is %.3f dB off 2 p^2.41 (want <= 0.05)", worst);
}

// Gain of channel 1 (or Master) for a CV, against the knob alone.
void vca_law_on(bool master) {
	const float knob = 0.6f;
	Rig ref;
	if (master)
		ref.f.params[F::MASTER] = knob;
	else
		ref.f.params[F::LEVEL_1] = knob;
	const float alone = dcThrough(ref, 1.f);
	auto withCv = [&](float volts) {
		Rig g;
		if (master) {
			g.f.params[F::MASTER] = knob;
			g.f.masterCv.channels = 1;
			g.mcv[0] = volts;
		}
		else {
			g.f.params[F::LEVEL_1] = knob;
			g.f.cv[0].channels = 1;
			g.cv[0][0] = volts;
		}
		return dcThrough(g, 1.f);
	};
	const char* who = master ? "Master" : "Level";
	const float expect = rka::BallastEngine::curveExact(knob) * (master ? rka::BallastEngine::curveExact(0.75f) : 1.f);
	CHECK(std::fabs(alone / expect - 1.f) < 2e-3, "%s: unpatched CV gives %.5f, want the knob's %.5f", who, alone, expect);
	CHECK(withCv(0.f) == 0.f && withCv(-3.f) == 0.f, "%s: 0 V and -3 V must be silent", who);
	CHECK(std::fabs(db(withCv(5.f) / alone) + 6.0206) <= 0.01, "%s: 5 V is %+.3f dB under the knob, want -6.02", who,
	      db(withCv(5.f) / alone));
	CHECK(withCv(10.f) == alone && withCv(12.f) == alone, "%s: 10 V and 12 V must give the knob's level", who);
	bool never = true;
	for (float v = -12.f; v <= 12.f; v += 0.25f)
		never = never && withCv(v) <= alone;
	CHECK(never, "%s: no CV may exceed the knob", who);
}

void vca_law() {
	vca_law_on(false);
}
void master_cv_law() {
	vca_law_on(true);
}

void cv_not_smoothed() {
	Rig g;
	g.f.inL[0].channels = 1;
	g.inL[0][0] = 1.f;
	g.f.cv[0].channels = 1;
	g.run(4800);
	const float k = rka::BallastEngine::curveExact(0.75f) * rka::BallastEngine::curveExact(0.75f);
	double worst = 0;
	for (int i = 0; i < 4800; i++) {
		const float c = 5.f + 5.f * std::sin(2.f * 3.14159265f * 1000.f * i / FS);
		g.cv[0][0] = c;
		g.step();
		worst = std::max(worst, (double) std::fabs(g.out.l - k * c / 10.f));
	}
	CHECK(worst <= 1e-5 * 50, "audio-rate CV lags or is slewed: worst error %.2e", worst);
}

void poly_cv_uses_channel_1() {
	Rig a, b;
	a.f.cv[0].channels = 1;
	a.cv[0][0] = 5.f;
	b.f.cv[0].channels = 3;
	b.cv[0][0] = 5.f;
	b.cv[0][1] = 0.f;
	b.cv[0][2] = 10.f;
	CHECK(dcThrough(a, 1.f) == dcThrough(b, 1.f), "a polyphonic CV must use its first channel");
}

void normals() {
	{
		Rig g;
		dcThrough(g, 1.f);
		CHECK(g.out.l == g.out.r && g.out.l > 0.f, "IN R unpatched must take IN L (L %g, R %g)", g.out.l, g.out.r);
	}
	{
		Rig g;
		g.f.inR[0].channels = 1;
		g.inR[0][0] = 1.f;
		g.run(4800);
		CHECK(g.out.l == 0.f && g.out.r > 0.f, "IN R alone: L must be silent (L %g, R %g)", g.out.l, g.out.r);
	}
	{
		Rig g;
		g.f.chainL.channels = 1;
		g.chL[0] = 1.f;
		g.run(4800);
		CHECK(g.out.l == g.out.r && g.out.l > 0.f, "CHAIN R unpatched must take CHAIN L (L %g, R %g)", g.out.l, g.out.r);
	}
}

void poly_inputs_summed() {
	Rig a, b;
	a.f.inL[0].channels = 16;
	for (int c = 0; c < 16; c++)
		a.inL[0][c] = 0.1f;
	b.f.inL[0].channels = 1;
	b.inL[0][0] = 1.6f;
	a.run(4800);
	b.run(4800);
	CHECK(std::fabs(a.out.l - b.out.l) < 1e-5f, "16 x 0.1 V must read as 1.6 V (%g against %g)", a.out.l, b.out.l);
	Rig c;
	c.f.chainL.channels = 4;
	for (int k = 0; k < 4; k++)
		c.chL[k] = 0.25f;
	c.run(4800);
	CHECK(std::fabs(c.out.l - rka::BallastEngine::curveExact(0.75f)) < 1e-5f, "a polyphonic CHAIN must be summed");
}

void mix_sum() {
	Rig g;
	const float v[4] = {0.1f, 0.2f, 0.3f, 0.4f};
	for (int c = 0; c < 4; c++) {
		g.f.inL[c].channels = g.f.inR[c].channels = 1;
		g.inL[c][0] = v[c];
		g.inR[c][0] = -v[c];
	}
	g.f.chainL.channels = g.f.chainR.channels = 1;
	g.chL[0] = 0.5f;
	g.chR[0] = -0.5f;
	g.f.params[F::LEVEL_2] = 1.f;
	g.f.params[F::MASTER] = 0.5f;
	g.run(4800);
	const float two = rka::BallastEngine::curveExact(1.f), m = rka::BallastEngine::curveExact(0.5f);
	const float want = (0.1f + 0.2f * two + 0.3f + 0.4f + 0.5f) * m;
	CHECK(std::fabs(g.out.l - want) < 1e-4f && std::fabs(g.out.r + want) < 1e-4f,
	      "the mix is L %g R %g, want +-%g (channels, CHAIN, then Master)", g.out.l, g.out.r, want);
}

void mono_normal() {
	Rig g;
	g.f.inL[0].channels = g.f.inR[0].channels = 1;
	g.inL[0][0] = 1.f;
	g.inR[0][0] = 0.5f;
	g.f.outR = false;
	g.run(4800);
	CHECK(std::fabs(g.out.l - 1.5f * 0.70710678f) < 1e-4f, "R unpatched: L is %g, want (L + R) / sqrt 2 = %g", g.out.l,
	      1.5f * 0.70710678f);
	g.f.outR = true;
	g.run(4800);
	CHECK(std::fabs(g.out.l - 1.f) < 1e-4f && std::fabs(g.out.r - 0.5f) < 1e-4f, "R patched: L %g R %g, want 1 and 0.5",
	      g.out.l, g.out.r);
}

void mono_normal_switch_no_click() {
	Rig g;
	g.f.inL[0].channels = g.f.inR[0].channels = 1;
	auto sine = [&](int i) {
		g.inL[0][0] = std::sin(2.f * 3.14159265f * 100.f * i / FS);
		g.inR[0][0] = -0.5f * g.inL[0][0];
	};
	double steady = 0, atSwitch = 0;
	float prev = 0.f;
	for (int i = 0; i < 24000; i++) {
		sine(i);
		if (i == 12000)
			g.f.outR = false;
		g.step();
		const double d = std::fabs(g.out.l - prev);
		if (i > 100 && i < 12000)
			steady = std::max(steady, d);
		if (i >= 12000 && i < 12000 + 960)
			atSwitch = std::max(atSwitch, d);
		prev = g.out.l;
	}
	CHECK(atSwitch <= 2 * steady + 0.01, "patching R steps L by %.4f V (steady %.4f)", atSwitch, steady);
}

void knob_smoothing() {
	{
		Rig g;
		g.f.params[F::LEVEL_1] = 0.3f;
		g.f.inL[0].channels = 1;
		g.inL[0][0] = 1.f;
		g.step();
		const float want = rka::BallastEngine::curveExact(0.3f) * rka::BallastEngine::curveExact(0.75f);
		CHECK(std::fabs(g.out.l - want) < 2e-3f * want + 1e-6f, "first sample %g, want the knob's %g (no fade-in)",
		      g.out.l, want);
	}
	Rig g;
	const float in = 1.f;
	dcThrough(g, in);
	g.f.params[F::LEVEL_1] = 0.f;
	float prev = g.out.l, worst = 0.f;
	int settled = -1;
	for (int i = 0; i < 9600; i++) {
		g.step();
		worst = std::max(worst, std::fabs(g.out.l - prev));
		prev = g.out.l;
		if (settled < 0 && std::fabs(g.out.l) < 1e-3f * in)
			settled = i;
	}
	CHECK(worst <= 0.05f * in, "a Level step jumps the output by %.3f V (want <= 5 %% of the input)", worst);
	CHECK(settled >= 0 && settled < 4800, "Level to 0 takes %d samples to reach -60 dB (want under 100 ms)", settled);
}

void nothing_patched_no_work() {
	Rig g;
	g.f.outL = g.f.outR = false;
	g.f.inL[0].channels = 1;
	g.inL[0][0] = 1.f;
	const long before = g.e.work();
	g.run(1000);
	CHECK(g.e.work() == before && g.out.l == 0.f && g.out.r == 0.f, "nothing patched: %ld samples of work, out %g",
	      g.e.work() - before, g.out.l);
}

} // namespace

int main() {
	curve_points();
	vca_law();
	master_cv_law();
	cv_not_smoothed();
	poly_cv_uses_channel_1();
	normals();
	poly_inputs_summed();
	mix_sum();
	mono_normal();
	mono_normal_switch_no_click();
	knob_smoothing();
	nothing_patched_no_work();
	return rkatest::finish("ballast test_engine");
}
