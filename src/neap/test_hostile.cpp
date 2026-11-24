// Neap under hostile conditions (plan Task 7), built with the address and
// undefined-behaviour sanitizers and a counting operator new: NaN, infinity
// and 1e30 on every frame field, sample rates from 8 kHz to 768 kHz and
// changes between them, bad sample times, Wave, Level and Drift toggled every
// few samples, RESET held and at audio rate, seeded fuzz of everything, and
// no allocation in process(). Every output is finite and inside its range
// (+-5 V, or 0-10 V in Level 0-10), HIGH is 0 or 10 V, the lights are 0..1,
// and Spring's inputs (shaped) stay in +-5 V.
#include "testing.hpp"
#include "neap/NeapEngine.hpp"
#include "hadal/Random.hpp"

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

using rka::NeapEngine;
using rka::NeapFrame;
using rka::NeapResult;
using rka::neap::WAVE_SINE;
using rka::neap::WAVE_TRIANGLE;

namespace {

bool ok(const NeapResult& r, bool uni) {
	const float lo = uni ? 0.f : -5.f, hi = uni ? 10.f : 5.f;
	for (int i = 0; i < 8; i++) {
		if (!(r.cycle[i] >= lo - 1e-4f && r.cycle[i] <= hi + 1e-4f))
			return false;
		if (!(r.shaped[i] >= -5.0001f && r.shaped[i] <= 5.0001f))
			return false;
		if (!(r.light[i] >= -1e-6f && r.light[i] <= 1.000001f))
			return false;
	}
	return r.tide >= lo - 1e-4f && r.tide <= hi + 1e-4f && (r.high == 0.f || r.high == 10.f);
}

const float BAD[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f};
const float RATES[] = {8000.f, 44100.f, 48000.f, 96000.f, 192000.f, 384000.f, 768000.f};

// Every float field, one at a time and then all together, with the rest sane,
// in both Levels and both Waves; then the sane frame again, and the module
// must be back.
void bad_values_everywhere() {
	for (int uni = 0; uni < 2; uni++)
		for (int wave = 0; wave < 2; wave++)
			for (float bad : BAD)
				for (int which = 0; which < 8; which++) {   // 7 fields, then all
					NeapEngine e;
					NeapFrame f;
					NeapResult r;
					f.unipolar = uni != 0;
					f.wave = wave ? WAVE_SINE : WAVE_TRIANGLE;
					f.rate = 0.8f;       // fast, so the cycles move
					f.rateCv = 1.f;
					f.weight = 0.4f;
					bool good = true;
					for (long n = 0; n < 12000; n++) {
						NeapFrame g = f;
						if (which == 0 || which == 7) g.sampleTime = bad;
						if (which == 1 || which == 7) g.rate = bad;
						if (which == 2 || which == 7) g.rateAtt = bad;
						if (which == 3 || which == 7) g.rateCv = bad;
						if (which == 4 || which == 7) g.weight = bad;
						if (which == 5 || which == 7) g.weightCv = bad;
						if (which == 6 || which == 7) { g.mark = bad; g.reset = bad; }
						e.process(g, r);
						good = good && ok(r, uni != 0);
					}
					CHECK(good, "uni %d wave %d: %g on field %d must keep every output finite and in range", uni, wave,
					      bad, which);
					// Back to sane values: the cycles still run and TIDE moves.
					float lo = 99.f, hi = -99.f;
					for (long n = 0; n < 96000; n++) {
						e.process(f, r);
						good = good && ok(r, uni != 0);
						lo = std::fmin(lo, r.shaped[0]);
						hi = std::fmax(hi, r.shaped[0]);
					}
					CHECK(good && hi - lo > 1.f, "uni %d wave %d: after %g on field %d cycle 1 runs again (%g..%g)", uni,
					      wave, bad, which, lo, hi);
				}
}

// After NaN everywhere, an ordinary run gives what a fresh engine gives once
// the bad samples' effect (phases, weight glide) is the same: no NaN is left
// in any state.
void recovers_after_nan() {
	NeapEngine e, fresh;
	NeapFrame f, nan;
	NeapResult r, rf;
	f.rate = 1.f;
	nan.sampleTime = nan.rate = nan.rateAtt = nan.rateCv = nan.weight = nan.weightCv = nan.mark = nan.reset = NAN;
	for (long n = 0; n < 3000; n++)
		e.process(f, r);
	for (long n = 0; n < 5000; n++)
		e.process(nan, r);
	e.reset();
	// Cycle phases are 0 after reset(); drift state and the crossfade differ
	// from a fresh engine only by drift (off here).
	f.drift = false;
	NeapFrame f2 = f;
	for (long n = 0; n < 96000; n++) {
		e.process(f2, r);
		fresh.process(f2, rf);
	}
	float worst = 0.f;
	for (int i = 0; i < 8; i++)
		worst = std::fmax(worst, std::fabs(r.shaped[i] - rf.shaped[i]));
	CHECK(worst < 1e-3f, "after NaN everywhere and a reset the cycles are a fresh engine's (differ by %g)", worst);
	// TIDE: the weight glide may differ (NaN weights keep the target), but
	// both are finite and in range after a second.
	CHECK(ok(r, false) && ok(rf, false), "outputs are sane after NaN everywhere");
}

// Cycle 1 at its fastest runs at 40 Hz at every rate Rack offers, 8 kHz to
// 768 kHz, and a change of rate mid-run keeps the phase.
void sample_rates() {
	for (float fs : RATES) {
		NeapEngine e;
		NeapFrame f;
		NeapResult r;
		f.rate = 1.f;
		f.drift = false;
		f.sampleTime = 1.f / fs;
		bool good = true;
		int up = 0;
		float prev = 0.f;
		const long n = long(fs);   // 1 s
		for (long k = 0; k < n; k++) {
			e.process(f, r);
			good = good && ok(r, false);
			if (k > 0 && prev < 0.f && r.shaped[0] >= 0.f)
				up++;
			prev = r.shaped[0];
		}
		CHECK(good, "%g Hz: outputs in range", fs);
		CHECK(up >= 39 && up <= 41, "%g Hz: cycle 1 made %d rising crossings in 1 s, want 40", fs, up);
	}
	// A change every few hundred samples, across the whole range, with the
	// cycles moving fast: no output leaves its range and cycle 1 keeps its
	// 40 Hz in seconds (counted in simulated time).
	NeapEngine e;
	NeapFrame f;
	NeapResult r;
	f.rate = 1.f;
	f.drift = false;
	rka::hadal::Random rng;
	rng.seed(5);
	bool good = true;
	double t = 0.0;
	int up = 0;
	float prev = 0.f;
	float maxStep = 0.f;
	for (long n = 0; n < 2000000; n++) {
		if (n % 257 == 0)
			f.sampleTime = 1.f / RATES[rng.next() % 7];
		e.process(f, r);
		t += double(f.sampleTime);
		good = good && ok(r, false);
		if (n > 0) {
			if (prev < 0.f && r.shaped[0] >= 0.f)
				up++;
			maxStep = std::fmax(maxStep, std::fabs(r.shaped[0] - prev));
		}
		prev = r.shaped[0];
	}
	CHECK(good, "rate changes every 257 samples keep every output in range");
	CHECK(std::fabs(up / t - 40.0) < 0.5, "rate changes: cycle 1 ran at %.2f Hz of simulated time, want 40", up / t);
	// At 8 kHz a 40 Hz cycle moves at most 5 V x 4 x 40 / 8000 = 0.1 V a sample (triangle).
	CHECK(maxStep < 0.11f, "no jump at a rate change: largest step %g V", maxStep);
}

// Sample times that are not sample times are ignored: the last good one stays.
void bad_sample_times() {
	NeapEngine a, b;
	NeapFrame fa, fb;
	NeapResult ra, rb;
	fa.rate = fb.rate = 1.f;
	fa.drift = fb.drift = false;
	const float bads[] = {0.f, -1.f / 48000.f, NAN, INFINITY, -INFINITY, 1e30f, 1.f, 5.f};
	for (long n = 0; n < 96000; n++) {
		fa.sampleTime = 1.f / 48000.f;
		fb.sampleTime = (n % 3 == 0) ? bads[(n / 3) % 8] : 1.f / 48000.f;
		a.process(fa, ra);
		b.process(fb, rb);
		if (n % 3 == 0)
			continue;   // the bad ones follow the last good dt (1/48000): identical
		for (int i = 0; i < 8; i++)
			if (ra.shaped[i] != rb.shaped[i]) {
				CHECK(false, "bad sample time changed cycle %d at %ld: %g vs %g", i, n, ra.shaped[i], rb.shaped[i]);
				return;
			}
	}
}

// Wave, Level and Drift toggled every few samples, on every period from 1 to
// 97: the crossfade stays inside +-1, Level changes only the offset.
void toggles() {
	for (int period : {1, 2, 3, 7, 48, 97, 481}) {
		NeapEngine e;
		NeapFrame f;
		NeapResult r;
		f.rate = 1.f;
		f.rateCv = 5.f;
		f.rateAtt = 1.f;
		bool good = true;
		for (long n = 0; n < 200000; n++) {
			f.wave = (n / period) % 2 ? WAVE_SINE : WAVE_TRIANGLE;
			f.unipolar = (n / (period + 1)) % 2 != 0;
			f.drift = (n / (period + 2)) % 2 != 0;
			e.process(f, r);
			good = good && ok(r, f.unipolar);
		}
		CHECK(good, "Wave, Level and Drift toggled every %d samples: every output in range", period);
	}
}

// RESET held high, high with noise around its thresholds, and at audio rate:
// one reset per rising edge only, nothing leaves its range.
void reset_hostile() {
	NeapEngine e;
	NeapFrame f;
	NeapResult r;
	f.rate = 1.f;
	f.drift = false;
	rka::hadal::Random rng;
	rng.seed(3);
	bool good = true;
	for (long n = 0; n < 20000; n++) {
		f.reset = 10.f;   // held high from the first sample: one reset, then it runs
		e.process(f, r);
		good = good && ok(r, false);
	}
	CHECK(good && std::fabs(r.shaped[0]) > 0.0f, "RESET held high keeps the cycles running");
	float hi = -99.f;
	for (long n = 0; n < 4800; n++) {
		e.process(f, r);
		hi = std::fmax(hi, r.shaped[0]);
	}
	CHECK(hi > 4.9f, "RESET held high does not hold the cycles at 0 (peak %g)", hi);
	// Noise between 0.1 V and 1 V, around the thresholds: no reset.
	f.reset = 0.f;
	for (long n = 0; n < 100; n++)
		e.process(f, r);
	hi = -99.f;
	float maxStep = 0.f, last = r.shaped[0];
	for (long n = 0; n < 48000; n++) {
		f.reset = 0.12f + 0.86f * rng.unit();
		e.process(f, r);
		good = good && ok(r, false);
		hi = std::fmax(hi, r.shaped[0]);
		maxStep = std::fmax(maxStep, std::fabs(r.shaped[0] - last));
		last = r.shaped[0];
	}
	// A reset is a jump to 0; cycle 1 moves at most 5 x 4 x 40 / 48000 = 0.017 V a sample.
	CHECK(hi > 4.9f && maxStep < 0.05f, "noise inside the Schmitt band never resets (peak %g, largest step %g)", hi, maxStep);
	// Audio-rate pulses and noise around 0.1 / 1 V, in both phases.
	for (int kind = 0; kind < 3; kind++)
		for (long n = 0; n < 100000; n++) {
			switch (kind) {
			case 0: f.reset = n % 2 ? 10.f : 0.f; break;
			case 1: f.reset = rng.unit() * 1.2f; break;
			default: f.reset = (rng.unit() < 0.5f ? 0.99f : 1.01f) * (n % 5 == 0 ? 0.f : 1.f); break;
			}
			e.process(f, r);
			good = good && ok(r, false);
		}
	CHECK(good, "RESET at audio rate and in noise keeps every output in range");
}

// No allocation in process() or reset(), through every path: bad values, both
// waves, all toggles, a rate change.
void no_allocation_in_process() {
	NeapEngine e;
	NeapFrame f;
	NeapResult r;
	rka::hadal::Random rng;
	rng.seed(8);
	const long before = tAllocs;
	for (long n = 0; n < 400000; n++) {
		if (n % 1000 == 0) {
			f.sampleTime = 1.f / RATES[rng.next() % 7];
			f.wave = rng.unit() < 0.5f ? WAVE_SINE : WAVE_TRIANGLE;
			f.unipolar = rng.unit() < 0.5f;
			f.drift = rng.unit() < 0.5f;
			f.rate = rng.unit();
			f.rateCv = rng.uniform() * 12.f;
			f.weight = rng.uniform();
			f.weightCv = rng.unit() < 0.1f ? NAN : rng.uniform() * 8.f;
		}
		f.reset = (n % 20011) < 20 ? 10.f : 0.f;
		e.process(f, r);
	}
	e.reset();
	CHECK(tAllocs == before, "%ld allocations in process() or reset()", tAllocs - before);
}

void seeded_fuzz() {
	for (uint32_t seed : {2026u, 1u, 99u}) {
		rka::hadal::Random rng;
		rng.seed(seed);
		NeapEngine e;
		NeapFrame f;
		NeapResult r;
		bool good = true;
		for (long n = 0; n < 1000000; n++) {
			if (n % 64 == 0) {
				auto pick = [&](float lo, float hi) {
					return rng.unit() < 0.05f ? BAD[rng.next() % 5] : lo + (hi - lo) * rng.unit();
				};
				f.sampleTime = rng.unit() < 0.05f ? BAD[rng.next() % 5] : 1.f / RATES[rng.next() % 7];
				f.rate = pick(-0.5f, 1.5f);
				f.rateAtt = pick(-1.5f, 1.5f);
				f.rateCv = pick(-15.f, 15.f);
				f.weight = pick(-1.5f, 1.5f);
				f.weightCv = pick(-15.f, 15.f);
				f.mark = pick(-8.f, 8.f);
				f.drift = rng.unit() < 0.5f;
				f.unipolar = rng.unit() < 0.5f;
				f.wave = rng.unit() < 0.5f ? WAVE_SINE : WAVE_TRIANGLE;
			}
			f.reset = rng.unit() < 0.01f ? BAD[rng.next() % 5] : (rng.unit() < 0.02f ? 10.f : rng.unit() < 0.5f ? 0.f : rng.unit() * 1.5f);
			if (n % 100003 == 50000)
				e.reset();
			e.process(f, r);
			good = good && ok(r, f.unipolar);
		}
		CHECK(good, "seed %u: fuzzed frames, toggles, rates and resets must keep every output finite and in range", seed);
	}
}

} // namespace

int main() {
	bad_values_everywhere();
	recovers_after_nan();
	sample_rates();
	bad_sample_times();
	toggles();
	reset_hostile();
	no_allocation_in_process();
	seeded_fuzz();
	return rkatest::finish("neap test_hostile");
}
