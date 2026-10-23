// Unit tests for src/sway/Phasor.hpp and Rate.hpp. Run with `make test`.
#include "Phasor.hpp"
#include "Rate.hpp"
#include "testing.hpp"
#include <cmath>
#include <initializer_list>

using rka::Phasor;

static const double SR = 48000.0;

// Count wraps over `seconds`, plus half a cycle so the last wrap doesn't sit
// on the final sample where rounding decides it.
static int countCycles(double freq, double seconds) {
	Phasor ph;
	int wraps = 0;
	for (long i = 0; i < long((seconds + 0.5 / freq) * SR); i++)
		wraps += ph.step(freq, 1.0 / SR, false, false);
	return wraps;
}

static void testFrequency() {
	CHECK(countCycles(1.0, 10.0) == 10, "1 Hz over 10 s gave %d", countCycles(1.0, 10.0));
	CHECK(countCycles(2.0, 10.0) == 20, "2 Hz over 10 s gave %d", countCycles(2.0, 10.0));
	CHECK(countCycles(100.0, 1.0) == 100, "100 Hz over 1 s gave %d", countCycles(100.0, 1.0));
}

// The bug: a float phase ran 2% fast at 2^-7 Hz and froze below 2^-10 Hz.
// Time two consecutive wraps and compare with 1/f.
static void testSlowPeriods() {
	for (float e : {-7.f, -14.f}) {
		const double f = std::exp2(e);
		Phasor ph;
		long first = -1, n = 0;
		double period = 0.0;
		for (; n < long(3.0 / f * SR); n++) {
			if (ph.step(f, 1.0 / SR, false, false)) {
				if (first < 0) {
					first = n;
				}
				else {
					period = (n - first) / SR;
					break;
				}
			}
		}
		CHECK(std::fabs(period * f - 1.0) < 0.001, "2^%g Hz: period %f s, expected %f s", e, period, 1.0 / f);
	}
}

static void testNoStallAtMinRate() {
	const double f = std::exp2(-24.0);
	Phasor ph;
	ph.phase = 0.9;  // where float resolution was worst
	const long n = 1000000;
	for (long i = 0; i < n; i++)
		ph.step(f, 1.0 / SR, false, false);
	const double expected = n * f / SR;
	CHECK(std::fabs((ph.phase - 0.9) / expected - 1.0) < 0.01, "advanced %g, expected %g", ph.phase - 0.9, expected);
}

static void testHugeStep() {
	// freq * dt > 1 must still land in [0, 1).
	Phasor ph;
	for (int i = 0; i < 100; i++) {
		ph.step(30000.0, 1.0 / SR, false, false);
		CHECK(ph.phase >= 0.0 && ph.phase < 1.0, "phase %f after big step", ph.phase);
	}
	ph.step(4096.0, 1.0 / 1000.0, false, false);
	CHECK(ph.phase >= 0.0 && ph.phase < 1.0, "phase %f after huge step", ph.phase);
}

static void testReset() {
	Phasor ph;
	for (int i = 0; i < 1234; i++)
		ph.step(5.0, 1.0 / SR, false, false);
	ph.reset();
	CHECK(ph.phase == 0.0 && !ph.parked, "reset phase %f", ph.phase);
}

static void testHold() {
	Phasor ph;
	ph.step(1.0, 0.1, false, false);
	const double before = ph.phase;
	CHECK(!ph.step(1.0, 0.95, true, false), "hold never wraps");
	CHECK(ph.phase == before, "hold froze phase: %f vs %f", ph.phase, before);
}

static void testOneShotParks() {
	Phasor ph;
	int wraps = 0;
	for (int i = 0; i < 150; i++)
		wraps += ph.step(1.0, 0.01, false, true);
	CHECK(wraps == 1, "one-shot wrapped %d times", wraps);
	CHECK(ph.parked && ph.phase == rka::PARK_PHASE, "parked at the end: phase %f", ph.phase);
}

static void testOneShotOffResumes() {
	Phasor ph;
	ph.park();
	CHECK(!ph.step(1.0, 0.01, false, true) && ph.phase == rka::PARK_PHASE, "stays parked while one-shot is on");
	ph.step(1.0, 0.01, false, false);
	ph.step(1.0, 0.01, false, false);
	CHECK(!ph.parked && ph.phase > 0.0 && ph.phase < 0.05, "resumes when one-shot is off: phase %f", ph.phase);
}

static void testRateExponent() {
	using namespace rka;
	CHECK(rateExponent(1.f, 0.f, RANGE_NORMAL) == 1.f, "normal default is 2 Hz");
	CHECK(rateExponent(0.f, 1.f, RANGE_NORMAL) == 1.f, "+1 V doubles");
	CHECK(rateExponent(-7.f, 0.f, RANGE_SLOW) == -14.f, "slow minimum is 2^-14 Hz");
	CHECK(rateExponent(7.f, 0.f, RANGE_SLOW) == 0.f, "slow maximum is 1 Hz");
	CHECK(std::fabs(rateExponent(0.f, 0.f, RANGE_AUDIO) - std::log2(261.6256f)) < 1e-5f, "audio centre is C4");
	CHECK(std::fabs(rateExponent(7.f, 0.f, RANGE_AUDIO) - std::log2(261.6256f) - 5.f) < 1e-5f, "audio spans +5 octaves");
}

static void testClamps() {
	using namespace rka;
	CHECK(expToHz(-40.f, 48000.f) == std::exp2(-24.0), "exponent floor -24");
	CHECK(expToHz(20.f, 48000.f) == std::exp2(14.0), "exponent ceiling 14 at 48 kHz");
	CHECK(expToHz(20.f, 22050.f) == 0.45 * 22050.0, "capped at 0.45 fs at low sample rates");
	CHECK(expToHz(1e9f, 48000.f) == std::exp2(14.0), "huge CV stays finite");
	CHECK(clampHz(1e6, 44100.f) == 0.45 * 44100.0, "clocked frequency capped at 0.45 fs");
	CHECK(clampHz(3.0, 44100.f) == 3.0, "normal frequency untouched");
}

static void testVolts() {
	CHECK(rka::lfoVolts(-1.f, false) == -5.f && rka::lfoVolts(1.f, false) == 5.f, "bipolar is ±5 V");
	CHECK(rka::lfoVolts(-1.f, true) == 0.f && rka::lfoVolts(1.f, true) == 10.f, "unipolar is 0..10 V");
}

int main() {
	testFrequency();
	testSlowPeriods();
	testNoStallAtMinRate();
	testHugeStep();
	testReset();
	testHold();
	testOneShotParks();
	testOneShotOffResumes();
	testRateExponent();
	testClamps();
	testVolts();
	return rkatest::finish("test_phasor");
}
