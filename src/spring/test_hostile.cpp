// Spring under hostile conditions (plan Task 7), built with the address and
// undefined-behaviour sanitizers and a counting operator new: NaN, infinity
// and 1e30 on every cycle input and on the sample time, every combination of
// patched T outputs and bad indices, sample rates from 8 kHz to 768 kHz,
// re-linking (no trigger burst), the link counter against a model, Neap's
// engine fuzzed into the core, and no allocation in step(). Every output is
// finite and in range, the T outputs are 0 or 10 V and the S outputs are
// the sixteen codes.
#include "testing.hpp"
#include "spring/SpringCore.hpp"
#include "neap/NeapEngine.hpp"
#include "hadal/Random.hpp"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

static thread_local long tAllocs = 0;

void* operator new(std::size_t n) {
	tAllocs++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	std::free(p);
}

using rka::SpringCore;
using rka::SpringLink;

namespace {

const float BAD[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f};
const float RATES[] = {8000.f, 44100.f, 48000.f, 96000.f, 192000.f, 384000.f, 768000.f};

bool isCode(float v) {
	for (int n = 0; n <= 15; n++)
		if (v == (n == 15 ? 10.f : n * 10.f / 15.f))
			return true;
	return false;
}

bool ok(const SpringCore& c) {
	for (int i = 0; i < SpringCore::N_OUT; i++)
		if (!(c.out[i] >= -10.f && c.out[i] <= 10.f))
			return false;
	for (int i : {SpringCore::F1, SpringCore::F3, SpringCore::F5, SpringCore::F7})
		if (!(c.out[i] >= 0.f))
			return false;
	for (int i : {SpringCore::T2, SpringCore::T4, SpringCore::T6, SpringCore::T8})
		if (!(c.out[i] == 0.f || c.out[i] == 10.f))
			return false;
	for (int i : {SpringCore::S14, SpringCore::S58, SpringCore::SODD, SpringCore::SEVEN})
		if (!isCode(c.out[i]))
			return false;
	return true;
}

// x for sample n: eight sines at distinct rates, in +-5 V.
void waves(long n, float fs, float x[8]) {
	for (int i = 0; i < 8; i++)
		x[i] = 5.f * float(std::sin(6.283185307179586 * (3.0 + 2.7 * i) * double(n) / double(fs)));
}

void bad_values_everywhere() {
	for (float bad : BAD)
		for (int which = 0; which <= 9; which++)   // 8 cycles, all, the sample time
			for (int patched = 0; patched < 16; patched++) {
				SpringCore c;
				for (int k = 0; k < 4; k++)
					c.setPatched(k, (patched >> k) & 1);
				bool good = true;
				for (long n = 0; n < 4000; n++) {
					float x[8];
					waves(n, 48000.f, x);
					float st = 1.f / 48000.f;
					if (which < 8) x[which] = bad;
					else if (which == 8) for (int i = 0; i < 8; i++) x[i] = bad;
					else st = bad;
					c.step(x, st);
					good = good && ok(c);
				}
				CHECK(good, "%g on %s with T mask %d must keep every output finite and in range", bad,
				      which < 8 ? "a cycle" : which == 8 ? "every cycle" : "the sample time", patched);
			}
}

// NaN on a trigger source clears its history: it can neither fire nor leave
// a pulse running; once it is sane again the triggers run as before.
void nan_does_not_fire_or_stick() {
	for (int k = 0; k < 4; k++) {
		SpringCore c;
		for (int j = 0; j < 4; j++)
			c.setPatched(j, true);
		float x[8];
		for (int i = 0; i < 8; i++) x[i] = 0.f;
		const float dt = 1.f / 48000.f;
		x[2 * k + 1] = -1.f;
		c.step(x, dt);
		x[2 * k + 1] = NAN;   // a NaN between a negative and a positive sample is not a crossing
		c.step(x, dt);
		x[2 * k + 1] = 1.f;
		c.step(x, dt);
		CHECK(c.out[SpringCore::T2 + k] == 0.f, "T%d: a NaN between -1 and +1 is not a rising crossing", 2 * k + 2);
		x[2 * k + 1] = -1.f;
		c.step(x, dt);
		x[2 * k + 1] = 1.f;
		c.step(x, dt);
		CHECK(c.out[SpringCore::T2 + k] == 10.f, "T%d fires on a real crossing after a NaN", 2 * k + 2);
		for (int n = 0; n < 1000; n++)
			c.step(x, dt);   // 20 ms of high
		CHECK(c.out[SpringCore::T2 + k] == 0.f, "T%d pulse ends", 2 * k + 2);
	}
}

// A re-link is reset() then steps: nothing fires on the first step, however
// the cycles stand, and every output is 0 after reset().
void relink_has_no_burst() {
	for (int phase = 0; phase < 64; phase++) {
		SpringCore c;
		for (int j = 0; j < 4; j++)
			c.setPatched(j, true);
		const float dt = 1.f / 48000.f;
		float x[8];
		long n = 0;
		for (; n < 20000 + phase * 37; n++) {
			waves(n, 48000.f, x);
			c.step(x, dt);
		}
		c.reset();
		for (int i = 0; i < SpringCore::N_OUT; i++)
			CHECK(c.out[i] == 0.f, "out %d is 0 after reset()", i);
		// First step after the loss: every cycle may be rising through 0 here.
		for (int i = 0; i < 8; i++) x[i] = 0.f;
		c.step(x, dt);
		CHECK(c.out[SpringCore::T2] == 0.f && c.out[SpringCore::T4] == 0.f && c.out[SpringCore::T6] == 0.f &&
		          c.out[SpringCore::T8] == 0.f,
		      "re-link at phase %d: no trigger on the first step", phase);
		waves(n, 48000.f, x);
		c.step(x, dt);
		CHECK(ok(c), "re-link at phase %d: outputs in range", phase);
	}
	// A cycle that goes from -1 to +1 across the loss: still no burst, since
	// the first value after the loss is only recorded.
	SpringCore c;
	for (int j = 0; j < 4; j++)
		c.setPatched(j, true);
	float x[8];
	for (int i = 0; i < 8; i++) x[i] = -1.f;
	c.step(x, 1.f / 48000.f);
	c.reset();
	for (int i = 0; i < 8; i++) x[i] = 1.f;
	c.step(x, 1.f / 48000.f);
	CHECK(c.out[SpringCore::T2] == 0.f && c.out[SpringCore::T8] == 0.f, "-1 before a loss, +1 after: no trigger");
}

// Pulses are 10 ms (to the sample) from 8 kHz to 768 kHz, and a rate change
// mid-pulse still ends it after 10 ms of simulated time.
void pulse_width_at_every_rate() {
	for (float fs : RATES) {
		SpringCore c;
		c.setPatched(0, true);
		const float dt = 1.f / fs;
		float x[8];
		for (int i = 0; i < 8; i++) x[i] = 0.f;
		x[1] = -1.f;
		c.step(x, dt);
		x[1] = 1.f;
		long high = 0;
		for (long n = 0; n < long(fs * 0.05f); n++) {
			c.step(x, dt);
			if (c.out[SpringCore::T2] == 10.f)
				high++;
		}
		const double ms = 1000.0 * high / fs;
		CHECK(std::fabs(ms - 10.0) < 1000.0 / fs + 0.01, "%g Hz: the pulse lasts %.3f ms, want 10", fs, ms);
	}
	SpringCore c;
	c.setPatched(0, true);
	float x[8];
	for (int i = 0; i < 8; i++) x[i] = 0.f;
	x[1] = -1.f;
	c.step(x, 1.f / 48000.f);
	x[1] = 1.f;
	double t = 0.0;
	bool good = true;
	float st = 1.f / 48000.f;
	for (long n = 0; n < 200000 && good; n++) {
		if (n % 50 == 0)
			st = 1.f / RATES[(n / 50) % 7];
		c.step(x, st);
		t += double(st);
		if (c.out[SpringCore::T2] == 0.f) {
			CHECK(std::fabs(t - 0.010) < 0.0002, "rate changes mid-pulse: the pulse ended after %.5f s", t);
			good = false;
		}
	}
	CHECK(!good, "the pulse ended");
}

// A sample time that is not one is ignored: the last good one stays, so a
// core fed bad ones between good ones matches a core fed only good ones.
void bad_sample_time_keeps_the_last() {
	const float bads[] = {0.f, -1.f / 48000.f, NAN, INFINITY, -INFINITY, 1e30f};
	SpringCore a, b;
	for (int k = 0; k < 4; k++) {
		a.setPatched(k, true);
		b.setPatched(k, true);
	}
	bool same = true;
	bool pulsed = false;
	for (long n = 0; n < 48000; n++) {
		float x[8];
		waves(n, 48000.f, x);
		a.step(x, 1.f / 48000.f);
		b.step(x, n % 3 ? 1.f / 48000.f : bads[(n / 3) % 6]);
		for (int i = 0; i < SpringCore::N_OUT; i++)
			same = same && a.out[i] == b.out[i];
		pulsed = pulsed || a.out[SpringCore::T2] == 10.f;
	}
	CHECK(pulsed, "the test's cycles do produce pulses");
	CHECK(same, "bad sample times between good ones change nothing");
}

// Bad patched indices change nothing; the sixteen masks all work.
void bad_indices() {
	SpringCore c;
	for (int i : {-1, 4, 5, 100, INT_MAX, INT_MIN, -100000})
		c.setPatched(i, true);
	float x[8];
	for (int i = 0; i < 8; i++) x[i] = -1.f;
	c.step(x, 1.f / 48000.f);
	for (int i = 0; i < 8; i++) x[i] = 1.f;
	c.step(x, 1.f / 48000.f);
	for (int k = 0; k < 4; k++)
		CHECK(c.out[SpringCore::T2 + k] == 0.f, "T%d: a bad index patched nothing", 2 * k + 2);
	CHECK(ok(c), "outputs in range after bad indices");
}

// The link counter against a model: linked from a call of neapCalled until
// 64 process calls in a row have seen none.
void link_against_model() {
	rka::hadal::Random rng;
	rng.seed(12);
	SpringLink l;
	bool model = false;
	int idle = 64;
	CHECK(!l.linkedAfterProcess(), "starts unlinked");
	bool good = true;
	for (long n = 0; n < 300000; n++) {
		const int burst = (n / 997) % 4;   // quiet, one per call, several per call
		const bool called = burst == 0 ? rng.unit() < 0.01f : burst == 1 ? true : rng.unit() < 0.5f;
		const int times = burst == 3 && called ? int(rng.next() % 9) : called ? 1 : 0;
		for (int k = 0; k < times; k++)
			l.neapCalled();
		if (times) {
			idle = 0;
			model = true;
		}
		else if (idle < 64)
			idle++;
		if (idle >= 64)
			model = false;
		good = good && l.linkedAfterProcess() == model;
	}
	CHECK(good, "the link follows the model: linked until 64 process calls see no Neap");
}

void no_allocation_in_step() {
	rka::hadal::Random rng;
	rng.seed(8);
	SpringCore c;
	SpringLink l;
	const long before = tAllocs;
	for (long n = 0; n < 400000; n++) {
		float x[8];
		waves(n, 48000.f, x);
		if (n % 777 == 0)
			x[n % 8] = BAD[rng.next() % 5];
		if (n % 1000 == 0)
			c.setPatched(int(rng.next() % 6) - 1, rng.unit() < 0.5f);
		if (n % 5003 == 0) {
			c.reset();
			l.neapCalled();
		}
		c.step(x, 1.f / RATES[(n / 1000) % 7]);
		l.linkedAfterProcess();
	}
	CHECK(tAllocs == before, "%ld allocations in step(), setPatched(), reset() or the link", tAllocs - before);
}

// Neap's engine, fuzzed, feeds the core; the link is lost and made again at
// random (reset), T outputs patched at random.
void chained_fuzz() {
	for (uint32_t seed : {2026u, 1u, 99u}) {
		rka::hadal::Random rng;
		rng.seed(seed);
		rka::NeapEngine e;
		rka::NeapFrame f;
		rka::NeapResult r;
		SpringCore c;
		bool good = true;
		for (long n = 0; n < 1000000; n++) {
			if (n % 64 == 0) {
				auto pick = [&](float lo, float hi) {
					return rng.unit() < 0.05f ? BAD[rng.next() % 5] : lo + (hi - lo) * rng.unit();
				};
				f.sampleTime = rng.unit() < 0.05f ? BAD[rng.next() % 5] : 1.f / RATES[rng.next() % 7];
				f.rate = pick(0.5f, 1.5f);   // fast, so the zero crossings come
				f.rateAtt = pick(-1.5f, 1.5f);
				f.rateCv = pick(-15.f, 15.f);
				f.weight = pick(-1.5f, 1.5f);
				f.weightCv = pick(-15.f, 15.f);
				f.drift = rng.unit() < 0.5f;
				f.wave = rng.unit() < 0.5f ? rka::neap::WAVE_SINE : rka::neap::WAVE_TRIANGLE;
				for (int k = 0; k < 4; k++)
					c.setPatched(k, rng.unit() < 0.5f);
			}
			f.reset = (n % 30011) < 10 ? 10.f : 0.f;
			e.process(f, r);
			if (rng.unit() < 0.0005f)
				c.reset();   // the link dropped and came back
			c.step(r.shaped, f.sampleTime);
			good = good && ok(c);
		}
		CHECK(good, "seed %u: Neap's fuzzed cycles through Spring keep every output finite and in range", seed);
	}
}

// Straight fuzz of the core: values in and slightly past +-5 V, bad values,
// bad sample times and patch changes.
void seeded_fuzz() {
	for (uint32_t seed : {2026u, 1u, 99u}) {
		rka::hadal::Random rng;
		rng.seed(seed);
		SpringCore c;
		bool good = true;
		float x[8];
		for (int i = 0; i < 8; i++) x[i] = 0.f;
		float st = 1.f / 48000.f;
		for (long n = 0; n < 1000000; n++) {
			if (n % 32 == 0)
				st = rng.unit() < 0.05f ? BAD[rng.next() % 5] : 1.f / RATES[rng.next() % 7];
			if (n % 50 == 0)
				c.setPatched(int(rng.next() % 6) - 1, rng.unit() < 0.5f);
			for (int i = 0; i < 8; i++) {
				const float u = rng.unit();
				x[i] = u < 0.01f ? BAD[rng.next() % 5] : u < 0.5f ? rng.uniform() * 6.f : (rng.unit() < 0.5f ? -1.f : 1.f) * (u < 0.7f ? 0.f : 1e-3f);
			}
			if (n % 100003 == 50000)
				c.reset();
			c.step(x, st);
			good = good && ok(c);
		}
		CHECK(good, "seed %u: fuzzed cycles, sample times and patches must keep every output finite and in range", seed);
	}
}

} // namespace

int main() {
	bad_values_everywhere();
	nan_does_not_fire_or_stick();
	relink_has_no_burst();
	pulse_width_at_every_rate();
	bad_indices();
	bad_sample_time_keeps_the_last();
	link_against_model();
	no_allocation_in_step();
	chained_fuzz();
	seeded_fuzz();
	return rkatest::finish("spring test_hostile");
}
