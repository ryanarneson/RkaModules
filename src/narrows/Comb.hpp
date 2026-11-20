#pragma once
// Narrows' comb: a short delay fed back on itself, ahead of the filter.
// With positive feedback its peaks sit at 1/period and that frequency's
// harmonics; with negative feedback they sit halfway between. Rack-free;
// only allocate() allocates.
//
// y[n] = a x[n] + feedback y[n - period], where a falls as the feedback
// rises so a peak comes out at most 6 dB above the dry level
// (a = 2 (1 - |f|) / (2 - |f|), and the peak gain is a / (1 - |f|)).
// At feedback exactly 0 the input is returned untouched, and still
// written, so turning the comb in finds the line full.
#include "../DelayLine.hpp"
#include "NarrowsTuning.hpp"

#include <cmath>

namespace rka {
namespace narrows {

struct Comb {
	// Room for one period of CUTOFF_MIN_HZ at this rate. Not on the audio
	// thread. `stagger`: see DelayLine::allocate.
	void allocate(float fs, uint32_t stagger = 0) {
		if (!(fs > 0.f))
			fs = 48000.f;
		line.allocate((int) (fs / CUTOFF_MIN_HZ) + 8, stagger);
		written = 0;
		d = 0.f;
		top = (float) (line.maxDelay() - 3);
	}

	void setSampleRate(float fs) {
		if (!(fs > 0.f))
			return;
		coef = 1.f - std::exp(-1.f / (COMB_SLIDE_S * fs));
	}

	// Forget what is in the line, at no cost (the engine calls this on the
	// audio thread when a voice joins): nothing written before now is read
	// again.
	void reset() {
		written = 0;
		d = 0.f;
	}

	// The period in use, in samples (it slides to the one asked for).
	float period() const {
		return d;
	}

	float process(float x, float periodSamples, float feedback) {
		float want = periodSamples >= COMB_MIN_SAMPLES ? periodSamples : COMB_MIN_SAMPLES;   // NaN too
		want = want <= top ? want : top;
		// The first period after a reset is taken at once; after that a
		// jump is slid, so the comb bends and does not click.
		if (d < COMB_MIN_SAMPLES)
			d = want;
		else {
			d += coef * (want - d);
			if (std::fabs(want - d) < 1e-3f)
				d = want;
		}
		if (feedback == 0.f) {
			put(x);
			return x;
		}
		const float f = feedback < 0.f ? -feedback : feedback;
		const float a = 2.f * (1.f - f) / (2.f - f);
		// Read before this sample is written: delay d - 1 is d samples back.
		float y = a * x + feedback * echo(d - 1.f);
		// Nothing subnormal goes round, and a NaN does not stay.
		if (!(std::fabs(y) >= 1e-18f))
			y = 0.f;
		put(y);
		return y;
	}

private:
	void put(float x) {
		line.write(x);
		if (written <= line.maxDelay())
			written++;
	}

	// line.read(), except that samples from before the last reset read as
	// silence. Once the line has been written right round, it is
	// line.read().
	float echo(float delay) const {
		if (written > line.maxDelay())
			return line.read(delay);
		const float hi = (float) (line.maxDelay() - 2);
		float dl = delay >= 1.f ? delay : 1.f;
		dl = dl <= hi ? dl : hi;
		const int i = (int) dl;
		const float f = dl - (float) i;
		const float xm1 = tap(i - 1), x0 = tap(i), x1 = tap(i + 1), x2 = tap(i + 2);
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * f + c2) * f + c1) * f + x0;
	}
	float tap(int delay) const {
		return delay < written ? line.readInt(delay) : 0.f;
	}

	DelayLine line;
	// Samples written since the last reset (counted up to the line's
	// length).
	int written = 0;
	float d = 0.f;
	float top = COMB_MIN_SAMPLES;
	float coef = 1.f;
};

} // namespace narrows
} // namespace rka
