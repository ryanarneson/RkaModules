#pragma once
// Doldrums' grain: one slice of tape turned into sound, PaulStretch's method
// (Paul Nasca) written afresh from its description. Each side of the slice is
// windowed and transformed; only the loudness of each frequency is kept; the
// tables (Tables.hpp) reshape the loudnesses; every frequency is given a
// fresh random phase; the result is transformed back and windowed; Overlap
// lays the finished grains down half a grain apart. Rack-free, header-only.
//
// The windows. Two grains that overlap have unrelated random phases, so where
// they cross their powers add, not their amplitudes. The window that lays a
// grain down is therefore the sine window, sin(pi (n + 1/2) / N), whose
// squares at half a grain apart sum to exactly 1: noise comes out with no
// ripple at the hop's rate (a window whose plain sum is flat, such as Hann,
// would dip 3 dB where grains cross). The window that takes a grain is Hann,
// 1/2 - 1/2 cos(2 pi n / N): its side lobes fall away fast (-31 dB, then
// 18 dB an octave), so a tone's loudness stays within a few bins of it and
// the skirt of noise round a held tone stays narrow. The analysis window
// alters the level only through the sum of its squares, which the fixed gain
// undoes: by Parseval a grain of white noise of power s^2 has spectral power
// s^2 sum(wa^2) per bin whatever the phases, so the random-phase grain has
// power s^2 sum(wa^2) / N per sample, and the gain sqrt(N / sum(wa^2))
// (sqrt(8/3) for Hann) brings it back to s^2. The same holds for a tone, whose
// energy in the grain is the same sum. pffft's inverse is N times the true one,
// so the laying-down window holds gain / N as well.
//
// Steps. begin() starts a grain; step() does one part of it. None is larger
// than one transform or one pass over the bins (or over the grain's frames):
//   WINDOW      the slices times the analysis window (NaN or inf read as 0)
//   FORWARD_L/R one forward transform each
//   LOUDNESS    |X| per bin for each side and the unit vector of
//               X_R conj(X_L) (both kept: the held loudnesses), then the
//               loudnesses copied for the tables with the bins under 16 Hz
//               zeroed (the same cut the tables make, so that at rest the
//               spectral steps change nothing)
//   SPECTRAL_L/R Tables::apply on each side's loudnesses
//   PHASES      a random phase per bin from the generator and a table of
//               4096 cosines and sines. By default one phase for both sides,
//               the right side turned further by the kept unit vector (from
//               one reciprocal square root, no trig; a zero vector reads as
//               no turn, and a bin equal on both sides as exactly none, so
//               a mono input gives sides equal to the bit). Wide: a phase of
//               its own for each side.
//   INVERSE_L/R one inverse transform each, times the laying-down window
// With take false the slices are not read (they may be null): WINDOW,
// FORWARD and the measuring half of LOUDNESS do nothing, and the loudnesses
// and turns kept from the last taken grain are used again with fresh phases.
// Bins 0 (DC) and the Nyquist bin are always 0. outL()/outR() hold the
// finished grain from INVERSE_L/R until the next grain's INVERSE.
//
// Tables: apply() is used only when live().bins is this grain's bins (the
// engine swaps tables of the right size in at a grain boundary); otherwise
// SPECTRAL does nothing. The 16 Hz cut uses live().rate.
//
// Overlap: add() lays a grain down starting at the sample next() gives next;
// the engine calls it once a hop, so the grains lie half a grain apart. A
// grain of any size up to the allocated one may follow one of another size.
// The gain multiplies the grain as it is laid (1 for the level above).
//
// Threads: GrainBuffers::allocate, Overlap::allocate and the Grain's
// constructor (the phase table) off the audio thread; everything else on it,
// with no allocation, lock or libm.
#include "Fft.hpp"
#include "Tables.hpp"
#include "hadal/Random.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace rka { namespace doldrums {

constexpr int kMinGrain = 32;
constexpr int kMaxGrain = 131072;
constexpr int kPhaseBits = 12;
constexpr int kPhases = 1 << kPhaseBits;

// Everything one grain size needs.
struct GrainBuffers {
	GrainBuffers() = default;
	~GrainBuffers() { release(); }
	GrainBuffers(const GrainBuffers&) = delete;
	GrainBuffers& operator=(const GrainBuffers&) = delete;

	// grain: a power of two in [32, 131072]. False (and nothing held) otherwise.
	bool allocate(int grain) {
		release();
		if (grain < kMinGrain || grain > kMaxGrain || (grain & (grain - 1)))
			return false;
		fft.reset(new RealFft(grain));
		if (fft->size() != grain) {
			release();
			return false;
		}
		float** aligned[] = {&timeL, &timeR, &specL, &specR, &outL, &outR};
		for (float** p : aligned) {
			*p = RealFft::alloc(grain);
			if (!*p) {
				release();
				return false;
			}
			std::fill(*p, *p + grain, 0.f);
		}
		const int bins = grain / 2;
		analysis.assign(grain, 0.f);
		laying.assign(grain, 0.f);
		const double pi = 3.14159265358979323846;
		double sumSq = 0.0;
		for (int n = 0; n < grain; n++) {
			const double w = 0.5 - 0.5 * std::cos(2.0 * pi * n / grain);
			analysis[n] = float(w);
			sumSq += double(analysis[n]) * analysis[n];
		}
		const double gain = std::sqrt(grain / sumSq);
		for (int n = 0; n < grain; n++)
			laying[n] = float(std::sin(pi * (n + 0.5) / grain) * gain / grain);
		heldL.assign(bins, 0.f);
		heldR.assign(bins, 0.f);
		turn.assign(2 * bins, 0.f);
		for (int k = 0; k < bins; k++) turn[2 * k] = 1.f;
		loudL.assign(bins, 0.f);
		loudR.assign(bins, 0.f);
		scratch.assign(bins, 0.f);
		grain_ = grain;
		return true;
	}

	void release() {
		fft.reset();
		float** aligned[] = {&timeL, &timeR, &specL, &specR, &outL, &outR};
		for (float** p : aligned) {
			RealFft::free(*p);
			*p = nullptr;
		}
		grain_ = 0;
	}

	int grain() const { return grain_; }
	int bins() const { return grain_ / 2; }

	std::unique_ptr<RealFft> fft;
	float *timeL = nullptr, *timeR = nullptr;   // the windowed slices
	float *specL = nullptr, *specR = nullptr;   // their spectra, then the new ones
	float *outL = nullptr, *outR = nullptr;     // the finished grain
	std::vector<float> analysis, laying;        // the windows (laying holds the gain and 1 / N)
	std::vector<float> heldL, heldR;            // the loudnesses last taken
	std::vector<float> turn;                    // unit vector of X_R conj(X_L) per bin, re/im
	std::vector<float> loudL, loudR, scratch;   // the loudnesses through the tables

private:
	int grain_ = 0;
};

struct Grain {
	enum Step { WINDOW, FORWARD_L, FORWARD_R, LOUDNESS, SPECTRAL_L, SPECTRAL_R, PHASES, INVERSE_L, INVERSE_R, kSteps };

	Grain() {
		const double pi = 3.14159265358979323846;
		for (int j = 0; j < kPhases; j++) {
			cosSin_[2 * j] = float(std::cos(2.0 * pi * j / kPhases));
			cosSin_[2 * j + 1] = float(std::sin(2.0 * pi * j / kPhases));
		}
	}

	// take false: keep the held loudnesses (the slices are not read).
	void begin(GrainBuffers& b, const float* sliceL, const float* sliceR, bool take) {
		b_ = b.grain() > 0 ? &b : nullptr;
		sliceL_ = sliceL;
		sliceR_ = sliceR;
		take_ = take;
	}

	void step(Step s, const Tables& t, rka::hadal::Random& rng, bool wide) {
		if (!b_) return;
		GrainBuffers& b = *b_;
		const int n = b.grain(), bins = b.bins();
		switch (s) {
		case WINDOW:
			if (take_) {
				windowed(sliceL_, b.timeL, b, n);
				windowed(sliceR_, b.timeR, b, n);
			}
			break;
		case FORWARD_L:
			if (take_) b.fft->forward(b.timeL, b.specL);
			break;
		case FORWARD_R:
			if (take_) b.fft->forward(b.timeR, b.specR);
			break;
		case LOUDNESS: {
			if (take_) measure(b, bins);
			const double c = 2.0 * kLowCutHz * bins / t.live().rate;   // the tables' cut
			int cut = int(c);
			if (cut < c) cut++;
			cut = std::max(1, std::min(cut, bins));
			std::fill(b.loudL.begin(), b.loudL.begin() + cut, 0.f);
			std::fill(b.loudR.begin(), b.loudR.begin() + cut, 0.f);
			std::copy(b.heldL.begin() + cut, b.heldL.end(), b.loudL.begin() + cut);
			std::copy(b.heldR.begin() + cut, b.heldR.end(), b.loudR.begin() + cut);
			break;
		}
		case SPECTRAL_L:
			if (t.live().bins == bins) t.apply(b.loudL.data(), b.scratch.data());
			break;
		case SPECTRAL_R:
			if (t.live().bins == bins) t.apply(b.loudR.data(), b.scratch.data());
			break;
		case PHASES:
			phases(b, bins, rng, wide);
			break;
		case INVERSE_L:
			b.fft->inverse(b.specL, b.outL);
			for (int i = 0; i < n; i++) b.outL[i] *= b.laying[i];
			break;
		case INVERSE_R:
			b.fft->inverse(b.specR, b.outR);
			for (int i = 0; i < n; i++) b.outR[i] *= b.laying[i];
			break;
		default:
			break;
		}
	}

	// The finished grain, windowed for laying down: grain() frames a side.
	const float* outL() const { return b_ ? b_->outL : nullptr; }
	const float* outR() const { return b_ ? b_->outR : nullptr; }

private:
	GrainBuffers* b_ = nullptr;
	const float* sliceL_ = nullptr;
	const float* sliceR_ = nullptr;
	bool take_ = false;
	float cosSin_[2 * kPhases];

	static void windowed(const float* in, float* out, const GrainBuffers& b, int n) {
		if (!in) {
			std::fill(out, out + n, 0.f);
			return;
		}
		for (int i = 0; i < n; i++) {
			const float x = in[i];
			out[i] = (x - x == 0.f ? x : 0.f) * b.analysis[i];   // NaN and inf read as 0
		}
	}

	// Loudness, held finite for the tables.
	static float magnitude(float re, float im) {
		const float m = std::sqrt(re * re + im * im);
		return m < 1e18f ? m : m == m ? 1e18f : 0.f;
	}

	static void measure(GrainBuffers& b, int bins) {
		const float* xl = b.specL;
		const float* xr = b.specR;
		b.heldL[0] = b.heldR[0] = 0.f;   // DC: always under the cut
		b.turn[0] = 1.f;
		b.turn[1] = 0.f;
		for (int k = 1; k < bins; k++) {
			const float lr = xl[2 * k], li = xl[2 * k + 1];
			const float rr = xr[2 * k], ri = xr[2 * k + 1];
			b.heldL[k] = magnitude(lr, li);
			b.heldR[k] = magnitude(rr, ri);
			// X_R conj(X_L). Equal sides are no turn, decided by comparing them:
			// the product's imaginary part might not come to exactly 0 if the
			// compiler fuses a multiply and a subtraction.
			float ur = 1.f, ui = 0.f;
			if (rr != lr || ri != li) {
				const float pr = rr * lr + ri * li;
				const float pi = ri * lr - rr * li;
				const float m2 = pr * pr + pi * pi;
				if (m2 > 1e-30f && m2 < 1e30f) {
					const float inv = 1.f / std::sqrt(m2);
					ur = pr * inv;
					ui = pi * inv;
				}
			}
			b.turn[2 * k] = ur;
			b.turn[2 * k + 1] = ui;
		}
	}

	void phases(GrainBuffers& b, int bins, rka::hadal::Random& rng, bool wide) {
		float* yl = b.specL;
		float* yr = b.specR;
		yl[0] = yl[1] = yr[0] = yr[1] = 0.f;   // DC and Nyquist
		const float* ll = b.loudL.data();
		const float* lr = b.loudR.data();
		const float* u = b.turn.data();
		for (int k = 1; k < bins; k++) {
			const uint32_t j = rng.next() >> (32 - kPhaseBits);
			const float c = cosSin_[2 * j], s = cosSin_[2 * j + 1];
			yl[2 * k] = ll[k] * c;
			yl[2 * k + 1] = ll[k] * s;
			if (wide) {
				const uint32_t j2 = rng.next() >> (32 - kPhaseBits);
				yr[2 * k] = lr[k] * cosSin_[2 * j2];
				yr[2 * k + 1] = lr[k] * cosSin_[2 * j2 + 1];
			} else {   // a turn of exactly (1, 0) leaves c and s as they are
				const float ur = u[2 * k], ui = u[2 * k + 1];
				yr[2 * k] = lr[k] * (c * ur - s * ui);
				yr[2 * k + 1] = lr[k] * (c * ui + s * ur);
			}
		}
	}
};

// Lays finished grains down half a grain apart and gives the sum a sample at a time.
struct Overlap {
	// maxGrain: the largest grain add() will be given.
	bool allocate(int maxGrain) {
		int size = 1;
		while (size < 2 * std::max(1, maxGrain)) size *= 2;
		accL_.assign(size, 0.f);
		accR_.assign(size, 0.f);
		mask_ = size - 1;
		maxGrain_ = std::max(1, maxGrain);
		pos_ = 0;
		return true;
	}

	void reset() {
		std::fill(accL_.begin(), accL_.end(), 0.f);
		std::fill(accR_.begin(), accR_.end(), 0.f);
		pos_ = 0;
	}

	// The grain (grain frames a side, held to the allocation) times gain, from
	// the sample next() gives next. A non-finite gain reads 0.
	void add(const float* l, const float* r, int grain, float gain) {
		if (accL_.empty() || !l || !r) return;
		if (!(gain - gain == 0.f)) gain = 0.f;
		const int n = std::max(0, std::min(grain, maxGrain_));
		for (int i = 0; i < n; i++) {
			const int at = (pos_ + i) & mask_;
			accL_[at] += gain * l[i];
			accR_[at] += gain * r[i];
		}
	}

	void next(float& l, float& r) {
		if (accL_.empty()) {
			l = r = 0.f;
			return;
		}
		l = accL_[pos_];
		r = accR_[pos_];
		accL_[pos_] = accR_[pos_] = 0.f;
		pos_ = (pos_ + 1) & mask_;
	}

private:
	std::vector<float> accL_, accR_;
	int mask_ = 0, maxGrain_ = 0, pos_ = 0;
};

}} // namespace rka::doldrums
