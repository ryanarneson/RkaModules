// Unit tests for src/sway/RandomGen.hpp. Run with `make test`.
#include "RandomGen.hpp"
#include "testing.hpp"
#include <cmath>

using rka::RandomGen;

static void testValuesInRange() {
	RandomGen r(7);
	float lo = 1.f, hi = -1.f;
	for (int i = 0; i < 100000; i++) {
		r.advance();
		lo = std::fmin(lo, r.step());
		hi = std::fmax(hi, r.step());
	}
	CHECK(lo >= -1.f && hi <= 1.f, "range [%f, %f]", lo, hi);
	CHECK(lo < -0.99f && hi > 0.99f, "covers the range: [%f, %f]", lo, hi);
}

static void testNewValueEachAdvance() {
	RandomGen r(3);
	float prev = r.step();
	int repeats = 0;
	for (int i = 0; i < 1000; i++) {
		r.advance();
		repeats += r.step() == prev;
		prev = r.step();
	}
	CHECK(repeats == 0, "%d repeated values", repeats);
}

static void testSmoothContinuousAcrossWrap() {
	RandomGen r(11);
	for (int i = 0; i < 100; i++) {
		r.advance();
		const float before = r.smooth(0.9999f);
		r.advance();
		const float after = r.smooth(0.f);
		CHECK(std::fabs(before - after) < 1e-3f, "jump %f -> %f", before, after);
	}
}

static void testSmoothEndpoints() {
	RandomGen r(5);
	r.advance();
	r.advance();
	CHECK(r.smooth(0.f) == r.prev && std::fabs(r.smooth(1.f) - r.next) < 1e-6f, "glides from prev to next");
}

static void testSeedsDiffer() {
	RandomGen a(1), b(2);
	a.advance();
	b.advance();
	CHECK(a.step() != b.step(), "seeds 1 and 2 gave the same first value");
}

static void testDeterministic() {
	RandomGen a(42), b(42);
	bool same = true;
	for (int i = 0; i < 100; i++) {
		a.advance();
		b.advance();
		same = same && a.step() == b.step();
	}
	CHECK(same, "same seed, different sequence");
}

int main() {
	testValuesInRange();
	testNewValueEachAdvance();
	testSmoothContinuousAcrossWrap();
	testSmoothEndpoints();
	testSeedsDiffer();
	testDeterministic();
	return rkatest::finish("test_random");
}
