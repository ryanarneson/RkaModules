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
		lo = std::fmin(lo, r.next);
		hi = std::fmax(hi, r.next);
	}
	CHECK(lo >= -1.f && hi <= 1.f, "range [%f, %f]", lo, hi);
	CHECK(lo < -0.99f && hi > 0.99f, "covers the range: [%f, %f]", lo, hi);
}

static void testNewValueEachAdvance() {
	RandomGen r(3);
	float prev = r.next;
	int repeats = 0;
	for (int i = 0; i < 1000; i++) {
		r.advance();
		repeats += r.next == prev;
		prev = r.next;
	}
	CHECK(repeats == 0, "%d repeated values", repeats);
}

static void testSmoothContinuousAcrossWrap() {
	RandomGen r(11);
	for (int i = 0; i < 100; i++) {
		r.advance();
		const float before = r.value(0.9999f, 1.f);
		r.advance();
		const float after = r.value(0.f, 1.f);
		CHECK(std::fabs(before - after) < 1e-3f, "jump %f -> %f", before, after);
	}
}

static void testSmoothEndpoints() {
	RandomGen r(5);
	r.advance();
	r.advance();
	CHECK(r.value(0.f, 1.f) == r.prev && std::fabs(r.value(1.f, 1.f) - r.next) < 1e-6f, "glides from prev to next");
}

static void testSeedsDiffer() {
	RandomGen a(1), b(2);
	a.advance();
	b.advance();
	CHECK(a.next != b.next, "seeds 1 and 2 gave the same first value");
}

static void testDeterministic() {
	RandomGen a(42), b(42);
	bool same = true;
	for (int i = 0; i < 100; i++) {
		a.advance();
		b.advance();
		same = same && a.next == b.next;
	}
	CHECK(same, "same seed, different sequence");
}

static void testGlideZeroIsStepped() {
	RandomGen r(9);
	r.advance();
	r.advance();
	bool stepped = true;
	for (int i = 0; i <= 100; i++)
		stepped = stepped && r.value(i / 100.f, 0.f) == r.next;
	CHECK(stepped, "glide 0 holds the new value all cycle");
}

static void testPartialGlide() {
	RandomGen r(13);
	r.advance();
	r.advance();
	CHECK(r.value(0.f, 0.25f) == r.prev, "starts at the previous value");
	CHECK(std::fabs(r.value(0.125f, 0.25f) - 0.5f * (r.prev + r.next)) < 1e-5f, "halfway through the glide");
	CHECK(r.value(0.25f, 0.25f) == r.next && r.value(0.6f, 0.25f) == r.next, "arrives at a quarter cycle and holds");
}

static void testFullGlideIsCosine() {
	RandomGen r(17);
	r.advance();
	r.advance();
	CHECK(std::fabs(r.value(0.5f, 1.f) - 0.5f * (r.prev + r.next)) < 1e-5f, "full glide is halfway at mid-cycle");
	CHECK(std::fabs(r.value(1.f, 1.f) - r.next) < 1e-6f, "full glide arrives at the cycle end");
}

static void testPartialGlideContinuousAcrossWrap() {
	RandomGen r(19);
	for (int i = 0; i < 100; i++) {
		r.advance();
		const float before = r.value(0.9999f, 0.3f);
		r.advance();
		const float after = r.value(0.f, 0.3f);
		CHECK(before == after, "jump %f -> %f", before, after);
	}
}

int main() {
	testValuesInRange();
	testNewValueEachAdvance();
	testSmoothContinuousAcrossWrap();
	testSmoothEndpoints();
	testSeedsDiffer();
	testDeterministic();
	testGlideZeroIsStepped();
	testPartialGlide();
	testFullGlideIsCosine();
	testPartialGlideContinuousAcrossWrap();
	return rkatest::finish("test_random");
}
