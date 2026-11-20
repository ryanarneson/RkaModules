// Rigging's strike (Strike.hpp): a burst of noise per string, about one
// period long, the six spread low to high over Spread.
#include "testing.hpp"
#include "rigging/Strike.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

using rka::rigging::Strike;
using rka::rigging::kStrings;

namespace {

const float kPeriods[kStrings] = {733.9f, 489.8f, 367.f, 244.9f, 183.5f, 122.4f};   // Fifths on C2 at 48 kHz

Strike make(uint32_t seed, float spread) {
	Strike s;
	s.seed(seed);
	s.setSampleRate(48000.f);
	s.setSpread(spread);
	for (int k = 0; k < kStrings; k++)
		s.setPeriod(k, kPeriods[k]);
	return s;
}

// Run n samples; first[k] / last[k]: the first and last sample at which
// string k's burst was not zero (-1: never).
struct Trace {
	long first[kStrings], last[kStrings];
	float peak = 0.f;
	std::vector<float> one;   // string 0's samples
};

Trace run(Strike& s, long n, long from = 0, Trace* into = nullptr) {
	Trace t;
	if (into)
		t = *into;
	else
		for (int k = 0; k < kStrings; k++)
			t.first[k] = t.last[k] = -1;
	float out[kStrings];
	for (long i = 0; i < n; i++) {
		s.process(out);
		t.one.push_back(out[0]);
		for (int k = 0; k < kStrings; k++) {
			if (out[k] != 0.f) {
				if (t.first[k] < 0)
					t.first[k] = from + i;
				t.last[k] = from + i;
			}
			t.peak = std::fabs(out[k]) > t.peak ? std::fabs(out[k]) : t.peak;
		}
	}
	return t;
}

void together() {
	Strike s = make(1, 0.f);
	float out[kStrings];
	s.process(out);
	bool idle = !s.active();
	for (int k = 0; k < kStrings; k++)
		idle = idle && out[k] == 0.f;
	CHECK(idle, "a strike that was never triggered makes sound");
	s.trigger();
	CHECK(s.active(), "not active after a trigger");
	const Trace t = run(s, 2000);
	for (int k = 0; k < kStrings; k++)
		CHECK(t.first[k] >= 0 && t.first[k] <= 1, "Spread 0: string %d starts at sample %ld", k + 1, t.first[k]);
	CHECK(!s.active(), "still active 2000 samples after a stab");
}

void rake() {
	Strike s = make(1, 1.5f);
	s.trigger();
	const Trace t = run(s, 5 * 14400 + 2000);
	for (int k = 0; k < kStrings; k++)
		CHECK(std::labs(t.first[k] - 14400L * k) <= 2, "Spread 1.5 s: string %d starts at sample %ld, want %ld", k + 1, t.first[k], 14400L * k);
	CHECK(!s.active(), "still active after the rake");
}

void burst_length() {
	Strike s = make(1, 0.f);
	s.trigger();
	const Trace t = run(s, 4000);
	for (int k = 0; k < kStrings; k++) {
		const double len = (double) (t.last[k] - t.first[k] + 1);
		CHECK(std::fabs(len / kPeriods[k] - 1.0) < 0.1, "string %d: a burst of %g samples for a period of %g", k + 1, len, kPeriods[k]);
	}
	// A very short period still gets a burst.
	Strike h = make(1, 0.f);
	h.setPeriod(0, 2.5f);
	h.trigger();
	const Trace th = run(h, 100);
	CHECK(th.last[0] - th.first[0] + 1 >= 8, "a 2.5-sample period got a burst of %ld samples", th.last[0] - th.first[0] + 1);
	CHECK(t.peak > 0.2f && t.peak <= rka::rigging::STRIKE_LEVEL * 1.001f, "burst peak %g, level %g", t.peak, rka::rigging::STRIKE_LEVEL);
}

void retrigger() {
	// A second trigger a third of the way through a rake.
	Strike s = make(1, 1.5f);
	s.trigger();
	Trace t = run(s, 30000);   // strings 1, 2, 3 struck (0, 14400, 28800)
	CHECK(t.first[2] >= 0 && t.first[3] < 0, "30000 samples into a rake: strings 1 to 3 struck only");
	s.trigger();
	for (int k = 0; k < kStrings; k++)
		t.first[k] = t.last[k] = -1;
	t = run(s, 5 * 14400 + 2000, 0, &t);
	for (int k = 0; k < kStrings; k++)
		CHECK(std::labs(t.first[k] - 14400L * k) <= 2, "after a retrigger string %d starts at %ld, want %ld (the old rake dropped)", k + 1, t.first[k],
		      14400L * k);
	// A trigger every 48 samples for 10 s.
	Strike f = make(2, 0.5f);
	float out[kStrings];
	float peak = 0.f;
	bool finite = true;
	for (long i = 0; i < 480000; i++) {
		if (i % 48 == 0)
			f.trigger();
		f.process(out);
		for (int k = 0; k < kStrings; k++) {
			finite = finite && std::isfinite(out[k]);
			peak = std::fabs(out[k]) > peak ? std::fabs(out[k]) : peak;
		}
	}
	CHECK(finite && peak <= rka::rigging::STRIKE_LEVEL * 1.001f, "a 1 kHz trigger train: peak %g", peak);
	run(f, 24000 + 2000);
	CHECK(!f.active(), "still active a rake after the last trigger");
}

void differs() {
	Strike a = make(5, 0.f), b = make(5, 0.f), c = make(6, 0.f);
	a.trigger();
	b.trigger();
	c.trigger();
	const Trace ta = run(a, 800), tb = run(b, 800), tc = run(c, 800);
	CHECK(ta.one == tb.one, "the same seed gives a different burst");
	CHECK(ta.one != tc.one, "two seeds give the same burst");
	a.trigger();
	const Trace ta2 = run(a, 800);
	CHECK(ta.one != ta2.one, "two strikes of one instrument are identical");
	// A burst is noise about zero, not a thump.
	double mean = 0.0;
	for (float v : ta.one)
		mean += v;
	CHECK(std::fabs(mean / 734.0) < 0.15 * rka::rigging::STRIKE_LEVEL, "a burst's mean is %g", mean / 734.0);
}

} // namespace

int main() {
	together();
	rake();
	burst_length();
	retrigger();
	differs();
	return rkatest::finish("rigging test_strike");
}
