#pragma once
// Doldrums' per-bin tables: what reshapes a grain's loudnesses between the
// transform and the random phases. Once a grain and per side, apply() takes
// loud[0..bins), bin k the loudness at k * rate / (2 bins), and in the
// design's order applies Shift, Blur, Tilt and the comb (Harmonics), then the
// 16 Hz cut. Rack-free, header-only.
//
// - Shift (ratio r), on power, so that noise keeps its power either way.
//   Down (r < 1), a scatter: bin k's power is shared between the two bins
//   either side of k r, in proportion to how near each is (a power-weighted
//   centroid at k r exactly), so a shifted tone stays one tone; stored per
//   source bin as the lower bin and the upper one's share. Several source
//   bins land between two output bins, so a map per output bin would lose
//   some of them. Up (r > 1), a gather: output bin j takes the power at j / r,
//   interpolated between the two source bins either side, times 1 / r;
//   stored per output bin as the lower source bin and the two bins' weights
//   (1 / r in them, and 0 for a source that is cut or lands past the top),
//   and done in place. Scattered upward, only one output bin in r would be filled, and
//   noise made of every r-th bin is periodic within the grain: a repetition
//   heard at the grain's rate. Gathered, a flat spectrum stays flat up to
//   where the source's top bin lands. The trade-off: a tone shifted up is
//   widened in proportion to the shift, spread over about 2 r bins round
//   k r (at +24 semitones, r = 4, with the Medium grain about 47 Hz wide).
//   A scatter would keep a tone narrow but leave the bins between empty.
//   What would land past the top bin is dropped, never folded back: up, the
//   source bins with k r >= bins are left out before the gather. Bins
//   under the cut are dropped before they move, so a DC offset's skirt is
//   not shifted up into the band.
// - Blur (w octaves): the power is smoothed along log frequency and the
//   loudness is its root. The smoothing is a pair of one-pole passes, up the
//   band and back down, each written so that it moves power and never makes
//   or loses any: a running carry takes in each bin's power, leaves the
//   fraction 1 - a_k of itself there and carries a_k on. With
//   a_k = exp(-r / (k ln 2)) the carry falls by e^r an octave at any
//   frequency, so the spread is the same in the bass as in the treble. The
//   upward pass falls at r = q - ln2 / 2 and the downward one at
//   q + ln2 / 2: a bin an octave up holds twice the bins an octave down does,
//   and with rates ln 2 apart a flat spectrum stays flat (measured at full:
//   within 0.01 dB from 100 Hz to the top, 0.07 dB from 50 Hz) while a single bin's power per bin
//   falls as e^(-(q + ln2 / 2) x) above it and e^(-(q - ln2 / 2) x) below, x
//   in octaves. q is set so that its half-power points are w octaves apart.
//   The top bin passes nothing on, so what the upward pass carries past the
//   top comes back down: the power is kept but for what the downward pass
//   carries under the cut. Two passes over the bins whatever w.
// - Tilt (s dB per octave): a gain of s log2(f / 1 kHz) dB, held within
//   +-kTiltBoundDb.
// - The comb (roots, partials, width sigma cents, amount h): teeth at
//   root x n, n = 1..partials, inside the band (16 Hz up to Nyquist; a tooth
//   outside it is left out). A tooth is exp(-(d / sigma')^2 / 2), d the
//   distance in cents from the tooth to the nearest edge of the bin (0 for
//   the bin it lies in), sigma' = sigma but never less than one bin's width
//   at the tooth; for each root the nearest tooth counts, and the mask is the
//   greatest over roots. (Measuring d to the bin's edges is this file's
//   reading of "never narrower than the transform can show": a tooth always
//   passes whole through the bin that holds it, as an oscillator's partial
//   at that frequency would; measured to the bin's centre, a low tooth half
//   way between two bins would pass at 0.88.) The gain on a bin is
//   (1 - h) + h g mask, g the makeup.
// - The makeup is measured from the sound, not worked out from the comb
//   alone (the final review's C1 reversed the design's rule, which assumed a
//   flat spectrum and so raised a sound already on the teeth by up to
//   +18 dB). Per grain, the loudnesses x as they reach the comb (after Shift,
//   Blur and Tilt) give before = sum x^2 and after = sum (x ((1 - h) + h mask))^2,
//   the power the comb would leave at g = 1; g = sqrt(before / after), held
//   to [1, kMakeupBound]. So a sound whose partials lie on the teeth gets
//   about 1 and keeps its level; one the comb thins gets what brings it back,
//   up to the bound (+18 dB: white noise at the defaults would need about
//   +25 dB, and sinks by the rest); a sound between the teeth is still cut,
//   raised by no more than the bound. With before or after 0 (silence, an
//   empty comb), or a sum that is not finite, g is 1. The two sides share one
//   g, from the sums of both, so a panned sound's image does not move.
//   apply(loud, scratch, power) does everything but the comb's gain and adds
//   the side's sums to `power`; makeupFrom(power) is g; comb(l, r, g) then
//   applies (1 - h) + h g mask to both sides in one pass. apply(loud,
//   scratch), for one side alone, does all three and returns g. At h = 0 the
//   comb is not built, nothing is measured and comb() does nothing.
// - Tilt is a gain per bin; the comb's mask is kept apart from it. With
//   Shift 1, Blur 0, Tilt 0 and h 0, apply() only zeroes the bins under
//   16 Hz: every other bin comes back to the bit.
//
// Rebuilding. Two sets of tables, the live one apply() reads and a spare.
// start() begins a rebuild of the spare (cheap: no table is touched);
// work(budget) does a part of it; swapIfReady(), at a grain boundary, makes
// a whole spare live. A start() during a rebuild, or while a built set waits
// for its boundary, is kept and begun after the swap, the newest one only;
// a start() asking for what is live or already on the way does nothing.
// The budget is in units of about 2 to 3 ns each on the development Mac (an
// M5 Pro): a tooth of the comb's setup, a bin of the Shift map or of the
// gain, two for a bin of each Blur pass's coefficients, and for a bin of the
// comb's mask one for each root that has teeth (up to 16). work() always
// does at least one bin. The worst case, 65536 bins with every table in use
// and 16 roots of 32 partials: 512 + 65536 x 22 = 1 442 304 units, 353 calls
// of 4096 or 705 of 2048 (counted by test_tables), about 3 ms in all. So the
// Long grain at 96 kHz, a hop of 65536 samples with a step every 128, needs
// a budget of at least 2817 to finish within half a grain on 512 calls.
//
// apply() is one call over the bins: at 65536 bins it measured 6 us with
// Tilt and the comb, 37 to 42 us with Shift down (by the ratio), 50 us with
// Shift up, 71 us with Blur and 88 us with every table in use (Shift up);
// Blur's passes run as eight chains side by side (one chain is 5x slower:
// each bin waits on the last). (Measured before the makeup was measured from
// the sound; the comb now adds a pass over the bins to measure it, in eight
// sums side by side, and comb() one more over both sides.)
//
// Threads: allocate() off the audio thread; start, work, swapIfReady,
// apply and comb on it, with no allocation, lock or libm (FastMath.hpp; std::sqrt and
// std::floor are instructions). apply() needs loud finite and >= 0 and
// scratch at least bins long.
//
// Bad input (start): bins held to [1, maxBins], a rate not in (0, 1e7] reads
// 48 kHz; NaN shift reads 1 and the ratio is held to [1/16, 16]; NaN Blur,
// Tilt and h read 0, as do Blur and h below 0; Blur is held to 4 octaves,
// Tilt to +-48 dB/oct, h to 1; roots held to [0, 16], partials to [0, 32], a root
// that is not finite and > 0 has no teeth; a NaN width reads 5 cents and the
// width is held to [1, 2400]. With h = 0 the comb's settings are cleared, so
// that a moving V/OCT does not rebuild a comb that is out. live() is the spec
// as it was held.
#include "FastMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rka { namespace doldrums {

constexpr int kMaxRoots = 16;
constexpr int kMaxPartials = 32;
constexpr float kLowCutHz = 16.f;
constexpr float kTiltBoundDb = 24.f;
constexpr float kMakeupBound = 8.f;   // +18 dB, a starting bound on the measured makeup
constexpr float kBlurMaxOctaves = 4.f;

struct TableSpec {                     // what the tables are built from; compared to see if a rebuild is due
	int bins;                          // grain / 2
	float rate;
	float shiftRatio, blurOctaves, tiltDbPerOct;
	float harmonics;                   // h
	int roots; float rootHz[16];
	int partials; float widthCents;
};

// The comb's two powers for one grain, summed over the sides (see "The makeup").
struct CombPower {
	double before = 0.0, after = 0.0;
};

namespace detail {

inline float held(float x, float lo, float hi, float ifNan) {
	return x != x ? ifNan : x < lo ? lo : x > hi ? hi : x;
}

inline TableSpec heldSpec(const TableSpec& in, int maxBins) {
	TableSpec s;
	std::memset(&s, 0, sizeof(s));
	s.bins = std::max(1, std::min(in.bins, std::max(1, maxBins)));
	s.rate = in.rate > 0.f && in.rate <= 1e7f ? in.rate : 48000.f;
	s.shiftRatio = held(in.shiftRatio, 0.0625f, 16.f, 1.f);
	s.blurOctaves = in.blurOctaves > 0.f ? std::min(in.blurOctaves, kBlurMaxOctaves) : 0.f;
	s.tiltDbPerOct = held(in.tiltDbPerOct, -48.f, 48.f, 0.f);
	s.harmonics = in.harmonics > 0.f ? std::min(in.harmonics, 1.f) : 0.f;
	if (s.harmonics > 0.f) {
		s.roots = std::max(0, std::min(in.roots, kMaxRoots));
		for (int i = 0; i < s.roots; i++)
			s.rootHz[i] = in.rootHz[i] > 0.f && in.rootHz[i] < 1e30f ? in.rootHz[i] : 0.f;
		s.partials = std::max(0, std::min(in.partials, kMaxPartials));
		s.widthCents = held(in.widthCents, 1.f, 2400.f, 5.f);
	}
	return s;
}

inline bool sameSpec(const TableSpec& a, const TableSpec& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

} // namespace detail

struct Tables {
	void allocate(int maxBins) {
		maxBins_ = std::max(1, maxBins);
		for (Set& s : sets_) {
			s.lower.assign(maxBins_, -1);
			s.upperShare.assign(maxBins_, 0.f);
			s.lowerShare.assign(maxBins_, 0.f);
			s.carryUp.assign(maxBins_, 0.f);
			s.carryDown.assign(maxBins_, 0.f);
			s.joinUp.assign(maxBins_, 0.f);
			s.joinDown.assign(maxBins_, 0.f);
			s.gain.assign(maxBins_, 1.f);
			s.mask.assign(maxBins_, 0.f);
		}
		live_ = 0;
		phase_ = IDLE;
		waiting_ = false;
		TableSpec rest;
		std::memset(&rest, 0, sizeof(rest));
		rest.bins = maxBins_;
		rest.rate = 48000.f;
		rest.shiftRatio = 1.f;
		for (Set& s : sets_) {
			s.spec = detail::heldSpec(rest, maxBins_);
			setFlags(s);
		}
	}

	void start(const TableSpec& in) {
		if (maxBins_ == 0) return;
		const TableSpec s = detail::heldSpec(in, maxBins_);
		if (phase_ == IDLE) {
			if (!detail::sameSpec(s, sets_[live_].spec)) begin(s);
			return;
		}
		// Building or built: the new values wait for the swap, unless they are
		// what is on the way already.
		waiting_ = !detail::sameSpec(s, sets_[1 - live_].spec);
		if (waiting_) next_ = s;
	}

	// True when nothing is left to build: a whole set waits for swapIfReady,
	// or nothing was asked for.
	bool work(int budget) {
		Set& s = sets_[1 - live_];
		const int bins = s.spec.bins;
		bool first = true;
		while (phase_ != IDLE && phase_ != READY && (budget > 0 || first)) {
			first = false;
			if (phase_ == TEETH) {
				const int total = s.spec.roots * s.spec.partials;
				const int end = std::min(total, cursor_ + std::max(1, budget));
				budget -= end - cursor_;
				for (; cursor_ < end; cursor_++) tooth(s, cursor_ / s.spec.partials, cursor_ % s.spec.partials + 1);
				if (cursor_ == total) next(s);
				continue;
			}
			const int cost = phase_ == MASK ? std::max(1, liveRoots_) : phase_ == BLUR_UP || phase_ == BLUR_DOWN ? 2 : 1;
			const int end = std::min(bins, cursor_ + std::max(1, budget / cost));
			budget -= (end - cursor_) * cost;
			switch (phase_) {
			case SHIFT: shiftBins(s, cursor_, end); break;
			case BLUR_UP: blurUpBins(s, cursor_, end); break;
			case BLUR_DOWN: blurDownBins(s, bins - end, bins - cursor_); break;
			case MASK: maskBins(s, cursor_, end); break;
			case GAIN: gainBins(s, cursor_, end); break;
			default: break;
			}
			cursor_ = end;
			if (cursor_ == bins) next(s);
		}
		return phase_ == IDLE || phase_ == READY;
	}

	// At a grain boundary: a whole spare becomes live. True if one did.
	bool swapIfReady() {
		if (phase_ != READY) return false;
		live_ = 1 - live_;
		phase_ = IDLE;
		if (waiting_) {
			waiting_ = false;
			if (!detail::sameSpec(next_, sets_[live_].spec)) begin(next_);
		}
		return true;
	}

	// One side alone, in place on loud[0..bins): Shift, Blur, Tilt, the 16 Hz
	// cut and the comb with its makeup measured from this side. Returns the makeup.
	float apply(float* loud, float* scratch) const {
		CombPower p;
		apply(loud, scratch, p);
		const float g = makeupFrom(p);
		comb(loud, nullptr, g);
		return g;
	}

	// The live set, applied in place to loud[0..bins): Shift, Blur, Tilt, the
	// 16 Hz cut, all but the comb's gain; with the comb on, the side's two
	// powers added to `power` (see "The makeup").
	void apply(float* loud, float* scratch, CombPower& power) const {
		if (maxBins_ == 0) return;
		const Set& s = sets_[live_];
		const int bins = s.spec.bins, cut = s.cut;
		if (s.shiftOn || s.blurOn) {
			float* p = loud;
			if (s.shiftOn && s.spec.shiftRatio > 1.f) {
				// Up: gathered in place from the top down. Output j reads source
				// bins i and i + 1 with i < j for j >= 1, so neither has been
				// written yet (i + 1 = j is read before it is); bin 0 reads bin 1,
				// already written, with both weights 0.
				for (int j = bins - 1; j >= 0; j--) {
					const int i = s.lower[j];
					const float a = loud[i], b = loud[i + 1];
					loud[j] = s.lowerShare[j] * a * a + s.upperShare[j] * b * b;
				}
			} else if (s.shiftOn) {
				p = scratch;
				std::fill(p, p + bins, 0.f);
				// k r only grows with k, so the two bins being filled are kept in
				// registers and each written once, when the source moves past it.
				int at = -1;
				float lower = 0.f, upper = 0.f;   // what bins at and at + 1 hold so far
				for (int k = cut; k < bins; k++) {
					const int lo = s.lower[k];
					if (lo < 0) break;   // past the top, as is every bin above it
					const float e = loud[k] * loud[k], hiPart = e * s.upperShare[k], loPart = e - hiPart;
					if (lo == at) {
						lower += loPart;
						upper += hiPart;
					} else if (lo == at + 1) {
						if (at >= 0) p[at] = lower;
						lower = upper + loPart;
						upper = hiPart;
						at = lo;
					} else {
						if (at >= 0) {
							p[at] = lower;
							p[at + 1] = upper;
						}
						lower = loPart;
						upper = hiPart;
						at = lo;
					}
				}
				if (at >= 0) {
					p[at] = lower;
					if (at + 1 < bins) p[at + 1] = upper;
				}
			} else {
				for (int k = cut; k < bins; k++) p[k] = loud[k] * loud[k];
				std::fill(p, p + cut, 0.f);
			}
			if (s.blurOn) {
				if (s.segments == kBlurSegments)
					blur<kBlurSegments>(s, p);
				else
					blur<1>(s, p);
			}
			if (s.tiltOn)
				for (int k = cut; k < bins; k++) loud[k] = std::sqrt(p[k]) * s.gain[k];
			else
				for (int k = cut; k < bins; k++) loud[k] = std::sqrt(p[k]);
		} else if (s.tiltOn) {
			for (int k = cut; k < bins; k++) loud[k] *= s.gain[k];
		}
		std::fill(loud, loud + std::min(cut, bins), 0.f);
		if (s.combOn) measure(s, loud, power);
	}

	// The makeup from a grain's powers: sqrt(before / after) held to
	// [1, kMakeupBound]; 1 if either is 0 or not finite.
	static float makeupFrom(const CombPower& p) {
		const double b = p.before, a = p.after;
		if (!(b > 0.0 && a > 0.0 && b - b == 0.0 && a - a == 0.0)) return 1.f;
		const double r = b / a, top = double(kMakeupBound) * kMakeupBound;
		return r >= top ? kMakeupBound : r > 1.0 ? float(std::sqrt(r)) : 1.f;
	}

	// The comb's gain, (1 - h) + h g mask, on both sides in one pass (r may be
	// null). Does nothing with the comb out.
	void comb(float* l, float* r, float g) const {
		if (maxBins_ == 0) return;
		const Set& s = sets_[live_];
		if (!s.combOn) return;
		const int bins = s.spec.bins;
		const float a = 1.f - s.spec.harmonics, b = s.spec.harmonics * g;
		const float* m = s.mask.data();
		if (r)
			for (int k = s.cut; k < bins; k++) {
				const float c = a + b * m[k];
				l[k] *= c;
				r[k] *= c;
			}
		else
			for (int k = s.cut; k < bins; k++) l[k] *= a + b * m[k];
	}

	bool combOn() const { return maxBins_ > 0 && sets_[live_].combOn; }
	const TableSpec& live() const { return sets_[live_].spec; }

private:
	struct Set {
		TableSpec spec{};   // zeroed, so live() before allocate reads no garbage
		int cut = 0;
		bool shiftOn = false, blurOn = false, combOn = false, tiltOn = false;
		int shiftTop = 0;   // Shift up: the first source bin that lands past the top
		std::vector<int32_t> lower;     // Shift: the bin below k r, -1 dropped
		std::vector<float> upperShare;  // and the share of the bin above
		std::vector<float> lowerShare;  // Shift up only: the lower source bin's weight
		std::vector<float> carryUp;     // Blur: a_k up the band
		std::vector<float> carryDown;   // and down
		// The passes run as `segments` chains side by side, each from a carry of
		// 0, then joined: the carry a segment should have had from the one
		// before, c, adds join[k] c to bin k, join[k] = (1 - a_k) x the product
		// of a over the segment's bins before k (in the pass's direction), and
		// passes on whole[seg] = the product over all of it.
		int segments = 1, segLength = 0;   // the last segment ends at bins
		std::vector<float> joinUp, joinDown;
		float wholeUp[8] = {}, wholeDown[8] = {};
		std::vector<float> gain;        // Tilt
		std::vector<float> mask;        // the comb's mask
	};
	enum Phase { IDLE, TEETH, SHIFT, BLUR_UP, BLUR_DOWN, MASK, GAIN, READY };
	static constexpr int kBlurSegments = 8;   // as many chains as the core keeps busy

	Set sets_[2];
	int maxBins_ = 0;
	int live_ = 0;
	Phase phase_ = IDLE;
	int cursor_ = 0;
	bool waiting_ = false;
	TableSpec next_{};

	// The build in hand.
	double binHz_ = 0;
	float blurUp_ = 0, blurDown_ = 0;   // r / ln(2)^2 for each pass
	float run_ = 1.f;                    // the product of a so far in the segment
	int liveRoots_ = 0;
	int lo_[kMaxRoots] = {}, hi_[kMaxRoots] = {};   // teeth n in [lo, hi], or none if lo > hi
	float perBin_[kMaxRoots] = {};                  // bins per root: f / root = k perBin
	float toothLog2_[kMaxRoots][kMaxPartials + 1] = {};
	float toothInvSigma_[kMaxRoots][kMaxPartials + 1] = {};

	static void setFlags(Set& s) {
		const TableSpec& p = s.spec;
		const double c = 2.0 * kLowCutHz * p.bins / p.rate;   // the first bin at or over 16 Hz
		int cut = int(c);
		if (cut < c) cut++;
		s.cut = std::min(std::max(cut, 0), p.bins);
		s.shiftOn = p.shiftRatio != 1.f && p.bins >= 2;
		const double top = double(p.bins) / p.shiftRatio;   // k r >= bins from here
		s.shiftTop = std::min(p.bins, int(top) + (int(top) < top ? 1 : 0));
		s.blurOn = p.blurOctaves > 0.f;
		s.combOn = p.harmonics > 0.f;
		s.tiltOn = p.tiltDbPerOct != 0.f;
		// Segments a little longer than an eighth, so that the eight chains do
		// not fall on the same cache sets (eighths of a power of two do: 5x
		// slower), the last one shorter.
		const bool split = p.bins >= 2048;
		s.segments = split ? kBlurSegments : 1;
		s.segLength = split ? p.bins / kBlurSegments + 16 : p.bins;
	}

	void begin(const TableSpec& spec) {
		Set& s = sets_[1 - live_];
		s.spec = spec;
		setFlags(s);
		binHz_ = double(spec.rate) / (2.0 * spec.bins);
		if (s.blurOn) {
			// Half-power points w octaves apart: w = ln 2 (1 / (q + ln2 / 2) + 1 / (q - ln2 / 2)).
			const double ln2 = 0.693147180559945, w = spec.blurOctaves;
			const double q = 0.5 * (2.0 * ln2 / w + std::sqrt(4.0 * ln2 * ln2 / (w * w) + ln2 * ln2));
			blurUp_ = float((q - 0.5 * ln2) / (ln2 * ln2));
			blurDown_ = float((q + 0.5 * ln2) / (ln2 * ln2));
		}
		liveRoots_ = 0;
		const double nyquist = 0.5 * spec.rate;
		for (int i = 0; i < kMaxRoots; i++) {
			lo_[i] = 1;
			hi_[i] = 0;
			if (!s.combOn || i >= spec.roots || !(spec.rootHz[i] > 0.f)) continue;
			const double r = spec.rootHz[i];
			// Teeth in [16 Hz, Nyquist): n from ceil(16 / r) to ceil(nyquist / r) - 1.
			const double lo = std::min(1e6, kLowCutHz / r), hi = std::min(1e6, nyquist / r);
			int a = int(lo);
			if (a < lo) a++;
			int b = int(hi);
			if (b >= hi) b--;
			lo_[i] = std::max(1, a);
			hi_[i] = std::min(spec.partials, b);
			perBin_[i] = float(binHz_ / r);
			if (lo_[i] <= hi_[i]) liveRoots_++;
		}
		cursor_ = 0;
		phase_ = TEETH;
		if (!s.combOn) next(s);
	}

	// On to the next phase this set needs.
	void next(Set& s) {
		cursor_ = 0;
		for (;;) {
			phase_ = Phase(phase_ + 1);
			if (phase_ == READY || (phase_ == SHIFT && s.shiftOn) || ((phase_ == BLUR_UP || phase_ == BLUR_DOWN) && s.blurOn) ||
			    (phase_ == MASK && s.combOn) || (phase_ == GAIN && s.tiltOn))
				return;
		}
	}

	void tooth(Set& s, int root, int n) {
		if (n < lo_[root] || n > hi_[root]) return;
		const float f = s.spec.rootHz[root] * float(n);
		const float l = fastLog2(f);
		const float binCents = 1200.f * (fastLog2(f + float(binHz_)) - l);
		toothLog2_[root][n] = l;
		toothInvSigma_[root][n] = 1.f / std::max(s.spec.widthCents, binCents);
	}

	void shiftBins(Set& s, int from, int to) {
		const double r = s.spec.shiftRatio;
		if (r > 1.0) {
			// Up: output j reads between source bins floor(j / r) and the one
			// above, both inside the band: j / r < bins - 1 for j < bins (Shift is
			// off below 2 bins).
			// The weights carry the 1 / r that keeps the power, and are 0 for a
			// source under the cut or one that lands past the top.
			const int top = s.spec.bins - 2;
			const float inv = float(1.0 / r);
			auto kept = [&s](int k) { return k >= s.cut && k < s.shiftTop; };
			for (int j = from; j < to; j++) {
				const double at = j / r;
				const int lo = std::min(int(at), top);
				const float up = float(std::min(1.0, at - lo));
				s.lower[j] = lo;
				s.lowerShare[j] = j >= 1 && kept(lo) ? inv * (1.f - up) : 0.f;
				s.upperShare[j] = j >= 1 && kept(lo + 1) ? inv * up : 0.f;
			}
			return;
		}
		for (int k = from; k < to; k++) {
			const double at = k * r;
			const int lo = int(at);
			const bool kept = k >= s.cut && lo < s.spec.bins;
			s.lower[k] = kept ? lo : -1;
			s.upperShare[k] = kept ? float(at - lo) : 0.f;
		}
	}

	// Bins [from, to) of the upward pass's coefficients, in rising order. The
	// top bin keeps what the upward pass brings it, for the downward one to
	// spread back: nothing is lost past the top.
	void blurUpBins(Set& s, int from, int to) {
		for (int k = from; k < to; k++) {
			const int seg = std::min(k / s.segLength, s.segments - 1);
			if (k == seg * s.segLength) run_ = 1.f;
			const float a = k == 0 || k == s.spec.bins - 1 ? 0.f : fastExp2(-blurUp_ / float(k));
			s.carryUp[k] = a;
			s.joinUp[k] = (1.f - a) * run_;
			run_ = run_ * a < 1e-30f ? 0.f : run_ * a;   // no denormals
			s.wholeUp[seg] = run_;
		}
	}

	// And of the downward pass, bins [from, to) in falling order.
	void blurDownBins(Set& s, int from, int to) {
		for (int k = to - 1; k >= from; k--) {
			const int seg = std::min(k / s.segLength, s.segments - 1);
			if (k == (seg == s.segments - 1 ? s.spec.bins : (seg + 1) * s.segLength) - 1) run_ = 1.f;
			const float a = k == 0 ? 0.f : fastExp2(-blurDown_ / float(k));
			s.carryDown[k] = a;
			s.joinDown[k] = (1.f - a) * run_;
			run_ = run_ * a < 1e-30f ? 0.f : run_ * a;
			s.wholeDown[seg] = run_;
		}
	}

	// One step of a pass: bin x takes in the carry, keeps 1 - a of the sum and
	// carries a on. t - c is exact (c is within a factor 2 of t or is 0), so
	// no power is made or lost by rounding.
	static void carryStep(float& x, float& c, float a) {
		const float t = c + x;
		c = t * a;
		x = t - c;
	}

	// Blur in place on the power p: up the band, then down, S chains at a time.
	// Segment j is [j n, (j + 1) n), the last [(S - 1) n, bins), `last` long.
	template <int S>
	static void blur(const Set& s, float* p) {
		const int n = s.segLength, last = s.spec.bins - (S - 1) * n;
		const float* up = s.carryUp.data();
		const float* down = s.carryDown.data();
		float c[S];
		for (int j = 0; j < S; j++) c[j] = 0.f;
		for (int i = 0; i < last; i++)
			for (int j = 0; j < S; j++) carryStep(p[j * n + i], c[j], up[j * n + i]);
		for (int i = last; i < n; i++)
			for (int j = 0; j < S - 1; j++) carryStep(p[j * n + i], c[j], up[j * n + i]);
		float in = 0.f;
		for (int j = 1; j < S; j++) {
			in = c[j - 1] + in * s.wholeUp[j - 1];
			join(p + j * n, s.joinUp.data() + j * n, j == S - 1 ? last : n, in);
		}
		// Down: chain j from the top of its segment.
		for (int j = 0; j < S; j++) c[j] = 0.f;
		for (int i = 0; i < last; i++) {
			for (int j = 0; j < S - 1; j++) {
				const int k = (j + 1) * n - 1 - i;
				carryStep(p[k], c[j], down[k]);
			}
			const int k = s.spec.bins - 1 - i;
			carryStep(p[k], c[S - 1], down[k]);
		}
		for (int i = last; i < n; i++)
			for (int j = 0; j < S - 1; j++) {
				const int k = (j + 1) * n - 1 - i;
				carryStep(p[k], c[j], down[k]);
			}
		in = 0.f;
		for (int j = S - 2; j >= 0; j--) {
			in = c[j + 1] + in * s.wholeDown[j + 1];
			join(p + j * n, s.joinDown.data() + j * n, n, in);
		}
	}

	static void join(float* p, const float* w, int n, float in) {
		for (int i = 0; i < n; i++) p[i] += w[i] * in;
	}

	void maskBins(Set& s, int from, int to) {
		const float hz = float(binHz_);
		for (int k = from; k < to; k++) {
			if (k < s.cut) {
				s.mask[k] = 0.f;
				continue;
			}
			const float below = fastLog2((float(k) - 0.5f) * hz), above = fastLog2((float(k) + 0.5f) * hz);
			float m = 0.f;
			for (int i = 0; i < s.spec.roots; i++) {
				if (lo_[i] > hi_[i]) continue;
				const int n0 = int(float(k) * perBin_[i]);
				const int a = std::min(std::max(n0, lo_[i]), hi_[i]);
				const int b = std::min(std::max(n0 + 1, lo_[i]), hi_[i]);
				m = std::max(m, bell(below, above, toothLog2_[i][a], toothInvSigma_[i][a]));
				if (b != a) m = std::max(m, bell(below, above, toothLog2_[i][b], toothInvSigma_[i][b]));
			}
			s.mask[k] = m;
		}
	}

	// The comb's two powers for one side's loudnesses x (bins under the cut
	// are 0): eight sums side by side, so that no add waits on the last.
	static void measure(const Set& s, const float* x, CombPower& power) {
		const int bins = s.spec.bins;
		const float a = 1.f - s.spec.harmonics, b = s.spec.harmonics;
		const float* m = s.mask.data();
		float before[8] = {}, after[8] = {};
		int k = 0;
		for (; k + 8 <= bins; k += 8)
			for (int j = 0; j < 8; j++) {
				const float v = x[k + j], y = v * (a + b * m[k + j]);
				before[j] += v * v;
				after[j] += y * y;
			}
		for (; k < bins; k++) {
			const float v = x[k], y = v * (a + b * m[k]);
			before[0] += v * v;
			after[0] += y * y;
		}
		double sb = 0.0, sa = 0.0;
		for (int j = 0; j < 8; j++) {
			sb += before[j];
			sa += after[j];
		}
		power.before += sb;
		power.after += sa;
	}

	// A tooth at log2 t seen by the bin [2^below, 2^above].
	static float bell(float below, float above, float t, float invSigma) {
		const float d = t < below ? below - t : t > above ? t - above : 0.f;
		const float u = 1200.f * d * invSigma;
		if (u >= 6.f) return 0.f;
		return fastExp2(-0.721347520444482f * u * u);   // e^(-u^2 / 2)
	}

	void gainBins(Set& s, int from, int to) {
		const TableSpec& p = s.spec;
		const float hz = float(binHz_);
		for (int k = from; k < to; k++) {
			const float db = p.tiltDbPerOct * (fastLog2(float(k) * hz) - 9.96578428466209f);   // log2(1000)
			s.gain[k] = k < s.cut ? 0.f : fastDbToGain(std::min(kTiltBoundDb, std::max(-kTiltBoundDb, db)));
		}
	}
};

}} // namespace rka::doldrums
