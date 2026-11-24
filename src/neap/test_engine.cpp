// Neap's engine (NeapEngine.hpp): Level, RESET, step bounds, hostile inputs,
// sample rate change.
#include "neap/NeapEngine.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using rka::NeapEngine;
using rka::NeapFrame;
using rka::NeapResult;

void levels() {
	NeapEngine a, b;
	a.reset();
	b.reset();
	NeapFrame fa, fb;
	fb.unipolar = true;
	NeapResult ra, rb;
	fa.rate = fb.rate = 1.f;
	for (int k = 0; k < 20000; k++) {
		a.process(fa, ra);
		b.process(fb, rb);
		for (int i = 0; i < 8; i++) {
			CHECK(ra.shaped[i] == rb.shaped[i], "shaped is Level-free");
			CHECK(ra.cycle[i] == ra.shaped[i], "bipolar cycle = shaped");
			CHECK(rb.cycle[i] == rb.shaped[i] + 5.f, "unipolar cycle = shaped + 5");
			CHECK(std::fabs(ra.light[i] - (ra.shaped[i] / 5.f + 1.f) * 0.5f) < 1e-6f, "light");
		}
		CHECK(rb.tide == ra.tide + 5.f, "tide + 5");
		CHECK(ra.high == rb.high, "HIGH same in both modes: %g %g", ra.high, rb.high);
		CHECK(ra.high == 0.f || ra.high == 10.f, "HIGH 0 or 10");
	}
	// HIGH switches at the same point whatever the mark, in both modes.
	const float marks[3] = {-3.f, 0.f, 2.f};
	for (int m = 0; m < 3; m++) {
		NeapEngine p, q;
		p.reset();
		q.reset();
		NeapFrame f1, f2;
		f1.rate = f2.rate = 1.f;
		f2.unipolar = true;
		f1.mark = f2.mark = marks[m];
		NeapResult r1, r2;
		int edges = 0;
		float last = -1.f;
		for (int k = 0; k < 200000; k++) {
			p.process(f1, r1);
			q.process(f2, r2);
			CHECK(r1.high == r2.high, "mark %g: same switching", marks[m]);
			if (r1.high != last) edges++;
			last = r1.high;
			// The comparison is TIDE vs MARK in either domain.
			if (r2.tide > f2.mark + 5.f + 0.05f) CHECK(r2.high == 10.f, "high above mark+5");
			if (r2.tide < f2.mark + 5.f - 0.05f) CHECK(r2.high == 0.f, "low below mark+5");
		}
		CHECK(edges > 2, "mark %g: HIGH moves (%d edges)", marks[m], edges);
	}
}

void reset_edges() {
	NeapEngine e;
	e.reset();
	NeapFrame f;
	f.rate = 1.f;
	NeapResult r;
	// Run until the cycles are away from 0.
	for (int k = 0; k < 5000; k++) e.process(f, r);
	CHECK(std::fabs(r.shaped[0]) > 0.1f, "cycle 1 is moving");
	// Rising through 1 V resets once.
	f.reset = 0.5f;
	e.process(f, r);
	CHECK(std::fabs(r.shaped[0]) > 0.1f, "0.5 V does not reset");
	f.reset = 1.f;
	e.process(f, r);
	CHECK(std::fabs(r.shaped[0]) < 0.02f, "1 V resets: %g", r.shaped[0]);
	// Held high: no repeat.
	float prev = r.shaped[0];
	for (int k = 0; k < 100; k++) e.process(f, r);
	CHECK(r.shaped[0] > prev && r.shaped[0] > 0.05f, "held high runs on: %g", r.shaped[0]);
	// Dropping to 0.5 V does not re-arm; rising again does not reset.
	f.reset = 0.5f;
	e.process(f, r);
	f.reset = 2.f;
	const float before = r.shaped[0];
	e.process(f, r);
	CHECK(r.shaped[0] > before, "no re-arm between 0.1 and 1 V");
	// Re-arms below 0.1 V.
	f.reset = 0.05f;
	for (int k = 0; k < 10; k++) e.process(f, r);
	f.reset = 1.5f;
	e.process(f, r);
	CHECK(std::fabs(r.shaped[0]) < 0.02f, "re-armed: %g", r.shaped[0]);
	// Noise between 0.1 and 1 V never resets.
	f.reset = 0.f;
	for (int k = 0; k < 10; k++) e.process(f, r);
	float minAbs = 1e9f;
	unsigned s = 12345;
	for (int k = 0; k < 20000; k++) {
		s = s * 1664525u + 1013904223u;
		f.reset = 0.11f + 0.88f * float(s >> 8) / 16777216.f;
		const float before2 = r.shaped[0];
		e.process(f, r);
		if (k > 100) {
			CHECK(std::fabs(r.shaped[0] - before2) < 0.1f, "noise reset at %d", k);
			minAbs = std::fmin(minAbs, std::fabs(r.shaped[0]));
		}
	}
}

void no_jumps() {
	const float fs = 48000.f;
	NeapEngine e;
	e.reset();
	NeapFrame f;
	NeapResult r, p;
	// 161 Hz (the RATE CV clamp, +2 octaves) x 6.3 (sine slope) per cycle x dt, plus the crossfade's 10 ms slope, in x units.
	const float cycBound = 5.f * (6.4f * 161.f / fs + 0.5f / (0.010f * fs)) + 1e-4f;
	const float tideBound = 2.f * cycBound + 1e-3f;
	bool have = false;
	long bad = 0;
	for (long k = 0; k < 600000; k++) {
		const float t = float(k) / fs;
		f.rate = 0.5f + 0.5f * std::sin(t * 3.1f);
		f.rateCv = 5.f * std::sin(t * 7.3f);
		f.rateAtt = std::sin(t * 0.9f);
		f.weight = std::sin(t * 1.7f);
		f.weightCv = 5.f * std::sin(t * 4.1f);
		f.mark = 5.f * std::sin(t * 2.3f);
		f.wave = (long(t * 20.f) % 2) ? rka::neap::WAVE_SINE : rka::neap::WAVE_TRIANGLE;
		e.process(f, r);
		if (have) {
			for (int i = 0; i < 8; i++)
				if (std::fabs(r.shaped[i] - p.shaped[i]) > cycBound) { if (bad < 5) std::printf("cyc %d k %ld d %g b %g\n", i, k, r.shaped[i]-p.shaped[i], cycBound); bad++; }
			if (std::fabs(r.tide - p.tide) > tideBound) { if (bad < 5) std::printf("tide k %ld d %g b %g\n", k, r.tide-p.tide, tideBound); bad++; }
		}
		p = r;
		have = true;
	}
	CHECK(bad == 0, "%ld steps beyond the bound", bad);
}

bool inRange(const NeapResult& r, bool uni) {
	const float lo = uni ? 0.f : -5.f, hi = uni ? 10.f : 5.f;
	for (int i = 0; i < 8; i++) {
		if (!(r.cycle[i] >= lo - 1e-4f && r.cycle[i] <= hi + 1e-4f)) return false;
		if (!(r.shaped[i] >= -5.0001f && r.shaped[i] <= 5.0001f)) return false;
		if (!(r.light[i] >= -1e-6f && r.light[i] <= 1.000001f)) return false;
	}
	return r.tide >= lo - 1e-4f && r.tide <= hi + 1e-4f && (r.high == 0.f || r.high == 10.f);
}

void hostile_inputs() {
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const float bad[6] = {nan, inf, -inf, 1e30f, -1e30f, 0.f};
	unsigned s = 99;
	for (int uni = 0; uni < 2; uni++) {
		NeapEngine e;
		e.reset();
		NeapFrame f;
		f.unipolar = uni != 0;
		NeapResult r;
		for (int k = 0; k < 60000; k++) {
			s = s * 1664525u + 1013904223u;
			auto pick = [&](float good) { return (s >> 3) % 3 == 0 ? bad[(s >> 12) % 6] : good; };
			if (k % 7 == 0) {
				f.sampleTime = pick(1.f / 48000.f);
				s = s * 1664525u + 1013904223u;
				f.rate = pick(0.7f);
				s = s * 1664525u + 1013904223u;
				f.rateAtt = pick(0.5f);
				s = s * 1664525u + 1013904223u;
				f.rateCv = pick(2.f);
				s = s * 1664525u + 1013904223u;
				f.weight = pick(0.3f);
				s = s * 1664525u + 1013904223u;
				f.weightCv = pick(-1.f);
				s = s * 1664525u + 1013904223u;
				f.mark = pick(1.f);
				s = s * 1664525u + 1013904223u;
				f.reset = pick(0.f);
				f.wave = (s >> 5) & 1 ? rka::neap::WAVE_SINE : rka::neap::WAVE_TRIANGLE;
			}
			e.process(f, r);
			if (!inRange(r, uni != 0)) {
				CHECK(false, "out of range at %d (uni %d)", k, uni);
				return;
			}
		}
		// Recovers on sane input.
		NeapFrame g;
		g.unipolar = uni != 0;
		for (int k = 0; k < 48000; k++) e.process(g, r);
		CHECK(inRange(r, uni != 0), "recovers");
	}
}

// A NaN CV reads as 0: the same TIDE as the knob alone.
void nan_cv_reads_zero() {
	const float nan = std::numeric_limits<float>::quiet_NaN();
	NeapEngine a, b;
	a.reset();
	b.reset();
	NeapFrame fa, fb;
	fa.weight = fb.weight = 0.6f;
	fb.weightCv = nan;
	fa.rateCv = 0.f;
	fb.rateCv = nan;
	NeapResult ra, rb;
	for (int k = 0; k < 30000; k++) {
		a.process(fa, ra);
		b.process(fb, rb);
		CHECK(ra.tide == rb.tide && ra.cycle[7] == rb.cycle[7], "NaN CV = 0 at %d", k);
	}
}

void sample_rate_change() {
	NeapEngine e;
	e.reset();
	NeapFrame f;
	f.drift = false;
	f.rate = 1.f;   // cycle 1: 40 Hz
	NeapResult r;
	// Count cycle 1's rising zero crossings per simulated second at each rate.
	auto crossings = [&](float fs, float seconds) {
		f.sampleTime = 1.f / fs;
		int n = 0;
		float prev = r.shaped[0];
		const long steps = long(seconds * fs);
		for (long k = 0; k < steps; k++) {
			e.process(f, r);
			if (prev < 0.f && r.shaped[0] >= 0.f) n++;
			prev = r.shaped[0];
		}
		return n;
	};
	const int a = crossings(48000.f, 1.f);
	CHECK(a >= 39 && a <= 41, "48 kHz: %d crossings", a);
	// Switch: phase continues (no jump), then 40 Hz again.
	NeapResult before = r;
	f.sampleTime = 1.f / 96000.f;
	e.process(f, r);
	CHECK(std::fabs(r.shaped[0] - before.shaped[0]) < 5.f * 4.f * 40.2f / 48000.f, "no jump at the switch");
	const int b = crossings(96000.f, 1.f);
	CHECK(b >= 39 && b <= 41, "96 kHz: %d crossings", b);
}

} // namespace

int main() {
	levels();
	reset_edges();
	no_jumps();
	hostile_inputs();
	nan_cv_reads_zero();
	sample_rate_change();
	return rkatest::finish("test_engine");
}
