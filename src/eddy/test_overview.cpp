// rka::Overview (src/Overview.hpp): the tape's 512 columns of peaks, built
// whole, kept up to date by rescans a few frames a sample. Built with the
// plugin's floating-point flags (PLUGIN_MATH_TESTS in tests.mk). The 14.4 M
// frame tape of build_matches is a real allocation (calloc, ~115 MB, mostly
// untouched pages); the whole test runs in about a second.
#include "Overview.hpp"
#include "testing.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

const int COLS = rka::Overview::COLS;

struct Noise {
	uint32_t s = 0x1234567u;
	float next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return (float) ((double) s / 4294967296.0 * 2.0 - 1.0);
	}
};

size_t colStart(int c, size_t frames) {
	return (size_t) ((unsigned long long) c * frames / COLS);
}

// Brute force: both sides, frames past the tape's length are 0, an empty
// column is 0.
void brute(const rka::Tape& t, int c, size_t frames, float& l, float& h) {
	const size_t f0 = colStart(c, frames), f1 = colStart(c + 1, frames);
	if (f1 <= f0) {
		l = h = 0.f;
		return;
	}
	l = 1e30f;
	h = -1e30f;
	for (size_t f = f0; f < f1; f++)
		for (int s = 0; s < 2; s++) {
			const float v = f < t.length ? t.ch[s][f] : 0.f;
			l = v < l ? v : l;
			h = v > h ? v : h;
		}
}

int mismatches(const rka::Overview& o, const rka::Tape& t, size_t frames) {
	int bad = 0;
	for (int c = 0; c < COLS; c++) {
		float l, h;
		brute(t, c, frames, l, h);
		if (o.lo[c].load() != l || o.hi[c].load() != h)
			bad++;
	}
	return bad;
}

void fill(rka::Tape& t, size_t n) {
	Noise nz;
	for (size_t f = 0; f < n; f++) {
		t.ch[0][f] = nz.next();
		t.ch[1][f] = 0.5f * nz.next();
	}
	t.length = n;
}

void service_all(rka::Overview& o, const rka::Tape& t, int budget) {
	for (int i = 0; i < 10000000 && !o.settled(); i++)
		o.service(t, budget);
}

void build_matches() {
	const size_t sizes[3] = {100, 48000, 14400000};
	for (int k = 0; k < 3; k++) {
		rka::Tape t;
		CHECK(t.allocate(sizes[k], 0), "allocate %zu", sizes[k]);
		if (k < 2)
			fill(t, sizes[k]);
		else {   // sparse: a few spikes, the pages between stay untouched
			Noise nz;
			for (int i = 0; i < 4000; i++) {
				const size_t f = (size_t) ((nz.next() * 0.5 + 0.5) * (double) (sizes[k] - 1));
				t.ch[i & 1][f] = nz.next() * 4.f;
			}
			t.ch[0][sizes[k] - 1] = 3.f;
			t.length = sizes[k];
		}
		rka::Overview o;
		o.build(t);
		CHECK(mismatches(o, t, sizes[k]) == 0, "%zu frames: %d columns differ", sizes[k], mismatches(o, t, sizes[k]));
		CHECK(o.settled(), "build leaves it settled");
	}
}

void touch_then_service() {
	const size_t n = 48000;
	rka::Tape t;
	t.allocate(n, 0);
	fill(t, n);
	rka::Overview o;
	o.build(t);
	rka::Overview before;
	before.build(t);
	const size_t f = 20000;
	t.ch[1][f] = 7.f;
	t.ch[0][f] = -6.f;
	o.touch(f, f + 1, n);
	CHECK(!o.settled(), "touched: not settled");
	const int col = (int) ((unsigned long long) f * COLS / n);
	// The column's frames, in 94-sample steps: afterwards just that one is
	// right and nothing else was rescanned (others are the old values).
	const int span = (int) (colStart(col + 1, n) - colStart(col, n));
	for (int i = 0; i < span; i++)
		o.service(t, 1);
	CHECK(o.settled(), "settled after the column's frames");
	CHECK(o.hi[col].load() == 7.f && o.lo[col].load() == -6.f, "column %d: %g %g", col, o.lo[col].load(), o.hi[col].load());
	CHECK(mismatches(o, t, n) == 0, "all columns right");
	// Not rescanned: change another column behind the overview's back.
	t.ch[0][1000] = 9.f;
	o.touch(f, f + 1, n);
	service_all(o, t, 1000);
	const int other = (int) ((unsigned long long) 1000 * COLS / n);
	CHECK(o.hi[other].load() != 9.f, "an untouched column was not rescanned");
	CHECK(o.hi[col].load() == 7.f, "touched column still right");
}

void shrinks() {
	const size_t n = 48000;
	rka::Tape t;
	t.allocate(n, 0);
	fill(t, n);
	rka::Overview o;
	o.build(t);
	const int col = 100;
	const size_t a = colStart(col, n), b = colStart(col + 1, n);
	const float was = o.hi[col].load();
	for (size_t f = a; f < b; f++) {
		t.ch[0][f] *= 0.5f;
		t.ch[1][f] *= 0.5f;
	}
	o.touch(a, b, n);
	service_all(o, t, 16);
	CHECK(o.hi[col].load() < was, "hi fell: %g -> %g", was, o.hi[col].load());
	CHECK(mismatches(o, t, n) == 0, "matches");
}

void budget() {
	const size_t n = 48000;
	rka::Tape t;
	t.allocate(n, 0);
	fill(t, n);
	rka::Overview o;
	o.build(t);
	const int col = 7;
	const size_t a = colStart(col, n), b = colStart(col + 1, n);
	const float oldHi = o.hi[col].load();
	// Put a peak at the column's last frame: seen only once all is read.
	t.ch[0][b - 1] = 50.f;
	o.touch(a, b, n);
	int calls = 0;
	while (!o.settled() && calls < 1000) {
		o.service(t, 8);
		calls++;
		if (!o.settled())
			CHECK(o.hi[col].load() == oldHi, "half-scanned column shown after %d calls", calls);
	}
	CHECK(calls == (int) ((b - a + 7) / 8), "calls %d for %zu frames", calls, b - a);
	CHECK(o.hi[col].load() == 50.f, "finished column right");
	// One call reads at most 8 frames: a peak at frame 8 of the column is not
	// in the scratch after one call, and the column is not yet published.
	t.ch[0][b - 1] = 0.f;
	o.touch(a, b, n);
	o.service(t, 8);
	CHECK(!o.settled() && o.hi[col].load() == 50.f, "one call of 8 does not finish a column of %zu", b - a);
	// A zero budget reads nothing.
	rka::Overview p;
	p.build(t);
	p.touch(a, b, n);
	p.service(t, 0);
	CHECK(!p.settled(), "budget 0 does nothing");
	// Touched again while being rescanned: ends correct.
	rka::Overview q;
	q.build(t);
	t.ch[0][a + 2] = 20.f;
	q.touch(a, b, n);
	q.service(t, 10);          // frames a..a+9 read: sees 20
	t.ch[0][a + 2] = 0.f;      // changed after it was read
	t.ch[1][a + 3] = -30.f;
	q.touch(a + 2, a + 4, n);
	service_all(q, t, 8);
	CHECK(mismatches(q, t, n) == 0, "touched during its rescan: right");
	CHECK(q.hi[col].load() < 20.f && q.lo[col].load() == -30.f, "%g %g", q.lo[col].load(), q.hi[col].load());
}

void grows_with_recording() {
	const size_t cap = 48000;
	rka::Tape t;
	t.allocate(cap, 480);
	rka::Overview o;
	o.build(t);   // empty
	CHECK(mismatches(o, t, 0) == 0, "empty tape: all 0");
	Noise nz;
	size_t written = 0;
	for (int step = 0; step < 4; step++) {
		const size_t add = 6000;
		for (size_t f = written; f < written + add; f++) {
			t.ch[0][f] = nz.next();
			t.ch[1][f] = nz.next();
		}
		o.touch(written, written + add, cap);
		written += add;
		t.length = written;
		service_all(o, t, 64);
		CHECK(mismatches(o, t, cap) == 0, "after %zu frames recorded", written);
	}
	// The left eighth-and-a-half is filled, the right is silent.
	const int edge = (int) ((unsigned long long) written * COLS / cap);
	CHECK(o.hi[0].load() > 0.5f && o.hi[edge - 2].load() > 0.5f, "left filled");
	CHECK(o.hi[edge + 2].load() == 0.f && o.lo[COLS - 1].load() == 0.f, "right empty");
}

void short_tape() {
	for (size_t n = 1; n <= 7; n += 3) {
		rka::Tape t;
		t.allocate(n, 0);
		fill(t, n);
		rka::Overview o;
		o.build(t);
		CHECK(mismatches(o, t, n) == 0, "build, %zu frames", n);
		t.ch[0][n - 1] = 2.f;
		o.touch(0, n, n);
		service_all(o, t, 3);
		CHECK(mismatches(o, t, n) == 0, "touch and service, %zu frames", n);
		for (int c = 0; c < COLS; c++)
			CHECK(o.lo[c].load() <= o.hi[c].load(), "column %d defined", c);
	}
	// A touch on a zero-length tape, and clear.
	rka::Tape e;
	rka::Overview o;
	o.build(e);
	o.touch(0, 10, 0);
	o.service(e, 100);
	CHECK(o.settled(), "empty tape settled");
	o.clear();
	CHECK(o.hi[3].load() == 0.f && o.settled(), "clear");
}

}

int main() {
	build_matches();
	touch_then_service();
	shrinks();
	budget();
	grows_with_recording();
	short_tape();
	return rkatest::finish("eddy test_overview");
}
