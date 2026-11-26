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
// The fade. While recording, a gain climbs one step a frame (1 / fade frames)
// to 1; frozen, it falls the same way and the faded frames are still written,
// so the newest frame at a freeze is 0 and the first at a thaw is one step
// above 0. Once the gain is 0 nothing is written and now() stands still.
// Frozen and thawed alike, the gain is a whole number of steps, so two
// frames of a DC input never differ by more than one step of it.
//
// The seam. When the fade-out ends the oldest `fade` frames, those next to
// the write point, are ramped up from 0 by the same steps. The fade-in at the
// thaw then writes over them step for step, so at the freeze and all through
// the thaw's fade the newest frame and the oldest one beside it differ by no
// more than one step of a DC input. What the ring cannot promise: a freeze
// cut short by a thaw before its fade ended leaves the oldest audio as it
// was (the seam is then as arbitrary as in any ring that is recording).
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
		if (frozen && count_ == 0)
			rampOldest();
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
	// The frames next to the write point, the oldest, ramped from 0 in the
	// steps the fade-in will write over them.
	void rampOldest() {
		const long long n = std::min(fade_, cap_);
		for (long long j = 0; j < n; j++) {
			const float g = float(j) / float(fade_);
			const size_t i = size_t(GUARD + (now_ + j) % cap_);
			store_[0][i] *= g;
			store_[1][i] *= g;
		}
	}

	std::vector<float> store_[2];
	long long cap_ = 0;
	long long now_ = 0;
	long long fade_ = 1;
	long long count_ = 1; // the gain is count_ / fade_
};

}} // namespace rka::doldrums
