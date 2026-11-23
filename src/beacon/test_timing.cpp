// Beacon's time law, typed values and stage curves (src/beacon/Timing.hpp,
// Curve.hpp), against the numbers in the plan's Global Constraints.
#include "beacon/Curve.hpp"
#include "beacon/Timing.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using namespace rka::beacon;

bool near(double a, double want, double rel) {
	return std::fabs(a / want - 1.0) <= rel;
}

void time_law() {
	CHECK(near(stageSeconds(0.f), 0.001, 1e-4), "knob 0 is %g s (want 1 ms)", stageSeconds(0.f));
	CHECK(near(stageSeconds(1.f), 60.0, 1e-4), "knob 1 is %g s (want 60 s)", stageSeconds(1.f));
	CHECK(near(stageSeconds(0.5f), 0.2449, 0.005), "noon is %g s (want 244.9 ms)", stageSeconds(0.5f));
	CHECK(near(stageSeconds(0.2093f), 0.010, 0.005), "Attack default is %g s", stageSeconds(0.2093f));
	CHECK(near(stageSeconds(0.5184f), 0.300, 0.005), "Decay default is %g s", stageSeconds(0.5184f));
	CHECK(near(stageSeconds(0.6279f), 1.0, 0.005), "Release default is %g s", stageSeconds(0.6279f));
	// Equal to 1e-6 relative (about 16 ulps), not bit for bit: with arguments
	// known at compile time GCC on arm64 folds one call exactly and fuses the
	// other's multiply-add.
	CHECK(near(stageSeconds(-3.f), stageSeconds(0.f), 1e-6) && near(stageSeconds(7.f), stageSeconds(1.f), 1e-6),
	      "the knob clamps");
	CHECK(stageSeconds(std::nanf("")) == stageSeconds(0.5f), "a NaN knob reads noon");
	for (int i = 0; i <= 100; i++) {
		const float k = i / 100.f;
		CHECK(std::fabs(stageKnob(stageSeconds(k)) - k) < 1e-4f, "round trip of %.2f is %g", k, stageKnob(stageSeconds(k)));
	}
	CHECK(stageKnob(0.f) == 0.f && stageKnob(-1.f) == 0.f && stageKnob(1e9f) == 1.f && stageKnob(std::nanf("")) == 0.f,
	      "typed times clamp");
}

void cv() {
	CHECK(near(stageSecondsCv(0.f, 10.f), 60.0, 1e-4), "+10 V from 0 is %g s", stageSecondsCv(0.f, 10.f));
	CHECK(near(stageSecondsCv(1.f, -10.f), 0.001, 1e-4), "-10 V from 1 is %g s", stageSecondsCv(1.f, -10.f));
	CHECK(stageSecondsCv(0.3f, std::nanf("")) == stageSeconds(0.3f), "NaN volts are ignored");
	CHECK(near(stageSecondsCv(0.25f, 5.f), stageSeconds(0.75f), 1e-5), "5 V is half the travel");
	CHECK(near(stageSecondsCv(0.5f, 1.f), stageSeconds(0.6f), 1e-5), "1 V is a tenth of the travel");
	CHECK(near(stageSecondsCv(0.5f, -2.f), stageSeconds(0.3f), 1e-5), "-2 V is minus a fifth");
	CHECK(near(stageSecondsCv(0.5f, 99.f), 60.0, 1e-4) && near(stageSecondsCv(0.5f, -99.f), 0.001, 1e-4), "the sum clamps");
}

void typed() {
	struct { const char* s; double want; } ok[] = {
		{"250", 0.25}, {"250 ms", 0.25}, {"250ms", 0.25}, {"1.5 s", 1.5}, {"1.5s", 1.5}, {"1.5S", 1.5}, {" 2 s ", 2.0},
	};
	for (auto& c : ok) {
		float v = -1.f;
		const bool good = parseSeconds(c.s, v);
		CHECK(good && std::fabs(v - c.want) < 1e-6, "\"%s\" parses to %g (want %g)", c.s, v, c.want);
	}
	for (const char* bad : {"abc", "", "  ", "ms", "12x", "1.5 sec"}) {
		float v = 0.f;
		CHECK(!parseSeconds(bad, v), "\"%s\" must not parse", bad);
	}
	{ float v = 0.f; CHECK(!parseSeconds(nullptr, v), "null does not parse"); }
	struct { const char* s; float knob; } edge[] = {{"0", 0.f}, {"-5", 0.f}, {"1e9", 1.f}, {"90 s", 1.f}};
	for (auto& c : edge) {
		float v = 0.f;
		CHECK(parseSeconds(c.s, v), "\"%s\" parses", c.s);
		CHECK(stageKnob(v) == c.knob, "\"%s\" clamps to knob %g (got %g)", c.s, c.knob, stageKnob(v));
	}
	// The displayed text of every 0.01 knob step reads back within one display digit.
	for (int i = 0; i <= 100; i++) {
		const float t = stageSeconds(i / 100.f);
		char buf[32];
		formatSeconds(t, buf, sizeof buf);
		float back = 0.f;
		const bool good = parseSeconds(buf, back);
		const double digit = t < 0.1f ? 1e-4 : t < 1.f ? 1e-3 : t < 10.f ? 1e-2 : 1e-1;
		CHECK(good && std::fabs(back - t) <= digit * 0.5 * 1.01, "knob %.2f: %g s shows \"%s\" reads %g", i / 100.f, t, buf, back);
		// And from 10 ms up (three digits) the shown text is within 0.5 % of
		// the time; below, one decimal of a millisecond.
		CHECK(good && (t < 0.00995f || std::fabs(back - t) <= 0.005 * t * 1.01), "knob %.2f: %g s shows \"%s\", %.2f %% off", i / 100.f, t, buf,
		      100.0 * std::fabs(back - t) / t);
	}
	char buf[32];
	formatSeconds(0.001f, buf, sizeof buf);
	CHECK(!std::strcmp(buf, "1.0 ms"), "1 ms shows \"%s\"", buf);
	formatSeconds(0.9996f, buf, sizeof buf);
	CHECK(!std::strcmp(buf, "1.00 s"), "999.6 ms shows \"%s\"", buf);
	formatSeconds(60.f, buf, sizeof buf);
	CHECK(!std::strcmp(buf, "60.0 s"), "60 s shows \"%s\"", buf);
	formatSeconds(0.3f, buf, sizeof buf);
	CHECK(!std::strcmp(buf, "300 ms"), "300 ms shows \"%s\"", buf);
	struct { float t; const char* want; } fm[] = {
		{0.0099f, "9.9 ms"}, {0.00996f, "10.0 ms"}, {0.010f, "10.0 ms"}, {0.0473f, "47.3 ms"}, {0.0999f, "99.9 ms"},
		{0.09996f, "100 ms"}, {0.1004f, "100 ms"}, {0.9994f, "999 ms"}, {0.9996f, "1.00 s"},
		{9.99f, "9.99 s"}, {9.996f, "10.0 s"}, {59.96f, "60.0 s"}, {0.f, "1.0 ms"}, {100.f, "60.0 s"},
	};
	for (auto& c : fm) {
		formatSeconds(c.t, buf, sizeof buf);
		CHECK(!std::strcmp(buf, c.want), "%g s shows \"%s\" (want \"%s\")", c.t, buf, c.want);
	}
	formatSeconds(0.5f, buf, 4);
	CHECK(std::strlen(buf) <= 3, "a short buffer is not overrun");
}

const float kShapes[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
// Odd shapes (and out-of-range ones) so rounding at the ends shows.
const float kOddShapes[] = {0.1f, 0.3f, 0.37f, 0.6f, 0.9f, 0.123f, 0.777f, -1.f, 2.f, 7.f};

void curve_ends() {
	for (float s : kShapes) {
		CHECK(curveRise(0.f, s) == 0.f && curveRise(1.f, s) == 1.f, "rise ends at shape %g: %g %g", s, curveRise(0.f, s), curveRise(1.f, s));
		CHECK(curveFall(0.f, s) == 0.f && curveFall(1.f, s) == 1.f, "fall ends at shape %g: %g %g", s, curveFall(0.f, s), curveFall(1.f, s));
	}
}

void curve_ends_odd() {
	for (float s : kOddShapes) {
		CHECK(curveRise(0.f, s) == 0.f && curveRise(1.f, s) == 1.f, "rise ends at shape %g", s);
		CHECK(curveFall(0.f, s) == 0.f && curveFall(1.f, s) == 1.f, "fall ends at shape %g", s);
		for (int i = 1; i < 100; i++) {
			const float r = curveRise(i / 100.f, s);
			CHECK(r >= 0.f && r <= 1.f, "shape %g p %d/100 leaves 0..1: %g", s, i, r);
		}
	}
	// Every shape in a fine sweep ends exactly, and matches the formula (Global Constraints).
	for (int j = 0; j <= 997; j++) {
		const float sh = j / 997.f;
		CHECK(curveRise(0.f, sh) == 0.f && curveRise(1.f, sh) == 1.f && curveFall(1.f, sh) == 1.f, "ends at shape %g", sh);
		const double a = std::fabs(2.0 * sh - 1.0);
		for (double pp : {0.1, 0.5, 0.8}) {
			const double c = sh >= 0.5 ? 1.0 - std::pow(1.0 - pp, 4) : std::pow(pp, 4);
			const double want = (1.0 - a) * pp + a * c;
			CHECK(std::fabs(curveRise(pp, sh) - want) < 1e-6 && std::fabs(curveFall(pp, sh) - want) < 1e-6, "shape %g p %g is %g (want %g)", sh, pp, curveRise(pp, sh), want);
		}
	}
	CHECK(curveRise(-1.f, 0.7f) == 0.f && curveRise(2.f, 0.7f) == 1.f && curveRise(std::nanf(""), 0.7f) == 0.f, "p clamps");
}

void curve_linear_noon() {
	for (int i = 0; i <= 20; i++) {
		const float p = i / 20.f;
		CHECK(std::fabs(curveRise(p, 0.5f) - p) < 1e-6f && std::fabs(curveFall(p, 0.5f) - p) < 1e-6f, "noon is linear at %g", p);
	}
	// A hair either side of noon is already bent.
	CHECK(curveRise(0.5f, 0.52f) > 0.5f + 1e-4f && curveRise(0.5f, 0.48f) < 0.5f - 1e-4f, "noon is the linear point, not near it");
}

void curve_bends() {
	CHECK(curveRise(0.25f, 1.f) > 0.6f, "classic rise starts fast: %g", curveRise(0.25f, 1.f));
	CHECK(curveFall(0.25f, 1.f) > 0.6f, "classic fall drops early: %g", curveFall(0.25f, 1.f));
	CHECK(curveRise(0.25f, 0.f) < 0.01f, "reverse rise starts slow: %g", curveRise(0.25f, 0.f));
	CHECK(curveFall(0.75f, 0.f) < 0.35f, "reverse fall goes late: %g", curveFall(0.75f, 0.f));
	for (float s : kShapes) {
		float r = 0.f, f = 0.f;
		for (int i = 1; i <= 1000; i++) {
			const float p = i / 1000.f;
			const float nr = curveRise(p, s), nf = curveFall(p, s);
			CHECK(nr >= r && nf >= f, "monotonic at shape %g p %g", s, p);
			r = nr;
			f = nf;
		}
	}
}

} // namespace

int main() {
	time_law();
	cv();
	typed();
	curve_ends();
	curve_ends_odd();
	curve_linear_noon();
	curve_bends();
	return rkatest::finish("beacon/test_timing");
}
