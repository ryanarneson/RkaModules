// PanCore, offline (plan Task 4): the pan laws, the CV law, smoothing and
// attach, alone and through a BallastEngine.
// Spec: docs/specs/2026-11-17-pan-design.md.
#include "testing.hpp"
#include "PanCore.hpp"
#include "ballast/BallastEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Constant loudness (pan law 1): equal power, +3 dB at the sides.
void mono_law() {
	TestPan pan;
	pan.in.panLaw = 1;
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

// Under audio-rate CV, constant loudness (law 1) holds equal power; the
// default no-boost law is pinned separately below.
void mono_equal_power_under_cv() {
	TestPan pan;
	pan.in.panLaw = 1;
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

// The default no-boost law (0) under the same 1 kHz CV (#43): mono power
// dips by design, about 3 dB at the sides against the centre, and never
// rises above it; the CV sweeps far enough to reach the full dip.
void mono_no_boost_dip_under_cv() {
	TestPan pan;
	pan.in.panLaw = 0;
	pan.in.att[0] = 1.f;
	pan.in.cv[0].channels = 1;
	pan.settle();
	double lo = 0, hi = -100;
	for (int i = 0; i < 4800; i++) {
		pan.cvv[0][0] = 5.f * std::sin(2.f * PI * 1000.f * i / FS);
		float l = 1.f, r = 1.f;
		pan.apply(0, l, r, rka::MONO);
		const double p = db((l * l + r * r) / 2.0) / 2.0;
		lo = std::min(lo, p);
		hi = std::max(hi, p);
	}
	CHECK(hi <= 0.01, "no boost under a 1 kHz pan CV: mono power rises %.3f dB above the centre", hi);
	CHECK(lo >= -3.05 && lo <= -2.9, "no boost under a 1 kHz pan CV: deepest dip %.3f dB (want about -3.01)", lo);
}

float panned(TestPan& pan, float knob, float& l, float& r, rka::BallastSource src) {
	pan.in.knob[0] = knob;
	pan.settle();
	pan.apply(0, l, r, src);
	return 0.f;
}

// True pan of a stereo source: p >= 0, theta = p pi / 2, s = sin theta:
// L' = L cos theta, R' = R (1 - (1 - k) s) + L k s; k = 1/2 with no boost
// (hard right is (L + R) / 2), 1 / sqrt 2 for constant loudness.
void true_pan_law(int law, float k) {
	TestPan pan;
	pan.in.panLaw = law;
	const float L = 0.6f, R = -0.3f;
	float l = L, r = R;
	panned(pan, 1.f, l, r, rka::STEREO);
	CHECK(std::fabs(l) < 1e-6f && std::fabs(r - k * (R + L)) < 1e-5f, "law %d true pan hard right: L %g R %g, want 0 %g",
	      law, l, r, k * (R + L));
	const float c = std::cos(PI / 4.f), s = std::sin(PI / 4.f);
	l = L, r = R;
	panned(pan, 0.75f, l, r, rka::STEREO);   // p = 0.5
	CHECK(std::fabs(l - L * c) < 1e-5f && std::fabs(r - (R * (1.f - (1.f - k) * s) + L * k * s)) < 1e-5f,
	      "law %d true pan p 0.5: L %g R %g", law, l, r);
	l = L, r = R;
	panned(pan, 0.25f, l, r, rka::STEREO);   // p = -0.5
	CHECK(std::fabs(r - R * c) < 1e-5f && std::fabs(l - (L * (1.f - (1.f - k) * s) + R * k * s)) < 1e-5f,
	      "law %d true pan p -0.5: L %g R %g", law, l, r);
}

void true_pan() {
	true_pan_law(0, 0.5f);
	true_pan_law(1, 0.70710678f);
}

// No boost (pan law 0, the default): no sample ever leaves louder than the
// louder of the channel's two sides went in, at any pan, mono or stereo,
// true pan or balance; and a mono source holds unity on its own side.
void no_boost() {
	TestPan pan;
	float l = 0.8f, r = 0.8f;
	panned(pan, 0.f, l, r, rka::MONO);
	CHECK(std::fabs(l - 0.8f) < 1e-5f && std::fabs(r) < 1e-5f, "no boost, mono hard left: L %g R %g, want 0.8 and 0", l,
	      r);
	uint32_t s = 3;
	auto next = [&]() {
		s = s * 1664525u + 1013904223u;
		return (float) (s >> 8) / 16777216.f * 2.f - 1.f;
	};
	double worst = 0;
	for (int mode = 0; mode < 2; mode++) {
		pan.in.stereoMode = mode;
		for (int i = 0; i <= 40; i++) {
			pan.in.knob[0] = i / 40.f;
			pan.settle();
			for (int k = 0; k < 200; k++) {
				const float L = next(), R = next();
				const rka::BallastSource src = k % 2 ? rka::MONO : rka::STEREO;
				float a = L, b = src == rka::MONO ? L : R;
				const float in = std::max(std::fabs(a), std::fabs(b));
				pan.postLevel(0, a, b, src);
				worst = std::max(worst, (double) std::max(std::fabs(a), std::fabs(b)) - in);
			}
		}
	}
	CHECK(worst <= 1e-5, "with no boost, a pan raised a sample by %.2e V", worst);
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
	CHECK(std::fabs(l) < 1e-5f && std::fabs(r - 1.f) < 1e-5f, "a CV step must land at once (L %g R %g, want 0 and 1)",
	      l, r);
}

void attach_glides_from_centre() {
	TestPan pan;
	pan.in.knob[0] = 1.f;
	pan.settle();
	pan.attached();   // Ballast saw it arrive
	pan.linked();
	const float first = pan.position(0);
	CHECK(first < 0.52f, "on re-attach the position starts from centre (got %.3f, knob at 1)", first);
	pan.settle();
	CHECK(std::fabs(pan.position(0) - 1.f) < 1e-3f, "after the glide the position reaches the knob (%.4f)",
	      pan.position(0));
}

// Only attached() restarts the smoothing: linking alone, however often,
// never moves a settled pan.
void no_restart_without_attach() {
	TestPan pan;
	pan.in.knob[0] = 1.f;
	pan.settle();
	float lowest = 1.f;
	for (int i = 0; i < 48000; i++) {
		pan.linked();
		lowest = std::min(lowest, pan.position(0));
	}
	CHECK(lowest == 1.f, "a settled hard-right pan dipped to %.4f without attached()", lowest);
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
	mono_no_boost_dip_under_cv();
	true_pan();
	no_boost();
	balance();
	cv_law();
	knob_smoothing();
	attach_glides_from_centre();
	no_restart_without_attach();
	poly_cv_uses_channel_1();
	return rkatest::finish("pan test_core");
}
