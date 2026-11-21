// Unit tests for src/narrows/Laws.hpp: the control laws. Run with `make test`.
#include "Laws.hpp"
#include "testing.hpp"
#include <cmath>
#include <string>

using namespace rka::narrows;

static bool near(double a, double b, double rel) {
	return std::fabs(a - b) <= rel * std::fabs(b);
}

static void cutoff_law() {
	CHECK(cutoffHz(0.f) == 16.f, "got %g", cutoffHz(0.f));
	CHECK(near(cutoffHz(0.6f), 1024.0, 1e-4), "got %g", cutoffHz(0.6f));
	CHECK(near(cutoffHz(1.f), 16384.0, 1e-6), "got %g", cutoffHz(1.f));
	for (float k = 0.f; k <= 1.f; k += 0.05f)
		CHECK(std::fabs(cutoffKnob(cutoffHz(k)) - k) < 1e-5f, "round trip at %g gave %g", k, cutoffKnob(cutoffHz(k)));
	CHECK(cutoffKnob(1.f) == 0.f && cutoffKnob(1e6f) == 1.f, "the knob is held to its travel");
	// The fast law the engine uses agrees with the display's.
	for (float o = -1.f; o <= 11.f; o += 0.37f)
		CHECK(near(hzFromOctaves(o), 16.0 * std::exp2(double(o)), 1e-6), "at %g octaves: %g", o, hzFromOctaves(o));
}

static void octaves_law() {
	CHECK(octaves(0.6f, 0.f, 0.f, 0.f, 0.f, 0) == 6.f, "knob alone");
	CHECK(octaves(0.6f, 1.f, 0.f, 0.f, 0.f, 0) == 7.f, "V/OCT is 1 V per octave");
	CHECK(octaves(0.6f, 0.f, 2.f, 0.5f, 0.f, 1) == 7.f, "FM volts times the FM knob");
	CHECK(octaves(0.6f, 0.f, 2.f, -1.f, 0.f, 1) == 4.f, "FM inverted");
	CHECK(octaves(0.5f, 0.f, 0.f, 0.f, 0.f, 0) == octaves(0.5f, 0.f, 0.f, 0.f, 0.f, 1), "Spread 0: sides equal");
	const float l = octaves(0.5f, 0.f, 0.f, 0.f, 1.f, 0), r = octaves(0.5f, 0.f, 0.f, 0.f, 1.f, 1);
	CHECK(r - l == 1.f && l == 4.5f, "Spread 1: half an octave each way, got %g and %g", l, r);
}

static void clamp_law() {
	CHECK(clampHz(1.f, 48000.f) == 8.f, "floor");
	CHECK(clampHz(30000.f, 48000.f) == 20000.f, "ceiling");
	CHECK(near(clampHz(30000.f, 44100.f), 19845.0, 1e-6), "0.45 fs at 44.1 kHz, got %g", clampHz(30000.f, 44100.f));
	CHECK(clampHz(NAN, 48000.f) == 8.f, "NaN");
	CHECK(clampHz(INFINITY, 48000.f) == 20000.f && clampHz(-INFINITY, 48000.f) == 8.f, "infinities");
	CHECK(clampHz(440.f, 48000.f) == 440.f, "inside");
	// A rate so low that the floor is above its ceiling: the ceiling wins,
	// so the filter is never asked for a cutoff past 0.45 fs.
	CHECK(clampHz(100.f, 10.f) == 4.5f && clampHz(1.f, 10.f) == 4.5f && clampHz(NAN, 10.f) == 4.5f, "at 10 Hz: %g, %g",
	      clampHz(100.f, 10.f), clampHz(1.f, 10.f));
}

static void amounts() {
	CHECK(ladderFeedback(0.f) == 0.f && near(ladderFeedback(1.f), 4.6, 1e-6), "ends: %g", ladderFeedback(1.f));
	CHECK(near(ladderFeedback(0.95f), 4.0, 1e-6), "self-oscillation begins at 95 %%: %g", ladderFeedback(0.95f));
	CHECK(near(ladderFeedback(0.475f), 1.0, 1e-6), "the square below the onset: %g", ladderFeedback(0.475f));
	CHECK(near(ladderFeedback(0.975f), 4.3, 1e-6), "a straight line above it: %g", ladderFeedback(0.975f));
	CHECK(driveGain(0.f) == 1.f, "Drive 0 is unity, got %g", driveGain(0.f));
	CHECK(near(driveGain(1.f), 16.0, 1e-6) && near(driveGain(0.5f), 4.0, 1e-6), "24 dB at full");
	CHECK(combFeedback(0.f) == 0.f, "noon is exactly 0");
	CHECK(near(combFeedback(1.f), 0.95, 1e-6) && near(combFeedback(-1.f), -0.95, 1e-6), "ends");
	CHECK(combFeedback(2.f) == combFeedback(1.f) && combFeedback(-2.f) == combFeedback(-1.f), "held past the ends");
	float last = -1.f;
	for (float c = -1.f; c <= 1.f; c += 0.01f) {
		const float f = combFeedback(c);
		CHECK(f > last, "monotonic at %g", c);
		CHECK(std::fabs(f + combFeedback(-c)) < 1e-6f, "odd at %g", c);
		last = f;
	}
	CHECK(combFeedback(0.5f) > 0.6f && combFeedback(0.5f) < 0.8f, "half travel is useful: %g", combFeedback(0.5f));
}

static void mode_laws() {
	const ModeWeights a = modeWeights(0.f), b = modeWeights(0.5f), c = modeWeights(1.f), q = modeWeights(0.25f),
	                  t = modeWeights(0.75f);
	CHECK(a.lp == 1.f && a.bp == 0.f && a.hp == 0.f, "LP");
	CHECK(b.lp == 0.f && b.bp == 1.f && b.hp == 0.f, "BP");
	CHECK(c.lp == 0.f && c.bp == 0.f && c.hp == 1.f, "HP");
	CHECK(q.lp == 0.5f && q.bp == 0.5f && q.hp == 0.f, "LP/BP halfway");
	CHECK(t.lp == 0.f && t.bp == 0.5f && t.hp == 0.5f, "BP/HP halfway");
	const ModeWeights lo = modeWeights(-3.f), hi = modeWeights(7.f), nan = modeWeights(NAN);
	CHECK(lo.lp == 1.f && hi.hp == 1.f && nan.lp == 1.f, "held to the ends; NaN is LP");

	const float rows[3][5] = {{0, 0, 0, 0, 1}, {0, 0, 4, -8, 4}, {1, -4, 6, -4, 1}};
	const float at[3] = {0.f, 0.5f, 1.f};
	for (int r = 0; r < 3; r++) {
		const LadderMix m = ladderMix(at[r]);
		for (int i = 0; i < 5; i++)
			CHECK(m.w[i] == rows[r][i], "row %d weight %d is %g", r, i, m.w[i]);
		CHECK(m.lowpass == (r == 0 ? 1.f : 0.f), "low-pass weight of row %d", r);
	}
	const LadderMix m = ladderMix(0.25f);
	for (int i = 0; i < 5; i++)
		CHECK(m.w[i] == 0.5f * (rows[0][i] + rows[1][i]), "midpoint weight %d is %g", i, m.w[i]);
	CHECK(m.lowpass == 0.5f, "midpoint low-pass weight");
}

static void names() {
	CHECK(modeName(0.f) == "LP" && modeName(0.5f) == "BP" && modeName(1.f) == "HP", "pure points");
	CHECK(modeName(0.2f) == "LP 60 % / BP 40 %", "got '%s'", modeName(0.2f).c_str());
	CHECK(modeName(0.9f) == "BP 20 % / HP 80 %", "got '%s'", modeName(0.9f).c_str());
	CHECK(modeName(0.001f) == "LP", "within half a percent of a point is the point: '%s'", modeName(0.001f).c_str());
	float hz = 0.f;
	CHECK(parseHz("440", hz) && hz == 440.f, "plain");
	CHECK(parseHz("440 Hz", hz) && hz == 440.f, "with Hz");
	CHECK(parseHz(" 2.5 k", hz) && hz == 2500.f, "with k");
	CHECK(parseHz("2.5kHz", hz) && hz == 2500.f, "with kHz");
	CHECK(parseHz("1K", hz) && hz == 1000.f, "capital K");
	hz = 7.f;
	CHECK(!parseHz("", hz) && !parseHz("abc", hz) && !parseHz("-5", hz) && !parseHz("nan", hz) && !parseHz("12 q", hz),
	      "junk is refused");
	CHECK(hz == 7.f, "and leaves the value alone");
}

int main() {
	cutoff_law();
	octaves_law();
	clamp_law();
	amounts();
	mode_laws();
	names();
	return rkatest::finish("narrows/test_laws");
}
