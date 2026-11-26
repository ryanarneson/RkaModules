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
// - Stopped (only in stop) stays silent until TRIG. Freeze rising does not
//   wake it, nor does Stretch at the stop (choice: "stays silent until a
//   trigger at TRIG starts the next pass"; waking on Freeze would set RUN
//   off before the trigger in the patch the design is for). Go round chosen
//   while stopped starts a pass at once (choice: in go round something is
//   always sounding). After reset() the first grain starts a pass, or in
//   stop is stopped.
// - Freeze rising starts a pass from the anchor. Freeze falling changes
//   nothing: the ordinary rules go on from where the read point is.
// - A Position move (a change in the position after the NaN rule) moves the
//   read point by the change in lag, at once, held to the edges of the tape,
//   and does not start a pass. The distance is reckoned at the current
//   capacity and grain, so a change of Grain alone moves nothing.
// - Held: Stretch at its stop (pace 0), or the tape frozen with the anchor
//   less than a grain from now (nothing to travel through). The read point
//   stands still, the grain sounds, a pass never ends, in either setting.
//   `take` is true on the grain held begins if the read point is still on
//   the tape, on a Position move, and on a change of Grain if the slice is
//   on the tape at the new size; false otherwise, frozen too (choice: one
//   rule for both causes; retaking a still tape would be harmless, but
//   keeping the slice also keeps the faded frames written just after a
//   freeze out of the held moment).
// - Leaving held: on from where the read point is if that is on the tape,
//   else a new pass from the anchor (never the end of a pass, in stop too).
// - Not frozen, a pass with less than a grain to travel (the anchor within a
//   grain of the guarded oldest edge; Position fully left is the case) sits
//   on the anchor every grain: it sounds, it is not held, it never ends and
//   it never marks a new pass, so RUN stays high (difference 2).
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
		end_ = 0;
		frac_ = 0.0;
		pos_ = 1.f;
	}

	PassOut step(const PassIn& in) {
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

		const bool held = pace == 0.0 || (in.frozen && now - anchor < grain);
		const bool noRoom = !in.frozen && anchor - low < grain;
		const bool moved = !fresh_ && pos != pos_;
		const long long shift = moved ? lagFrames(pos_, in.capacity, grain) - lag : 0;
		pos_ = pos;

		bool start = in.trig;
		if (fresh_) {
			fresh_ = false;
			stopped_ = in.stopAtEnd;
			start = start || !stopped_;
		}
		if (stopped_ && !in.stopAtEnd)
			start = true;
		if (!stopped_ && in.freezeRose)
			start = true;
		if (start)
			return begin(anchor, held, o);
		if (stopped_)
			return stop(anchor, o);

		const auto onTape = [&](long long e) { return e >= low && e <= now; };

		if (held) {
			if (moved) {
				end_ = clamp(end_ + shift, low, now);
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

private:
	bool fresh_ = true;     // no grain since reset()
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
