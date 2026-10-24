// Unit tests for src/fathom/Drift.hpp. Run with `make test`.
#include "Drift.hpp"
#include "testing.hpp"
#include <cmath>

using namespace rka;

static const float FS = 48000.f;
static const float DT = 1.f / FS;
static const int SEC = 48000;

static void testZeroAmountIsSilent() {
	DriftWalk w(1);
	bool ok = true;
	for (int i = 0; i < SEC && ok; i++) {
		w.process(0.f, DT);
		ok = w.cents == 0.f && w.db == 0.f;
	}
	CHECK(ok, "amount 0 moved: cents %f db %f", w.cents, w.db);
}

static void testBounds() {
	DriftWalk w(1);
	float minC = 0.f, maxC = 0.f, minDb = 0.f, maxDb = 0.f;
	for (int i = 0; i < 60 * SEC; i++) {
		w.process(1.f, DT);
		minC = std::fmin(minC, w.cents);
		maxC = std::fmax(maxC, w.cents);
		minDb = std::fmin(minDb, w.db);
		maxDb = std::fmax(maxDb, w.db);
	}
	CHECK(minC >= -6.f && maxC <= 6.f, "cents range %f..%f", minC, maxC);
	CHECK(minDb >= -3.f && maxDb <= 0.f, "db range %f..%f", minDb, maxDb);
	CHECK(maxC > 1.f && minC < -1.f, "walk barely moved: %f..%f", minC, maxC);
}

static void testSlewed() {
	DriftWalk w(2);
	float maxDc = 0.f, maxDdb = 0.f;
	float prevC = w.cents, prevDb = w.db;
	for (int i = 0; i < 60 * SEC; i++) {
		w.process(1.f, DT);
		maxDc = std::fmax(maxDc, std::fabs(w.cents - prevC));
		maxDdb = std::fmax(maxDdb, std::fabs(w.db - prevDb));
		prevC = w.cents;
		prevDb = w.db;
	}
	CHECK(maxDc < 1e-3f, "cents stepped by %f", maxDc);
	CHECK(maxDdb < 5e-4f, "db stepped by %f", maxDdb);
}

static void testWalksDiffer() {
	DriftWalk a(1), b(2);
	float maxDiff = 0.f;
	for (int i = 0; i < 10 * SEC; i++) {
		a.process(1.f, DT);
		b.process(1.f, DT);
		maxDiff = std::fmax(maxDiff, std::fabs(a.cents - b.cents));
	}
	CHECK(maxDiff > 0.5f, "seeds 1 and 2 track each other: max diff %f", maxDiff);
}

static void testAmountBackToZero() {
	DriftWalk w(3);
	for (int i = 0; i < 5 * SEC; i++)
		w.process(1.f, DT);
	float maxDc = 0.f, prevC = w.cents;
	for (int i = 0; i < 5 * SEC; i++) {
		w.process(0.f, DT);
		maxDc = std::fmax(maxDc, std::fabs(w.cents - prevC));
		prevC = w.cents;
	}
	CHECK(std::fabs(w.cents) < 0.05f, "cents did not return: %f", w.cents);
	CHECK(std::fabs(w.db) < 0.02f, "db did not return: %f", w.db);
	CHECK(maxDc < 1e-3f, "returning to zero stepped by %f", maxDc);
}

static void testAmountClamps() {
	DriftWalk w(4);
	float maxAbs = 0.f, minDb = 0.f;
	for (int i = 0; i < 30 * SEC; i++) {
		w.process(3.f, DT);
		maxAbs = std::fmax(maxAbs, std::fabs(w.cents));
		minDb = std::fmin(minDb, w.db);
	}
	CHECK(maxAbs <= 6.f && minDb >= -3.f, "amount 3 exceeded amount 1 bounds: %f cents, %f dB", maxAbs, minDb);
}

static void testDriftRatios() {
	Drift d;
	for (int i = 0; i < SEC; i++)
		d.process(1.f, DT);
	const float lo = std::exp2(-6.f / 1200.f), hi = std::exp2(6.f / 1200.f);
	for (int i = 0; i < Drift::N; i++) {
		CHECK(d.pitchRatio[i] >= lo && d.pitchRatio[i] <= hi, "pitchRatio[%d] %f", i, d.pitchRatio[i]);
		CHECK(d.levelGain[i] >= 0.7079f && d.levelGain[i] <= 1.f, "levelGain[%d] %f", i, d.levelGain[i]);
	}
	CHECK(d.subGain >= 0.7079f && d.subGain <= 1.f, "subGain %f", d.subGain);
}

int main() {
	testZeroAmountIsSilent();
	testBounds();
	testSlewed();
	testWalksDiffer();
	testAmountBackToZero();
	testAmountClamps();
	testDriftRatios();
	return rkatest::finish("test_drift");
}
