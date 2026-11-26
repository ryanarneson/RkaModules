// Doldrums' ring tape (Ring.hpp): the wrap, the frozen standstill, the fade
// at Freeze's edges, the bounds of copy (the buffer fenced with
// ASan's poisoning, as Eddy's hostile test fences its tapes), hostile
// samples, and Freeze toggled every sample.
#include "Ring.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
#if defined(__SANITIZE_ADDRESS__)
#define RING_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RING_ASAN 1
#endif
#endif
#ifdef RING_ASAN
#include <sanitizer/asan_interface.h>
#endif

using rka::doldrums::Ring;

namespace {

// The GUARD frames either side of each channel, poisoned (or not).
void fence(const Ring& r, bool on) {
#ifdef RING_ASAN
	for (int c = 0; c < 2; c++) {
		const float* ch = r.channel(c);
		if (!ch)
			continue;
		void* lo = const_cast<float*>(ch) - Ring::GUARD;
		void* hi = const_cast<float*>(ch) + r.capacity();
		if (on) {
			ASAN_POISON_MEMORY_REGION(lo, Ring::GUARD * sizeof(float));
			ASAN_POISON_MEMORY_REGION(hi, Ring::GUARD * sizeof(float));
		}
		else {
			ASAN_UNPOISON_MEMORY_REGION(lo, Ring::GUARD * sizeof(float));
			ASAN_UNPOISON_MEMORY_REGION(hi, Ring::GUARD * sizeof(float));
		}
	}
#else
	(void) r; (void) on;
#endif
}

uint32_t bitsOf(float v) {
	uint32_t b;
	std::memcpy(&b, &v, sizeof b);
	return b;
}

// A frame's value as a function of its absolute index, distinct per channel.
float sampleL(long long i) { return float(i % 977) * 0.01f - 4.f; }
float sampleR(long long i) { return float((i * 7) % 1013) * -0.013f + 3.f; }

// Largest step between neighbours in absolute order, oldest to newest.
float chronoStep(const Ring& r) {
	std::vector<float> a(size_t(r.capacity())), b(a.size());
	if (!r.copy(r.now(), int(r.capacity()), a.data(), b.data()))
		return 1e9f;
	float worst = 0.f;
	for (size_t i = 1; i < a.size(); i++)
		worst = std::max(worst, std::fabs(a[i] - a[i - 1]));
	return worst;
}

void writes_and_reads_back() {
	Ring r;
	CHECK(r.allocate(1000), "allocate");
	CHECK(r.capacity() == 1000, "capacity");
	CHECK(r.now() == 0, "starts at 0");
	r.setFadeFrames(1);
	const long long total = 2537; // wraps twice
	for (long long i = 0; i < total; i++)
		r.write(sampleL(i), sampleR(i), false);
	CHECK(r.now() == total, "now counts frames, not wrapped");
	// Frames written long ago are where physical() says, bit for bit.
	for (long long i = total - 1000; i < total; i++) {
		const long long p = r.physical(i);
		CHECK(bitsOf(r.channel(0)[p]) == bitsOf(sampleL(i)), "L physical %lld", i);
		CHECK(bitsOf(r.channel(1)[p]) == bitsOf(sampleR(i)), "R physical %lld", i);
	}
	// Whole tape, then pieces that cross the physical end.
	const long long ends[] = {total, total - 1, total - 300, total - 999};
	const int ns[] = {1000, 1, 700, 1};
	for (int k = 0; k < 4; k++) {
		const size_t len = size_t(ns[k]);
		std::vector<float> l(len, 0.f);
		std::vector<float> rr(len, 0.f);
		CHECK(r.copy(ends[k], ns[k], l.data(), rr.data()), "copy %d", k);
		bool same = true;
		for (int i = 0; i < ns[k]; i++) {
			const long long a = ends[k] - ns[k] + i;
			same = same && bitsOf(l[size_t(i)]) == bitsOf(sampleL(a)) && bitsOf(rr[size_t(i)]) == bitsOf(sampleR(a));
		}
		CHECK(same, "copy %d returns the frames bit for bit", k);
	}
	// One channel only.
	float one[3];
	CHECK(r.copy(total, 3, one, nullptr), "copy L only");
	CHECK(bitsOf(one[2]) == bitsOf(sampleL(total - 1)), "L only value");
	// clear() silences and keeps the position.
	r.clear();
	CHECK(r.now() == total, "clear keeps now");
	float l = 1.f, rr = 1.f;
	CHECK(r.copy(total, 1, &l, &rr) && l == 0.f && rr == 0.f, "clear silences");
	r.release();
	CHECK(r.capacity() == 0 && r.channel(0) == nullptr, "release");
}

void now_stands_still_frozen() {
	Ring r;
	r.allocate(500);
	r.setFadeFrames(16);
	for (int i = 0; i < 100; i++)
		r.write(5.f, -5.f, false);
	CHECK(r.now() == 100, "recording advances");
	for (int i = 0; i < 16; i++)
		r.write(5.f, -5.f, true);
	CHECK(r.now() == 116, "the fade-out is written: %lld", r.now());
	std::vector<float> before(500), after(500), junk(500);
	r.copy(r.now(), 500, before.data(), junk.data());
	for (int i = 0; i < 5000; i++)
		r.write(float(i), float(-i), true);
	CHECK(r.now() == 116, "now stands still frozen: %lld", r.now());
	r.copy(r.now(), 500, after.data(), junk.data());
	CHECK(before == after, "nothing is written frozen");
	// A thaw goes on from the same place.
	r.write(5.f, 5.f, false);
	CHECK(r.now() == 117, "recording resumes at the same place");
	// Frozen before the first frame: nothing is written either.
	Ring q;
	q.allocate(100);
	q.setFadeFrames(4);
	for (int i = 0; i < 10; i++)
		q.write(1.f, 1.f, true);
	CHECK(q.now() == 4, "a fresh tape fades out too: %lld", q.now());
}

void freeze_edges_fade() {
	const int F = 48;
	const float bound = 10.f / float(F) + 1e-6f;
	Ring r;
	r.allocate(2000);
	r.setFadeFrames(F);
	for (int i = 0; i < 3000; i++) // full, all DC
		r.write(10.f, -10.f, false);
	CHECK(chronoStep(r) == 0.f, "DC tape has no steps");
	// Freeze: the tape runs 10 -> 0 in F steps and stops.
	float worst = 0.f;
	for (int i = 0; i < 400; i++) {
		r.write(10.f, -10.f, true);
		worst = std::max(worst, chronoStep(r));
	}
	CHECK(worst <= bound, "largest step across the freeze %g > %g", worst, bound);
	CHECK(r.now() == 3000 + F, "fade-out length");
	{
		float l, rr;
		CHECK(r.copy(r.now(), 1, &l, &rr) && l == 0.f && rr == 0.f, "newest frame at a freeze is 0");
	}
	{
		// What was on the tape stays as it was: no taper at the write point.
		float l, rr;
		CHECK(r.copy(r.now() - r.capacity() + 1, 1, &l, &rr) && l == 10.f && rr == -10.f, "the oldest frame is untouched");
	}
	// Thaw: through the whole fade-in.
	worst = 0.f;
	for (int i = 0; i < 400; i++) {
		r.write(10.f, -10.f, false);
		worst = std::max(worst, chronoStep(r));
	}
	CHECK(worst <= bound, "largest step across the thaw %g > %g", worst, bound);
	float l, rr;
	r.copy(r.now(), 1, &l, &rr);
	CHECK(l == 10.f && rr == -10.f, "recording again at full level");
	CHECK(r.now() == 3000 + F + 400, "now after the thaw");
	// The steps are real: a fade is happening, not a no-op.
	{
		Ring q;
		q.allocate(200);
		q.setFadeFrames(F);
		for (int i = 0; i < 300; i++)
			q.write(10.f, 10.f, false);
		for (int i = 0; i < F; i++)
			q.write(10.f, 10.f, true);
		float a, b;
		q.copy(q.now() - F / 2, 1, &a, &b);
		CHECK(a > 4.f && a < 6.f, "mid-fade the tape is half way: %g", a);
	}
	// Freeze before the tape is full: the oldest frames are silence.
	{
		Ring q;
		q.allocate(2000);
		q.setFadeFrames(F);
		for (int i = 0; i < 700; i++)
			q.write(10.f, 10.f, false);
		for (int i = 0; i < 100; i++)
			q.write(10.f, 10.f, true);
		float worstQ = 0.f;
		for (int i = 0; i < 100; i++) {
			q.write(10.f, 10.f, false);
			worstQ = std::max(worstQ, chronoStep(q));
		}
		// From 0 to the first 10 of the recording is the tape's arrival, not a freeze.
		CHECK(worstQ <= 10.f + 1e-6f, "bounded");
	}
}

void copy_bounds() {
	Ring r;
	r.allocate(256);
	r.setFadeFrames(1);
	for (long long i = 0; i < 700; i++)
		r.write(sampleL(i), sampleR(i), false);
	fence(r, true);
	const long long now = r.now(), cap = r.capacity();
	std::vector<float> l(300, 7.f), rr(300, 7.f);
	auto zeros = [&](int n) {
		for (int i = 0; i < n; i++)
			if (l[size_t(i)] != 0.f || rr[size_t(i)] != 0.f)
				return false;
		return true;
	};
	// The tightest legal windows.
	CHECK(r.copy(now, 256, l.data(), rr.data()), "newest, the whole tape");
	CHECK(r.copy(now - cap + 10, 10, l.data(), rr.data()), "oldest 10 frames");
	CHECK(bitsOf(l[0]) == bitsOf(sampleL(now - cap)), "oldest frame is now - capacity");
	CHECK(r.copy(now, 1, l.data(), rr.data()), "last frame");
	CHECK(r.copy(now - cap + 1, 1, l.data(), rr.data()), "oldest frame");
	// One frame too old, one too new.
	std::fill(l.begin(), l.end(), 7.f);
	std::fill(rr.begin(), rr.end(), 7.f);
	CHECK(!r.copy(now - cap + 10 - 1, 10, l.data(), rr.data()), "one frame too old");
	CHECK(zeros(10), "one too old gives zeros");
	std::fill(l.begin(), l.end(), 7.f);
	std::fill(rr.begin(), rr.end(), 7.f);
	CHECK(!r.copy(now + 1, 10, l.data(), rr.data()), "one frame too new");
	CHECK(zeros(10), "one too new gives zeros");
	std::fill(l.begin(), l.end(), 7.f);
	std::fill(rr.begin(), rr.end(), 7.f);
	CHECK(!r.copy(now, 257, l.data(), rr.data()), "longer than the tape");
	CHECK(zeros(257 > 300 ? 300 : 257), "longer gives zeros");
	CHECK(!r.copy(now, -5, l.data(), rr.data()), "negative n");
	CHECK(!r.copy(-100000, 10, l.data(), rr.data()), "far too old");
	CHECK(!r.copy(std::numeric_limits<long long>::max(), 10, l.data(), rr.data()), "far too new");
	CHECK(!r.copy(std::numeric_limits<long long>::min(), 10, l.data(), rr.data()), "far too old, min");
	// Before the tape has wrapped: a window that is all silence is legal.
	Ring q;
	q.allocate(256);
	q.setFadeFrames(1);
	for (int i = 0; i < 40; i++)
		q.write(1.f, 1.f, false);
	fence(q, true);
	CHECK(q.copy(q.now(), 256, l.data(), rr.data()), "not yet full: window reaches before frame 0");
	CHECK(l[0] == 0.f && l[215] == 0.f && l[216] == 1.f, "silence, then the input");
	// The window at every start across a wrap, never leaving the allocation.
	bool all = true;
	for (long long e = now - cap + 33; e <= now; e++)
		all = all && r.copy(e, 33, l.data(), rr.data());
	CHECK(all, "every 33-frame window across the wrap");
	// physical() stays in [0, capacity) for any index.
	const long long probes[] = {0, -1, -256, -257, 255, 256, now, 1LL << 40, -(1LL << 40) - 3};
	for (long long p : probes) {
		const long long ph = r.physical(p);
		CHECK(ph >= 0 && ph < cap, "physical(%lld) = %lld", p, ph);
	}
	fence(r, false);
	fence(q, false);
}

void hostile_samples() {
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	Ring r;
	r.allocate(64);
	r.setFadeFrames(4);
	r.write(nan, inf, false);
	r.write(-inf, nan, false);
	r.write(1e30f, -1e30f, false);
	r.write(3.f, 4.f, false);
	float l[4], rr[4];
	CHECK(r.copy(r.now(), 4, l, rr), "copy");
	bool finite = true;
	for (int i = 0; i < 4; i++)
		finite = finite && std::isfinite(l[i]) && std::isfinite(rr[i]);
	CHECK(finite, "NaN and inf do not reach the tape");
	CHECK(l[0] == 0.f && rr[0] == 0.f && l[1] == 0.f && rr[1] == 0.f, "they read as 0");
	// An unallocated ring, a null channel, a tiny ring.
	Ring u;
	u.write(1.f, 1.f, false);
	CHECK(u.now() == 0, "unallocated: write is nothing");
	CHECK(!u.copy(0, 1, l, rr), "unallocated: copy fails");
	CHECK(u.copy(0, 0, l, rr), "n = 0 is trivially fine");
	CHECK(u.physical(5) == 0, "unallocated physical");
	CHECK(!u.allocate(0) && !u.allocate(-3), "no frames, no ring");
	Ring t;
	CHECK(t.allocate(1), "one frame");
	t.setFadeFrames(100);
	for (int i = 0; i < 10; i++)
		t.write(2.f, 2.f, i % 3 == 0);
	CHECK(t.copy(t.now(), 1, l, rr), "one-frame ring copies");
	t.setFadeFrames(-4);
	t.setFadeFrames(0);
	t.write(1.f, 1.f, true);
	t.write(1.f, 1.f, false);
	CHECK(true, "odd fades do not crash");
	// Reallocating starts over.
	Ring v;
	v.allocate(32);
	v.write(1.f, 1.f, false);
	v.allocate(64);
	CHECK(v.now() == 0 && v.capacity() == 64, "allocate again resets");
}

void rapid_freeze() {
	const int F = 240; // 5 ms at 48 kHz
	// Two floats near 10 V are 9.5e-7 apart, so the rounded step of the two
	// frames can pass the ideal one by up to two of them.
	const float bound = 10.f / float(F) + 2e-6f;
	for (int pattern = 0; pattern < 3; pattern++) {
		Ring r;
		r.allocate(4000);
		r.setFadeFrames(F);
		for (int i = 0; i < 5000; i++) // full of DC
			r.write(10.f, -10.f, false);
		float worst = 0.f;
		uint32_t seed = 12345;
		long long prev = r.now();
		for (int i = 0; i < 48000; i++) {
			bool frozen;
			if (pattern == 0)
				frozen = (i & 1) != 0;
			else if (pattern == 1) {
				seed = seed * 1664525u + 1013904223u;
				frozen = (seed >> 16) & 1;
			}
			else
				frozen = (i / 100) & 1; // fades that end, and some that do not
			r.write(10.f, -10.f, frozen);
			if (r.now() > prev) {
				float a[2], b[2];
				if (r.now() - prev == 1 && r.copy(r.now(), 2, a, b))
					worst = std::max(worst, std::fabs(a[1] - a[0]));
				prev = r.now();
			}
		}
		CHECK(worst <= bound, "pattern %d: step %g > %g", pattern, worst, bound);
		CHECK(worst > 0.f, "pattern %d: the fade really ran", pattern);
	}
}

} // namespace

int main() {
	writes_and_reads_back();
	now_stands_still_frozen();
	freeze_edges_fade();
	copy_bounds();
	hostile_samples();
	rapid_freeze();
	return rkatest::finish("test_ring");
}
