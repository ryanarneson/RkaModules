#pragma once
// Doldrums' read point and its passes, as pure logic: stepped once a grain,
// it says which slice of the tape the grain takes, whether the grain sounds,
// and whether it begins a pass. No sound, no tape; Rack-free, header-only,
// safe per grain (no allocation, no libm).
//
// Frames are absolute indices that only grow (Ring.hpp). The read point is
// the index one past the slice's last frame, so a grain takes
// [end - grain, end). The tape holds [now - capacity, now), so a slice is
// whole for end in [now - capacity + grain, now]. Position's lag is
// lagFrames(position, capacity, grain) (Laws.hpp); the anchor, where a pass
// starts, is now - lag. The read point moves pace * hop frames a grain, in
// whole frames, the fraction carried to the next grain.
//
// The rules (the design's "The read point, Position and Stretch", "Held",
// "Play once, and RUN"; the plan's second deliberate difference), in the
// order step() applies them:
//
// - The guard. While the tape moves, the engine copies the slice a little
//   after the grain boundary and the write head goes on meanwhile, so a slice
//   whose start is within one hop of the oldest frame counts as lost: the
//   lowest end is then now - capacity + grain + hop. The tape moves while
//   recording, and also on the grain Freeze rises (the ring writes up to 5 ms
//   of faded frames after the edge). Frozen after that, the guard is 0. The
//   anchor is held inside these bounds, so Position fully left, recording,
//   starts a hop in.
// - TRIG starts a pass from the anchor at any time: mid pass, held, stopped.
// - Freeze rising, in go round, starts a pass from the anchor, always. In
//   stop it is one rule, whatever was happening before (a pass running,
//   held, stopped): with a pass to travel it ends any pass and the module
//   waits, stopped and silent, for TRIG; with nothing to travel through (the
//   frozen anchor less than a grain from now) it is held (below). (The final
//   review's ruling, superseding Task 8's, under which Freeze in stop
//   started a pass if one was running and waited only if none was: the same
//   press played or did not play by history. The design says both that a
//   stopped module "stays silent until a trigger at TRIG starts the next
//   pass" and that a tape with nothing to travel through is held "with
//   either setting of At the end of a pass"; with a pass to travel, nothing
//   sounds or gates between the freeze and the trigger.) TRIG on the same
//   grain wins: a pass starts.
// - Freeze falling (a thaw) returns the read point to the anchor at once, a
//   new pass, whenever the module is sounding (running or held), in either
//   setting: with Position fully right that is straight back to the live
//   input, the frozen moment dissolving into it over a grain. Stopped (in
//   stop, waiting for TRIG) it stays stopped: a thaw starts no pass and
//   raises no RUN. Held at the Stretch stop, the slice at the anchor is
//   taken once and held there, on a tape now recording (in Drift; in Follow
//   the read point follows from the anchor, below). (The user's ruling
//   at the first test build, superseding "Freeze going off changes
//   nothing", under which a thaw from a hold at Position fully right went
//   on from the frozen moment and came back to now only when that pass
//   ended, 30 / (1 - pace) s later.) The engine reports at most one of
//   Freeze's edges a grain: the one that leaves Freeze as it now is.
// - Stopped (only in stop) stays silent until TRIG, or until the tape is
//   frozen with nothing to travel through: frozen and stopped, that is held
//   and sounding whenever it is true (at a freeze, at reset, and when
//   Position is turned fully right while stopped, which wakes it into held;
//   so a state saved and reopened sounds as it did). Stretch at the stop,
//   Freeze falling or any other Position move do not wake it. Go round
//   chosen while stopped starts a pass at once (choice: in go round
//   something is always sounding). After reset() the first grain starts a
//   pass; in stop it is stopped, unless the tape is frozen with nothing to
//   travel through (then held), so a patch opened frozen in stop waits for
//   a trigger unless it is held.
// - A Position move moves the read point by the change in lag, at once,
//   held to the edges of the tape, and does not start a pass. It is decided
//   in frames: a move is a change of at least a frame in the rounded lag
//   since the last grain, so a wobble in a CV's last bit is no move. A slow
//   drift is not lost: the rounded lags' differences add up to all of it.
//   The lag is reckoned at the current capacity and grain, so a change of
//   Grain alone moves nothing.
// - Held: Stretch at its stop (pace 0; not while following, below), or the
//   tape frozen with the anchor less than a grain from now (nothing to
//   travel through). The read point stands still, the grain sounds, a pass
//   never ends, in either setting. `take` is true on the grain held begins
//   if the read point is still on the tape, on a Position move, and on a
//   change of Grain if the slice is on the tape at the new size. Frozen, a
//   Position move while the held place is on the tape moves it as above (the
//   scrub). Recording, a move takes the new slice at the anchor, Position's
//   own place, still held and with no new pass, wherever the held place is
//   (ruling: as leaving held off the tape goes from the anchor; and the fix
//   wave's re-review, Task 14: before, a held place still on the tape was
//   shifted by the change, so a patch reopened held on the silence from
//   before it opened could not be woken by turning Position for up to 30 s).
//   `take` is false otherwise, frozen too (choice: one rule for both causes;
//   retaking a still tape would be harmless, but keeping the slice also
//   keeps the faded frames written just after a freeze out of the held
//   moment).
// - Leaving held: on from where the read point is if that is on the tape,
//   else a new pass from the anchor (never the end of a pass, in stop too).
// - Not frozen, a pass with less than a grain to travel (the anchor within a
//   grain of the guarded oldest edge; Position fully left is the case) sits
//   on the anchor every grain: it sounds, it is not held, it never ends and
//   it never marks a new pass, so RUN stays high (difference 2).
// - While recording: Follow (the menu; PassIn::follow; the user's request at
//   2.0.46, PaulXStretch's way: capture first, stretch after; Drift, the
//   default, is every other rule here). Recording in Follow ("following"),
//   the read point is on the anchor at every pace, as at x1 below: taken,
//   sounding, never held (Stretch's stop does not hold while following: to
//   hold, Freeze), a pass that never ends (RUN stays high) and no new pass
//   while it stays there; a Position move moves it with the anchor. Arriving
//   from elsewhere (Follow chosen with the read point behind the anchor, or
//   held) is one return to the anchor, a new pass, once (by the x1 rule's
//   onAnchor_; leaving held is as above). Stopped stays stopped: Follow
//   wakes nothing. TRIG, a thaw, Freeze rising and a press in the window
//   keep their rules (a thaw, a new pass from the anchor, then follows).
//   Drift chosen while following goes on from the anchor at the pace and
//   falls behind (no return, no new pass). Frozen, Follow and Drift are the
//   same in every respect.
// - Recording at exactly Stretch 1 (pace 1, the dial's left stop or a CV
//   holding it there), the read point is on the anchor: if it is not there
//   at a grain (Stretch brought back to 1 after it fell behind, or from the
//   Stretch stop), it returns to the anchor at that grain, a new pass, once;
//   from then on it keeps pace on the anchor, and a Position move moves it
//   with the anchor. (The user's observation at 2.0.46: before, x1 kept pace
//   from wherever the read point was, so the dial said x1 while the module
//   played the input late by any amount, for ever.) Frozen, x1 travels from
//   where it is as any pace does; a pace under 1 falls behind and returns at
//   the pass's end; stopped stays stopped.
// - Otherwise the read point travels. Frozen, the pass is over on the grain
//   it would pass now (a grain ending exactly at now still sounds).
//   Recording, the pass is over on the grain its end falls below the guarded
//   lowest end; it never passes now (held at now if it would, as after a
//   thaw part way through a hop). Frozen, a slice pushed below the oldest
//   whole grain (only by a larger Grain) is held at that edge. A pass over:
//   in go round, back on the anchor with newPass; in stop, stopped.
//
// So a frozen pass from lag L lasts floor(L / (pace hop)) + 1 grains, and a
// recording one floor((capacity - grain - hop - L) / ((1 - pace) hop)) + 1.
// At pace 1, recording, the read point keeps pace with now and never returns.
//
// Bad input: position NaN reads 0 and is held to [0, 1]; pace NaN or below 0
// reads 0 (held), above 1 reads 1; hop is held to [1, grain]. A grain of 0
// or one larger than the tape takes nothing and sounds nothing. `now` must
// not fall; if it does, every slice taken is still inside the tape.
#include "Laws.hpp"
#include <algorithm>

namespace rka { namespace doldrums {

struct PassIn {
	long long now, capacity;
	int grain, hop;
	float position;        // after CV
	double pace;           // 0 = Stretch at the stop
	bool frozen, freezeRose, trig, stopAtEnd, grainChanged;
	bool freezeFell;       // Freeze went off since the last grain (the engine reports at most one of the two edges)
	bool follow;           // the menu's While recording: false Drift, true Follow
};

struct PassOut {
	long long end;         // the slice to take: [end - grain, end)
	bool take;             // false: keep the loudnesses already held
	bool sound;            // false: stopped, this grain is silent
	bool newPass;          // this grain begins a pass (RUN's marker)
	bool held;             // for either reason
	bool stopped;
	long long anchor;      // for the window
};

struct PassLogic {
	void reset() {
		fresh_ = true;
		stopped_ = false;
		held_ = false;
		onAnchor_ = false;
		end_ = 0;
		frac_ = 0.0;
		pos_ = 1.f;
	}

	PassOut step(const PassIn& in) {
		const PassOut o = stepOnce(in);
		onAnchor_ = o.sound && o.end == o.anchor;
		return o;
	}

private:
	PassOut stepOnce(const PassIn& in) {
		PassOut o{};
		o.end = o.anchor = in.now;
		if (in.capacity < 1 || in.grain < 1 || in.grain > in.capacity)
			return o;
		const long long now = in.now;
		const int grain = in.grain;
		const int hop = std::max(1, std::min(in.hop, grain));
		const float pos = detail::clampLo(in.position, 0.f, 1.f);
		const double pace = in.pace > 0.0 ? (in.pace < 1.0 ? in.pace : 1.0) : 0.0;

		const bool still = in.frozen && !in.freezeRose;
		const long long low = std::min(now - in.capacity + grain + (still ? 0 : hop), now);
		const long long lag = lagFrames(pos, in.capacity, grain);
		const long long anchor = clamp(now - lag, low, now);
		o.anchor = anchor;

		const bool nothing = in.frozen && now - anchor < grain;   // nothing to travel through
		const bool following = in.follow && !in.frozen;            // While recording: Follow, and recording
		const bool held = (pace == 0.0 && !following) || nothing;
		const bool noRoom = !in.frozen && anchor - low < grain;
		const long long shift = fresh_ ? 0 : lagFrames(pos_, in.capacity, grain) - lag;
		const bool moved = shift != 0;
		pos_ = pos;

		bool start = in.trig;
		if (fresh_) {
			fresh_ = false;
			stopped_ = in.stopAtEnd && !nothing;
			start = start || !stopped_;
		}
		if (in.freezeFell && !stopped_)
			start = true;   // a thaw: back to the anchor, unless stopped
		if (in.freezeRose) {
			if (!in.stopAtEnd || nothing)
				start = true;
			else
				stopped_ = true;   // stop, a pass to travel: wait for TRIG (unless it came with the freeze)
		}
		if (stopped_ && (!in.stopAtEnd || nothing))
			start = true;
		if (start)
			return begin(anchor, held, o);
		if (stopped_)
			return stop(anchor, o);

		const auto onTape = [&](long long e) { return e >= low && e <= now; };

		if (held) {
			if (moved) {
				end_ = in.frozen && onTape(end_) ? clamp(end_ + shift, low, now) : anchor;
				o.take = true;
			}
			else
				o.take = (!held_ || in.grainChanged) && onTape(end_);
			held_ = true;
			return sounding(o);
		}
		if (held_ && !onTape(end_))
			return begin(anchor, false, o);
		held_ = false;

		if (noRoom) {
			end_ = anchor;
			frac_ = 0.0;
			o.take = true;
			return sounding(o);
		}

		if (!in.frozen && (pace == 1.0 || following)) {
			if (!onAnchor_)
				return begin(anchor, false, o);   // x1 again, or Follow: back on the anchor, a new pass
			end_ = anchor;                        // on it, keeping pace (a Position move moves it with the anchor)
			frac_ = 0.0;
			o.take = true;
			return sounding(o);
		}

		long long e = moved ? clamp(end_ + shift, low, now) : end_;
		frac_ += pace * double(hop);
		const long long whole = (long long) frac_;
		frac_ -= double(whole);
		e += whole;
		const bool over = in.frozen ? e > now : e < low;
		if (over) {
			if (in.stopAtEnd) {
				stopped_ = true;
				return stop(anchor, o);
			}
			return begin(anchor, false, o);
		}
		end_ = clamp(e, low, now);
		o.take = true;
		return sounding(o);
	}

	bool fresh_ = true;     // no grain since reset()
	bool onAnchor_ = false; // the last grain sounded with the read point on the anchor
	bool stopped_ = false;
	bool held_ = false;     // the last grain was held
	long long end_ = 0;     // the read point
	double frac_ = 0.0;     // travel short of a whole frame, carried
	float pos_ = 1.f;       // the last grain's position

	static long long clamp(long long x, long long lo, long long hi) { return std::max(lo, std::min(x, hi)); }

	PassOut begin(long long anchor, bool held, PassOut& o) {
		end_ = anchor;
		frac_ = 0.0;
		stopped_ = false;
		held_ = held;
		o.take = true;
		o.newPass = true;
		return sounding(o);
	}
	PassOut sounding(PassOut& o) const {
		o.end = end_;
		o.sound = true;
		o.stopped = false;
		o.held = held_;
		return o;
	}
	PassOut stop(long long anchor, PassOut& o) {
		end_ = anchor;
		frac_ = 0.0;
		held_ = false;
		o.end = anchor;
		o.stopped = true;
		return o;
	}
};

}} // namespace rka::doldrums
