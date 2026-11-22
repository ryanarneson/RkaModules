#pragma once
// A stereo tape that stands still while heads move over it (Eddy; later
// Spindrift). Rack-free, C++11, header-only.
//
// Tape: two channels of float frames, `length` frames of tape followed by
// `tail` frames of audio that ran on after it (a recording's tail, read by
// the join). Reads are Hermite (the maths of src/DelayLine.hpp), at any
// fractional frame, so a head may move at any speed in either direction.
// A head faster than 1 aliases through these, and between frames they
// lose the top octave; Eddy reads through src/BandLimitedRead.hpp instead.
//
// TapeWriter: the write head, which travels with a read head at whatever
// speed it moves and lays the input down at that speed (see its comment).
//
// Threads: allocate() and release() take and free memory and are never
// called on the audio thread; everything else is for the thread that owns
// the tape (the audio thread), allocates nothing and calls no libm.
#include <cstddef>
#include <cstdlib>

#include <cmath>

namespace rka {

struct Tape {
	// The allocation holds GUARD frames of silence before frame 0 and after
	// the tail's capacity. Tape's own reads clamp each interpolator tap to the
	// tape and its tail and never touch them; they are there so code reading
	// ch[] with its own short kernel may step up to GUARD frames out and read
	// zeros rather than another allocation.
	static constexpr int GUARD = 4;

	Tape() {}
	~Tape() {
		release();
	}
	Tape(const Tape&) = delete;
	Tape& operator=(const Tape&) = delete;

	// Room for capacityFrames of tape and tailFrames of tail, zeroed, and
	// empty (length and tail 0). calloc, so untouched pages cost nothing.
	// False, with nothing allocated, if the memory is not there. Never on the
	// audio thread.
	bool allocate(size_t capacityFrames, size_t tailFrames) {
		release();
		const size_t most = (size_t) 1 << 34;   // far over 2^24 + a tail; keeps the sum below from overflowing
		if (capacityFrames > most || tailFrames > most)
			return false;
		const size_t n = capacityFrames + tailFrames + 2 * (size_t) GUARD;
		float* a = (float*) std::calloc(n, sizeof(float));
		float* b = (float*) std::calloc(n, sizeof(float));
		if (!a || !b) {
			std::free(a);
			std::free(b);
			return false;
		}
		base_[0] = a;
		base_[1] = b;
		ch[0] = a + GUARD;
		ch[1] = b + GUARD;
		capacity_ = capacityFrames;
		tailCapacity_ = tailFrames;
		return true;
	}

	// Frees the memory; the tape is then unallocated and empty. Never on the
	// audio thread.
	void release() {
		std::free(base_[0]);
		std::free(base_[1]);
		base_[0] = base_[1] = nullptr;
		ch[0] = ch[1] = nullptr;
		capacity_ = tailCapacity_ = 0;
		length = tail = 0;
	}

	size_t capacity() const {   // frames, without the tail
		return capacity_;
	}
	size_t tailCapacity() const {
		return tailCapacity_;
	}

	size_t length = 0;   // frames in use
	size_t tail = 0;     // frames of audio after `length`
	float* ch[2] = {nullptr, nullptr};

	// Hermite read of side c at frame p, 0 <= p < length + tail. Clamped:
	// p outside (or NaN, read as 0) reads the nearest end, and each of the
	// four taps is held to the tape and its tail, so a read never touches a
	// frame outside them. A length or tail larger than the allocation is held
	// to the allocation. At a whole frame it is that frame, to the bit.
	float read(int c, double p) const {
		const long long n = (long long) extent(length + tail);
		if (n <= 0 || !ch[0])
			return 0.f;
		const double last = (double) (n - 1);
		double q = p > 0.0 ? p : 0.0;   // NaN fails the test: 0
		q = q < last ? q : last;
		const long long i = (long long) q;
		const float f = (float) (q - (double) i);
		const float* x = ch[c ? 1 : 0];
		const long long im1 = i > 0 ? i - 1 : 0;
		const long long i1 = i + 1 < n ? i + 1 : n - 1;
		const long long i2 = i + 2 < n ? i + 2 : n - 1;
		return hermite(x[im1], x[i], x[i1], x[i2], f);
	}

	// As read, but p wraps round [0, length): for a window that passes the
	// end. The taps wrap too, so the read across the seam joins the end to
	// the beginning. The tail is not read. NaN and infinite p read frame 0.
	float readWrapped(int c, double p) const {
		const long long n = (long long) extent(length);
		if (n <= 0 || !ch[0])
			return 0.f;
		const double len = (double) n;
		double q = p - std::floor(p / len) * len;
		if (!(q >= 0.0 && q < len))   // NaN, infinity, or rounding up to len
			q = 0.0;
		const long long i = (long long) q;
		const float f = (float) (q - (double) i);
		const float* x = ch[c ? 1 : 0];
		const long long im1 = i > 0 ? i - 1 : n - 1;
		long long i1 = i + 1, i2 = i + 2;
		if (i1 >= n)
			i1 -= n;
		if (i2 >= n)
			i2 -= n;
		if (i2 >= n)   // a tape of one frame
			i2 -= n;
		return hermite(x[im1], x[i], x[i1], x[i2], f);
	}

	// The frames a head may touch: n, held to the allocation.
	size_t extent(size_t n) const {
		const size_t have = capacity_ + tailCapacity_;
		return n < have ? n : have;
	}

private:
	static float hermite(float xm1, float x0, float x1, float x2, float f) {
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * f + c2) * f + c1) * f + x0;
	}

	float* base_[2] = {nullptr, nullptr};
	size_t capacity_ = 0;
	size_t tailCapacity_ = 0;
};

// The write head. It remembers the last input, so the input can be placed
// between samples.
//
// Design. The head moved from p0 to p1 this sample while the input went from
// the last sample's value to this one. Which frames are written: those the
// head has passed, k in [p0, p1) forwards and [p1, p0) backwards, so a run
// split anywhere (a window's wrap, one call per sample) writes each frame
// once, and a stop writes nothing. What is written depends on the speed:
//
// - At 1 or faster each frame gets the input as it was when the head was at
//   that frame, interpolated linearly between the last input and this one.
//   At speed 1 from a whole frame that is the input itself, to the bit.
// - Slower than 1 a frame spans more than one sample of input, and each
//   frame gets the average of the input over the head's travel across it
//   (half a frame either side), weighted by how far the head moved during
//   each sample: a box filter in tape position, so input between frames is
//   averaged, not dropped, and the average is centred on the frame, so
//   nothing lags. To centre it the write of frame k waits until the head
//   is half a frame past the frame centre, less half this sample's move;
//   at speed 1 that is the moment it passes k, so the two ways meet there.
//   This is monome softcut's idea of a record head that travels with the
//   play head, with the decimation filtered rather than point-sampled.
//
// Internally a point w rides ahead of the head by half a sample's move (at
// most half a frame) and frame k is written when w crosses k + 1/2 (k - 1/2
// going backwards). w never goes back while the direction holds, so a
// slowing or stopping head re-writes nothing. A reversal starts afresh from
// the head. A call whose p0 is not the last call's p1 (the caller wrapped
// a window, or jumped) carries the head's state over by the jump. The frame
// in progress keeps its place if the head had already passed it, and moves
// with the jump if not: so a window wrapped either way writes each of its
// frames once a turn and nothing outside it (forwards, the frame at the
// window's end is its start; backwards, the start is the frame passed).
//
// Frames written are held to [0, length + tail): the writer may write the
// tail when the caller moves the head there (the engine records its tail by
// plain array writes, not through here). Others are skipped and `mix` is
// not called for them. Audio thread only; no allocation, no libm.
struct TapeWriter {
	void reset() {
		valid_ = false;
		dir_ = 0;
		w_ = last_ = 0.0;
		prev_[0] = prev_[1] = 0.f;
		acc_[0] = acc_[1] = 0.0;
		weight_ = 0.0;
		keep_ = false;
	}

	// The head went from p0 to p1 this sample (|p1 - p0| <= 4; the caller splits
	// a move that wraps). For each whole frame passed, in the order passed:
	//   tape.ch[c][frame] = mix(c, tape.ch[c][frame], input as it was at that moment)
	// Slower than 1, the input between two frames is averaged, not dropped.
	// A NaN or infinite input is taken as 0. A non-finite position, one
	// beyond 2^40, or a move over 4 (with a little room for rounding) writes
	// nothing and starts the head afresh at the next call.
	template <class Mix>
	void write(Tape& tape, double p0, double p1, const float in[2], Mix&& mix) {
		float x[2];
		for (int c = 0; c < 2; c++) {
			const float v = in[c];
			x[c] = v > -1e30f && v < 1e30f ? v : 0.f;   // NaN fails both
		}
		const double r = p1 - p0;
		const double FAR = 1099511627776.0;   // 2^40
		if (!(p0 > -FAR && p0 < FAR && p1 > -FAR && p1 < FAR && r >= -MAX_MOVE && r <= MAX_MOVE)) {
			valid_ = false;
			prev_[0] = x[0];
			prev_[1] = x[1];
			return;
		}
		if (!valid_) {
			valid_ = true;
			dir_ = 0;
			w_ = p0;
			last_ = p0;
			acc_[0] = acc_[1] = 0.0;
			weight_ = 0.0;
			keep_ = false;
		}
		if (p0 != last_ && dir_ != 0) {
			// A wrap or a jump: the head's state moves with it. The frame in
			// progress stays where it is if the head had passed it.
			const long long k = frameAt(nextBoundary(w_, dir_), dir_);
			if (dir_ > 0 ? (double) k < last_ : (double) k >= last_) {
				keep_ = true;
				keepFrame_ = k;
			}
			w_ += p0 - last_;
		}
		last_ = p1;

		const int d = r > 0.0 ? 1 : r < 0.0 ? -1 : 0;
		const double speed = r < 0.0 ? -r : r;
		const double lead = (speed < 1.0 ? speed : 1.0) * 0.5;
		if (d != 0 && d != dir_) {
			// A reversal (or the first move): start from the head.
			dir_ = d;
			w_ = p0 + d * lead;
			acc_[0] = acc_[1] = 0.0;
			weight_ = 0.0;
			keep_ = false;
		}
		if (dir_ == 0) {   // has never moved
			prev_[0] = x[0];
			prev_[1] = x[1];
			return;
		}

		const double w0 = w_;
		double w1 = p1 + dir_ * lead;
		if (dir_ > 0 ? w1 < w0 : w1 > w0)
			w1 = w0;   // slowing or stopped: w waits
		w_ = w1;

		const long long n = (long long) tape.extent(tape.length + tape.tail);
		const bool canWrite = tape.ch[0] && n > 0;
		const bool fast = speed >= 1.0;
		double at = w0;
		// The frame boundaries (half-frames) w crosses, in order. At most
		// 4 + 1/2 frames of travel: the cap only guards the arithmetic.
		double b = nextBoundary(w0, dir_);
		for (int guard = 0; guard < 8; guard++) {
			if (dir_ > 0 ? !(b < w1) : !(b >= w1))
				break;
			const long long k = frameAt(b, dir_);
			const double seg = dir_ > 0 ? b - at : at - b;
			float v[2];
			// Fast: the input when the head was at k, between the last input and this.
			double u = (double) k - p0;
			u = r != 0.0 ? u / r : 1.0;
			u = u > 0.0 ? (u < 1.0 ? u : 1.0) : 0.0;
			const float uf = (float) u;
			for (int c = 0; c < 2; c++) {
				// The ends exactly, whatever the compiler makes of the blend.
				const float point = uf <= 0.f ? prev_[c] : uf >= 1.f ? x[c] : (1.f - uf) * prev_[c] + uf * x[c];
				const double sum = acc_[c] + x[c] * seg;
				const double wt = weight_ + seg;
				v[c] = fast || !(wt > 0.0) ? point : (float) (sum / wt);
			}
			const long long at_k = keep_ ? keepFrame_ : k;
			keep_ = false;
			if (canWrite && at_k >= 0 && at_k < n)
				for (int c = 0; c < 2; c++)
					tape.ch[c][at_k] = mix(c, tape.ch[c][at_k], v[c]);
			acc_[0] = acc_[1] = 0.0;
			weight_ = 0.0;
			at = b;
			b += dir_;
		}
		const double seg = dir_ > 0 ? w1 - at : at - w1;
		for (int c = 0; c < 2; c++)
			acc_[c] += x[c] * seg;
		weight_ += seg;
		prev_[0] = x[0];
		prev_[1] = x[1];
	}

private:
	static constexpr double MAX_MOVE = 4.5;

	// The first frame boundary w meets moving in direction d: forwards the
	// half-frame at or above w, backwards the one below it (w on a boundary
	// going down has already passed it).
	static double nextBoundary(double w, int d) {
		double b = d > 0 ? std::floor(w - 0.5) + 1.5 : std::floor(w + 0.5) - 0.5;
		if (d > 0 && b - 1.0 >= w)
			b -= 1.0;
		if (d < 0 && b >= w)
			b -= 1.0;
		return b;
	}
	// The frame a boundary closes: b is k + 1/2 forwards, k - 1/2 backwards.
	static long long frameAt(double b, int d) {
		return (long long) (b - 0.5 * d);
	}

	bool valid_ = false;
	bool keep_ = false;           // the next frame written goes to keepFrame_
	long long keepFrame_ = 0;
	int dir_ = 0;           // +1, -1, or 0 before the first move
	double w_ = 0.0;        // the point that rides ahead of the head
	double last_ = 0.0;     // the last call's p1
	float prev_[2] = {0.f, 0.f};
	double acc_[2] = {0.0, 0.0};   // the frame in progress: input x travel
	double weight_ = 0.0;          // and the travel
};

} // namespace rka
