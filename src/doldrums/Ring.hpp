#pragma once
// Doldrums' tape: a stereo ring that holds the last `capacity` frames of the
// input, with the input faded out and in at Freeze's edges so that no step is
// written to it. Rack-free.
//
// Safe per sample: write and copy (no allocation, no lock, no libm).
// allocate and release are for off the audio thread.
//
// Frames have absolute indices that only grow: now() is one past the newest,
// and frame i lives at physical(i) in channel(). A reader asks for frames by
// absolute index and never meets the seam where the newest frame stands next
// to the oldest.
//
// The fade. One gain, a whole number of steps of 1 / fade frames, climbs a
// step a frame while recording and falls a step a frame while frozen. Frozen
// frames are still written until the gain reaches 0: write(frozen = true)
// writes the faded input, and now() advances, for up to `fade` frames after
// the freeze edge. Then nothing is written and now() stands still until the
// thaw, which writes on from the same place, one step above 0. So the newest
// frame at a freeze is 0, the first at a thaw is one step above 0, and no two
// neighbouring written frames of a DC input differ by more than one step of
// it, even when a thaw cuts a fade-out short or a freeze a fade-in (the gain
// just turns round). The frames already on the tape are never altered.
//
// The junction between the newest frame and the oldest one (the write point)
// is not smoothed: no reader crosses it.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rka { namespace doldrums {

struct Ring {
	// Frames of fence on each side of a channel, for a test to poison.
	static constexpr long long GUARD = 64;

	// False, and an empty ring, for frames < 1.
	bool allocate(long long frames) {
		release();
		if (frames < 1)
			return false;
		for (int c = 0; c < 2; c++)
			store_[c].assign(size_t(frames + 2 * GUARD), 0.f);
		cap_ = frames;
		now_ = 0;
		fade_ = std::min<long long>(fade_, cap_);
		count_ = fade_;
		return true;
	}
	void release() {
		for (int c = 0; c < 2; c++)
			std::vector<float>().swap(store_[c]);
		cap_ = 0;
		now_ = 0;
		count_ = fade_;
	}
	// Silence; the position and the fade state are kept.
	void clear() {
		for (int c = 0; c < 2; c++)
			std::fill(store_[c].begin(), store_[c].end(), 0.f);
	}
	long long capacity() const { return cap_; }
	// One past the newest frame; never falls. Frozen, it keeps advancing
	// until the fade-out (up to `fade` frames) has been written, then stands
	// still.
	long long now() const { return now_; }

	// The fade in frames, at least 1, at most the capacity. A fade in
	// progress keeps its place (the gain is count / fade); full gain stays full.
	void setFadeFrames(int n) {
		const bool full = count_ == fade_;
		fade_ = std::max<long long>(1, n);
		if (cap_ > 0)
			fade_ = std::min(fade_, cap_);
		count_ = full ? fade_ : std::min(count_, fade_);
	}

	// One frame. Not frozen: the input times the gain, which climbs a step.
	// Frozen: the same with the gain falling a step, until it is 0; from then
	// on nothing is written and now() stands still. NaN and inf read as 0.
	void write(float l, float r, bool frozen) {
		if (cap_ < 1)
			return;
		if (frozen) {
			if (count_ == 0)
				return;
			count_--;
		}
		else if (count_ < fade_)
			count_++;
		const float g = count_ == fade_ ? 1.f : float(count_) / float(fade_);
		const long long p = now_ % cap_;
		store_[0][size_t(GUARD + p)] = clean(l) * g;
		store_[1][size_t(GUARD + p)] = clean(r) * g;
		now_++;
	}

	// n frames ending at absolute index `end` (exclusive), end in
	// [now - capacity + n, now]. False, and zeros, if it is not.
	bool copy(long long end, int n, float* l, float* r) const {
		if (n < 1)
			return n == 0;
		const bool ok = cap_ > 0 && n <= cap_ && end <= now_ && end >= now_ - cap_ + n;
		if (!ok) {
			if (l) std::memset(l, 0, size_t(n) * sizeof(float));
			if (r) std::memset(r, 0, size_t(n) * sizeof(float));
			return false;
		}
		const long long first = physical(end - n);
		const long long head = std::min<long long>(n, cap_ - first);
		float* out[2] = {l, r};
		for (int c = 0; c < 2; c++) {
			if (!out[c])
				continue;
			const float* src = channel(c);
			std::memcpy(out[c], src + first, size_t(head) * sizeof(float));
			if (head < n)
				std::memcpy(out[c] + head, src, size_t(n - head) * sizeof(float));
		}
		return true;
	}

	// Frame `capacity` long, starting at physical 0. Null before allocate.
	const float* channel(int c) const {
		return store_[c & 1].empty() ? nullptr : store_[c & 1].data() + GUARD;
	}
	// Index into channel() of an absolute frame (any sign).
	long long physical(long long absolute) const {
		if (cap_ < 1)
			return 0;
		const long long m = absolute % cap_;
		return m < 0 ? m + cap_ : m;
	}

private:
	// NaN and infinity read as 0 (a test of the exponent bits, no libm).
	static float clean(float v) {
		uint32_t b;
		std::memcpy(&b, &v, sizeof b);
		return (b & 0x7f800000u) == 0x7f800000u ? 0.f : v;
	}

	std::vector<float> store_[2];
	long long cap_ = 0;
	long long now_ = 0;
	long long fade_ = 1;
	long long count_ = 1; // the gain is count_ / fade_
};

}} // namespace rka::doldrums
