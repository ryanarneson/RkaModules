#pragma once
// What Doldrums' window shows (Task 12 draws it), Rack-free: a reduced copy of
// the 30 s ring as 512 columns of peaks, kept up to date as frames are
// written, and the snapshot of it, with the marks and the state, that the
// audio thread hands to the UI thread without a lock. Header-only, C++11.
//
// The columns. The tape is cut into columns by absolute frame index (Ring.hpp):
// column j covers frames [floor(j * capacity / 512), floor((j + 1) * capacity
// / 512)), so a column is capacity / 512 frames, give or take one, and the
// cut repeats exactly every `capacity` frames (a whole 512 columns): the
// remainder of a capacity that 512 does not divide is spread by that floor,
// no frame is dropped or counted twice, and the columns stay aligned with the
// tape for as long as the engine runs. A column's low is the least and its
// high the greatest sample over both channels in its frames, as Eddy's
// Overview has it (src/Overview.hpp, which fits a tape whose columns stand
// still; a ring that scrolls needs the columns tied to absolute frames, so
// this one does its own); a column with no frames, or none yet, is 0, 0.
// NaN and infinity count as 0, as the Ring writes them.
//
// What the snapshot holds. 512 columns, oldest first: the 511 columns that
// closed last, then the open one, still filling, so a sound shows in the
// window the frame it is written and moves a column left each time a column
// closes (capacity / 512 frames, 58.6 ms at 48 kHz). Frozen the tape stands
// still and so does the picture. The window therefore stands for the last
// (511 + the open column's share) / 512 of the tape: always inside what the
// ring still holds, and short of the whole 30 s by under one column. The
// marks are not rounded to columns: they are true fractions of the 30 s
// (absolute - (now - capacity)) / capacity, 0 being 30 s ago and 1 now, so a
// mark and the sound it points at can differ by up to one column (0.2 % of
// the window).
//
// The marks. anchor is the Position's place (DoldrumsState::anchor);
// readStart and readEnd are the slice last taken, [end - grain, end), so
// readEnd - readStart is the grain's true width. Fractions of the window,
// not held to 0: a held sound's read point stands where it was while the tape
// goes on (not frozen), so it rides left with the sound and below 0 once
// the sound leaves the tape. Every mark is held to [-1, 1] so that what the
// window draws stays finite, however long a hold goes on.
//
// The state word. "" when plainly running. Otherwise the builder's words,
// upper case, short enough for a corner in the panel's condensed type:
// "FROZEN", "HELD", "STOPPED", and where two apply both are shown, FROZEN
// first: "FROZEN HELD", "FROZEN STOPPED" (the longest, 14 letters; a tape
// can be frozen and its sound held, or frozen and the pass stopped, and
// dropping either would hide a state the panel must show). Stopped and held
// do not appear together: a stopped pass is not holding, so STOPPED wins.
// Held because there is nothing to travel through (DoldrumsState::atEnd:
// frozen, the anchor within a grain of now, as at Position fully right)
// says "FROZEN AT END" (13 letters), so the window says why Stretch does
// nothing (the user's decision at 2.0.46); "FROZEN HELD" is then held by
// Stretch's stop alone. Both at once say "FROZEN AT END": it is the more
// specific reason Stretch is inert (moving Stretch off its stop would still
// leave the head with no tape ahead). Recording in While recording: Follow,
// sounding (DoldrumsState::following), it says "FOLLOW", so the window says
// why Stretch does nothing until Freeze (Task 14, the user's rule of least
// astonishment). STOPPED keeps its precedence (a module stopped in Follow
// says STOPPED: it is waiting for TRIG, not following), and frozen, the
// words are the frozen ones (a following flag from the boundary before a
// freeze is not shown under FROZEN); Drift shows nothing new.
//
// Threads.
//   audio thread: configure, clear, wrote, publish (they share the columns
//     and the open column, which are plain data of the audio thread's);
//   UI thread (or any other, any number): read;
//   a worker: rebuild, but only while the audio thread is making none of the
//     calls above on this view. The caller guarantees that, and that the
//     audio thread's first call afterwards happens after rebuild returns
//     (an atomic the worker release-stores and the audio thread acquire-loads,
//     as Task 11's hand-over of a tape does; test_threads does it so under
//     ThreadSanitizer). read() may run all the while: it touches only the
//     published snapshot, which rebuild does not.
//
// The owner's pattern, in the audio callback, after engine.process():
//     if the ring's now() rose: view.wrote(l, r) of the frame just written,
//       as it stands on the ring (the faded value, read back with
//       ring.channel(c)[ring.physical(ring.now() - 1)]); a frame is not
//       written while frozen and standing still, so wrote is not called
//     about every rate / 60 frames: view.publish(engine.state())
//     the tape cleared (Initialize): view.clear(); a new ring: view.configure
// publish() looks at the state's capacity and now: if they are not what
// the columns have been fed (a new ring, a missed call), it starts the columns
// again from the state, so the picture is never of another tape, only blank
// until the tape fills it.
//
// Hand-off. publish() writes the snapshot into an array of atomic words
// under a sequence counter (a seqlock): the counter is made odd, the words
// are stored (release), the counter is made even again. read() takes the
// counter (acquire), copies the words (acquire loads), and takes the counter
// again; if it changed, or was odd, it tries again, up to kTries times, and
// then returns false, leaving `out` as it was. So a reader gets a whole
// snapshot or false, never a mix, and a writer never waits: no lock, no
// allocation, no fence (the words' orderings do what a fence would, and
// ThreadSanitizer, which does not model fences, can check it). read() returns
// false too until the first publish(). A reader that is slow costs the writer
// nothing; one that is busy retries a few times in microseconds.
//
// Cost. wrote(): a few operations a frame (min, max, a compare; a column
// closing, one in capacity / 512 frames, a few more). publish(): about 1100
// words copied, 60 times a second. The bench (bench.inc) measures both.
// None of them allocates, locks or calls libm.
#include "Laws.hpp"
#include "Ring.hpp"
#include "DoldrumsEngine.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace rka { namespace doldrums {

struct TapeSnapshot {
	enum { COLS = 512 };
	float low[COLS], high[COLS];      // oldest column first
	float anchor, readStart, readEnd; // fractions of the window, 0 = 30 s ago, 1 = now; may be < 0 (off the edge)
	bool frozen, held, stopped, following;
	char state[16];                   // "", "FROZEN", "HELD", "STOPPED", "FOLLOW", "FROZEN HELD", "FROZEN AT END", "FROZEN STOPPED"
};

struct TapeView {
	enum { COLS = TapeSnapshot::COLS };
	static constexpr int kTries = 8;   // read()'s attempts at a whole snapshot

	TapeView() { configure(0, 0); }
	TapeView(const TapeView&) = delete;
	TapeView& operator=(const TapeView&) = delete;

	// ---- Audio thread (or, off it, with it not running).

	// Columns for a tape of `capacity` frames whose newest frame is
	// `now` - 1 (a new ring: now 0), all blank. 0 capacity: nothing is kept.
	void configure(long long capacity, long long now) {
		cap_ = capacity < 0 ? 0 : capacity;
		now_ = now < 0 ? 0 : now;
		for (int i = 0; i < COLS; i++)
			lo_[i] = hi_[i] = 0.f;
		col_ = cap_ > 0 ? columnAt(now_) : 0;
		next_ = cap_ > 0 ? boundary(col_ + 1) : 0;
		open_ = false;
	}

	// The tape was cleared (Initialize; the Ring keeps its place): every
	// column blank, the places kept.
	void clear() { configure(cap_, now_); }

	// One frame went onto the tape, as the ring holds it.
	void wrote(float l, float r) {
		if (cap_ < 1)
			return;
		l = finite(l);
		r = finite(r);
		const float lo = l < r ? l : r, hi = l < r ? r : l;
		if (!open_) {
			curLo_ = lo;
			curHi_ = hi;
			open_ = true;
		}
		else {
			curLo_ = lo < curLo_ ? lo : curLo_;
			curHi_ = hi > curHi_ ? hi : curHi_;
		}
		now_++;
		while (now_ >= next_)   // more than one: a tape of under 512 frames has columns without any
			close();
	}

	// About 60 times a second, after the frame's wrote(). The state is the
	// engine's (DoldrumsEngine::state()).
	void publish(const DoldrumsState& s) {
		if (s.capacity != cap_ || s.now != now_)
			configure(s.capacity, s.now);
		TapeSnapshot& o = scratch_;
		std::memset(&o, 0, sizeof o);   // the padding too: the words are copied whole
		// The 511 columns that closed last, then the open one.
		for (int i = 0; i < COLS - 1; i++) {
			const int slot = int((col_ + 1 + i) & (COLS - 1));
			o.low[i] = lo_[slot];
			o.high[i] = hi_[slot];
		}
		o.low[COLS - 1] = open_ ? curLo_ : 0.f;
		o.high[COLS - 1] = open_ ? curHi_ : 0.f;
		o.anchor = fraction(s.anchor, s);
		o.readEnd = fraction(s.end, s);
		o.readStart = fraction(s.end - s.grain, s);
		o.frozen = s.frozen;
		o.held = s.held;
		o.stopped = s.stopped;
		o.following = s.following && !s.frozen && !s.stopped;
		word(s, o.state);

		uint32_t w[kWords];
		std::memcpy(w, &o, sizeof o);
		const uint32_t q = seq_.load(std::memory_order_relaxed);
		seq_.exchange(q | 1u, std::memory_order_acq_rel);   // odd: a write is under way
		for (int i = 0; i < kWords; i++)
			words_[i].store(w[i], std::memory_order_release);
		seq_.store((q | 1u) + 1u, std::memory_order_release);
	}

	// ---- A worker, with the audio thread not calling the above.

	// The columns of the tape as it stands: scans the 511 columns before the
	// newest and the open one (at most the tape's `capacity` frames).
	void rebuild(const Ring& ring) {
		configure(ring.capacity(), ring.now());
		if (cap_ < 1)
			return;
		const long long end = now_;
		const long long firstCol = col_ >= COLS - 1 ? col_ - (COLS - 1) : 0;
		now_ = boundary(firstCol);
		col_ = columnAt(now_);   // not firstCol itself if it has no frames: its frame is in the next
		next_ = boundary(col_ + 1);
		const float* ch0 = ring.channel(0);
		const float* ch1 = ring.channel(1);
		long long p = ring.physical(now_);
		while (now_ < end) {
			wrote(ch0[p], ch1[p]);
			if (++p == cap_)
				p = 0;
		}
	}

	// ---- The UI thread.

	// A whole snapshot, or false (nothing published yet, or the writer kept
	// at it through kTries attempts); `out` is untouched on false.
	bool read(TapeSnapshot& out) const {
		for (int attempt = 0; attempt < kTries; attempt++) {
			const uint32_t a = seq_.load(std::memory_order_acquire);
			if (a == 0)
				return false;
			if (a & 1u)
				continue;
			uint32_t w[kWords];
			for (int i = 0; i < kWords; i++)
				w[i] = words_[i].load(std::memory_order_acquire);
			if (seq_.load(std::memory_order_relaxed) == a) {
				std::memcpy(&out, w, sizeof out);
				return true;
			}
		}
		return false;
	}

private:
	// test_view.cpp's access to the counter, to put the view in the state a
	// reader meets in the middle of a publish (the counter odd). It exists for
	// that test; the audio path does not use it.
	friend struct TapeViewTestAccess;

	static_assert(sizeof(TapeSnapshot) % 4 == 0, "the snapshot is copied in words");
	static_assert(std::is_trivially_copyable<TapeSnapshot>::value, "the snapshot is copied as bytes");
	enum { kWords = int(sizeof(TapeSnapshot) / 4) };

	// First frame of column j (j >= 0): floor(j * capacity / 512), without a
	// product that grows with j.
	long long boundary(long long j) const { return (j >> 9) * cap_ + (((j & (COLS - 1)) * cap_) >> 9); }
	// The column that frame f (>= 0) is in: the last j with boundary(j) <= f.
	long long columnAt(long long f) const {
		long long j = (f / cap_) * COLS + (((f % cap_) * COLS) / cap_);
		while (boundary(j + 1) <= f)
			j++;
		while (j > 0 && boundary(j) > f)
			j--;
		return j;
	}
	// The open column is complete: file it, begin the next.
	void close() {
		const int slot = int(col_ & (COLS - 1));
		lo_[slot] = open_ ? curLo_ : 0.f;
		hi_[slot] = open_ ? curHi_ : 0.f;
		open_ = false;
		col_++;
		next_ = boundary(col_ + 1);
	}

	// NaN and infinity read as 0 (a test of the exponent bits, no libm).
	static float finite(float v) {
		uint32_t b;
		std::memcpy(&b, &v, sizeof b);
		return (b & 0x7f800000u) == 0x7f800000u ? 0.f : v;
	}
	// An absolute frame as a fraction of the window, held to [-1, 1].
	static float fraction(long long a, const DoldrumsState& s) {
		if (s.capacity < 1)
			return 0.f;
		const double f = double(a - (s.now - s.capacity)) / double(s.capacity);
		return float(f < -1.0 ? -1.0 : (f > 1.0 ? 1.0 : f));
	}
	static void word(const DoldrumsState& s, char* out) {
		const char* w = "";
		if (s.stopped)
			w = s.frozen ? "FROZEN STOPPED" : "STOPPED";
		else if (s.atEnd)
			w = "FROZEN AT END";
		else if (s.held)
			w = s.frozen ? "FROZEN HELD" : "HELD";
		else if (s.following && !s.frozen)
			w = "FOLLOW";
		else if (s.frozen)
			w = "FROZEN";
		std::strncpy(out, w, 15);   // out is zeroed and 16 long
	}

	// The audio thread's.
	float lo_[COLS], hi_[COLS];   // closed columns, by absolute column number mod 512
	float curLo_ = 0.f, curHi_ = 0.f;
	bool open_ = false;           // the open column has a frame
	long long cap_ = 0;
	long long now_ = 0;           // frames fed, as the ring counts them
	long long col_ = 0;           // the open column's absolute number
	long long next_ = 0;          // the frame that ends it
	TapeSnapshot scratch_;

	// The hand-off.
	std::atomic<uint32_t> seq_{0};
	std::atomic<uint32_t> words_[kWords];
};

// The drag: a fraction of the window (0 = 30 s ago, 1 = now) to a POSITION.
// The anchor is now - lagFrames(position, capacity, grain) (Laws.hpp), whose
// range is not the whole window: Position 1 puts it at now (fraction 1),
// Position 0 a grain short of the oldest frame, at fraction grain / capacity.
// With the capacity and the grain this is the exact inverse over that range,
// and a fraction left of Position 0's place is Position 0; one right of 1 is 1.
inline float positionForX(float fraction, long long capacity, int grain) {
	if (!(fraction == fraction))
		return 1.f;   // NaN: the default, now
	const long long room = capacity - grain;
	if (room <= 0)
		return 1.f;   // a grain bigger than the tape leaves no lag
	const double p = 1.0 - (1.0 - double(fraction)) * double(capacity) / double(room);
	return float(p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p));
}
// Without them, as for a grain that is small against the tape (the grain's
// share of the window is under 2 %): x = 1 is 1, x = 0 is 0.
inline float positionForX(float fraction) {
	return positionForX(fraction, 1LL << 40, 0);
}

}} // namespace rka::doldrums
