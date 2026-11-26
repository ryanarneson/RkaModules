#pragma once
// One of Rigging's strings (docs/specs/2026-11-19-rigging-design.md): a
// delay line fed back on itself (Karplus-Strong, as a waveguide). Rack-free,
// C++11. process() uses no libm; the coefficients are worked out with libm
// only when the pitch, Ring or Damp moved.
//
// The loop, one trip a period:  delay (Hermite read) -> damping low-pass ->
// gain -> + input -> bound -> delay.
//
// - Pitch. The loop's whole delay is one period: the delay line's length is
//   the period less the one sample of the write and the low-pass's phase
//   delay at the fundamental.
// - Ring. The gain per trip is 10^(-3 / (f * T60)), raised by what the
//   Hermite read loses at the fundamental, but never past a 60 s Ring's
//   gain (the read is flat at DC, where this is the loop's gain). So the
//   highest strings cannot ring long: about 1.4 s at 3.5 kHz and 0.7 s at
//   5 kHz at 44.1 kHz.
// - Damp. A one-pole low-pass set so the fundamental loses exactly
//   rate / f per trip; harmonic n then loses about n * n times that. It is
//   not made up for, so Damp shortens the ring too.
// - DC. The loop holds DC as it holds the string's pitch (a filter in the
//   loop to block it pulls a low string's overtones out of tune with its
//   fundamental: 10 cents at C2 for a 3 Hz blocker). So the gain is never
//   more than a 60 s Ring's, DC in the loop dies as fast as that at worst,
//   and whoever uses a string blocks DC at its output.
// - A new pitch or Damp glides; the delay's length and the gain move a
//   little each sample, never in a step.
// - The bound is the identity below its knee, so a quiet string rings for
//   exactly as long as the gain says.
#include <cmath>

#include "DelayLine.hpp"
#include "FastMath.hpp"
#include "rigging/RiggingTuning.hpp"

namespace rka {
namespace rigging {

class String {
public:
	// Allocates for 8 Hz at fs: never call from process().
	void setSampleRate(float fs) {
		if (!(fs > 0.f))
			fs = 48000.f;
		fs_ = fs;
		delay_.allocate((int) (fs / 8.f) + 8);
		clear();
		snap();
	}

	// The target; the string glides to it. Clamped as Chords.hpp's clampHz.
	void setFrequency(float hz) {
		const float top = 0.4f * fs_ < 12000.f ? 0.4f * fs_ : 12000.f;
		hz = !(hz >= 8.f) ? 8.f : hz > top ? top : hz;
		if (hz != targetHz_) {
			targetHz_ = hz;
			dirty_ = true;
		}
	}
	// The time to fall 60 dB, at Damp 0.
	void setRing(float t60Seconds) {
		if (!(t60Seconds >= 0.01f))
			t60Seconds = 0.01f;
		if (t60Seconds != ring_) {
			ring_ = t60Seconds;
			dirty_ = true;
		}
	}
	void setDamp(float damp01) {
		damp01 = !(damp01 > 0.f) ? 0.f : damp01 > 1.f ? 1.f : damp01;
		if (damp01 != dampTarget_) {
			dampTarget_ = damp01;
			dirty_ = true;
		}
	}

	// Jump to the targets (at the start, and after a rate change).
	void snap() {
		hz_ = targetHz_;
		damp_ = dampTarget_;
		coefficients();
		length_ = lengthGoal_ = lengthTarget_;
		gain_ = gainGoal_ = gainTarget_;
		lengthStep_ = gainStep_ = 0.f;
		steps_ = 0;
		dirty_ = false;
	}

	void clear() {
		delay_.clear();
		lp_ = 0.f;
	}

	float process(float in) {
		if (--countdown_ <= 0) {
			countdown_ = kControl;
			if (dirty_)
				control();
		}
		if (steps_ > 0) {
			length_ += lengthStep_;
			gain_ += gainStep_;
			// The last step lands exactly (sixteen small additions do not).
			if (--steps_ == 0) {
				length_ = lengthGoal_;
				gain_ = gainGoal_;
			}
		}
		const float y = delay_.read(length_);
		lp_ += a_ * (y - lp_);
		// What comes round the loop is the string's sound. The input joins
		// it here, after the output is taken: heard only once it has been
		// round, so noise in the gaps between the harmonics, and above
		// where Damp has closed the string, does not pass straight through.
		const float out = gain_ * lp_;
		float w = out + (in - in == 0.f ? in : 0.f);
		// The bound; and nothing subnormal goes round the loop.
		const float mag = w >= 0.f ? w : -w;
		if (mag > LOOP_KNEE) {
			const float room = LOOP_CEIL - LOOP_KNEE;
			const float b = LOOP_KNEE + room * fastTanh((mag - LOOP_KNEE) / room);
			w = w >= 0.f ? b : -b;
		}
		else if (mag < 1e-20f) {
			w = 0.f;
			if (lp_ < 1e-20f && lp_ > -1e-20f)
				lp_ = 0.f;
		}
		delay_.write(w);
		return out;
	}

	float frequency() const { return hz_; }
	// Still on its way to a new pitch, Ring or Damp (and so still working
	// coefficients out at each control tick).
	bool gliding() const { return dirty_; }
	float periodSamples() const { return fs_ / hz_; }
	// The gain to give a steady, noise-like input so the string is about as
	// loud whatever Ring is: 1 / sqrt of the sum, over its harmonics, of each
	// one's resonant gain.
	float inputShare() const { return share_; }

private:
	enum { kControl = 16 };

	// One control tick while something moved: glide the pitch and Damp, then the
	// coefficients for where it is now; the delay's length and the gain
	// reach them over the next kControl samples, so neither a new pitch nor
	// a new Ring is a step in the sound. The glide arrives: when a step is
	// too small to register, it is taken whole.
	void control() {
		const double k = 1.0 - std::exp(-(double) kControl / ((double) GLIDE_TAU_S * fs_));
		if (hz_ != targetHz_) {
			const double logNow = std::log2((double) hz_), logTarget = std::log2((double) targetHz_);
			const double next = logNow + (logTarget - logNow) * k;
			const float hzNext = (float) std::exp2(next);
			// Within a hundredth of a cent, or no longer moving: arrived.
			if (std::fabs(logTarget - next) < 1e-5 || hzNext == hz_)
				hz_ = targetHz_;
			else
				hz_ = hzNext;
		}
		// Damp glides the same way, in the knob's units: a step would open
		// the low-pass on the raw input the delay line holds, a click.
		if (damp_ != dampTarget_) {
			const float next = damp_ + (dampTarget_ - damp_) * (float) k;
			const float d = dampTarget_ - next;
			damp_ = (d < 1e-4f && d > -1e-4f) || next == damp_ ? dampTarget_ : next;
		}
		coefficients();
		gainGoal_ = gainTarget_;
		lengthGoal_ = lengthTarget_;
		gainStep_ = (gainGoal_ - gain_) / (float) kControl;
		lengthStep_ = (lengthGoal_ - length_) / (float) kControl;
		steps_ = kControl;
		dirty_ = hz_ != targetHz_ || damp_ != dampTarget_;
	}

	void coefficients() {
		const double period = (double) fs_ / hz_;
		const double w0 = 6.283185307179586 / period;

		// Ring: the gain per trip.
		const double ringRate = 6.907755278982137 / ring_;   // nepers a second for -60 dB in ring_
		double gain = std::exp(-ringRate / hz_);

		// Damp: the fundamental keeps (1 - loss) per trip.
		double dampRate = DAMP_MAX_RATE;
		for (int i = 0; i < DAMP_POWER; i++)
			dampRate *= damp_;
		// And air's, the same n^2 loss pinned to AIR_HZ: the fundamental's
		// share of it is (hz / AIR_HZ)^2, held at the pivot's above it.
		const double airRel = (double) (hz_ < AIR_PIVOT_HZ ? hz_ : AIR_PIVOT_HZ) / AIR_HZ;
		const double airRate = 6.907755278982137 / AIR_T60_S * airRel * airRel;
		dampRate += airRate;
		gain = std::exp(-(ringRate - airRate) / hz_);   // the fundamental's air given back
		double lpDelay = 0.0;
		if (dampRate > 0.0) {
			double keep = std::exp(-dampRate / hz_);
			if (keep < 0.5)
				keep = 0.5;
			// |H|^2 = (1 - r)^2 / (1 - 2 r cos w + r^2) = G for the pole r.
			const double G = keep * keep, c = std::cos(w0);
			const double A = G - 1.0, B = 2.0 - 2.0 * G * c;
			const double disc = B * B - 4.0 * A * A;
			// The smaller root, as 2 A / (-B - sqrt(disc)): the same number
			// as (-B + sqrt(disc)) / 2 A, without its 0 / 0 when the loss is
			// too small to register and A rounds to 0 (Damp just off 0).
			double r = disc > 0.0 ? 2.0 * A / (-B - std::sqrt(disc)) : 0.0;
			r = r > 0.0 ? (r > 0.9999 ? 0.9999 : r) : 0.0;   // a NaN comes out 0
			a_ = (float) (1.0 - r);
			lpDelay = std::atan2(r * std::sin(w0), 1.0 - r * std::cos(w0)) / w0;
		}
		else {
			a_ = 1.f;
		}

		// The delay line's length, and what the Hermite read loses of the
		// fundamental at that length's fraction. (Its phase error there is
		// far under a cent up to 8 kHz: not worth taking off.)
		const double length = period - 1.0 - lpDelay;
		double hermiteGain = 1.0;
		{
			const double l = length < 1.0 ? 1.0 : length;
			const double f = l - std::floor(l);
			// Weights of the taps at delays i - 1, i, i + 1, i + 2.
			const double h[4] = {-0.5 * f + f * f - 0.5 * f * f * f, 1.0 - 2.5 * f * f + 1.5 * f * f * f,
			                     0.5 * f + 2.0 * f * f - 1.5 * f * f * f, -0.5 * f * f + 0.5 * f * f * f};
			double re = 0.0, im = 0.0;
			for (int k = 0; k < 4; k++) {
				const double off = (double) (k - 1) - f;   // this tap's delay past the wanted one
				re += h[k] * std::cos(w0 * off);
				im -= h[k] * std::sin(w0 * off);
			}
			hermiteGain = std::sqrt(re * re + im * im);
		}
		if (hermiteGain > 1e-3)
			gain /= hermiteGain;
		// The make-up never takes the gain past a 60 s Ring's: this is the
		// loop's gain at DC, and DC must die too.
		const double most = std::exp(-6.907755278982137 / (60.0 * hz_));
		if (gain > most)
			gain = most;
		gainTarget_ = (float) gain;
		lengthTarget_ = (float) (length < 1.0 ? 1.0 : length);

		// The input's share. Under white noise harmonic n comes out with
		// power G_n^2 / (1 - G_n^2) = 1 / (e^(2 x_n) - 1), where
		// x_n = (ringRate + n^2 dampRate) / f is its loss per trip: about
		// 1 / (2 x_n) - 1/2 while x_n is small, and nothing once it is large.
		// Summed over the harmonics below half the sample rate.
		const double harmonics = period / 2.0;
		double sum;
		if (dampRate > 1e-9) {
			const double x = harmonics * std::sqrt(dampRate / ringRate);
			sum = 0.5 * hz_ * std::atan(x) / std::sqrt(ringRate * dampRate);
			// The harmonics that ring at all: x_n under 1.
			double live = hz_ > ringRate ? std::sqrt((hz_ - ringRate) / dampRate) : 0.0;
			if (live > harmonics)
				live = harmonics;
			sum -= 0.5 * live;
		}
		else {
			sum = harmonics / (std::exp(2.0 * ringRate / hz_) - 1.0);
		}
		const double one = 1.0 / (std::exp(2.0 * (ringRate + dampRate) / hz_) - 1.0);   // the fundamental alone
		if (sum < one)
			sum = one;
		share_ = (float) (1.0 / std::sqrt(sum > 1e-6 ? sum : 1e-6));
	}

	DelayLine delay_;
	float fs_ = 48000.f;
	float targetHz_ = 65.40639f, hz_ = 65.40639f, ring_ = 4.f, damp_ = 0.f, dampTarget_ = 0.f;
	float length_ = 100.f, lengthTarget_ = 100.f, lengthStep_ = 0.f;
	float gain_ = 0.f, gainTarget_ = 0.f, gainStep_ = 0.f, a_ = 1.f, lp_ = 0.f, share_ = 1.f;
	float lengthGoal_ = 100.f, gainGoal_ = 0.f;
	int countdown_ = 0, steps_ = 0;
	bool dirty_ = true;
};

} // namespace rigging
} // namespace rka
