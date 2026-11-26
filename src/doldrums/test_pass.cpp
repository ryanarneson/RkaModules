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
		in.trig = in.freezeRose = in.grainChanged = false;
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

void stretch_1_recording_never_returns() {
	Sim r(0.5f, 1, false);
	r.next();
	bool ok = true;
	for (int i = 0; i < 20000; i++) {
		const PassOut o = r.next();
		ok = ok && !o.newPass && o.take && r.nowWas - o.end == 711808;
	}
	CHECK(ok, "x1 keeps pace for 20000 grains (about an hour), late by Position's distance");
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
	// grains); a turn left by 427 085 would take it off: it stops at the edge.
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 51; i++)
		h.next();
	CHECK(h.o.end >= h.low(true), "the held place is still on the tape");
	h.in.position = 0.2f;
	o = h.next();
	CHECK(o.end == h.low(true) && o.take && o.held && !o.newPass, "held: a turn that would leave the tape stops at its edge");

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

	// In stop, stopped: Freeze on starts a pass too.
	Sim s(0.5f, 8, false, true);
	CHECK(s.next().stopped, "stopped from reset");
	s.in.frozen = s.in.freezeRose = true;
	o = s.next();
	CHECK(o.end == s.anchorFor(0.5f) && o.newPass && o.take && o.sound && !o.stopped, "stopped: Freeze on, a new pass");

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

void freeze_off_changes_nothing() {
	Sim f(0.5f, 8, true);
	for (int i = 0; i < 10; i++)
		f.next();
	const long long before = f.o.end;
	f.in.frozen = false;
	f.in.now += 3000;   // recording resumes part way through a hop
	PassOut o = f.next();
	CHECK(o.end == before + 1024 && !o.newPass && o.take, "Freeze off: the read point goes on from where it is");
	bool ok = true;
	for (int k = 2; k <= 50; k++) {
		o = f.next();
		ok = ok && o.end == before + k * 1024 && !o.newPass;
	}
	CHECK(ok, "and at its pace while recording");
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

	// A Position move takes a new slice at the moved place.
	Sim h(0.5f, 0, false);
	for (int i = 0; i < 20; i++)
		h.next();
	const long long here = h.o.end;
	h.in.position = 0.45f;
	o = h.next();
	const long long shift = lagFrames(0.5f, C, G) - lagFrames(0.45f, C, G);
	CHECK(o.take && o.held && o.end == here + shift, "a Position move: taken at the moved place");
	o = h.next();
	CHECK(!o.take && o.end == here + shift, "and kept");

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
	// stopped module (Freeze going on does: freeze_wakes_a_stopped_module).
	f.in.frozen = false;
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

// The ruling: Freeze going on starts a pass from the anchor even when
// stopped, so "hold what is playing" works in stop too.
void freeze_wakes_a_stopped_module() {
	// Stopped from reset, Position fully right: Freeze holds the moment.
	Sim h(1.f, 8, false, true);
	for (int i = 0; i < 3; i++)
		h.next();
	CHECK(h.o.stopped && !h.o.sound, "stopped from reset");
	h.in.frozen = h.in.freezeRose = true;
	PassOut o = h.next();
	CHECK(o.newPass && o.held && o.sound && o.take && !o.stopped && o.end == h.nowWas, "Freeze on: held at now");
	bool ok = true;
	for (int i = 0; i < 1000; i++) {
		o = h.next();
		ok = ok && o.held && o.sound && !o.stopped && !o.newPass;
	}
	CHECK(ok, "and held, never stopped");

	// Stopped after a pass ended, Position 0.5: Freeze on plays one pass from
	// the anchor, then stops again.
	Sim f(0.5f, 8, false, true);
	f.in.trig = true;
	f.next();
	for (int i = 0; i < 200 && !f.o.stopped; i++)
		f.next();
	CHECK(f.o.stopped, "the recording pass ended: stopped");
	f.in.frozen = f.in.freezeRose = true;
	o = f.next();
	CHECK(o.newPass && o.sound && !o.stopped && o.end == f.anchorFor(0.5f), "Freeze on: a new pass from the anchor");
	const long long n = frozenPassGrains(711808, 1024);
	for (long long i = 1; i < n; i++)
		f.next();
	CHECK(f.o.sound && !f.o.stopped, "the pass sounds whole");
	o = f.next();
	CHECK(o.stopped && !o.sound, "and in stop, stops at its end");
	// Freeze off and on again wakes it again.
	f.in.frozen = false;
	f.next();
	f.in.frozen = f.in.freezeRose = true;
	o = f.next();
	CHECK(o.newPass && o.sound && !o.stopped, "every Freeze on starts a pass");
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
	long long seenTake = 0, seenPass = 0, seenHeld = 0, seenStopped = 0, seenKept = 0;
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
		for (int i = 0; i < 333334; i++, steps++) {
			const double u = unit();
			in.trig = unit() < 0.01;
			in.grainChanged = false;
			in.freezeRose = false;
			if (unit() < 0.01) {
				const bool f = !in.frozen;
				in.frozen = f;
				in.freezeRose = f;
				tail = f ? 1 : 0;
			}
			if (unit() < 0.002) {
				in.grain = grains[next() % 4];
				in.hop = unit() < 0.9 ? in.grain / 2 : int(1 + next() % uint64_t(in.grain));
				in.grainChanged = true;
			}
			if (unit() < 0.002)
				in.stopAtEnd = !in.stopAtEnd;
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
			if ((o.take && !o.sound) || (o.held && !o.sound) || (o.newPass && !(o.take && o.sound)) ||
			    ((in.trig || in.freezeRose) && (o.stopped || !o.newPass)))
				other++;
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
	CHECK(other == 0, "%lld grains taking or held while silent, a new pass not taken, or TRIG ignored", other);
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
	stretch_1_recording_never_returns();
	position_turn_moves_by_that_much();
	position_turn_stops_at_the_edge();
	trig_restarts();
	freeze_on_restarts();
	freeze_off_changes_nothing();
	held_at_the_stop();
	position_wobble_is_no_move();
	held_off_tape_position_move_takes_at_the_anchor();
	leaving_held_on_tape();
	leaving_held_off_tape();
	nothing_to_travel_frozen();
	full_left_recording();
	stop_at_end();
	freeze_wakes_a_stopped_module();
	grain_change_while_held();
	bad_input();
	fuzz();
	return rkatest::finish("test_pass");
}
