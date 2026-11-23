#pragma once
// Spindrift's rate conversion at its edges: Rack's sample rate to Beads'
// codec rate (24, 32 or 48 kHz) on the way in, and back on the way out, for
// one stereo pair, so Beads' code never sees Rack's rate. Rack-free, C++11.
//
// Each direction is a polyphase windowed sinc (Kaiser), the kernel of
// src/Resample.hpp with its cutoff lowered below the lower Nyquist: pass to
// 0.41 of the lower rate, stop from 0.5 of it, 100 dB down (Kaiser beta
// 10.06, about 71 / lower-rate seconds long, so about 32 zero crossings a
// side). Where the two rates have a small ratio (every rate Rack offers
// against 24, 32 and 48 kHz) the table holds every phase the ratio visits,
// so no phase is interpolated; otherwise (an odd rate) it holds up to 1024
// phases and interpolates between two. Equal rates pass through untouched,
// so at 48 kHz in Digital the engine is Beads to the bit.
//
// Latency is fixed for a pair of rates (latency()): the kernels' half
// lengths plus the FIFO that absorbs Beads' blocks.
//
// Threads: setRackRate() allocates and runs off the audio thread; select(),
// and everything else, on the engine's thread without allocating.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spindrift {

namespace edgedetail {

inline double besselI0(double x) {
	double sum = 1., term = 1.;
	const double q = x * x * 0.25;
	for (int k = 1; k < 500; k++) {
		term *= q / ((double) k * k);
		sum += term;
		if (term < 1e-15 * sum)
			break;
	}
	return sum;
}

inline int64_t gcd(int64_t a, int64_t b) {
	while (b) {
		int64_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

}  // namespace edgedetail

// One direction, stereo: push input frames, pop output frames when due.
class Resampler {
public:
	// The design (functions, not constants, so C++11 needs no definitions).
	static double pass() { return 0.41; }          // of the lower rate
	static double stop() { return 0.5; }
	static double attenuation() { return 100.0; }  // dB
	static int maxPhases() { return 1024; }

	// Off the audio thread (allocates). Rates in Hz, > 0.
	void setup(int inRate, int outRate) {
		using namespace edgedetail;
		in_ = inRate;
		out_ = outRate;
		bypass_ = inRate == outRate;
		if (bypass_) {
			W_ = 0;
			P_ = 1;
			exact_ = true;
			table_.assign(1, 1.f);
			cap_ = 2;
		}
		else {
			const double lo = std::min(inRate, outRate);
			const double fcn = 0.5 * (pass() + stop()) * lo / inRate;    // cutoff, cycles per input sample
			const double dfn = (stop() - pass()) * lo / inRate;          // transition width, likewise
			const double D = (attenuation() - 7.95) / 14.36;             // Kaiser's length rule
			W_ = (int) std::ceil(D / dfn);
			W_ += W_ & 1;                                                // even: a whole-sample delay at phase 0
			const double beta = 0.1102 * (attenuation() - 8.7);
			const int64_t L = outRate / gcd(inRate, outRate);            // phases the ratio visits
			exact_ = L <= maxPhases();
			if (exact_)
				P_ = (int) L;
			else
				P_ = std::min(maxPhases(), std::max(16, (int) (2048. * fcn) + 1));
			const double i0b = besselI0(beta);
			const double pi = 3.14159265358979323846;
			table_.assign((size_t) (P_ + 1) * W_, 0.f);
			// Row p holds the kernel at phase p / P, reversed so the history
			// reads forward: tap j weighs x[n - W + j] at u = frac + (W-1-j) - W/2.
			for (int p = 0; p <= P_; p++) {
				const double frac = (double) p / P_;
				for (int j = 0; j < W_; j++) {
					const double u = frac + (W_ - 1 - j) - 0.5 * W_;
					const double t = 2. * u / W_;
					double w = t * t < 1. ? besselI0(beta * std::sqrt(1. - t * t)) / i0b : 0.;
					const double x = 2. * fcn * u;
					const double s = x == 0. ? 1. : std::sin(pi * x) / (pi * x);
					table_[(size_t) p * W_ + j] = (float) (2. * fcn * s * w);
				}
			}
			cap_ = W_ + 2;
		}
		histL_.assign(2 * (size_t) cap_, 0.f);
		histR_.assign(2 * (size_t) cap_, 0.f);
		reset();
	}

	// Silence the history; the next pushed frame is input 0, and the first
	// output falls on it.
	void reset() {
		std::fill(histL_.begin(), histL_.end(), 0.f);
		std::fill(histR_.begin(), histR_.end(), 0.f);
		w_ = 0;
		pos_ = out_;
		have_ = false;
	}

	void push(float l, float r) {
		if (bypass_) {
			lastL_ = l;
			lastR_ = r;
			have_ = true;
			return;
		}
		histL_[w_] = histL_[w_ + cap_] = l;
		histR_[w_] = histR_[w_ + cap_] = r;
		if (++w_ == cap_)
			w_ = 0;
		pos_ -= out_;
	}

	bool ready() const {
		return bypass_ ? have_ : pos_ <= 0;
	}

	// The next output frame; nearestR is the input sample nearest the
	// kernel's centre (Spindrift's gate output, which must not ring).
	void pop(float& l, float& r, float& nearestR) {
		if (bypass_) {
			l = lastL_;
			r = nearestR = lastR_;
			have_ = false;
			return;
		}
		// The output's time is n - 1 + phase / out_ input samples (n the
		// newest input), phase in (0, out_]; its taps are x[n - W .. n - 1].
		const int64_t phase = pos_ + out_;
		const int64_t num = phase * P_;
		const int p = (int) (num / out_);
		const int64_t rem = num % out_;
		const size_t start = (size_t) ((w_ + cap_ - 1 - W_) % cap_);
		const float* hl = &histL_[start];
		const float* hr = &histR_[start];
		const float* c0 = &table_[(size_t) p * W_];
		float al = 0.f, ar = 0.f;
		for (int j = 0; j < W_; j++) {
			al += hl[j] * c0[j];
			ar += hr[j] * c0[j];
		}
		if (rem != 0) {
			const float f = (float) ((double) rem / (double) out_);
			const float* c1 = c0 + W_;
			float bl = 0.f, br = 0.f;
			for (int j = 0; j < W_; j++) {
				bl += hl[j] * c1[j];
				br += hr[j] * c1[j];
			}
			al += f * (bl - al);
			ar += f * (br - ar);
		}
		l = al;
		r = ar;
		// Centre: x at n - 1 - W/2 + round(phase / out_) (phase 1 is the next one up).
		const int up = 2 * phase >= out_ ? 1 : 0;
		nearestR = hr[W_ - 1 - W_ / 2 + up];
		pos_ += in_;
	}

	// The kernel's delay, in input samples.
	double delay() const {
		return 0.5 * W_;
	}
	int taps() const {
		return W_;
	}
	int phases() const {
		return P_;
	}
	bool exact() const {
		return exact_;
	}
	bool bypass() const {
		return bypass_;
	}

private:
	int in_ = 48000, out_ = 48000;
	bool bypass_ = true, exact_ = true;
	int W_ = 0, P_ = 1, cap_ = 2;
	std::vector<float> table_, histL_, histR_;
	int w_ = 0;
	int64_t pos_ = 0;
	bool have_ = false;
	float lastL_ = 0.f, lastR_ = 0.f;
};

// A Rack-rate output frame on its way out: volts, the gate already decided.
struct EdgeFrame {
	float l, r;
};

// Both directions for each of Beads' three rates, and the FIFO between
// Beads' blocks and Rack's samples.
class EdgeRate {
public:
	// Beads' three codec rates: Dirty Tape; Vintage Digital; Digital, Clean
	// Tape and the synth.
	static int beadsRate(int i) {
		return i == 0 ? 24000 : (i == 1 ? 32000 : 48000);
	}
	enum { kBlockRate = 4000 };  // beads.cc: blocks of rate / 4000 frames
	enum { kFifo = 1024 };       // > two blocks' worth at 768 kHz

	// Rack's rate, clamped to what the tables are built for.
	static int clampRate(float rackRate) {
		if (!(rackRate >= 1000.f))
			return rackRate > 0.f ? 1000 : 48000;  // NaN and <= 0: 48 kHz; tiny: 1 kHz
		if (rackRate > 768000.f)
			return 768000;
		return (int) std::lround(rackRate);
	}

	// Off the audio thread (allocates the tables for all three Beads rates).
	void setRackRate(float rackRate) {
		rack_ = clampRate(rackRate);
		for (int i = 0; i < 3; i++) {
			in_[i].setup(rack_, beadsRate(i));
			out_[i].setup(beadsRate(i), rack_);
		}
		select(beads_);
	}

	// The engine's thread: switch to a Beads rate (one of beadsRate(i)) and
	// start from silence, as a codec restart does.
	void select(int beadsRate) {
		beads_ = beadsRate;
		cur_ = beadsRate == 24000 ? 0 : (beadsRate == 32000 ? 1 : 2);
		in_[cur_].reset();
		out_[cur_].reset();
		const int block = beadsRate / kBlockRate;
		// Beads hands back a block only once it has a block's input, so the
		// FIFO starts with enough silence to cover a block's worth of Rack
		// samples plus one; at equal rates exactly one block.
		if (in_[cur_].bypass())
			prefill_ = (size_t) block;
		else
			prefill_ = (size_t) ((block * (int64_t) rack_ + beadsRate - 1) / beadsRate) + 1;
		head_ = 0;
		count_ = prefill_;
		for (size_t i = 0; i < prefill_; i++)
			fifo_[i] = EdgeFrame{0.f, 0.f};
		tail_ = prefill_ % kFifo;
	}

	Resampler& in() {
		return in_[cur_];
	}
	Resampler& out() {
		return out_[cur_];
	}

	void pushOut(const EdgeFrame& f) {
		if (count_ == kFifo) {
			overruns_++;
			return;
		}
		fifo_[tail_] = f;
		tail_ = (tail_ + 1) % kFifo;
		count_++;
	}
	EdgeFrame popOut() {
		if (count_ == 0) {
			underruns_++;
			return EdgeFrame{0.f, 0.f};
		}
		EdgeFrame f = fifo_[head_];
		head_ = (head_ + 1) % kFifo;
		count_--;
		return f;
	}

	// Rack samples from an input frame to the output frame it becomes
	// (excluding anything Beads itself delays): the FIFO's silence, the
	// input kernel's half length, the output kernel's in Rack samples.
	double latency() const {
		return (double) prefill_ + in_[cur_].delay() + out_[cur_].delay() * rack_ / beads_;
	}
	int rackRate() const {
		return rack_;
	}
	int currentBeadsRate() const {
		return beads_;
	}
	long underruns() const {
		return underruns_;
	}
	long overruns() const {
		return overruns_;
	}

private:
	int rack_ = 48000, beads_ = 48000, cur_ = 2;
	Resampler in_[3], out_[3];
	EdgeFrame fifo_[kFifo];
	size_t head_ = 0, tail_ = 0, count_ = 0, prefill_ = 0;
	long underruns_ = 0, overruns_ = 0;
};

}  // namespace spindrift
