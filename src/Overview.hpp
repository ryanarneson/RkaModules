#pragma once
// The overview of a tape (Eddy): COLS columns of the tape's peaks, both sides
// together, which the module's window draws. Rack-free, C++11, header-only.
//
// Column c covers frames [c * frames / COLS, (c + 1) * frames / COLS) of a
// tape `frames` long; its lo is the least and its hi the greatest sample in
// those frames over both channels. Frames at or beyond tape.length count as
// silence (0). A column with no frames (a tape shorter than COLS) is 0, 0.
//
// The mapping length. `frames` is whatever was last given: build() sets it to
// tape.length and touch() sets it to its argument. While recording the engine
// passes the tape's capacity, so the waveform grows from the left against the
// limit. If touch() is given a different length from the last, every column
// has changed its meaning and all are marked for a rescan.
//
// Threads. touch() and service() are for the audio thread alone; build() and
// clear() run off it, on an overview the audio thread is not using. lo and hi
// are atomics because the UI thread reads them at any time (relaxed: a
// column's peaks are independent of one another). The bookkeeping (which
// columns are touched, where the rescan has got to) is plain data that the
// audio thread alone uses. Nothing here allocates, locks or calls libm.
//
// Rescans. service() reads at most `budget` frames a call, from the touched
// columns in turn. A column is rescanned as a true min and max over its
// frames (not a running max, which could never fall), into a scratch pair that
// is published only when the column is complete, so the window never shows a
// half-scanned column. A column touched again, in frames already scanned,
// while it is being rescanned starts over.
#include <atomic>
#include <cfloat>
#include <cstddef>

#include "Tape.hpp"

namespace rka {

struct Overview {
	static constexpr int COLS = 512;

	std::atomic<float> lo[COLS], hi[COLS];

	Overview() {
		clear();
	}

	// Every column 0, nothing touched, mapping length 0. Off the audio thread.
	void clear() {
		for (int c = 0; c < COLS; c++) {
			lo[c].store(0.f, std::memory_order_relaxed);
			hi[c].store(0.f, std::memory_order_relaxed);
			dirty_[c] = false;
		}
		dirtyCount_ = 0;
		cursor_ = 0;
		cur_ = -1;
		frames_ = 0;
	}

	// The whole tape, mapped over tape.length. Off the audio thread.
	void build(const Tape& tape) {
		clear();
		frames_ = tape.length;
		for (int c = 0; c < COLS; c++) {
			float l, h;
			scan(tape, start(c, frames_), start(c + 1, frames_), l, h);
			if (l > h)   // nothing finite in it
				l = h = 0.f;
			lo[c].store(l, std::memory_order_relaxed);
			hi[c].store(h, std::memory_order_relaxed);
		}
	}

	// Audio thread. Frames [a, b) changed, on a tape `frames` long: marks the
	// columns they fall in. (Frames past `frames` are held to it.)
	void touch(size_t a, size_t b, size_t frames) {
		if (frames != frames_) {
			frames_ = frames;
			for (int c = 0; c < COLS; c++)
				mark(c);
			cur_ = -1;   // the column in hand was cut to the old length
			return;
		}
		if (b > frames)
			b = frames;
		if (a >= b)
			return;
		const unsigned long long F = frames, N = COLS;
		long long c0 = (long long) (((a + 1) * N + F - 1) / F) - 1;
		long long c1 = (long long) ((b * N + F - 1) / F) - 1;
		if (c0 < 0)
			c0 = 0;
		if (c1 > COLS - 1)
			c1 = COLS - 1;
		for (long long c = c0; c <= c1; c++) {
			if (c == cur_) {
				// Restart only if frames already scanned changed; frames
				// still to come will be read anyway.
				if (a < pos_ && b > start((int) c, frames_)) {
					pos_ = start((int) c, frames_);
					slo_ = FLT_MAX;
					shi_ = -FLT_MAX;
				}
			}
			else
				mark((int) c);
		}
	}

	// Audio thread, every sample: rescans at most `budget` frames of touched
	// columns (the rest wait for the next call).
	void service(const Tape& tape, int budget) {
		while (budget > 0 || (cur_ >= 0 && pos_ >= end_)) {
			if (cur_ < 0) {
				if (!next())
					return;
				continue;
			}
			if (pos_ < end_) {
				size_t n = end_ - pos_;
				if (n > (size_t) budget)
					n = (size_t) budget;
				float l, h;
				scan(tape, pos_, pos_ + n, l, h);
				if (l < slo_)
					slo_ = l;
				if (h > shi_)
					shi_ = h;
				pos_ += n;
				budget -= (int) n;
			}
			if (pos_ >= end_) {
				lo[cur_].store(slo_ <= shi_ ? slo_ : 0.f, std::memory_order_relaxed);
				hi[cur_].store(slo_ <= shi_ ? shi_ : 0.f, std::memory_order_relaxed);
				cur_ = -1;
			}
		}
	}

	// Nothing touched and unscanned. Audio thread.
	bool settled() const {
		return dirtyCount_ == 0 && cur_ < 0;
	}

private:
	static size_t start(int c, size_t frames) {
		return (size_t) ((unsigned long long) c * frames / COLS);
	}

	void mark(int c) {
		if (!dirty_[c]) {
			dirty_[c] = true;
			dirtyCount_++;
		}
	}

	// Take the next touched column (from the cursor on, round) as the one in
	// hand. False if there is none.
	bool next() {
		if (dirtyCount_ == 0)
			return false;
		for (int i = 0; i < COLS; i++) {
			const int c = (cursor_ + i) % COLS;
			if (dirty_[c]) {
				dirty_[c] = false;
				dirtyCount_--;
				cursor_ = (c + 1) % COLS;
				cur_ = c;
				pos_ = start(c, frames_);
				end_ = start(c + 1, frames_);
				slo_ = FLT_MAX;
				shi_ = -FLT_MAX;
				return true;
			}
		}
		return false;
	}

	// Min and max of both sides over frames [f0, f1); frames at or beyond the
	// tape's length are 0. With no frames at all, 0 and 0 (an empty range). A
	// column whose frames hold nothing finite gives l > h (the caller's
	// scratch keeps FLT_MAX / -FLT_MAX); NaN samples are skipped.
	static void scan(const Tape& tape, size_t f0, size_t f1, float& l, float& h) {
		l = FLT_MAX;
		h = -FLT_MAX;
		const size_t len = tape.ch[0] ? tape.extent(tape.length) : 0;
		const size_t real = f1 < len ? f1 : len;
		for (int s = 0; s < 2; s++) {
			const float* x = tape.ch[s];
			for (size_t f = f0; f < real; f++) {
				const float v = x[f];
				if (v < l)
					l = v;
				if (v > h)
					h = v;
			}
		}
		if (f1 > real && f1 > f0) {   // silence past the tape's length
			if (0.f < l)
				l = 0.f;
			if (0.f > h)
				h = 0.f;
		}
		if (f1 <= f0) {
			l = h = 0.f;
		}
	}

	bool dirty_[COLS];
	int dirtyCount_ = 0;
	int cursor_ = 0;
	int cur_ = -1;        // the column being rescanned, or -1
	size_t pos_ = 0, end_ = 0;
	float slo_ = FLT_MAX, shi_ = -FLT_MAX;
	size_t frames_ = 0;   // the mapping length
};

} // namespace rka
