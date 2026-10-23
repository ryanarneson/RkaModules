// Unit tests for src/sway/Shaper.hpp. Run with `make test`.
#include "Shaper.hpp"
#include "Waves.hpp"
#include "testing.hpp"
#include <cmath>
#include <initializer_list>

using namespace rka;

static const float PW = 0.3f;

static void testPureShapes() {
	for (int i = 0; i < 100; i++) {
		const float p = i / 100.f;
		CHECK(std::fabs(shape(p, 0.5f, 0.f, 0.f, PW, 0.f) - waveSin(p)) < 1e-5f, "sin at p=%f", p);
		CHECK(std::fabs(shape(p, 0.5f, 1.f / 3, 0.f, PW, 0.f) - waveTri(p)) < 1e-5f, "tri at p=%f", p);
		CHECK(std::fabs(shape(p, 0.5f, 2.f / 3, 0.f, PW, 0.f) - waveSaw(p)) < 1e-5f, "saw at p=%f", p);
		CHECK(std::fabs(shape(p, 0.5f, 1.f, 0.f, PW, 0.f) - waveSqr(p, PW)) < 1e-5f, "sqr at p=%f", p);
	}
}

static void testMorphCrossfades() {
	const float p = 0.1f;
	const float mid = shape(p, 0.5f, 0.5f, 0.f, PW, 0.f);
	CHECK(std::fabs(mid - 0.5f * (waveTri(p) + waveSaw(p))) < 1e-5f, "morph 0.5 is half tri, half saw: %f", mid);
}

static void testSkewIdentity() {
	for (int i = 0; i < 1000; i++) {
		const float p = i / 1000.f;
		CHECK(std::fabs(skewPhase(p, 0.5f) - p) < 1e-6f, "skew 0.5 moved p=%f to %f", p, skewPhase(p, 0.5f));
	}
}

// Tri measured from its trough (p = 0.75): `s` in [from, to] of the cycle.
static bool triMonotonic(float skew, float from, float to, bool rising) {
	float prev = shape(std::fmod(0.75f + from, 1.f), skew, 1.f / 3, 0.f, PW, 0.f);
	for (int i = 1; i <= 1000; i++) {
		const float s = from + (to - from) * i / 1000.f;
		const float v = shape(std::fmod(0.75f + s, 1.f), skew, 1.f / 3, 0.f, PW, 0.f);
		if (rising ? v < prev - 1e-6f : v > prev + 1e-6f)
			return false;
		prev = v;
	}
	return true;
}

static void testSkewRamps() {
	CHECK(triMonotonic(1.f, 0.002f, 0.99f, true), "skew 1 is a rising ramp");
	CHECK(triMonotonic(0.f, 0.01f, 0.998f, false), "skew 0 is a falling ramp");
	CHECK(std::fabs(shape(0.75f + 0.2f, 0.2f, 1.f / 3, 0.f, PW, 0.f) - 1.f) < 1e-4f, "skew 0.2 peaks 0.2 of a cycle after the trough");
}

static void testSkewExtremesFinite() {
	for (float skew : {0.f, 1.f})
		for (float morph : {0.f, 1.f / 3, 0.5f, 2.f / 3, 1.f})
			for (int i = 0; i < 1000; i++) {
				const float v = shape(i / 1000.f, skew, morph, 0.f, PW, 0.01f);
				CHECK(std::isfinite(v) && v >= -1.001f && v <= 1.001f, "skew %f morph %f p %f -> %f", skew, morph, i / 1000.f, v);
			}
}

static void testSmoothZeroIdentity() {
	for (int i = 0; i < 100; i++) {
		const float p = i / 100.f;
		const float expected = 0.8f * waveTri(p) + 0.2f * waveSaw(p);  // morph 0.4 = 1.2 segments
		CHECK(std::fabs(shape(p, 0.5f, 0.4f, 0.f, PW, 0.f) - expected) < 1e-5f, "p=%f", p);
	}
}

static void testRoundDiffersAndStaysInRange() {
	float maxDiff = 0.f;
	for (int i = 0; i < 1000; i++) {
		const float p = i / 1000.f;
		const float v = shape(p, 0.5f, 1.f / 3, -1.f, PW, 0.f);
		CHECK(v >= -1.f && v <= 1.f, "rounded tri %f out of range", v);
		maxDiff = std::fmax(maxDiff, std::fabs(v - waveTri(p)));
	}
	CHECK(maxDiff > 0.05f, "smooth -1 changed tri by only %f", maxDiff);
}

static void testFoldInRange() {
	for (float morph : {0.f, 1.f / 3, 2.f / 3, 1.f})
		for (int i = 0; i < 1000; i++) {
			const float v = shape(i / 1000.f, 0.5f, morph, 1.f, PW, 0.f);
			CHECK(v >= -1.f && v <= 1.f, "folded %f out of range", v);
		}
}

static void testFoldFolds() {
	int maxima = 0;
	float a = shape(0.f, 0.5f, 1.f / 3, 1.f, PW, 0.f), b = shape(0.001f, 0.5f, 1.f / 3, 1.f, PW, 0.f);
	for (int i = 2; i <= 1000; i++) {
		const float c = shape(i / 1000.f, 0.5f, 1.f / 3, 1.f, PW, 0.f);
		maxima += b > a && b >= c;
		a = b;
		b = c;
	}
	CHECK(maxima > 2, "folded tri has %d maxima per cycle", maxima);
}

static void testWrapStaysBelowOne() {
	CHECK(wrap01(-1e-9f) < 1.f, "wrap01(-1e-9) = %f", wrap01(-1e-9f));
	CHECK(wrap01(0.25f) == 0.25f && wrap01(1.25f) == 0.25f, "plain wraps");
}

int main() {
	testWrapStaysBelowOne();
	testPureShapes();
	testMorphCrossfades();
	testSkewIdentity();
	testSkewRamps();
	testSkewExtremesFinite();
	testSmoothZeroIdentity();
	testRoundDiffersAndStaysInRange();
	testFoldInRange();
	testFoldFolds();
	return rkatest::finish("test_shaper");
}
