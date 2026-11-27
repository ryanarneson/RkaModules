// Doldrums' control laws (doldrums/Laws.hpp): grain sizes, the CV rule, the
// Stretch dial and its tooltip, the lag, the comb's root and width, Tilt,
// Shift, the Mix gains, the staging gap and the RUN dip.
#include "doldrums/Laws.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

using namespace rka::doldrums;

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

bool near(double a, double want, double rel) {
	return std::fabs(a / want - 1.0) <= rel;
}

void grain_sizes() {
	const float rates[] = {44100.f, 48000.f, 88200.f, 96000.f, 192000.f, 768000.f};
	const int want[][3] = {{8192, 16384, 32768}, {8192, 16384, 32768}, {16384, 32768, 65536},
	                       {16384, 32768, 65536}, {32768, 65536, 131072}, {131072, 131072, 131072}};
	for (int i = 0; i < 6; i++)
		for (int g = 0; g < 3; g++) {
			const int got = grainFrames(GrainSize(g), rates[i]);
			CHECK(got == want[i][g], "grain %d at %g Hz is %d, want %d", g, rates[i], got, want[i][g]);
		}
	CHECK(grainFrames(GRAIN_LONG, 1e9f) == 131072, "the cap");
	CHECK(grainFrames(GRAIN_SHORT, 0.f) >= 1, "a zero rate still gives a grain");
	CHECK(grainFrames(GRAIN_SHORT, kNaN) >= 1, "a NaN rate still gives a grain");
	CHECK(tapeFrames(48000.f) == 1440000, "30 s at 48 kHz");
	CHECK(tapeFrames(44100.f) == 1323000, "30 s at 44.1 kHz");
	CHECK(tapeFrames(88200.f) == 2646000, "30 s at 88.2 kHz");
}

void cv_rule() {
	CHECK(cvValue(0.5f, 1.f, 0.f) == 0.5f, "0 V leaves the knob");
	CHECK(std::fabs(cvValue(0.5f, 1.f, 2.f) - 0.7f) < 1e-6f, "2 V at full attenuverter adds 0.2");
	CHECK(std::fabs(cvValue(0.5f, -0.5f, 2.f) - 0.4f) < 1e-6f, "negative attenuverter subtracts");
	CHECK(cvValue(0.9f, 1.f, 10.f) == 1.f, "clamped at the top");
	CHECK(cvValue(0.1f, 1.f, -10.f) == 0.f, "clamped at the bottom");
	CHECK(cvValue(0.5f, 1.f, kNaN) == 0.5f, "NaN volts read as 0 V");
	CHECK(cvValue(0.5f, 1.f, kInf) == 0.5f, "inf volts read as 0 V");
	CHECK(cvValue(0.5f, 0.f, 1e30f) == 0.5f, "huge volts with the attenuverter at 0");
	CHECK(cvValue(kNaN, 1.f, 0.f) == 0.f, "NaN knob reads 0");
	CHECK(cvValue(0.5f, kNaN, 5.f) == 0.5f, "NaN attenuverter reads 0");
}

void dial_lettered_values() {
	for (int i = 0; i < 6; i++) {
		const double stretch = 1.0 / paceFromDial(i / 6.0);
		CHECK(near(stretch, std::ldexp(1.0, i), 1e-4), "t = %d/6 gives x%g", i, stretch);
	}
}

void dial_monotone_to_the_stop() {
	double last = paceFromDial(0.0);
	for (int i = 1; i <= 1000; i++) {
		const double p = paceFromDial(i / 1000.0);
		CHECK(p <= last, "pace rose at step %d: %g after %g", i, p, last);
		last = p;
	}
	CHECK(paceFromDial(1.0) == 0.0, "t = 1 is exactly held");
	CHECK(paceFromDial(1.5) == 0.0, "t = 1.5 is exactly held");
	CHECK(paceFromDial(0.999999) > 0.0, "just under 1 still moves");
	CHECK(paceFromDial(-1.0) == 1.0, "below 0 reads 0");
	CHECK(paceFromDial(std::nan("")) == 1.0, "NaN reads 0");
}

void dial_passes_64_and_128() {
	// Continuous at 5/6, then through 64 and 128 on the way to the stop.
	CHECK(near(paceFromDial(5.0 / 6.0 + 1e-9), paceFromDial(5.0 / 6.0), 1e-6), "a step at 5/6");
	double t = 5.0 / 6.0, s = 0;
	bool got64 = false, got128 = false;
	for (int i = 0; i < 100000 && t < 1.0; i++, t += 1.0 / 600000.0) {
		s = 1.0 / paceFromDial(t);
		if (!got64 && s >= 64.0) got64 = true;
		if (!got128 && s >= 128.0) got128 = true;
	}
	CHECK(got64 && got128, "the dial reaches 64 and 128 before the stop (last x%g)", s);
	CHECK(near(1.0 / paceFromDial((5.0 + 0.2929) / 6.0), 64.0, 2e-3), "x64 at u = 0.293");
	CHECK(near(1.0 / paceFromDial(5.5 / 6.0), 128.0, 1e-6), "x128 at u = 0.5");
}

void stretch_text() {
	char b[16];
	stretchText(paceFromDial(0.5), b, sizeof b);   CHECK(!std::strcmp(b, "x8.0"), "'%s'", b);
	stretchText(1.0, b, sizeof b);                 CHECK(!std::strcmp(b, "x1.0"), "'%s'", b);
	stretchText(1.0 / 99.0, b, sizeof b);          CHECK(!std::strcmp(b, "x99.0"), "'%s'", b);
	stretchText(1.0 / 99.99, b, sizeof b);         CHECK(!std::strcmp(b, "x100"), "never x100.0: '%s'", b);
	stretchText(1.0 / 128.0, b, sizeof b);         CHECK(!std::strcmp(b, "x128"), "'%s'", b);
	stretchText(0.0, b, sizeof b);                 CHECK(!std::strcmp(b, "held"), "'%s'", b);
	stretchText(paceFromDial(1.0), b, sizeof b);   CHECK(!std::strcmp(b, "held"), "'%s'", b);
	stretchText(std::nan(""), b, sizeof b);        CHECK(!std::strcmp(b, "held"), "NaN: '%s'", b);
	char s[4];
	stretchText(1.0 / 128.0, s, sizeof s);         CHECK(std::strlen(s) < 4, "truncates inside the buffer");
	stretchText(0.5, nullptr, 0);                  // must not write
}

void lag() {
	const long long cap = 1440000;
	const int grain = 16384;
	CHECK(lagFrames(1.f, cap, grain) == 0, "position 1 is now");
	CHECK(lagFrames(0.f, cap, grain) == cap - grain, "position 0 is the oldest");
	CHECK(lagFrames(0.5f, cap, grain) == (cap - grain) / 2, "the middle");
	CHECK(lagFrames(kNaN, cap, grain) == cap - grain, "NaN reads as 0");
	CHECK(lagFrames(2.f, cap, grain) == 0 && lagFrames(-1.f, cap, grain) == cap - grain, "clamped");
	CHECK(lagFrames(0.f, 100, 200) == 0, "a grain bigger than the tape has no room");
}

void root() {
	CHECK(near(rootHz(-24.f, 0.f), 65.4064, 1e-5), "C2 is %g", rootHz(-24.f, 0.f));
	CHECK(near(rootHz(0.f, 0.f), 261.6256, 1e-5), "C4");
	CHECK(near(rootHz(-24.f, 1.f), 2 * rootHz(-24.f, 0.f), 1e-6), "+1 V doubles");
	CHECK(near(rootHz(12.f, -1.f), rootHz(0.f, 0.f), 1e-6), "12 semitones is a volt");
	const float odd[] = {kInf, -kInf, kNaN, 1e30f, -1e30f};
	for (float v : odd) {
		const float h = rootHz(-24.f, v);
		CHECK(std::isfinite(h) && h > 0.f, "volts %g give %g", v, h);
		const float h2 = rootHz(v, 0.f);
		CHECK(std::isfinite(h2) && h2 > 0.f, "semitones %g give %g", v, h2);
	}
	CHECK(near(rootHz(-24.f, kNaN), rootHz(-24.f, 0.f), 1e-6), "NaN volts read as 0 V");
}

void width_ends() {
	CHECK(near(widthCents(0.f), 5.0, 1e-5), "%g", widthCents(0.f));
	CHECK(near(widthCents(1.f), 300.0, 1e-5), "%g", widthCents(1.f));
	CHECK(std::fabs(widthCents(0.393f) - 25.f) < 0.5f, "default is %g", widthCents(0.393f));
	CHECK(widthCents(-1.f) == widthCents(0.f) && widthCents(2.f) == widthCents(1.f), "clamped");
}

void tilt() {
	CHECK(tiltDbPerOctave(0.f) == 0.f, "flat");
	CHECK(tiltDbPerOctave(1.f) == 6.f && tiltDbPerOctave(-1.f) == -6.f, "6 dB per octave at the ends");
	CHECK(std::fabs(tiltDbPerOctave(0.5f) - 3.f) < 1e-6f, "linear");
}

void shift_detent() {
	CHECK(shiftRatio(0.f) == 1.f, "0");
	CHECK(shiftRatio(0.25f) == 1.f && shiftRatio(-0.25f) == 1.f, "the detent edge");
	CHECK(shiftRatio(0.1f) == 1.f && shiftRatio(-0.2f) == 1.f, "inside");
	CHECK(shiftRatio(0.3f) > 1.f && shiftRatio(-0.3f) < 1.f, "just outside moves");
	CHECK(near(shiftRatio(12.f), 2.0, 1e-6) && near(shiftRatio(-12.f), 0.5, 1e-6), "an octave");
	CHECK(near(shiftRatio(24.f), 4.0, 1e-6), "two octaves");
	CHECK(shiftRatio(kNaN) == 1.f, "NaN is no shift");
}

void mix_equal_power() {
	float d, w;
	for (int i = 0; i <= 100; i++) {
		mixGains(i / 100.f, d, w);
		CHECK(std::fabs(d * d + w * w - 1.f) < 1e-6f, "m = %d/100: %g", i, d * d + w * w);
		CHECK(d >= 0.f && w >= 0.f, "gains are not negative");
	}
	mixGains(0.f, d, w); CHECK(d == 1.f && w == 0.f, "m = 0: %g %g", d, w);
	mixGains(1.f, d, w); CHECK(d == 0.f && w == 1.f, "m = 1: %g %g", d, w);
	mixGains(0.5f, d, w); CHECK(std::fabs(d - 0.70710678f) < 1e-6f && std::fabs(w - 0.70710678f) < 1e-6f, "halfway");
	mixGains(3.f, d, w); CHECK(d == 0.f && w == 1.f, "clamped");
}

void staging_and_dip() {
	CHECK(stepGap(48000.f) == 64, "48 kHz");
	CHECK(stepGap(44100.f) == 64, "44.1 kHz stays at the floor");
	CHECK(stepGap(96000.f) == 128, "96 kHz");
	CHECK(stepGap(192000.f) == 256, "192 kHz");
	CHECK(stepGap(kNaN) == 64, "NaN rate");
	CHECK(runDipFrames(48000.f) == 48, "1 ms at 48 kHz");
	CHECK(runDipFrames(44100.f) == 44, "1 ms at 44.1 kHz");
	CHECK(runDipFrames(96000.f) == 96, "96 kHz");
	CHECK(runDipFrames(100.f) == 1, "never under one frame");
	CHECK(runDipFrames(kNaN) == 1, "NaN rate");
}

} // namespace

int main() {
	grain_sizes();
	cv_rule();
	dial_lettered_values();
	dial_monotone_to_the_stop();
	dial_passes_64_and_128();
	stretch_text();
	lag();
	root();
	width_ends();
	tilt();
	shift_detent();
	mix_equal_power();
	staging_and_dip();
	return rkatest::finish("doldrums/test_laws");
}
