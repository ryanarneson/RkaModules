#pragma once

// Rack-free circular delay line with cubic (Hermite) fractional reads.
// Shared by modules that need a delay buffer. The buffer is a power of two
// so indexing is a mask. Delay 0 is the most recently written sample.
// Only allocate() allocates; write(), read() and readInt() never do.
#include <cstdint>
#include <vector>

namespace rka {

struct DelayLine {
	std::vector<float> buf;
	uint32_t mask = 0;
	uint32_t pos = 0;
	int maxDel = 0;
	uint32_t start = 0; // where pos starts and returns to on clear()

	// Room for delays up to maxDelaySamples (plus the interpolator's guard
	// samples), rounded up to a power of two, zeroed. Never call from process().
	//
	// stagger offsets the write position's start by stagger * 67 cache lines
	// (16 floats each), masked. Many large power-of-two buffers are page
	// aligned, and lines that all advance by one per sample from pos 0 write
	// to the same L1 cache set and evict each other. Give each line of a
	// module a different stagger index so their sets differ (67 is odd, so
	// consecutive indices spread over 64 or 256 sets). A ring of zeros read
	// relative to pos behaves the same from any start: output is unchanged.
	void allocate(int maxDelaySamples, uint32_t stagger = 0) {
		if (maxDelaySamples < 1)
			maxDelaySamples = 1;
		uint32_t n = 8;
		while (n < (uint32_t) maxDelaySamples + 4u)
			n <<= 1;
		buf.assign(n, 0.f);
		mask = n - 1;
		start = (stagger * 67u * 16u) & mask;
		pos = start;
		maxDel = (int) n - 1;
	}

	void clear() {
		for (size_t i = 0; i < buf.size(); i++)
			buf[i] = 0.f;
		pos = start;
	}

	int maxDelay() const {
		return maxDel;
	}

	void write(float x) {
		if (buf.empty())
			return;
		pos = (pos + 1) & mask;
		buf[pos] = x;
	}

	// Fade the newest `n` samples in place, from untouched (n writes ago)
	// down to silence (the latest): for a writer that stopped and is about
	// to start again somewhere else in the sound, so the join is not a step.
	void fadeOutNewest(int n) {
		if (buf.empty() || n < 1)
			return;
		if (n > maxDel)
			n = maxDel;
		for (int k = 0; k < n; k++)
			buf[(pos - (uint32_t) k) & mask] *= (float) k / (float) n;
	}

	// The sample written delaySamples writes ago (0 = the latest).
	float readInt(int delaySamples) const {
		if (buf.empty())
			return 0.f;
		return buf[(pos - (uint32_t) delaySamples) & mask];
	}

	// Hermite read at a fractional delay, clamped to [1, maxDelay - 2] so the
	// four taps (i - 1 .. i + 2) stay inside the buffer. NaN reads as 1.
	float read(float delaySamples) const {
		if (buf.empty())
			return 0.f;
		const float hi = (float) (maxDel - 2);
		float d = delaySamples >= 1.f ? delaySamples : 1.f;
		d = d <= hi ? d : hi;
		const int i = (int) d;
		const float f = d - (float) i;
		const uint32_t p = pos - (uint32_t) i;
		const float xm1 = buf[(p + 1) & mask]; // newer
		const float x0 = buf[p & mask];
		const float x1 = buf[(p - 1) & mask]; // older
		const float x2 = buf[(p - 2) & mask];
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * f + c2) * f + c1) * f + x0;
	}
};

} // namespace rka
