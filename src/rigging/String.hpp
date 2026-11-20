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
//   Hermite read loses at the fundamental, but kept under 1 (the read is
//   flat at DC: above 1 the loop would grow there). So the highest strings
//   cannot ring long: about 1.4 s at 3.5 kHz and 0.7 s at 5 kHz at 44.1 kHz.
// - Damp. A one-pole low-pass set so the fundamental loses exactly
//   rate / f per trip; harmonic n then loses about n * n times that. It is
//   not made up for, so Damp shortens the ring too.
// - A new pitch glides; the delay's length moves a little each sample.
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
		if (damp01 != damp_) {
			damp_ = damp01;
			dirty_ = true;
		}
	}

	// Jump to the targets (at the start, and after a rate change).
	void snap() {
		hz_ = targetHz_;
		coefficients();
		length_ = lengthTarget_;
		lengthStep_ = 0.f;
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
			steps_--;
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
	float periodSamples() const { return fs_ / hz_; }
	// The gain to give a steady, noise-like input so the string is about as
	// loud whatever Ring is: 1 / sqrt of the sum, over its harmonics, of each
	// one's resonant gain.
	float inputShare() const { return share_; }

private:
	enum { kControl = 16 };

	// One control tick while something moved: glide the pitch, then the
	// coefficients for where it is now, reached over the next kControl samples.
	void control() {
		if (hz_ != targetHz_) {
			const float k = 1.f - (float) std::exp(-(double) kControl / (GLIDE_TAU_S * fs_));
			const float logNow = (float) std::log2(hz_), logTarget = (float) std::log2(targetHz_);
			float next = logNow + (logTarget - logNow) * k;
			// Within a hundredth of a cent: arrived.
			if (std::fabs(logTarget - next) < 1e-5f)
				hz_ = targetHz_;
			else
				hz_ = (float) std::exp2(next);
		}
		coefficients();
		lengthStep_ = (lengthTarget_ - length_) / (float) kControl;
		steps_ = kControl;
		dirty_ = hz_ != targetHz_;
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
		double lpDelay = 0.0;
		if (dampRate > 0.0) {
			double keep = std::exp(-dampRate / hz_);
			if (keep < 0.5)
				keep = 0.5;
			// |H|^2 = (1 - r)^2 / (1 - 2 r cos w + r^2) = G for the pole r.
			const double G = keep * keep, c = std::cos(w0);
			const double A = G - 1.0, B = 2.0 - 2.0 * G * c;
			const double disc = B * B - 4.0 * A * A;
			double r = disc > 0.0 ? (-B + std::sqrt(disc)) / (2.0 * A) : 0.0;
			r = r < 0.0 ? 0.0 : r > 0.9999 ? 0.9999 : r;
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
		// Under 1 at DC, whatever was made up.
		if (gain > 0.999999)
			gain = 0.999999;
		gain_ = (float) gain;
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
	float targetHz_ = 65.40639f, hz_ = 65.40639f, ring_ = 4.f, damp_ = 0.f;
	float length_ = 100.f, lengthTarget_ = 100.f, lengthStep_ = 0.f;
	float gain_ = 0.f, a_ = 1.f, lp_ = 0.f, share_ = 1.f;
	int countdown_ = 0, steps_ = 0;
	bool dirty_ = true;
};

} // namespace rigging
} // namespace rka
