// Unit tests for src/sway/LfoCore.hpp. Run with `make test`.
#include "LfoCore.hpp"
#include "testing.hpp"
#include <cmath>
#include <initializer_list>

using rka::LfoCore;

static const float SR = 48000.f;

// Count phase wraps over `seconds`, plus half a cycle so the last wrap
// doesn't sit on the final sample where rounding decides it.
static int countCycles(float freq, float seconds) {
	LfoCore lfo;
	int wraps = 0;
	float prev = lfo.phase;
	for (int i = 0; i < int((seconds + 0.5f / freq) * SR); i++) {
		lfo.step(freq, 1.f / SR);
		if (lfo.phase < prev)
			wraps++;
		prev = lfo.phase;
	}
	return wraps;
}

static void testFrequency() {
	CHECK(countCycles(1.f, 10.f) == 10, "1 Hz over 10 s gave %d", countCycles(1.f, 10.f));
	CHECK(countCycles(2.f, 10.f) == 20, "2 Hz over 10 s gave %d", countCycles(2.f, 10.f));
	CHECK(countCycles(100.f, 1.f) == 100, "100 Hz over 1 s gave %d", countCycles(100.f, 1.f));
}

static void testRateMapping() {
	CHECK(std::fabs(rka::lfoFrequency(1.f, 0.f) - 2.f) < 1e-6f, "default rate should be 2 Hz");
	CHECK(std::fabs(rka::lfoFrequency(0.f, 1.f) - 2.f) < 1e-6f, "+1 V should double 1 Hz");
	CHECK(std::fabs(rka::lfoFrequency(1.f, -1.f) - 1.f) < 1e-6f, "-1 V should halve 2 Hz");
	CHECK(std::isfinite(rka::lfoFrequency(7.f, 1e9f)), "huge CV must stay finite");
	CHECK(rka::lfoFrequency(-7.f, -1e9f) > 0.f, "tiny CV must stay positive");
}

static void testRanges() {
	LfoCore lfo;
	float lo[4] = {1, 1, 1, 1}, hi[4] = {-1, -1, -1, -1};
	for (int i = 0; i < int(SR); i++) {
		lfo.step(3.f, 1.f / SR);
		CHECK(lfo.phase >= 0.f && lfo.phase < 1.f, "phase %f out of range", lfo.phase);
		float v[4] = {lfo.sin(), lfo.tri(), lfo.saw(), lfo.sqr(0.5f)};
		for (int k = 0; k < 4; k++) {
			lo[k] = std::fmin(lo[k], v[k]);
			hi[k] = std::fmax(hi[k], v[k]);
		}
	}
	const char* names[4] = {"sin", "tri", "saw", "sqr"};
	for (int k = 0; k < 4; k++) {
		CHECK(lo[k] >= -1.f && lo[k] < -0.99f, "%s min %f", names[k], lo[k]);
		CHECK(hi[k] <= 1.f && hi[k] > 0.99f, "%s max %f", names[k], hi[k]);
	}
}

static void testTriangleContinuity() {
	// The largest step between samples at 1 Hz should be ~4/SR, never a jump.
	LfoCore lfo;
	float prev = lfo.tri();
	float maxStep = 0.f;
	for (int i = 0; i < int(2 * SR); i++) {
		lfo.step(1.f, 1.f / SR);
		maxStep = std::fmax(maxStep, std::fabs(lfo.tri() - prev));
		prev = lfo.tri();
	}
	CHECK(maxStep < 5.f / SR, "triangle jumped by %f", maxStep);
}

static void testPulseWidth() {
	for (float pw : {0.1f, 0.5f, 0.9f}) {
		LfoCore lfo;
		int high = 0, n = int(SR);
		for (int i = 0; i < n; i++) {
			lfo.step(1.f, 1.f / SR);
			high += lfo.sqr(pw) > 0.f;
		}
		float duty = float(high) / n;
		CHECK(std::fabs(duty - pw) < 0.001f, "pw %f gave duty %f", pw, duty);
	}
}

static void testSinTriInPhase() {
	LfoCore lfo;
	CHECK(lfo.sin() == 0.f && lfo.tri() == 0.f, "both start at 0");
	lfo.phase = 0.25f;
	CHECK(std::fabs(lfo.sin() - 1.f) < 1e-6f && std::fabs(lfo.tri() - 1.f) < 1e-6f, "both peak at 1/4");
	lfo.phase = 0.75f;
	CHECK(std::fabs(lfo.sin() + 1.f) < 1e-6f && std::fabs(lfo.tri() + 1.f) < 1e-6f, "both trough at 3/4");
}

static void testReset() {
	LfoCore lfo;
	for (int i = 0; i < 1234; i++)
		lfo.step(5.f, 1.f / SR);
	lfo.reset();
	CHECK(lfo.phase == 0.f, "reset phase %f", lfo.phase);
}

static void testHugeStep() {
	// freq * sampleTime > 1 must still land in [0, 1).
	LfoCore lfo;
	lfo.step(4096.f, 1.f / 1000.f);
	CHECK(lfo.phase >= 0.f && lfo.phase < 1.f, "phase %f after huge step", lfo.phase);
}

static void testVolts() {
	CHECK(rka::lfoVolts(-1.f, false) == -5.f && rka::lfoVolts(1.f, false) == 5.f, "bipolar is ±5 V");
	CHECK(rka::lfoVolts(-1.f, true) == 0.f && rka::lfoVolts(1.f, true) == 10.f, "unipolar is 0..10 V");
}

int main() {
	testFrequency();
	testRateMapping();
	testRanges();
	testTriangleContinuity();
	testPulseWidth();
	testSinTriInPhase();
	testReset();
	testHugeStep();
	testVolts();
	return rkatest::finish("test_lfo_core");
}
