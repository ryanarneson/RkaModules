// Doldrums' window model (TapeView.hpp): the columns against a scan of the
// tape (recording, frozen, rebuilt), the scroll, the marks, the state words
// and the drag.
#include "TapeView.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rka::doldrums;

namespace {

uint32_t rng = 12345;
float noise() {
	rng = rng * 1664525u + 1013904223u;
	return float(rng >> 8) * (1.f / 8388608.f) - 1.f;
}

// The owner's pattern: the frame written to the ring, then to the view as
// the ring holds it.
struct Rig {
	Ring ring;
	TapeView view;
	explicit Rig(long long cap) {
		ring.allocate(cap);
		view.configure(cap, 0);
	}
	void write(float l, float r, bool frozen) {
		const long long before = ring.now();
		ring.write(l, r, frozen);
		if (ring.now() > before) {
			const long long p = ring.physical(ring.now() - 1);
			view.wrote(ring.channel(0)[p], ring.channel(1)[p]);
		}
	}
	// n frames of a signal that swells and falls, different per side.
	void record(long long n, bool frozen = false) {
		for (long long i = 0; i < n; i++) {
			const float env = float((ring.now() / 7919) % 11) / 10.f;
			write(noise() * env * 4.f, noise() * (1.f - env) * 3.f + 0.5f, frozen);
		}
	}
	DoldrumsState state() const {
		DoldrumsState s;
		s.now = ring.now();
		s.capacity = ring.capacity();
		return s;
	}
};

// The first frame of column j, by its own arithmetic (not the view's).
long long first(long long j, long long cap) { return j * cap / 512; }

// Each snapshot column against a scan of the ring, for the columns
// jo-511..jo, jo the column that holds the newest frame's successor.
void matches(const Ring& ring, const TapeSnapshot& s, const char* what) {
	const long long cap = ring.capacity(), now = ring.now();
	long long jo = 0;
	while (first(jo + 1, cap) <= now) jo++;
	int bad = 0;
	for (int i = 0; i < 512; i++) {
		const long long j = jo - 511 + i;
		float lo = 0.f, hi = 0.f;
		if (j >= 0) {
			bool any = false;
			for (long long f = first(j, cap); f < first(j + 1, cap) && f < now; f++) {
				const long long p = ring.physical(f);
				for (int c = 0; c < 2; c++) {
					const float v = ring.channel(c)[p];
					lo = !any || v < lo ? v : lo;
					hi = !any || v > hi ? v : hi;
					any = true;
				}
			}
		}
		if (s.low[i] != lo || s.high[i] != hi) {
			if (!bad++)
				std::printf("%s: column %d (absolute %lld) is %g..%g, the tape says %g..%g\n", what, i, j, s.low[i],
				            s.high[i], lo, hi);
		}
	}
	CHECK(bad == 0, "%s: %d columns differ from a scan of the tape", what, bad);
}

void columns_match_the_tape() {
	const long long caps[] = {1440000, 1323000, 300, 1000};   // 48 kHz, 44.1 kHz (not a multiple of 512), tiny
	const double secs[] = {10, 30, 75};
	for (long long cap : caps)
		for (double sec : secs) {
			const long long frames = cap < 5000 ? long(cap * sec / 30) + 7 : (long long)(sec * double(cap) / 30.0);
			Rig rig(cap);
			rig.record(frames);
			char what[96];
			std::snprintf(what, sizeof what, "cap %lld, %.0f s recording", cap, sec);
			TapeSnapshot s;
			rig.view.publish(rig.state());
			CHECK(rig.view.read(s), "%s: read", what);
			matches(rig.ring, s, what);

			// Frozen: the fade-out is written, then the tape stands still.
			rig.record(cap < 5000 ? 5 : 480, true);
			rig.record(200, true);
			rig.view.publish(rig.state());
			CHECK(rig.view.read(s), "frozen read");
			std::snprintf(what, sizeof what, "cap %lld, %.0f s, frozen", cap, sec);
			matches(rig.ring, s, what);

			// A tape that arrives: the same columns from the ring alone.
			TapeView built;
			built.rebuild(rig.ring);
			built.publish(rig.state());
			TapeSnapshot t;
			CHECK(built.read(t), "rebuilt read");
			std::snprintf(what, sizeof what, "cap %lld, %.0f s, rebuilt", cap, sec);
			matches(rig.ring, t, what);
			CHECK(std::memcmp(s.low, t.low, sizeof s.low) == 0 && std::memcmp(s.high, t.high, sizeof s.high) == 0,
			      "%s: the rebuilt columns are the live ones", what);

			// A view that held another tape's columns: rebuild leaves nothing of them.
			TapeView used;
			used.configure(cap, 12345);
			for (int i = 0; i < 20000; i++) used.wrote(noise() * 9.f, noise() * 9.f);
			used.rebuild(rig.ring);
			used.publish(rig.state());
			TapeSnapshot u;
			CHECK(used.read(u), "used read");
			CHECK(std::memcmp(s.low, u.low, sizeof s.low) == 0 && std::memcmp(s.high, u.high, sizeof s.high) == 0,
			      "%s: a view rebuilt over another tape's columns holds only this tape's", what);

			// And it goes on from there as the live one does.
			rig.record(1000);
			for (int i = 0; i < 1000; i++) {
				const long long p = rig.ring.physical(rig.ring.now() - 1000 + i);
				built.wrote(rig.ring.channel(0)[p], rig.ring.channel(1)[p]);
			}
			rig.view.publish(rig.state());
			built.publish(rig.state());
			TapeSnapshot a, b;
			CHECK(rig.view.read(a) && built.read(b), "read both");
			CHECK(std::memcmp(a.low, b.low, sizeof a.low) == 0 && std::memcmp(a.high, b.high, sizeof a.high) == 0,
			      "%s: a rebuilt view then fed matches the live one", what);

			// Cleared with the tape: blank, and still aligned.
			rig.ring.clear();
			rig.view.clear();
			rig.view.publish(rig.state());
			CHECK(rig.view.read(a), "read after clear");
			bool blank = true;
			for (int i = 0; i < 512; i++) blank = blank && a.low[i] == 0.f && a.high[i] == 0.f;
			CHECK(blank, "%s: cleared columns are blank", what);
			rig.ring.clear();
			rig.view.clear();   // again, and no publish between it and the frames
			rig.record(cap / 3);
			rig.view.publish(rig.state());
			rig.view.read(a);
			std::snprintf(what, sizeof what, "cap %lld, after a clear", cap);
			matches(rig.ring, a, what);
		}
}

void alignment_for_hours() {
	// Ten tapes' worth at a capacity 512 does not divide: still the tape's columns.
	Rig rig(1323000);
	for (int pass = 0; pass < 10; pass++)
		rig.record(1323000);
	rig.record(12345);
	TapeSnapshot s;
	rig.view.publish(rig.state());
	CHECK(rig.view.read(s), "read");
	matches(rig.ring, s, "ten tapes later");
}

void scrolls() {
	const long long cap = 51200;   // 100 frames a column
	Rig rig(cap);
	rig.record(0);
	for (long long i = 0; i < 2 * cap + 150; i++) rig.write(0.f, 0.f, false);   // silence, 50 frames into a column
	rig.write(2.f, -3.f, false);                                                  // a one-frame burst
	auto where = [&]() {
		TapeSnapshot s;
		rig.view.publish(rig.state());
		CHECK(rig.view.read(s), "read");
		int at = -1, n = 0;
		for (int i = 0; i < 512; i++)
			if (s.high[i] != 0.f || s.low[i] != 0.f) {
				at = i;
				n++;
				CHECK(s.high[i] == 2.f && s.low[i] == -3.f, "the burst's peaks %g %g", s.low[i], s.high[i]);
			}
		CHECK(n == 1, "one column holds the burst, got %d", n);
		return at;
	};
	CHECK(where() == 511, "the burst is in the open column, at the right edge");
	// It sat at frame 100k + 50, so the column closes 49 frames after it was written.
	for (int i = 0; i < 48; i++) rig.write(0.f, 0.f, false);
	CHECK(where() == 511, "not yet moved");
	rig.write(0.f, 0.f, false);
	CHECK(where() == 510, "moved a column when it closed");
	for (int m = 1; m <= 20; m++) {
		for (int i = 0; i < 99; i++) rig.write(0.f, 0.f, false);
		CHECK(where() == 510 - (m - 1), "still in its column %d", m);
		rig.write(0.f, 0.f, false);
		CHECK(where() == 510 - m, "a column left every capacity / 512 frames (%d)", m);
	}
	// Frozen: the fade-out (5 ms, here the ring's own 1 frame default) is written, then it stands.
	for (int i = 0; i < 5; i++) rig.write(0.f, 0.f, true);
	const int at = where();
	for (int i = 0; i < 5000; i++) rig.write(1.f, 1.f, true);
	CHECK(where() == at, "frozen, the burst stands still");
	// Run to its leaving: a column at the very left, then gone.
	for (int i = 0; i < 100 * 600; i++) rig.write(0.f, 0.f, false);
	TapeSnapshot s;
	rig.view.publish(rig.state());
	rig.view.read(s);
	bool gone = true;
	for (int i = 0; i < 512; i++) gone = gone && s.high[i] == 0.f && s.low[i] == 0.f;
	CHECK(gone, "a burst older than the window is not in it");
}

bool near(float a, double b) { return std::fabs(double(a) - b) < 1e-6; }

void marks() {
	const long long cap = 1440000;
	TapeView v;
	TapeSnapshot s;
	DoldrumsState st;
	st.capacity = cap;
	st.now = 3000000;
	st.grain = 16384;
	const long long base = st.now - cap;

	// Position 1: the anchor is at now.
	st.anchor = st.now - lagFrames(1.f, cap, st.grain);
	st.end = st.now - 20000;
	v.publish(st);
	CHECK(v.read(s), "read");
	CHECK(near(s.anchor, 1.0), "Position 1: anchor at now, %g", s.anchor);
	CHECK(near(s.readEnd, 1.0 - 20000.0 / cap), "read end %g", s.readEnd);
	CHECK(near(s.readEnd - s.readStart, 16384.0 / cap), "the slice's true width %g", s.readEnd - s.readStart);

	// Position 0: a grain short of the oldest frame.
	st.anchor = st.now - lagFrames(0.f, cap, st.grain);
	v.publish(st);
	v.read(s);
	CHECK(near(s.anchor, 16384.0 / cap), "Position 0: anchor %g", s.anchor);

	// Three grain sizes, the width true each time.
	const int grains[] = {8192, 16384, 32768};
	for (int g : grains) {
		st.grain = g;
		st.end = base + cap / 2 + g;
		v.publish(st);
		v.read(s);
		CHECK(near(s.readEnd - s.readStart, double(g) / cap), "width of %d: %g", g, s.readEnd - s.readStart);
		CHECK(near(s.readStart, 0.5), "slice starts at the middle: %g", s.readStart);
	}

	// Held, tape running: the read point rides left with its sound, below 0.
	st.grain = 16384;
	st.end = base + 100000;
	st.held = true;
	float prev = 2.f;
	bool below = false;
	for (int k = 0; k < 30; k++) {
		st.now += 15000;
		v.publish(st);
		v.read(s);
		CHECK(s.readEnd < prev, "the held read point moves left (%g after %g)", s.readEnd, prev);
		CHECK(near(s.readEnd - s.readStart, 16384.0 / cap), "width kept while it rides");
		prev = s.readEnd;
		if (s.readStart < 0.f && s.readEnd > 0.f) below = true;
	}
	CHECK(below, "the slice crossed the left edge with its start below 0");
	CHECK(s.readEnd < 0.f && s.readStart < 0.f, "off the edge both marks are below 0: %g %g", s.readStart, s.readEnd);
	st.now += 40 * cap;
	v.publish(st);
	v.read(s);
	CHECK(s.readStart == -1.f && s.readEnd == -1.f, "long gone, held to -1 so it stays drawable: %g %g", s.readStart, s.readEnd);
	CHECK(s.held && !s.frozen, "flags carried");

	// Held and frozen: stands still.
	st.frozen = true;
	v.publish(st);
	TapeSnapshot s2;
	v.read(s);
	v.publish(st);
	v.read(s2);
	CHECK(s.readStart == s2.readStart && s.readEnd == s2.readEnd, "no motion with nothing new");

	// No tape: finite and 0.
	DoldrumsState none;
	v.publish(none);
	CHECK(v.read(s) && s.anchor == 0.f && s.readStart == 0.f && s.readEnd == 0.f, "no capacity: marks 0");
}

void state_words() {
	TapeView v;
	TapeSnapshot s;
	DoldrumsState st;
	st.capacity = 1000;
	auto word = [&](bool frozen, bool held, bool stopped) {
		st.frozen = frozen;
		st.held = held;
		st.stopped = stopped;
		v.publish(st);
		CHECK(v.read(s), "read");
		CHECK(s.frozen == frozen && s.held == held && s.stopped == stopped, "flags");
		return std::string(s.state);
	};
	CHECK(word(false, false, false) == "", "plain running says nothing");
	CHECK(word(true, false, false) == "FROZEN", "frozen");
	CHECK(word(false, true, false) == "HELD", "held");
	CHECK(word(false, false, true) == "STOPPED", "stopped");
	CHECK(word(true, true, false) == "FROZEN HELD", "frozen and held");
	CHECK(word(true, false, true) == "FROZEN STOPPED", "frozen and stopped");
	CHECK(word(false, true, true) == "STOPPED", "stopped wins over held");
	CHECK(word(true, true, true) == "FROZEN STOPPED", "all three");
	CHECK(std::strlen("FROZEN STOPPED") < sizeof s.state, "the longest word fits with its end");
}

void drag_maps_to_position() {
	CHECK(positionForX(1.f) == 1.f, "x = 1 is Position 1");
	CHECK(positionForX(0.f) == 0.f, "x = 0 is Position 0");
	CHECK(positionForX(-0.5f) == 0.f && positionForX(1.5f) == 1.f, "held to the ends");
	CHECK(positionForX(0.5f) > 0.49f && positionForX(0.5f) < 0.51f, "the middle");
	const long long caps[] = {1440000, 1323000, 2646000};
	const int grains[] = {8192, 16384, 32768, 65536};
	for (long long cap : caps)
		for (int g : grains) {
			CHECK(positionForX(1.f, cap, g) == 1.f, "x 1 at %lld/%d", cap, g);
			CHECK(positionForX(float(double(g) / cap), cap, g) < 1e-6f, "Position 0's place is Position 0");
			CHECK(positionForX(float(double(g) / cap) * 0.5f, cap, g) == 0.f, "left of what Position 0 reaches");
			CHECK(positionForX(0.f, cap, g) == 0.f, "x 0");
			for (int k = 0; k <= 100; k++) {
				const float p = float(k) / 100.f;
				const long long now = 5 * cap + 1234;
				const long long anchor = now - lagFrames(p, cap, g);
				const float x = float(double(anchor - (now - cap)) / double(cap));
				const float back = positionForX(x, cap, g);
				CHECK(std::fabs(back - p) < 2e-4f, "Position %g through the window is %g (cap %lld grain %d)", p, back, cap, g);
			}
		}
	CHECK(positionForX(std::nanf(""), 1440000, 16384) == 1.f, "NaN is the default, now");
	CHECK(positionForX(0.5f, 1000, 2000) == 1.f, "a grain bigger than the tape leaves no lag");
}

void read_before_publish_and_hostile() {
	TapeView v;
	TapeSnapshot s;
	std::memset(&s, 0x5a, sizeof s);
	CHECK(!v.read(s), "nothing published: false");
	CHECK(s.low[3] != 0.f || ((unsigned char*) &s)[12] == 0x5a, "out untouched");
	v.configure(1000, 0);
	v.wrote(std::nanf(""), INFINITY);
	v.wrote(1e30f, -1e30f);
	DoldrumsState st;
	st.capacity = 1000;
	st.now = 2;
	v.publish(st);
	CHECK(v.read(s), "read");
	CHECK(s.low[511] == -1e30f && s.high[511] == 1e30f, "non-finite counts as 0, finite kept");
}

} // namespace

int main() {
	columns_match_the_tape();
	alignment_for_hours();
	scrolls();
	marks();
	state_words();
	drag_maps_to_position();
	read_before_publish_and_hostile();
	return rkatest::finish("doldrums/test_view");
}
