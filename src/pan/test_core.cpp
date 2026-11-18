// PanCore, offline (plan Task 4): the pan laws, the CV law, smoothing and
// attach, alone and through a BallastEngine.
// Spec: docs/specs/2026-11-17-pan-design.md.
#include "testing.hpp"
#include "PanCore.hpp"
#include "ballast/BallastEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

typedef rka::BallastFrame BF;

namespace {

const float FS = 48000.f;
const float PI = 3.14159265358979f;

double db(double g) {
	return 20.0 * std::log10(g);
}

// A PanCore whose controls come from a test frame.
struct TestPan : rka::PanCore {
	rka::PanFrame in;
	float cvv[4][16] = {};
	int idleValue = 0;
	TestPan() {
		for (int c = 0; c < 4; c++) {
			in.knob[c] = 0.5f;
			in.att[c] = 0.f;
			in.cv[c].v = cvv[c];
			in.cv[c].channels = 0;
		}
		in.stereoMode = 0;
		setSampleRate(FS);
	}
	void readFrame(rka::PanFrame& f) override {
		f = in;
	}
	int idle() const override {
		return idleValue;
	}
	// Settle the smoothing at the current knobs (0.5 s, 50 time constants).
	void settle() {
		for (int i = 0; i < 24000; i++)
			linked();
	}
	// One sample of channel ch through the pan stage.
	void apply(int ch, float& l, float& r, rka::BallastSource src) {
		linked();
		postLevel(ch, l, r, src);
	}
};

// A Ballast with one channel patched as given, and optionally a Pan.
struct Rig {
	rka::BallastEngine e;
	BF f;
	float inL[4][16] = {}, inR[4][16] = {}, cv[4][16] = {}, mcv[16] = {}, chL[16] = {}, chR[16] = {};
	rka::BallastResult out;
	Rig() {
		e.setSampleRate(FS);
		for (int p = 0; p < BF::PARAMS_LEN; p++)
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
	}
};

void centre_is_identity() {
	for (int mode = 0; mode < 2; mode++) {
		Rig a, b;
		TestPan pan;
		pan.in.stereoMode = mode;
		// Channel 1 mono via IN L, 2 mono via IN R, 3 stereo, 4 unpatched.
		for (Rig* g : {&a, &b}) {
			g->f.inL[0].channels = 1;
			g->f.inR[1].channels = 1;
			g->f.inL[2].channels = g->f.inR[2].channels = 1;
		}
		rka::BallastExpander* chain[1] = {&pan};
		uint32_t s = 7;
		bool same = true;
		for (int i = 0; i < (int) (2 * FS); i++) {
			for (Rig* g : {&a, &b})
				for (int c = 0; c < 3; c++) {
					s = s * 1664525u + 1013904223u;
					const float x = (float) (s >> 8) / 16777216.f - 0.5f;
					g->inL[c][0] = x;
					g->inR[c][0] = -0.7f * x;
				}
			for (int c = 0; c < 3; c++) {
				b.inL[c][0] = a.inL[c][0];
				b.inR[c][0] = a.inR[c][0];
			}
			a.e.process(a.f, a.out);
			pan.linked();
			b.e.process(b.f, b.out, chain, 1);
			same = same && a.out.l == b.out.l && a.out.r == b.out.r;
		}
		CHECK(same, "Pan at centre (%s) must leave Ballast bit-identical", mode ? "balance" : "true pan");
	}
}

void mono_law() {
	TestPan pan;
	const float x = 0.8f;
	for (float knob : {0.f, 1.f}) {
		pan.in.knob[0] = knob;
		pan.settle();
		float l = x, r = x;
		pan.apply(0, l, r, rka::MONO);
		const float wantL = knob == 0.f ? std::sqrt(2.f) * x : 0.f, wantR = knob == 0.f ? 0.f : std::sqrt(2.f) * x;
		CHECK(std::fabs(l - wantL) < 1e-5f && std::fabs(r - wantR) < 1e-5f, "mono hard %s: L %g R %g, want %g %g",
		      knob == 0.f ? "left" : "right", l, r, wantL, wantR);
	}
	double worst = 0;
	for (int i = 0; i <= 40; i++) {
		pan.in.knob[0] = i / 40.f;
		pan.settle();
		float l = x, r = x;
		pan.apply(0, l, r, rka::MONO);
		worst = std::max(worst, std::fabs(db((l * l + r * r) / (2.0 * x * x)) / 2.0));
	}
	CHECK(worst <= 0.05, "mono pan power moves by %.3f dB across the knob (want equal power, 0.05)", worst);
}

void mono_equal_power_under_cv() {
	TestPan pan;
	pan.in.att[0] = 1.f;
	pan.in.cv[0].channels = 1;
	pan.settle();
	double worst = 0;
	for (int i = 0; i < 4800; i++) {
		pan.cvv[0][0] = 5.f * std::sin(2.f * PI * 1000.f * i / FS);
		float l = 1.f, r = 1.f;
		pan.apply(0, l, r, rka::MONO);
		worst = std::max(worst, std::fabs(db((l * l + r * r) / 2.0) / 2.0));
	}
	CHECK(worst <= 0.1, "a 1 kHz pan CV moves mono power by %.3f dB (want under 0.1)", worst);
}

float panned(TestPan& pan, float knob, float& l, float& r, rka::BallastSource src) {
	pan.in.knob[0] = knob;
	pan.settle();
	pan.apply(0, l, r, src);
	return 0.f;
}

void true_pan() {
	TestPan pan;
	const float L = 0.6f, R = -0.3f;
	float l = L, r = R;
	panned(pan, 1.f, l, r, rka::STEREO);
	CHECK(std::fabs(l) < 1e-6f && std::fabs(r - (R + L)) < 1e-5f, "true pan hard right: L %g R %g, want 0 and %g", l, r,
	      R + L);
	l = L, r = R;
	panned(pan, 0.75f, l, r, rka::STEREO);   // p = 0.5
	const float c = std::cos(PI / 4.f), s = std::sin(PI / 4.f);
	CHECK(std::fabs(l - L * c) < 1e-5f && std::fabs(r - (R + L * s)) < 1e-5f, "true pan p 0.5: L %g R %g, want %g %g",
	      l, r, L * c, R + L * s);
	l = L, r = R;
	panned(pan, 0.25f, l, r, rka::STEREO);   // p = -0.5
	CHECK(std::fabs(r - R * c) < 1e-5f && std::fabs(l - (L + R * s)) < 1e-5f, "true pan p -0.5: L %g R %g, want %g %g",
	      l, r, L + R * s, R * c);
}

void balance() {
	TestPan pan;
	pan.in.stereoMode = 1;
	const float L = 0.6f, R = -0.3f;
	float l = L, r = R;
	panned(pan, 1.f, l, r, rka::STEREO);
	CHECK(std::fabs(l) < 1e-6f && r == R, "balance hard right: L %g R %g, want 0 and %g", l, r, R);
	l = L, r = R;
	panned(pan, 0.25f, l, r, rka::STEREO);   // p = -0.5
	CHECK(std::fabs(r - R * std::cos(PI / 4.f)) < 1e-5f && l == L, "balance p -0.5: L %g R %g, want %g %g", l, r, L,
	      R * std::cos(PI / 4.f));
}

void cv_law() {
	struct Case {
		float knob, att, cv, wantP;
	} cases[] = {{0.5f, 1.f, 5.f, 1.f}, {0.5f, -1.f, 5.f, -1.f}, {0.5f, 0.f, 5.f, 0.f}, {1.f, 1.f, 5.f, 1.f},
	             {0.5f, 1.f, -2.5f, -0.5f}};
	for (const Case& k : cases) {
		TestPan pan;
		pan.in.knob[0] = k.knob;
		pan.in.att[0] = k.att;
		pan.in.cv[0].channels = 1;
		pan.cvv[0][0] = k.cv;
		pan.settle();
		const float p = 2.f * pan.position(0) - 1.f;
		CHECK(std::fabs(p - k.wantP) < 1e-4f, "knob %.2f att %.1f CV %.1f V: p %g, want %g", k.knob, k.att, k.cv, p,
		      k.wantP);
	}
}

void knob_smoothing() {
	TestPan pan;
	pan.settle();
	pan.in.knob[0] = 1.f;
	float prev = pan.position(0), worst = 0.f;
	int settled = -1;
	for (int i = 0; i < 4800; i++) {
		pan.linked();
		worst = std::max(worst, std::fabs(pan.position(0) - prev));
		prev = pan.position(0);
		if (settled < 0 && std::fabs(pan.position(0) - 1.f) < 0.005f)   // 1 % of the step
			settled = i;
	}
	CHECK(worst <= 0.05f * 0.5f, "a knob step moves the position by %.4f in one sample (want <= 5 %% of the step)",
	      worst);
	CHECK(settled >= 0 && settled < 2400, "the knob takes %d samples to settle within 1 %% (want under 50 ms)", settled);
	// CV is not smoothed: a CV step lands on the same sample.
	TestPan q;
	q.in.att[0] = 1.f;
	q.in.cv[0].channels = 1;
	q.settle();
	q.cvv[0][0] = 5.f;
	float l = 1.f, r = 1.f;
	q.apply(0, l, r, rka::MONO);
	CHECK(std::fabs(l) < 1e-5f && std::fabs(r - std::sqrt(2.f)) < 1e-5f, "a CV step must land at once (L %g R %g)", l,
	      r);
}

void attach_glides_from_centre() {
	TestPan pan;
	pan.in.knob[0] = 1.f;
	pan.settle();
	pan.idleValue = 3;   // not linked for a while
	pan.linked();
	pan.idleValue = 0;
	const float first = pan.position(0);
	CHECK(first < 0.52f, "on re-attach the position starts from centre (got %.3f, knob at 1)", first);
	pan.settle();
	CHECK(std::fabs(pan.position(0) - 1.f) < 1e-3f, "after the glide the position reaches the knob (%.4f)",
	      pan.position(0));
}

void poly_cv_uses_channel_1() {
	TestPan a, b;
	for (TestPan* p : {&a, &b}) {
		p->in.att[0] = 1.f;
		p->in.cv[0].channels = 1;
		p->cvv[0][0] = 2.5f;
	}
	b.in.cv[0].channels = 3;
	b.cvv[0][1] = -5.f;
	b.cvv[0][2] = 5.f;
	a.settle();
	b.settle();
	CHECK(a.position(0) == b.position(0), "a polyphonic pan CV must use its first channel");
}

} // namespace

int main() {
	centre_is_identity();
	mono_law();
	mono_equal_power_under_cv();
	true_pan();
	balance();
	cv_law();
	knob_smoothing();
	attach_glides_from_centre();
	poly_cv_uses_channel_1();
	return rkatest::finish("pan test_core");
}
