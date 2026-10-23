// Unit tests for src/sway/ClockSync.hpp. Run with `make test`.
#include "ClockSync.hpp"
#include "Phasor.hpp"
#include "testing.hpp"
#include <cmath>
#include <vector>

using rka::ClockSync;

static const double SR = 48000.0;
static const int EDGE_EVERY = 24000;  // 120 BPM: an edge every 0.5 s
static const int TIMES4 = 3;          // index of x4 in the ratio table
static const int DIV4 = -3;           // index of /4

// Feed `edges` clock edges; return the edge numbers (1-based) that resynced.
static std::vector<int> run(ClockSync& cs, int edges, int index) {
	std::vector<int> syncs;
	int edge = 0;
	for (long n = 1; n <= long(edges) * EDGE_EVERY; n++) {
		const bool isEdge = n % EDGE_EVERY == 0;
		edge += isEdge;
		if (cs.process(isEdge, 1.0 / SR, index))
			syncs.push_back(edge);
	}
	return syncs;
}

static void testUnlockedBeforeTwoEdges() {
	ClockSync cs;
	CHECK(!cs.locked(), "unlocked at start");
	run(cs, 1, TIMES4);
	CHECK(!cs.locked(), "unlocked after one edge");
	run(cs, 1, TIMES4);
	CHECK(cs.locked(), "locked after two edges");
}

static void testTimesFour() {
	ClockSync cs;
	std::vector<int> syncs = run(cs, 6, TIMES4);
	CHECK(std::fabs(cs.frequency(TIMES4) - 8.0) < 1e-3, "x4 of 2 Hz is %f Hz", cs.frequency(TIMES4));
	CHECK(syncs == std::vector<int>({2, 3, 4, 5, 6}), "x4 resyncs on every locked edge (%zu syncs)", syncs.size());
}

static void testDivideFour() {
	ClockSync cs;
	std::vector<int> syncs = run(cs, 12, DIV4);
	CHECK(std::fabs(cs.frequency(DIV4) - 0.5) < 1e-3, "/4 of 2 Hz is %f Hz", cs.frequency(DIV4));
	CHECK(syncs == std::vector<int>({4, 8, 12}), "/4 resyncs on edges 4, 8, 12 (%zu syncs)", syncs.size());
}

static void testResetCount() {
	ClockSync cs;
	run(cs, 2, DIV4);
	cs.resetCount();
	std::vector<int> syncs = run(cs, 8, DIV4);
	CHECK(syncs == std::vector<int>({4, 8}), "after resetCount the next resync is 4 edges later (%zu syncs)", syncs.size());
}

static void testClearUnlocks() {
	ClockSync cs;
	run(cs, 3, TIMES4);
	cs.clear();
	CHECK(!cs.locked(), "clear() unlocks");
	run(cs, 1, TIMES4);
	CHECK(!cs.locked(), "needs two new edges after clear()");
	run(cs, 1, TIMES4);
	CHECK(cs.locked(), "relocks");
}

static void testRatioIndexClamps() {
	CHECK(rka::ratioIndex(7.f, 3.f) == 7, "clamps high");
	CHECK(rka::ratioIndex(-7.f, -3.f) == -7, "clamps low");
	CHECK(rka::ratioIndex(0.4f, 0.f) == 0, "rounds to x1");
	CHECK(rka::clockRatio(-7) == 1.f / 16.f && rka::clockRatio(7) == 16.f && rka::clockRatio(0) == 1.f, "table ends");
}

static void testPhasorLocks() {
	ClockSync cs;
	rka::Phasor ph;
	float worst = 0.f;
	for (long n = 1; n <= 10L * EDGE_EVERY; n++) {
		const bool isEdge = n % EDGE_EVERY == 0;
		const bool sync = cs.process(isEdge, 1.0 / SR, TIMES4);
		if (cs.locked())
			ph.step(cs.frequency(TIMES4), 1.0 / SR, false, false);
		// From the third edge on, the free-running phase should already be
		// back at the start of a cycle when the resync arrives.
		if (isEdge && n > 2L * EDGE_EVERY) {
			const float off = float(std::fmin(ph.phase, 1.0 - ph.phase));
			worst = std::fmax(worst, off);
		}
		if (sync)
			ph.reset();
	}
	CHECK(worst < 0.01f, "phase off by %f at a clock edge", worst);
}

int main() {
	testUnlockedBeforeTwoEdges();
	testTimesFour();
	testDivideFour();
	testResetCount();
	testClearUnlocks();
	testRatioIndexClamps();
	testPhasorLocks();
	return rkatest::finish("test_clock_sync");
}
