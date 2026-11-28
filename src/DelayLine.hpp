#pragma once

// Rack-free circular delay line with cubic (Hermite) fractional reads, and
// an allpass fractional read for a line inside a feedback loop.
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

	// Join the newest `n` samples in place to the audio `period` samples
	// older, as a frozen loop of that length joins its end to its start:
	// from untouched (n writes ago) to all the older audio (the latest). The
	// line then runs on, without a step, into what followed that older
	// audio, readInt(period - 1) at the next write: for a writer that
	// stopped and is about to start again elsewhere in the sound, and
	// crossfades from that continuation into what it writes.
	void joinNewest(int n, int period) {
		if (buf.empty() || n < 1 || period < 1)
			return;
		if (period > maxDel - 1)
			period = maxDel - 1;
		if (n > maxDel - period)
			n = maxDel - period;
		for (int k = 0; k < n; k++) {
			float& x = buf[(pos - (uint32_t) k) & mask];
			const float older = buf[(pos - (uint32_t) (k + period)) & mask];
			x += (float) (n - k) / (float) n * (older - x);
		}
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

	// Allpass read at a fractional delay: a first-order (Thiran) allpass
	// makes the fraction, y = eta (x[i] - y[-1]) + x[i + 1], eta = (1 - f) /
	// (1 + f), with f in [0.5, 1.5) so eta stays in (-0.2, 1/3]. Its gain is
	// exactly 1 at every frequency, where the Hermite read's falls towards
	// Nyquist (-3.3 dB at a third of the rate, halfway between samples): for
	// a line in a feedback loop, where that loss is taken on every pass and
	// shortens the high decay. `state` is the caller's, one per reader: the
	// last value returned (0 to start).
	//
	// Only for a delay that is fixed or moves slowly, a few hundredths of a
	// sample per sample: the state stands for the signal one sample back at
	// the last delay, so a read that jumps or sweeps fast is wrong until it
	// settles (a few samples). For those, use read(), and hand its result to
	// `state` so a later readAllpass() carries on from it.
	// Clamped to [1, maxDelay - 2] as read(). NaN reads as 1.
	float readAllpass(float delaySamples, float& state) const {
		if (buf.empty())
			return 0.f;
		const float hi = (float) (maxDel - 2);
		float d = delaySamples >= 1.f ? delaySamples : 1.f;
		d = d <= hi ? d : hi;
		const int i = (int) (d - 0.5f);
		const float f = d - (float) i;
		const float eta = (1.f - f) / (1.f + f);
		const uint32_t p = pos - (uint32_t) i;
		float y = eta * (buf[p & mask] - state) + buf[(p - 1) & mask];
		y = (y > 1e-20f || y < -1e-20f) ? y : 0.f; // denormals and NaN to 0
		state = y;
		return y;
	}
};

} // namespace rka
