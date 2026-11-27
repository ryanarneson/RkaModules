// Doldrums' read point and its passes (doldrums/Pass.hpp), as pure logic: no
// sound, no tape. Each check is written from the design's "The read point,
// Position and Stretch", "Held" and "Play once, and RUN", and the plan's
// second deliberate difference (not frozen, Position fully left). Capacity
// 1 440 000 (30 s at 48 kHz), grain 16384, hop 8192 throughout.
#include "doldrums/Pass.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

using namespace rka::doldrums;

const long long C = 1440000;
const int G = 16384;
const int H = 8192;
const long long ROOM = C - G;      // the lag at Position 0
const long long NOW0 = 5000000;    // a tape long since full
const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// A PassLogic driven a grain at a time. Recording, the tape moves on a hop
// after each grain; the one-shot flags clear after each grain.
struct Sim {
	PassLogic p;
	PassIn in;
	long long nowWas = 0;   // `now` as the last grain saw it
	PassOut o{};

	Sim(float pos, double stretch, bool frozen, bool stopAtEnd = false) {
		in = PassIn{};
		in.now = NOW0;
		in.capacity = C;
		in.grain = G;
		in.hop = H;
		in.position = pos;
		in.pace = stretch > 0 ? 1.0 / stretch : 0.0;
		in.frozen = frozen;
		in.stopAtEnd = stopAtEnd;
		p.reset();
	}
	void stretch(double s) { in.pace = s > 0 ? 1.0 / s : 0.0; }
	PassOut next() {
		nowWas = in.now;
		o = p.step(in);
		in.trig = in.freezeRose = in.freezeFell = in.grainChanged = in.press = false;
		if (!in.frozen)
			in.now += in.hop;
		return o;
	}
	// The anchor a Position asks for at the last grain's `now`, before the guard.
	long long anchorFor(float pos) const { return nowWas - lagFrames(pos, in.capacity, in.grain); }
	// The lowest end a slice may have at the last grain: the oldest whole
	// grain, plus a hop while the tape moves.
	long long low(bool moving) const { return nowWas - in.capacity + in.grain + (moving ? in.hop : 0); }
};

// A position whose lag is exactly L frames.
float posForLag(long long L) {
	float p = float(1.0 - double(L) / double(ROOM));
	for (int i = 0; i < 1000 && lagFrames(p, C, G) != L; i++)
		p = std::nextafter(p, lagFrames(p, C, G) > L ? 2.f : -1.f);
	CHECK(lagFrames(p, C, G) == L, "no position gives lag %lld", L);
	return p;
}

// Grains in a pass, frozen: the read point travels pace * hop a grain from
// the anchor and the pass ends on the grain it would pass now.
long long frozenPassGrains(long long distance, long long adv) { return distance / adv + 1; }
// Not frozen: the read point falls (1 - pace) hop a grain behind now, and the
// pass ends on the grain its slice comes within a hop of the oldest frame.
long long recordingPassGrains(long long lag, long long fall) { return (C - G - H - lag) / fall + 1; }

void anchor_for_positions() {
	const float pos[] = {1.f, 0.5f, 0.f};
	const long long lag[] = {0, 711808, ROOM};
	for (int i = 0; i < 3; i++) {
		CHECK(lagFrames(pos[i], C, G) == lag[i], "lag at %g", pos[i]);
		Sim f(pos[i], 8, true);
		PassOut o = f.next();
		CHECK(o.anchor == NOW0 - lag[i] && o.end == o.anchor && o.take && o.newPass && o.sound,
		      "frozen, Position %g: anchor %lld end %lld", pos[i], o.anchor, o.end);
		Sim r(pos[i], 8, false);
		o = r.next();
		// Recording, a slice within a hop of the oldest frame is already
		// lost, so Position 0 starts a hop in.
		const long long want = i == 2 ? NOW0 - lag[i] + H : NOW0 - lag[i];
		CHECK(o.anchor == want && o.end == want && o.take && o.newPass && o.sound,
		      "recording, Position %g: anchor %lld end %lld want %lld", pos[i], o.anchor, o.end, want);
		// The anchor rides with now while recording.
		o = r.next();
		CHECK(o.anchor == want + H, "recording, Position %g: the anchor moves a hop a grain", pos[i]);
	}
}

void pace_frozen() {
	const int stretches[] = {1, 2, 8, 32};
	for (int s : stretches) {
		Sim f(0.f, s, true);
		const long long a = f.next().end;
		bool ok = true;
		for (int k = 1; k <= 40; k++) {
			const PassOut o = f.next();
			ok = ok && o.end == a + k * (H / s) && !o.newPass && o.take && o.sound && !o.held;
		}
		CHECK(ok, "frozen x%d: end rises %d a grain", s, H / s);
	}
	// A pace that is not a whole number of frames a grain: whole frames,
	// never between them, and no drift over many grains.
	Sim f(0.f, 3, true);
	const long long a = f.next().end;
	for (int k = 1; k <= 100; k++)
		f.next();
	CHECK(f.o.end == a + (long long) std::floor(100.0 * H / 3.0), "x3: 100 grains travel floor(100 hop / 3)");
}

void pace_recording() {
	const int stretches[] = {1, 2, 8, 32};
	for (int s : stretches) {
		Sim r(0.5f, s, false);
		const long long a = r.next().end;
		bool ok = true, lagOk = true;
		for (int k = 1; k <= 40; k++) {
			const PassOut o = r.next();
			ok = ok && o.end == a + k * (H / s) && !o.newPass && o.take && o.sound;
			// Behind now by the lag plus (1 - pace) hop a grain.
			lagOk = lagOk && r.nowWas - o.end == 711808 + k * (H - H / s);
		}
		CHECK(ok, "recording x%d: end rises %d a grain in absolute frames", s, H / s);
		CHECK(lagOk, "recording x%d: falls behind now by %d a grain", s, H - H / s);
	}
}

void return_frozen() {
	// A distance that is a whole number of grains' travel: the last grain of
	// the pass ends exactly at now, and the next is back on the anchor.
	const long long m = 300;
	Sim f(posForLag(m * 1024), 8, true);
	const long long a = f.next().end;
	CHECK(a == NOW0 - m * 1024, "anchor");
	bool ok = true;
	for (long long k = 1; k <= m; k++) {
		const PassOut o = f.next();
		ok = ok && o.end == a + k * 1024 && !o.newPass;
	}
	CHECK(ok && f.o.end == NOW0, "the pass's last grain ends at now, %lld", f.o.end);
	PassOut o = f.next();
	CHECK(o.end == a && o.newPass && o.take && o.sound && !o.stopped, "back on the anchor with newPass (end %lld)", o.end);
	o = f.next();
	CHECK(o.end == a + 1024 && !o.newPass, "and goes again");

	// A distance that is not: the return is on the grain that would pass now.
	Sim g(posForLag(m * 1024 + 700), 8, true);
	const long long b = g.next().end;
	for (long long k = 1; k <= m; k++)
		g.next();
	CHECK(g.o.end == b + m * 1024 && g.o.end == NOW0 - 700 && !g.o.newPass, "the last grain stops short of now");
	o = g.next();
	CHECK(o.end == b && o.newPass, "the next would pass now: back on the anchor");
}

void return_recording() {
	// x2: the read point falls 4096 a grain behind now. With the lag chosen so
	// that the room above the guard is k * 4096, grain k ends exactly on the
	// guarded edge and is still taken; grain k + 1 has returned.
	const long long k = 100;
	const long long lag = C - G - H - k * 4096;
	Sim r(posForLag(lag), 2, false);
	const long long a = r.next().end;
	CHECK(a == NOW0 - lag, "anchor");
	bool ok = true;
	for (long long i = 1; i <= k; i++) {
		const PassOut o = r.next();
		ok = ok && !o.newPass && o.take && o.end == a + i * 4096;
	}
	CHECK(ok, "no return before grain %lld", k + 1);
	CHECK(r.o.end == r.low(true), "grain %lld sits exactly a hop above the oldest whole grain (end %lld, edge %lld)", k,
	      r.o.end, r.low(true));
	PassOut o = r.next();
	CHECK(o.newPass && o.end == r.anchorFor(r.in.position) && o.take, "grain %lld returns to the anchor", k + 1);
	// Without the guard the slice would still be on the tape here: the hop's
	// guard is what returns it.
	CHECK(a + (k + 1) * 4096 >= r.low(false),
	      "the lost slice was inside the tape but within a hop of its oldest frame");
}

void pass_length_formula() {
	const int stretches[] = {2, 4, 8, 32};
	const float positions[] = {0.1f, 0.25f, 0.5f, 0.75f};
	for (int s : stretches)
		for (float pos : positions) {
			const long long lag = lagFrames(pos, C, G);
			for (int frozen = 0; frozen < 2; frozen++) {
				Sim x(pos, s, frozen != 0);
				x.next();
				const long long want = frozen ? frozenPassGrains(lag, H / s) : recordingPassGrains(lag, H - H / s);
				// Two whole passes: grains from one newPass to the next.
				long long got[2] = {0, 0};
				for (int pass = 0; pass < 2; pass++) {
					long long n = 1;
					while (!x.next().newPass && n < 100000)
						n++;
					got[pass] = n;
				}
				CHECK(got[0] == want && got[1] == want, "%s x%d Position %g: passes of %lld and %lld grains, want %lld",
				      frozen ? "frozen" : "recording", s, pos, got[0], got[1], want);
			}
		}
	// In time, a frozen pass is the distance times the Stretch, within a grain.
	const long long lag = lagFrames(0.5f, C, G);
	const double seconds = double(frozenPassGrains(lag, H / 8) * H) / 48000.0;
	CHECK(std::fabs(seconds - 8.0 * double(lag) / 48000.0) < double(G) / 48000.0, "x8 over 14.8 s lasts %g s", seconds);
}

// Recording at x1 the read point sits on the flag: once on it, it never
// returns (no new pass), late by Position's distance.
void stretch_1_recording_stays_on_the_flag() {
	Sim r(0.5f, 1, false);
	r.next();
	bool ok = true;
	for (int i = 0; i < 20000; i++) {
		const PassOut o = r.next();
		ok = ok && !o.newPass && o.take && r.nowWas - o.end == 711808 && o.end == o.anchor;
	}
	CHECK(ok, "x1 keeps pace on the flag for 20000 grains (about an hour), late by Position's distance");
}

// The user's observation at 2.0.46 (Pass.hpp): recording, Stretch back at
// exactly 1 after the read point fell behind returns it to the flag at the
// next grain (a new pass, once) and keeps it there. Frozen, x1 is as
// before; a pace just under 1 still falls behind; stopped stays stopped.
void stretch_back_to_1_returns_to_the_flag() {
	Sim r(0.9f, 8, false);
	for (int i = 0; i < 40; i++) r.next();
	CHECK(r.o.sound && r.o.anchor - r.o.end > 30 * 7000, "x8: the read point well behind the flag (%lld frames)", r.o.anchor - r.o.end);
	r.stretch(1);
	PassOut o = r.next();
	CHECK(o.newPass && o.take && o.sound && o.end == o.anchor, "back at x1: on the flag at the next grain, a new pass (end %lld, flag %lld)", o.end,
	      o.anchor);
	bool ok = true;
	for (int i = 0; i < 200; i++) {
		o = r.next();
		ok = ok && !o.newPass && o.end == o.anchor && o.sound;
	}
	CHECK(ok, "and stays on the flag, with no further new pass");
	// A Position move at x1 moves the flag and the read point together, no new pass.
	r.in.position = 0.8f;
	o = r.next();
	CHECK(!o.newPass && o.end == o.anchor, "x1, Position moved: still on the flag, no new pass");
	// Leaving the Stretch stop for x1: back on the flag too.
	Sim h(0.9f, 0, false);
	for (int i = 0; i < 30; i++) h.next();
	CHECK(h.o.held && h.o.end < h.o.anchor, "held: the head rides behind the flag");
	h.stretch(1);
	o = h.next();
	CHECK(o.newPass && o.end == o.anchor && !o.held, "held, then x1: on the flag");
	// A pace just under 1 still falls behind.
	Sim u(0.9f, 8, false);
	for (int i = 0; i < 40; i++) u.next();
	const long long behind = u.o.anchor - u.o.end;
	u.stretch(1.0 / 0.96);
	o = u.next();
	CHECK(!o.newPass && o.anchor - o.end > behind, "pace 0.96: no return, still falling behind");
	// Frozen, x1 travels from where it is, as before.
	Sim f(0.5f, 8, true);
	for (int i = 0; i < 10; i++) f.next();
	const long long at = f.o.end;
	f.stretch(1);
	o = f.next();
	CHECK(!o.newPass && o.end == at + 8192, "frozen, x1: travels on from where it is (end %lld)", o.end - at);
	// Stopped, recording: x1 does not wake it.
	Sim s(0.9f, 8, false, true);
	o = s.next();
	CHECK(o.stopped, "stop: stopped from reset");
	s.stretch(1);
	for (int i = 0; i < 10; i++) o = s.next();
	CHECK(o.stopped && !o.sound, "stop, recording at x1: stays stopped");
}

void position_turn_moves_by_that_much() {
	for (int frozen = 0; frozen < 2; frozen++) {
		Sim x(0.5f, 8, frozen != 0);
		for (int i = 0; i < 6; i++)
			x.next();
		const long long before = x.o.end;
		x.in.position = 0.6f;
		const long long shift = lagFrames(0.5f, C, G) - lagFrames(0.6f, C, G);
		PassOut o = x.next();
		CHECK(o.end == before + 1024 + shift && !o.newPass && o.take,
		      "%s: a turn moves the read point by %lld at once (end %lld, want %lld)", frozen ? "frozen" : "recording",
		      shift, o.end, before + 1024 + shift);
		o = x.next();
		CHECK(o.end == before + 2048 + shift && !o.newPass, "and it goes on from there at its pace");
		// Back the other way.
		x.in.position = 0.5f;
		o = x.next();
		CHECK(o.end == before + 3072 && !o.newPass, "a turn back undoes it");
	}
}

void position_turn_stops_at_the_edge() {
	// Recording: 50 grains into a pass the read point is 358400 behind the
	// anchor; Position to 0.1 would take it off the old end of the tape.
	Sim r(0.5f, 8, false);
	for (int i = 0; i < 51; i++)
		r.next();
	r.in.position = 0.1f;
	PassOut o = r.next();
	CHECK(o.end == r.low(true) + 1024 && !o.newPass && o.take, "stopped at the edge, then on at its pace (end %lld, edge %lld)",
	      o.end, r.low(true));
	o = r.next();
	CHECK(o.newPass && o.end == r.anchorFor(0.1f), "the oldest sound catches it: the pass is over");

	// Held, recording, still on the tape (1 121 408 behind now after 50
	// grains): a turn takes the slice at the new anchor, which is always on
	// the tape (held_recording_position_move_takes_at_the_anchor).
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 51; i++)
		h.next();
	CHECK(h.o.end >= h.low(true), "the held place is still on the tape");
	h.in.position = 0.2f;
	o = h.next();
	CHECK(o.end == h.anchorFor(0.2f) && o.take && o.held && !o.newPass, "held, recording: a turn takes the slice at the new anchor");

	// Frozen, the read point never falls behind its anchor, so a turn left
	// cannot take it off; a turn right past now ends the pass.
	Sim f(0.5f, 8, true);
	for (int i = 0; i < 600; i++)
		f.next();
	f.in.position = 0.9f;
	o = f.next();
	CHECK(o.newPass && o.end == f.anchorFor(0.9f), "frozen: a turn past now ends the pass, back on the new anchor");
}

void trig_restarts() {
	// Mid pass.
	Sim f(0.5f, 8, true);
	for (int i = 0; i < 10; i++)
		f.next();
	f.in.trig = true;
	PassOut o = f.next();
	CHECK(o.end == f.anchorFor(0.5f) && o.newPass && o.take && o.sound, "TRIG in mid pass");
	// Held at the stop, recording.
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 10; i++)
		h.next();
	h.in.trig = true;
	o = h.next();
	CHECK(o.end == h.anchorFor(0.5f) && o.end != NOW0 - 711808 && o.newPass && o.take && o.held, "TRIG while held");
	o = h.next();
	CHECK(o.held && !o.take && !o.newPass, "and holds the new slice");
	// Stopped.
	Sim s(0.5f, 8, true, true);
	o = s.next();
	CHECK(o.stopped && !o.sound, "stopped");
	s.in.trig = true;
	o = s.next();
	CHECK(o.end == s.anchorFor(0.5f) && o.newPass && o.sound && !o.stopped && o.take, "TRIG while stopped");
}

void freeze_on_restarts() {
	Sim r(0.5f, 8, false);
	for (int i = 0; i < 20; i++)
		r.next();
	r.in.frozen = r.in.freezeRose = true;
	PassOut o = r.next();
	CHECK(o.end == r.anchorFor(0.5f) && o.newPass && o.take, "Freeze on: a new pass from the anchor");
	const long long a = o.end;
	// The ring writes its faded frames for up to 5 ms after the edge.
	r.in.now += 240;
	o = r.next();
	CHECK(o.end == a + 1024 && !o.newPass && o.take, "and the read point travels on from it");
	r.next();
	CHECK(r.o.end == a + 2048, "the tape now still");

	// In stop, a pass running: Freeze on starts a pass from the anchor, as in
	// go round (freeze_in_stop has the whole table).
	Sim s(0.5f, 8, false, true);
	s.in.trig = true;
	for (int i = 0; i < 20; i++)
		s.next();
	CHECK(!s.o.stopped && s.o.sound, "in stop, a pass running");
	s.in.frozen = s.in.freezeRose = true;
	o = s.next();
	CHECK(o.newPass && o.sound && o.take && !o.stopped && o.end == s.anchorFor(0.5f), "in stop, running: Freeze on starts a pass from the anchor");
	// In go round, Freeze on at Position fully right: a pass from the anchor, held.
	Sim w(1.f, 8, false);
	for (int i = 0; i < 20; i++)
		w.next();
	w.in.frozen = w.in.freezeRose = true;
	o = w.next();
	CHECK(o.end == w.anchorFor(1.f) && o.newPass && o.held && o.sound, "go round, fully right: Freeze on holds the moment");

	// Position 0: the pass starts a hop in, so the faded frames written after
	// the edge do not reach its slice.
	Sim z(0.f, 8, false);
	for (int i = 0; i < 3; i++)
		z.next();
	z.in.frozen = z.in.freezeRose = true;
	o = z.next();
	CHECK(o.end == z.low(true) && o.newPass, "Freeze on at Position 0: a hop in from the oldest whole grain");
	z.in.now += 240;
	o = z.next();
	CHECK(o.end >= z.low(false) && o.take && !o.newPass, "still on the tape after the fade-out is written");
	// The next pass, on the still tape, starts at the oldest whole grain.
	for (int i = 0; i < 100000 && !z.next().newPass; i++) {}
	CHECK(z.o.end == z.low(false), "later passes start at the oldest whole grain");
}

// The user's ruling (Pass.hpp): Freeze going off returns the read point to
// the anchor at once, a new pass, whenever the module is sounding (running
// or held), in either setting; stopped, it stays stopped.
void thaw_returns_to_the_anchor() {
	for (int stop = 0; stop < 2; stop++) {
		const char* set = stop ? "stop" : "go round";
		// Held at Position fully right: back to now at the next grain.
		Sim h(1.f, 8, false, stop != 0);
		if (stop) h.in.trig = true;
		for (int i = 0; i < 10; i++) h.next();
		h.in.frozen = h.in.freezeRose = true;
		h.next();
		h.in.now += 240;
		for (int i = 0; i < 20; i++) h.next();
		CHECK(h.o.held && h.o.sound, "%s: frozen at Position fully right, held", set);
		h.in.frozen = false;
		h.in.freezeFell = true;
		h.in.now += 3000;   // recording resumes part way through a hop
		PassOut o = h.next();
		CHECK(o.newPass && o.take && o.sound && !o.held && !o.stopped && o.end == h.nowWas && o.end == o.anchor,
		      "%s: thaw at Position fully right: a new pass at now (end %lld, now %lld)", set, o.end, h.nowWas);
		o = h.next();
		CHECK(o.sound && !o.newPass && o.take && o.end == h.nowWas - 7168, "%s: then on from now at its pace", set);
		// A frozen pass mid-tape: back to the anchor.
		Sim f(0.5f, 8, true, stop != 0);
		if (stop) f.in.trig = true;
		for (int i = 0; i < 10; i++) f.next();
		CHECK(f.o.sound && f.o.end == f.anchorFor(0.5f) + 9 * 1024, "%s: a frozen pass under way", set);
		f.in.frozen = false;
		f.in.freezeFell = true;
		f.in.now += 3000;
		o = f.next();
		CHECK(o.newPass && o.take && o.sound && !o.stopped && o.end == f.anchorFor(0.5f), "%s: thaw mid-tape: a new pass from the anchor", set);
		// Held at the Stretch stop, frozen mid-tape: the slice at the anchor,
		// taken once and held there.
		Sim z(0.5f, 8, true, stop != 0);
		if (stop) z.in.trig = true;
		for (int i = 0; i < 10; i++) z.next();
		z.stretch(0);   // held where the pass has got to, off the anchor
		z.next();
		CHECK(z.o.held && z.o.end == z.anchorFor(0.5f) + 9 * 1024, "%s: held at the Stretch stop, off the anchor", set);
		z.in.frozen = false;
		z.in.freezeFell = true;
		o = z.next();
		CHECK(o.newPass && o.take && o.held && o.sound && o.end == z.anchorFor(0.5f), "%s: thaw at the Stretch stop: held at the anchor", set);
		o = z.next();
		CHECK(o.held && !o.take && !o.newPass && o.sound, "%s: and held there, taken once", set);
	}
	// Stopped (stop, the pass ended): a thaw leaves it stopped and silent.
	Sim s(0.5f, 8, true, true);
	s.in.trig = true;
	s.next();
	for (int i = 0; i < 2000 && !s.o.stopped; i++) s.next();
	CHECK(s.o.stopped, "stop: the frozen pass ended");
	s.in.frozen = false;
	s.in.freezeFell = true;
	PassOut o = s.next();
	CHECK(o.stopped && !o.sound && !o.newPass && !o.take, "stop, stopped: a thaw stays stopped");
	bool ok = true;
	for (int i = 0; i < 50; i++) {
		o = s.next();
		ok = ok && o.stopped && !o.sound;
	}
	CHECK(ok, "and silent until a trigger");
	// Without an edge (a frame that only says not frozen) nothing returns.
	Sim n(0.5f, 8, true);
	for (int i = 0; i < 10; i++) n.next();
	const long long before = n.o.end;
	n.in.frozen = false;
	o = n.next();
	CHECK(o.end == before + 1024 && !o.newPass, "no edge, no return");
}

void held_at_the_stop() {
	Sim r(0.5f, 8, false);
	for (int i = 0; i < 6; i++)
		r.next();
	const long long last = r.o.end;
	r.stretch(0);
	PassOut o = r.next();
	CHECK(o.held && o.take && o.sound && !o.newPass && o.end == last, "the stop: the slice where it is, taken once");
	bool ok = true;
	for (int i = 0; i < 352; i++) {   // 60 s at 48 kHz
		o = r.next();
		ok = ok && o.held && !o.take && o.sound && !o.newPass && !o.stopped && o.end == last;
	}
	CHECK(ok, "then kept, not read, for 60 s while the tape runs on");
	CHECK(r.o.end < r.low(true), "by then the tape under it has been recorded over");

	// (A Position move while held and recording: held_recording_position_move_takes_at_the_anchor.)
	// Frozen, a move is a scrub.
	Sim f(0.5f, 0, true);
	f.next();
	f.next();
	CHECK(!f.o.take && f.o.held, "frozen, held");
	f.in.position = 0.2f;
	o = f.next();
	CHECK(o.take && o.end == f.anchorFor(0.2f), "frozen: a scrub to the new place");
}

// A move is decided in frames: a change in Position's last bit is no move.
void position_wobble_is_no_move() {
	// Held, recording, the held place long since recorded over.
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 400; i++)
		h.next();
	const long long here = h.o.end;
	CHECK(here < h.low(true), "recorded over");
	h.in.position = std::nextafter(0.5f, 2.f);
	PassOut o = h.next();
	CHECK(!o.take && o.held && o.end == here, "one float step up: kept, not retaken (end %lld, was %lld)", o.end, here);
	h.in.position = 0.5f;
	o = h.next();
	CHECK(!o.take && o.end == here, "and back: kept");
	// Travelling: a wobble does not move the read point.
	Sim f(0.5f, 8, true);
	f.next();
	for (int i = 1; i <= 20; i++) {
		f.in.position = (i & 1) ? std::nextafter(0.5f, 2.f) : 0.5f;
		f.next();
	}
	CHECK(f.o.end == f.anchorFor(0.5f) + 20 * 1024, "a frozen pass under a wobbling Position travels at its pace");
	// A slow drift, under a frame a grain, is not lost: it is applied once it
	// adds up to a frame.
	Sim d(0.5f, 8, true);
	const long long a = d.next().end;
	float p = 0.5f;
	for (int i = 1; i <= 100; i++) {
		p += 1e-7f;   // about 0.14 frames a grain
		d.in.position = p;
		d.next();
	}
	const long long drift = lagFrames(0.5f, C, G) - lagFrames(p, C, G);
	CHECK(drift >= 10 && d.o.end == a + 100 * 1024 + drift, "a slow drift moves the read point by all of it (%lld)", drift);
}

// Ruling: held, the held place recorded over, a real Position move takes the
// new slice at the anchor, still held, with no new pass.
void held_off_tape_position_move_takes_at_the_anchor() {
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 400; i++)
		h.next();
	CHECK(h.o.end < h.low(true), "recorded over");
	h.in.position = 0.55f;
	PassOut o = h.next();
	CHECK(o.end == h.anchorFor(0.55f) && o.end == o.anchor && o.take && o.held && o.sound && !o.newPass && !o.stopped,
	      "taken at the anchor, held, no new pass (end %lld, anchor %lld)", o.end, o.anchor);
	o = h.next();
	CHECK(!o.take && o.held && o.end == h.anchorFor(0.55f) - H, "then kept there while the tape runs on");
	// The same in stop.
	Sim s(0.5f, 0, false, true);
	s.in.trig = true;
	for (int i = 0; i < 400; i++)
		s.next();
	s.in.position = 0.3f;
	o = s.next();
	CHECK(o.end == s.anchorFor(0.3f) && o.take && o.held && !o.newPass && !o.stopped, "in stop too");
}

void leaving_held_on_tape() {
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 20; i++)
		h.next();
	const long long here = h.o.end;
	h.stretch(8);
	PassOut o = h.next();
	CHECK(o.end == here + 1024 && !o.newPass && o.take && !o.held, "on the tape: goes on from where it is");
}

void leaving_held_off_tape() {
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 400; i++)
		h.next();
	h.stretch(8);
	PassOut o = h.next();
	CHECK(o.end == h.anchorFor(0.5f) && o.newPass && o.take && !o.held && !o.stopped, "off the tape: from the anchor, a new pass");
	// The same in stop: leaving held is not the end of a pass.
	Sim s(0.5f, 0, false, true);
	s.in.trig = true;
	for (int i = 0; i < 400; i++)
		s.next();
	s.stretch(8);
	o = s.next();
	CHECK(o.newPass && o.sound && !o.stopped, "in stop too");
}

void nothing_to_travel_frozen() {
	const double stretches[] = {1, 8, 32};
	const long long lags[] = {0, 1, G - 1};
	for (int stop = 0; stop < 2; stop++)
		for (double s : stretches)
			for (long long lag : lags) {
				// In stop the module is stopped from reset when Freeze is pressed.
				Sim f(posForLag(lag), s, false, stop != 0);
				for (int i = 0; i < 5; i++)
					f.next();
				CHECK(f.o.stopped == (stop != 0), "lag %lld x%g stop %d: stopped before Freeze in stop only", lag, s, stop);
				f.in.frozen = f.in.freezeRose = true;
				PassOut o = f.next();
				CHECK(o.held && o.sound && o.take && o.newPass && !o.stopped && o.end == f.anchorFor(f.in.position),
				      "lag %lld x%g stop %d: Freeze holds the moment", lag, s, stop);
				f.in.now += 240;
				bool ok = true;
				for (int i = 0; i < 2000; i++) {
					o = f.next();
					ok = ok && o.held && o.sound && !o.take && !o.newPass && !o.stopped;
				}
				CHECK(ok, "lag %lld x%g stop %d: held, sounding, never stopped", lag, s, stop);
			}
	// A lag of a whole grain is a pass.
	Sim f(posForLag(G), 8, true);
	f.next();
	CHECK(!f.o.held, "a lag of one grain is a pass");
	f.next();
	CHECK(f.o.end == f.anchorFor(f.in.position) + 1024 && !f.o.held, "and travels");
	// Position fully right from reset, frozen, in stop, after a trigger.
	Sim g(1.f, 8, true, true);
	g.in.trig = true;
	PassOut o = g.next();
	CHECK(o.held && o.sound && !o.stopped && o.end == NOW0, "Position fully right, frozen: held at now");
}

void full_left_recording() {
	const double stretches[] = {1.5, 8, 32};
	for (int stop = 0; stop < 2; stop++)
		for (double s : stretches) {
			Sim r(0.f, s, false, stop != 0);
			r.in.trig = true;
			r.next();
			bool ok = true;
			for (int i = 0; i < 500; i++) {
				const PassOut o = r.next();
				ok = ok && o.end == r.low(true) && o.end == o.anchor && o.take && o.sound && !o.held && !o.newPass &&
				     !o.stopped;
			}
			CHECK(ok, "x%g stop %d: back on the anchor every grain, sounding, RUN's marker never set", s, stop);
		}
}

void stop_at_end() {
	Sim f(0.5f, 8, true, true);
	PassOut o = f.next();
	CHECK(o.stopped && !o.sound && !o.take && !o.newPass, "reset in stop: stopped from the start");
	CHECK(o.end == o.anchor && o.anchor == NOW0 - 711808, "waiting on the anchor");
	f.in.trig = true;
	o = f.next();
	CHECK(o.newPass && o.sound && !o.stopped, "TRIG starts the pass");
	const long long n = frozenPassGrains(711808, 1024);
	bool ok = true;
	for (long long i = 1; i < n; i++) {
		o = f.next();
		ok = ok && o.sound && !o.stopped && !o.newPass;
	}
	CHECK(ok, "the pass sounds for %lld grains", n);
	bool stopped = true;
	for (int i = 0; i < 200; i++) {
		o = f.next();
		stopped = stopped && o.stopped && !o.sound && !o.take && !o.newPass && !o.held;
	}
	CHECK(stopped, "then silent and stopped");
	// Freeze going off, Stretch at the stop and a Position move do not wake a
	// stopped module (Freeze going on does: freeze_in_stop).
	f.in.frozen = false;
	f.in.freezeFell = true;
	o = f.next();
	CHECK(o.stopped && !o.sound, "stopped: Freeze off waits for the trigger");
	f.stretch(0);
	o = f.next();
	CHECK(o.stopped && !o.sound, "stopped: the Stretch stop waits for the trigger");
	f.stretch(8);
	f.in.position = 0.3f;
	o = f.next();
	CHECK(o.stopped && !o.sound, "stopped: a Position move waits for the trigger");
	f.in.position = 0.5f;
	f.in.frozen = true;
	o = f.next();
	CHECK(o.stopped && !o.sound, "stopped: a tape already frozen waits for the trigger");
	f.in.trig = true;
	o = f.next();
	CHECK(o.newPass && o.sound && o.end == f.anchorFor(0.5f), "until TRIG");

	// Frozen and stopped, Position turned fully right: nothing to travel
	// through, so held and sounding at once (I5 of the final review: the same
	// state saved and reopened sounds too); turned back, the pass goes on from
	// the read point the turn moves to, and stops at its end.
	Sim q(0.5f, 8, true, true);
	o = q.next();
	CHECK(o.stopped && !o.sound, "frozen in stop: stopped");
	q.in.position = 1.f;
	o = q.next();
	CHECK(o.held && o.sound && o.newPass && o.take && !o.stopped && o.end == NOW0, "stopped, Position fully right: held at now");
	o = q.next();
	CHECK(o.held && o.sound && !o.stopped && !o.newPass, "and stays held");
	q.in.position = 0.5f;
	o = q.next();
	CHECK(o.sound && !o.held && !o.stopped && !o.newPass && o.end == q.anchorFor(0.5f) + 1024,
	      "turned back: the pass goes on from the moved read point (end %lld, anchor %lld)", o.end, q.anchorFor(0.5f));
	for (int i = 0; i < 2000 && !q.o.stopped; i++)
		q.next();
	CHECK(q.o.stopped && !q.o.sound, "and stops at the pass's end");
	// Stopped, Stretch at its stop alone does not wake it; with Position
	// fully right it is held.
	Sim z(0.5f, 0, true, true);
	o = z.next();
	CHECK(o.stopped && !o.sound, "frozen in stop at the Stretch stop: stopped");
	for (int i = 0; i < 10; i++)
		o = z.next();
	CHECK(o.stopped && !o.sound, "the Stretch stop alone does not wake it");

	// Recording, a pass ends when the oldest sound catches the read point.
	Sim r(0.5f, 8, false, true);
	r.in.trig = true;
	r.next();
	const long long m = recordingPassGrains(711808, 7168);
	for (long long i = 1; i < m; i++)
		r.next();
	CHECK(r.o.sound && !r.o.stopped, "recording: the last grain sounds");
	o = r.next();
	CHECK(o.stopped && !o.sound, "recording: then stopped");

	// Reset in go round: a pass from the start.
	Sim g(0.5f, 8, true, false);
	o = g.next();
	CHECK(o.newPass && o.sound && !o.stopped, "reset in go round: a pass at once");
	// Stopped, then the menu set to go round: a pass at once.
	Sim t(0.5f, 8, true, true);
	t.next();
	t.in.stopAtEnd = false;
	o = t.next();
	CHECK(o.newPass && o.sound && !o.stopped, "go round chosen while stopped: a pass at once");
	// reset() itself.
	g.p.reset();
	g.in.stopAtEnd = true;
	o = g.next();
	CHECK(o.stopped && !o.sound, "reset() then stop: stopped from the start");
}

// The user's ruling at 2.0.49 (Pass.hpp): a press in the window moves the
// read point to its place and plays on, a new pass; the anchor does not move.
void press_moves_the_read_point_only() {
	const long long back = 300000;   // the press: this far behind now
	for (int stop = 0; stop < 2; stop++) {
		const char* set = stop ? "stop" : "go round";
		// Frozen, a pass travelling from Position 0.5 (in stop: stopped, from reset).
		Sim f(0.5f, 8, true, stop != 0);
		for (int i = 0; i < 10; i++) f.next();
		CHECK(f.o.stopped == (stop != 0), "%s: the state before", set);
		const long long anchor = f.anchorFor(0.5f);
		f.in.press = true;
		f.in.pressLag = back;
		PassOut o = f.next();
		CHECK(o.end == NOW0 - back && o.newPass && o.take && o.sound && !o.stopped && !o.held && o.anchor == anchor,
		      "%s, frozen: a press: the read point there, a new pass, the anchor unmoved", set);
		o = f.next();
		CHECK(o.end == NOW0 - back + 1024 && !o.newPass && o.anchor == anchor, "%s: and it travels on from there", set);
		// To the end of the tape: then back on the anchor (go round) or stopped.
		const long long n = frozenPassGrains(back, 1024);
		for (long long i = 2; i < n; i++) f.next();
		CHECK(f.o.sound && !f.o.newPass && f.o.end <= NOW0, "%s: the pressed pass sounds to the end", set);
		o = f.next();
		if (stop) CHECK(o.stopped && !o.sound, "stop: then stopped");
		else CHECK(o.newPass && o.end == anchor, "go round: then a pass from the anchor");

		// Held at the Stretch stop, frozen: the slice at the press, held.
		Sim h(0.5f, 0, true, stop != 0);
		h.in.trig = true;
		for (int i = 0; i < 5; i++) h.next();
		h.in.press = true;
		h.in.pressLag = back;
		o = h.next();
		CHECK(o.end == NOW0 - back && o.held && o.take && o.newPass && o.sound, "%s, the Stretch stop: a press takes the slice there, held", set);
		o = h.next();
		CHECK(o.end == NOW0 - back && o.held && !o.take && !o.newPass, "%s: and holds it", set);

		// Frozen at the end (Position 1, nothing to travel through): the
		// pressed pass plays to the end, then the hold at the anchor again.
		Sim e(1.f, 8, true, stop != 0);
		for (int i = 0; i < 5; i++) e.next();
		CHECK(e.o.held && e.o.sound && e.o.end == NOW0, "%s: frozen at the end, held", set);
		e.in.press = true;
		e.in.pressLag = back;
		o = e.next();
		CHECK(o.end == NOW0 - back && o.newPass && !o.held && o.sound && o.anchor == NOW0, "%s, at the end: a press plays from there", set);
		bool ok = true;
		for (long long i = 1; i < n; i++) {
			o = e.next();
			ok = ok && o.sound && !o.held && !o.newPass && !o.stopped;
		}
		CHECK(ok, "%s, at the end: the pressed pass travels, %lld grains", set, n);
		o = e.next();
		CHECK(o.end == NOW0 && o.held && o.sound && !o.stopped && o.newPass, "%s, at the end: then held at the anchor again, never stopped", set);
		o = e.next();
		CHECK(o.end == NOW0 && o.held && o.sound && !o.newPass, "%s: and stays held", set);

		// Recording in Drift at x8: the read point goes to the place behind now.
		Sim r(0.5f, 8, false, stop != 0);
		r.in.trig = true;
		for (int i = 0; i < 10; i++) r.next();
		r.in.press = true;
		r.in.pressLag = back;
		o = r.next();
		CHECK(o.end == r.nowWas - back && o.newPass && o.sound && o.anchor == r.anchorFor(0.5f), "%s, recording: a press: the place behind now", set);
		// Tied to the anchor (recording at x1; following): a press is a trigger.
		for (int follow = 0; follow < 2; follow++) {
			Sim t(0.5f, follow ? 8 : 1, false, stop != 0);
			t.in.follow = follow != 0;
			for (int i = 0; i < 10; i++) t.next();   // in stop: stopped from reset
			t.in.press = true;
			t.in.pressLag = back;
			o = t.next();
			CHECK(o.end == t.anchorFor(0.5f) && o.newPass && o.sound && !o.stopped, "%s, %s: a press is a pass from the anchor", set,
			      follow ? "following" : "recording at x1");
		}
	}
	// A press off the tape is held to it; with TRIG, or with Freeze's edge,
	// the pass is from the anchor.
	Sim f(0.5f, 8, true);
	f.next();
	f.in.press = true;
	f.in.pressLag = 100000000;
	PassOut o = f.next();
	CHECK(o.end == f.low(false) && o.newPass, "a press left of the tape: the oldest whole grain");
	f.in.press = true;
	f.in.pressLag = -5;
	o = f.next();
	CHECK(o.end == NOW0 && o.newPass && o.sound, "a press right of now: now");
	f.in.press = f.in.trig = true;
	f.in.pressLag = back;
	o = f.next();
	CHECK(o.end == f.anchorFor(0.5f) && o.newPass, "a press with TRIG: the anchor");
}

// The user's ruling at 2.0.49 (Pass.hpp): in stop, Freeze going on starts a
// pass from the anchor, whatever was happening before; it plays once to the
// end of the frozen tape and the module then stops and waits for TRIG. With
// nothing to travel through, or at the Stretch stop, it is held. The table:
// the state before (stopped from reset; stopped after a pass ended; a pass
// running by TRIG, recording; held at the Stretch stop after TRIG) by
// Position (0.5, a pass to travel; 1, nothing to travel through once frozen).
void freeze_in_stop() {
	const char* states[] = {"stopped from reset", "stopped after a pass", "a pass running", "held at the Stretch stop"};
	for (int st = 0; st < 4; st++)
		for (float pos : {0.5f, 1.f}) {
			Sim f(pos, st == 3 ? 0 : 8, false, true);
			if (st == 1 || st == 2 || st == 3) f.in.trig = true;
			f.next();
			if (st == 1)
				for (int i = 0; i < 400 && !f.o.stopped; i++) f.next();
			else
				for (int i = 0; i < 20; i++) f.next();
			const bool wasStopped = st < 2, wasHeld = st == 3;
			CHECK(f.o.stopped == wasStopped && f.o.sound == !wasStopped && f.o.held == wasHeld, "%s, Position %g: the state before",
			      states[st], pos);
			f.in.frozen = f.in.freezeRose = true;
			PassOut o = f.next();
			const bool holds = pos == 1.f || st == 3;   // nothing to travel through, or the Stretch stop
			CHECK(o.newPass && o.sound && o.take && !o.stopped && o.held == holds && o.end == f.anchorFor(pos),
			      "%s, Position %g: Freeze on: a pass from the anchor%s", states[st], pos, holds ? ", held" : "");
			f.in.now += 240;   // the faded frames written after the edge
			if (holds) {
				bool ok = true;
				for (int i = 0; i < 200; i++) {
					o = f.next();
					ok = ok && o.held && o.sound && !o.stopped && !o.newPass;
				}
				CHECK(ok, "%s, Position %g: and held, never stopped", states[st], pos);
			}
			else {
				// One pass, whole: the read point travels the lag and the
				// 240 frames, then the module stops and stays stopped.
				const long long n = frozenPassGrains(711808 + 240, 1024);
				bool ok = true;
				for (long long i = 1; i < n; i++) {
					o = f.next();
					ok = ok && o.sound && o.take && !o.stopped && !o.newPass && !o.held;
				}
				CHECK(ok, "%s, Position %g: the pass sounds whole, %lld grains", states[st], pos, n);
				ok = true;
				for (int i = 0; i < 200; i++) {
					o = f.next();
					ok = ok && o.stopped && !o.sound && !o.newPass && !o.take;
				}
				CHECK(ok, "%s, Position %g: then stopped and silent until a trigger", states[st], pos);
			}
			f.in.trig = true;
			o = f.next();
			CHECK(o.newPass && o.sound && !o.stopped && o.end == f.anchorFor(pos), "%s, Position %g: TRIG, a pass from the anchor",
			      states[st], pos);
		}
	// Freeze and TRIG on the same grain: one pass from the anchor.
	Sim t(0.5f, 8, false, true);
	t.in.trig = true;
	for (int i = 0; i < 5; i++) t.next();
	t.in.frozen = t.in.freezeRose = t.in.trig = true;
	PassOut o = t.next();
	CHECK(o.newPass && o.sound && !o.stopped && o.end == t.anchorFor(0.5f), "Freeze with TRIG: a pass from the anchor");
	// It plays whole and stops; Freeze off leaves it stopped, and Freeze on
	// again starts a pass again.
	const long long n = frozenPassGrains(711808, 1024);
	for (long long i = 1; i < n; i++) t.next();
	CHECK(t.o.sound && !t.o.stopped, "the pass sounds whole");
	o = t.next();
	CHECK(o.stopped && !o.sound, "and in stop, stops at its end");
	t.in.frozen = false;
	t.in.freezeFell = true;
	o = t.next();
	CHECK(o.stopped && !o.sound, "Freeze off, stopped: stays stopped");
	t.in.frozen = t.in.freezeRose = true;
	o = t.next();
	CHECK(o.newPass && o.sound && !o.stopped && o.end == t.anchorFor(0.5f), "Freeze on again: a pass from the anchor");
	// Frozen already at reset (a patch opened frozen): in stop it waits with a
	// pass to travel, and is held with nothing to travel through; in go round
	// it starts a pass.
	Sim w(0.5f, 8, true, true);
	o = w.next();
	CHECK(o.stopped && !o.sound, "opened frozen in stop, a pass to travel: waits");
	Sim x(1.f, 8, true, true);
	o = x.next();
	CHECK(o.held && o.sound && o.newPass && !o.stopped, "opened frozen in stop, nothing to travel: held");
	Sim y(0.5f, 8, true, false);
	o = y.next();
	CHECK(o.newPass && o.sound && !o.held, "opened frozen in go round: a pass");
}

// The ruling from the fix wave's re-review (Pass.hpp): held on a tape that
// is recording (Stretch at its stop), a real Position move takes the new
// slice at the anchor, where Position now points, even while the old place
// is still on the tape; so a patch reopened held on the silence from before
// it opened wakes when Position is turned. Frozen, the place shifts by the
// change (the scrub), as before.
void held_recording_position_move_takes_at_the_anchor() {
	const long long shift = lagFrames(0.5f, C, G) - lagFrames(0.45f, C, G);
	for (int stop = 0; stop < 2; stop++) {
		Sim h(0.5f, 0, false, stop != 0);
		h.in.trig = true;
		for (int i = 0; i < 20; i++)
			h.next();
		const long long here = h.o.end;
		CHECK(here >= h.low(true) && here != h.o.anchor, "stop %d: held on the tape, behind the flag", stop);
		h.in.position = 0.45f;
		PassOut o = h.next();
		CHECK(o.take && o.held && o.sound && !o.newPass && !o.stopped && o.end == h.anchorFor(0.45f) && o.end == o.anchor,
		      "stop %d: a Position move takes the slice at the new anchor (end %lld, anchor %lld, the shifted place %lld)", stop, o.end, o.anchor,
		      here + shift);
		const long long at = o.end;
		o = h.next();
		CHECK(!o.take && o.held && o.end == at, "stop %d: and keeps it", stop);
		// A wobble is still no move.
		h.in.position = std::nextafter(0.45f, 2.f);
		o = h.next();
		CHECK(!o.take && o.end == at, "stop %d: a wobble keeps it", stop);
	}
	// Frozen, held off the anchor (a pass under way, then the Stretch stop): a
	// move shifts the place by the change.
	Sim f(0.5f, 8, true);
	for (int i = 0; i < 10; i++)
		f.next();
	f.stretch(0);
	f.next();
	const long long here = f.o.end;
	CHECK(f.o.held && here == f.anchorFor(0.5f) + 9 * 1024, "frozen: held off the anchor");
	f.in.position = 0.45f;
	PassOut o = f.next();
	CHECK(o.take && o.held && o.end == here + shift && o.end != o.anchor, "frozen: a move shifts the held place by the change (end %lld, want %lld)",
	      o.end, here + shift);
}

// While recording: Follow (Pass.hpp). Recording, the read point is on the
// flag every grain whatever the pace, its stop included: taken, sounding,
// never held, and no new pass once there.
void follow_sits_on_the_flag() {
	const double stretches[] = {1, 8, 32, 0};
	for (double s : stretches)
		for (float pos : {0.9f, 0.5f, 0.f}) {
			Sim r(pos, s, false);
			r.in.follow = true;
			int passes = r.next().newPass ? 1 : 0;
			bool ok = true;
			for (int i = 0; i < 400; i++) {
				const PassOut o = r.next();
				passes += o.newPass;
				ok = ok && o.end == o.anchor && o.anchor == r.anchorFor(pos) + (pos == 0.f ? H : 0) && o.take && o.sound && !o.held &&
				     !o.stopped;
			}
			CHECK(ok && passes <= 1, "Follow, x%g, Position %g: on the flag for 400 grains, taken and sounding, never held (%d new passes)", s, pos,
			      passes);
		}
	// A Position move moves the read point with the flag, no new pass.
	Sim r(0.9f, 8, false);
	r.in.follow = true;
	for (int i = 0; i < 10; i++)
		r.next();
	r.in.position = 0.6f;
	PassOut o = r.next();
	CHECK(o.end == o.anchor && o.end == r.anchorFor(0.6f) && !o.newPass && o.take, "Follow, a Position move: on the new flag, no new pass");
	// TRIG and a thaw keep their rules: a new pass from the flag.
	r.in.trig = true;
	o = r.next();
	CHECK(o.newPass && o.end == o.anchor && o.sound && !o.held, "Follow, TRIG: a new pass from the flag");
	Sim t(0.5f, 0, true);
	t.in.follow = true;
	for (int i = 0; i < 10; i++)
		t.next();
	CHECK(t.o.held, "Follow, frozen at the Stretch stop: held");
	t.in.frozen = false;
	t.in.freezeFell = true;
	o = t.next();
	CHECK(o.newPass && o.sound && !o.held && o.end == o.anchor, "Follow, a thaw at the Stretch stop: back to the flag, following, not held");
	o = t.next();
	CHECK(!o.newPass && o.take && !o.held && o.end == o.anchor, "and following");
}

// Frozen, Follow and Drift are the same in every respect. Two passes, one
// in each setting, recorded differently (Drift falls behind at x8, Follow
// sits on the flag), then frozen and driven alike for 3000 grains of
// moving Position, Stretch, TRIG, the Stop setting and Grain: every field
// of every grain the same from the freeze on.
void follow_frozen_is_as_drift() {
	for (int stop = 0; stop < 2; stop++) {
		Sim d(0.7f, 8, false, stop != 0), w(0.7f, 8, false, stop != 0);
		w.in.follow = true;
		d.in.trig = w.in.trig = true;
		for (int i = 0; i < 40; i++) {
			d.next();
			w.next();
		}
		CHECK(d.o.end != w.o.end && w.o.end == w.o.anchor, "stop %d: recorded differently (Drift %lld, Follow %lld)", stop, d.o.end, w.o.end);
		uint64_t x = 12345;
		auto rnd = [&x]() {
			x ^= x << 13;
			x ^= x >> 7;
			x ^= x << 17;
			return x;
		};
		long long diff = 0, sounded = 0, passes = 0;
		for (int i = 0; i < 3000; i++) {
			Sim* both[2] = {&d, &w};
			const bool rose = i == 0, trig = rnd() % 50 == 0, flip = rnd() % 400 == 0;
			const uint64_t r = rnd();
			const bool movePos = r % 20 == 0, moveStretch = (r >> 8) % 30 == 0, grain = (r >> 16) % 500 == 0;
			const float pos = float((r >> 20) % 1001) / 1000.f;
			const double st = ((r >> 32) % 5) == 0 ? 0.0 : double(1 + (r >> 36) % 40);
			for (Sim* s : both) {
				s->in.frozen = true;
				s->in.freezeRose = rose;
				s->in.trig = trig;
				if (flip) s->in.stopAtEnd = !s->in.stopAtEnd;
				if (movePos) s->in.position = pos;
				if (moveStretch) s->stretch(st);
				if (grain) {
					s->in.grain = s->in.grain == G ? 32768 : G;
					s->in.hop = s->in.grain / 2;
					s->in.grainChanged = true;
				}
			}
			const PassOut a = d.next(), b = w.next();
			if (rose) {
				d.in.now += 240;
				w.in.now += 240;
			}
			diff += a.end != b.end || a.take != b.take || a.sound != b.sound || a.newPass != b.newPass || a.held != b.held || a.stopped != b.stopped ||
			        a.anchor != b.anchor;
			sounded += a.sound;
			passes += a.newPass;
		}
		CHECK(diff == 0 && sounded > 500 && passes > 20, "stop %d: frozen, Follow and Drift alike over 3000 grains (%lld differ; %lld sounded, %lld passes)",
		      stop, diff, sounded, passes);
	}
}

// Drift, then Follow while recording: one return to the flag, a new pass,
// once; from the head behind the flag at a pace, and from the Stretch stop.
void drift_to_follow_rejoins_once() {
	for (double s : {8.0, 0.0}) {
		Sim r(0.9f, s, false);
		for (int i = 0; i < 40; i++)
			r.next();
		CHECK(r.o.sound && r.o.anchor - r.o.end > 100000 && r.o.held == (s == 0.0), "x%g: the head behind the flag (%lld)", s, r.o.anchor - r.o.end);
		r.in.follow = true;
		PassOut o = r.next();
		CHECK(o.newPass && o.take && o.sound && !o.held && o.end == o.anchor, "x%g, Follow chosen: back on the flag, a new pass", s);
		bool ok = true;
		for (int i = 0; i < 200; i++) {
			o = r.next();
			ok = ok && !o.newPass && o.end == o.anchor && o.take && !o.held;
		}
		CHECK(ok, "x%g: then on the flag with no further new pass", s);
	}
}

// Follow, then Drift while recording: the head goes on from the flag and
// falls behind at Stretch's pace; no return, no new pass.
void follow_to_drift_falls_behind_from_the_flag() {
	Sim r(0.9f, 8, false);
	r.in.follow = true;
	for (int i = 0; i < 20; i++)
		r.next();
	const long long at = r.o.end;
	CHECK(at == r.o.anchor, "Follow: on the flag");
	r.in.follow = false;
	bool ok = true;
	for (int k = 1; k <= 30; k++) {
		const PassOut o = r.next();
		ok = ok && !o.newPass && o.take && o.sound && !o.held && o.end == at + k * 1024 && o.anchor - o.end == k * (H - 1024);
	}
	CHECK(ok, "Drift chosen: on from the flag at x8, falling 7168 a grain behind it, no new pass");
	// At the Stretch stop: held where it is, on the flag at that grain.
	Sim h(0.9f, 0, false);
	h.in.follow = true;
	for (int i = 0; i < 20; i++)
		h.next();
	const long long here = h.o.end;
	h.in.follow = false;
	PassOut o = h.next();
	CHECK(o.held && o.take && !o.newPass && o.end == here, "Drift chosen at the Stretch stop: held where the head was");
}

// The Stop setting: stopped stays stopped (Follow does not wake it); a
// module sounding goes on following, and a pass by TRIG never ends.
void follow_stopped_stays_stopped() {
	Sim s(0.9f, 8, false, true);
	s.in.follow = true;
	PassOut o = s.next();
	bool ok = o.stopped && !o.sound;
	for (int i = 0; i < 50; i++) {
		o = s.next();
		ok = ok && o.stopped && !o.sound && !o.newPass;
	}
	CHECK(ok, "Follow in stop, from reset: stopped and silent");
	// Stopped by a Drift pass's end, then Follow: still stopped.
	Sim d(0.1f, 2, false, true);
	d.in.trig = true;
	d.next();
	for (int i = 0; i < 2000 && !d.o.stopped; i++)
		d.next();
	CHECK(d.o.stopped, "Drift in stop: the pass ended");
	d.in.follow = true;
	ok = true;
	for (int i = 0; i < 50; i++) {
		o = d.next();
		ok = ok && o.stopped && !o.sound && !o.newPass;
	}
	CHECK(ok, "then Follow: stays stopped");
	// TRIG: a pass from the flag, following, that never ends.
	d.in.trig = true;
	o = d.next();
	CHECK(o.newPass && o.sound && o.end == o.anchor, "TRIG: a pass from the flag");
	ok = true;
	for (int i = 0; i < 2000; i++) {
		o = d.next();
		ok = ok && o.sound && !o.stopped && !o.newPass && o.end == o.anchor;
	}
	CHECK(ok, "and it follows, never stopping");
}

// Stretch at its stop does not hold while recording in Follow: to hold, Freeze.
void follow_stop_does_not_hold() {
	Sim r(0.5f, 8, false);
	r.in.follow = true;
	for (int i = 0; i < 10; i++)
		r.next();
	r.stretch(0);
	bool ok = true;
	for (int i = 0; i < 100; i++) {
		const PassOut o = r.next();
		ok = ok && !o.held && o.take && o.sound && !o.newPass && o.end == o.anchor;
	}
	CHECK(ok, "Follow, the Stretch stop, recording: not held, on the flag, taken every grain");
	r.in.frozen = r.in.freezeRose = true;
	PassOut o = r.next();
	CHECK(o.held && o.newPass && o.sound, "Freeze: held");
	r.in.now += 240;
	o = r.next();
	CHECK(o.held && !o.take, "and kept");
}

void grain_change_while_held() {
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 5; i++)
		h.next();
	const long long here = h.o.end;
	h.in.grain = 32768;
	h.in.hop = 16384;
	h.in.grainChanged = true;
	PassOut o = h.next();
	CHECK(o.take && o.held && o.end == here, "on the tape: taken again at the new size");
	o = h.next();
	CHECK(!o.take && o.held, "then kept");

	Sim g(0.5f, 0, false);
	for (int i = 0; i < 400; i++)
		g.next();
	g.in.grain = 8192;
	g.in.hop = 4096;
	g.in.grainChanged = true;
	o = g.next();
	CHECK(!o.take && o.held && o.sound, "off the tape: the held slice keeps its size");

	// Frozen at Position fully right.
	Sim f(1.f, 8, true);
	f.next();
	f.next();
	f.in.grain = 32768;
	f.in.hop = 16384;
	f.in.grainChanged = true;
	o = f.next();
	CHECK(o.take && o.held && o.end == NOW0, "nothing to travel: taken again at the new size");
}

void bad_input() {
	// Position: NaN reads 0, infinities and strays are held to [0, 1].
	Sim n(kNaN, 8, true);
	CHECK(n.next().anchor == NOW0 - ROOM, "NaN Position reads 0");
	Sim i(kInf, 8, true);
	CHECK(i.next().anchor == NOW0, "+inf Position reads 1");
	Sim m(-kInf, 8, true);
	CHECK(m.next().anchor == NOW0 - ROOM, "-inf Position reads 0");
	// A NaN Position held steady is no move: a held slice is kept.
	Sim h(kNaN, 0, false);
	h.next();
	PassOut o = h.next();
	CHECK(o.held && !o.take, "held at a NaN Position: kept, not retaken");
	// Pace: NaN and below 0 are the stop; above 1 is 1.
	const double paces[] = {double(kNaN), -1.0, -double(kInf)};
	for (double pc : paces) {
		Sim p(0.5f, 8, false);
		p.in.pace = pc;
		p.next();
		o = p.next();
		CHECK(o.held && !o.take, "pace %g reads as the stop", pc);
	}
	Sim q(0.f, 8, true);
	q.in.pace = 2.0;
	const long long a = q.next().end;
	CHECK(q.next().end == a + H, "pace 2 reads 1");
	q.in.pace = double(kInf);
	CHECK(q.next().end == a + 2 * H, "pace inf reads 1");
	// Hop: held to [1, grain].
	Sim z(0.f, 8, false);
	z.in.hop = 0;
	CHECK(z.next().end == NOW0 - ROOM + 1, "hop 0 reads 1: the guard is a frame");
	Sim w(0.f, 8, false);
	w.in.hop = 1 << 30;
	CHECK(w.next().end == NOW0 - ROOM + G, "a hop over a grain reads a grain");
	// A grain that does not fit the tape takes and sounds nothing.
	Sim g(0.5f, 8, false);
	g.in.grain = int(C) + 1;
	o = g.next();
	CHECK(!o.take && !o.sound && !o.stopped, "a grain larger than the tape: nothing");
	g.in.grain = 0;
	o = g.next();
	CHECK(!o.take && !o.sound, "a grain of 0: nothing");
}

// Review Focus 1 and 2: every field at random, with NaN and infinite
// positions and paces, Freeze and TRIG on random grains.
void fuzz() {
	uint64_t s = 0x9E3779B97F4A7C15ull;
	auto next = [&s]() {
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	};
	auto unit = [&]() { return double(next() >> 11) * (1.0 / 9007199254740992.0); };
	const int grains[] = {8192, 16384, 32768, 65536};
	const long long caps[] = {1440000, 1323000, 2880000};
	long long bad = 0, badAnchor = 0, both = 0, other = 0;
	long long seenTake = 0, seenPass = 0, seenHeld = 0, seenStopped = 0, seenKept = 0, seenWait = 0, seenWake = 0, seenHeldWake = 0, seenThaw = 0,
	          seenThawStopped = 0, seenFullPace = 0, seenFollow = 0, seenFollowStopped = 0, followPass = 0, badPress = 0, seenPress = 0,
	          seenPressTied = 0, seenDetour = 0;
	int steps = 0;
	for (int run = 0; run < 3; run++) {
		PassLogic p;
		p.reset();
		PassIn in{};
		in.capacity = caps[run];
		in.grain = 16384;
		in.hop = 8192;
		in.now = run == 0 ? 0 : 7000000;   // the first run starts on a fresh tape
		in.position = 1.f;
		in.pace = 0.125;
		int tail = 0;
		bool wasStopped = false, wasOnAnchor = false, wasHeld = false, detour = false;
		for (int i = 0; i < 333334; i++, steps++) {
			const double u = unit();
			in.trig = unit() < 0.01;
			in.grainChanged = false;
			in.freezeRose = in.freezeFell = false;
			if (unit() < 0.01) {
				const bool f = !in.frozen;
				in.frozen = f;
				in.freezeRose = f;
				in.freezeFell = !f;
				tail = f ? 1 : 0;
			}
			if (unit() < 0.002) {
				in.grain = grains[next() % 4];
				in.hop = unit() < 0.9 ? in.grain / 2 : int(1 + next() % uint64_t(in.grain));
				in.grainChanged = true;
			}
			if (unit() < 0.002)
				in.stopAtEnd = !in.stopAtEnd;
			if (unit() < 0.002)
				in.follow = !in.follow;
			if (u < 0.2) {
				const float specials[] = {kNaN, kInf, -kInf, 0.f, 1.f, -1.f, 2.f, 1e30f};
				in.position = specials[next() % 8];
			}
			else if (u < 0.5)
				in.position = float(unit());
			if (unit() < 0.1) {
				const double specials[] = {0.0, 1.0, double(kNaN), double(kInf), -1.0, 1e-9, 0.5, 2.0};
				in.pace = specials[next() % 8];
			}
			else if (unit() < 0.05)
				in.pace = unit();
			in.press = unit() < 0.01;
			in.pressLag = unit() < 0.1 ? (long long)(next() % 9000000) - 1000000 : (long long)(unit() * double(in.capacity));
			const PassOut o = p.step(in);
			const long long lo = in.now - in.capacity + in.grain;
			const bool moving = !in.frozen || in.freezeRose;
			if (o.take && (o.end > in.now || o.end < lo + (moving ? in.hop : 0)))
				bad++;
			if (o.anchor > in.now || o.anchor < lo)
				badAnchor++;
			seenTake += o.take;
			seenPass += o.newPass;
			seenHeld += o.held;
			seenStopped += o.stopped;
			seenKept += o.sound && !o.take;
			if (o.sound && o.stopped)
				both++;
			// TRIG always starts a pass, and so does Freeze rising, in either
			// setting, whatever came before; onto nothing to travel through
			// (the frozen anchor within a grain of now) it is a pass that is
			// held. And frozen with nothing to travel through is held and
			// sounding, always.
			const bool nothing = in.frozen && in.now - o.anchor < in.grain;
			const bool mustStart = in.trig || in.freezeRose;
			// A thaw: a new pass from the anchor while sounding, in either
			// setting; stopped (in stop) it stays stopped.
			const bool thawStopped = in.freezeFell && wasStopped && in.stopAtEnd && !in.trig && !in.press;
			const bool mustReturn = in.freezeFell && !thawStopped;
			if ((mustReturn && !(o.newPass && o.sound && o.end == o.anchor)) || (thawStopped && !(o.stopped && !o.sound)))
				other++;
			seenThaw += mustReturn && o.newPass;
			// Recording at pace 1 (or above, which reads 1), sounding: on the flag.
			const bool fullPace = !in.frozen && in.pace >= 1.0;
			if (fullPace && o.sound && o.end != o.anchor)
				other++;
			seenFullPace += fullPace && o.sound;
			// Following (Follow, recording), sounding: on the flag, taken, never
			// held, and a new pass only for TRIG, a thaw, arriving there from
			// elsewhere, or leaving held with the held place off the tape (Drift
			// held on the flag, which then left the tape), or the first grain.
			const bool following = in.follow && !in.frozen;
			if (following && o.sound && (o.end != o.anchor || !o.take || o.held))
				other++;
			if (following && o.newPass && !in.trig && !in.freezeFell && !in.press && wasOnAnchor && !wasHeld && i > 0)
				followPass++;
			seenFollow += following && o.sound;
			seenFollowStopped += following && o.stopped;
			wasOnAnchor = o.sound && o.end == o.anchor;
			wasHeld = o.held;
			seenThawStopped += thawStopped && o.stopped;
			// A press moves the read point to its place, held to the tape, a
			// new pass, and never the anchor; where another rule starts a pass
			// from the anchor on the same grain (TRIG, Freeze's edges, a wake)
			// or ties the read point to the anchor (recording at full pace,
			// following, no room), the pass is from the anchor. Frozen with
			// nothing to travel through, the pressed pass plays to the end
			// before the hold (the detour).
			{
				const double pc = in.pace > 0.0 ? (in.pace < 1.0 ? in.pace : 1.0) : 0.0;
				const long long lowNow = std::min(in.now - in.capacity + in.grain + (in.frozen ? 0 : std::max(1, std::min(in.hop, in.grain))), in.now);
				const bool tied = !in.frozen && (pc == 1.0 || in.follow || o.anchor - lowNow < in.grain);
				const bool starts = in.trig || in.freezeRose || (in.freezeFell && !wasStopped) || (wasStopped && (!in.stopAtEnd || nothing)) || i == 0;
				if (in.press && !starts) {
					const long long want = tied ? o.anchor : std::max(lowNow, std::min(in.now - std::max(0LL, in.pressLag), in.now));
					if (!(o.newPass && o.sound && o.take && !o.stopped && o.end == want))
						badPress++;
					seenPress += !tied;
					seenPressTied += tied;
					detour = !tied && nothing;
				}
				else if (o.newPass || o.stopped || !nothing)
					detour = false;
				seenDetour += detour;
			}
			if ((o.take && !o.sound) || (o.held && !o.sound) || (o.newPass && !(o.take && o.sound)) ||
			    (mustStart && (o.stopped || !o.newPass || o.end != o.anchor)) ||
			    (nothing && !detour && !(o.held && o.sound)) || (nothing && !o.sound))
				other++;
			seenWait += in.freezeRose && in.stopAtEnd && !nothing && !in.trig && wasStopped && o.newPass && !o.held;
			seenWake += in.freezeRose && in.stopAtEnd && nothing && !in.trig && o.newPass && o.held;
			seenHeldWake += !in.freezeRose && !in.trig && nothing && in.stopAtEnd && wasStopped && o.held && o.newPass;
			wasStopped = o.stopped;
			// The tape: a hop a grain, give or take, while recording; the
			// faded frames on the grain after a freeze edge; now and then a
			// jump.
			if (!in.frozen)
				in.now += in.hop + (long long)(next() % 64) - 32;
			else if (tail) {
				in.now += (long long)(next() % 480);
				tail = 0;
			}
			if (unit() < 0.0005)
				in.now += (long long)(next() % 5000000);
		}
	}
	CHECK(bad == 0, "%lld of %d slices taken off the tape", bad, steps);
	CHECK(badAnchor == 0, "%lld anchors off the tape", badAnchor);
	CHECK(both == 0, "%lld grains both sounding and stopped", both);
	CHECK(other == 0, "%lld grains taking or held while silent, a new pass not taken, TRIG ignored, Freeze in stop not by the rule,"
	      " or nothing to travel through not held", other);
	CHECK(badPress == 0 && seenPress > 1000 && seenPressTied > 100 && seenDetour > 100,
	      "presses: %lld not at their place as a new pass; %lld seen free, %lld tied to the anchor, %lld grains of a detour at the end", badPress,
	      seenPress, seenPressTied, seenDetour);
	CHECK(seenThaw > 0 && seenThawStopped > 0, "thaws: %lld returned, %lld stayed stopped", seenThaw, seenThawStopped);
	CHECK(seenFullPace > 1000, "recording at full pace, sounding: %lld grains", seenFullPace);
	CHECK(followPass == 0 && seenFollow > 10000 && seenFollowStopped > 1000,
	      "following: %lld new passes on the flag with no TRIG or thaw; %lld grains following, %lld stopped", followPass, seenFollow,
	      seenFollowStopped);
	CHECK(seenWait > 0 && seenWake > 0 && seenHeldWake > 0, "Freeze rising in stop: %lld stopped woken into a pass, %lld held; %lld stopped woken into held",
	      seenWait, seenWake, seenHeldWake);
	CHECK(steps == 1000002 && seenTake > 100000 && seenPass > 10000 && seenHeld > 10000 && seenStopped > 10000 && seenKept > 10000,
	      "the fuzz reached every state: %d steps, %lld taken, %lld passes, %lld held, %lld stopped, %lld kept", steps,
	      seenTake, seenPass, seenHeld, seenStopped, seenKept);
}

} // namespace

int main() {
	anchor_for_positions();
	pace_frozen();
	pace_recording();
	return_frozen();
	return_recording();
	pass_length_formula();
	stretch_1_recording_stays_on_the_flag();
	stretch_back_to_1_returns_to_the_flag();
	position_turn_moves_by_that_much();
	position_turn_stops_at_the_edge();
	trig_restarts();
	freeze_on_restarts();
	thaw_returns_to_the_anchor();
	held_at_the_stop();
	position_wobble_is_no_move();
	held_off_tape_position_move_takes_at_the_anchor();
	leaving_held_on_tape();
	leaving_held_off_tape();
	nothing_to_travel_frozen();
	full_left_recording();
	stop_at_end();
	freeze_in_stop();
	press_moves_the_read_point_only();
	held_recording_position_move_takes_at_the_anchor();
	follow_sits_on_the_flag();
	follow_frozen_is_as_drift();
	drift_to_follow_rejoins_once();
	follow_to_drift_falls_behind_from_the_flag();
	follow_stopped_stays_stopped();
	follow_stop_does_not_hold();
	grain_change_while_held();
	bad_input();
	fuzz();
	return rkatest::finish("test_pass");
}
