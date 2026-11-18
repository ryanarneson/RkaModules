#pragma once
// Squall's Swell (spec: Outputs, SWELL): a smooth, non-overshooting follower
// of Step's voltage. Two cascaded one-poles at omega = SWELL_OMEGA / the
// average gap: critically damped, so a step settles to 5 % in two average
// gaps with no overshoot, and the output's slope is continuous (no corners).
// Each coefficient is 1 - exp(-omega / fs), which stays stable at any rate.
// The state is double: at 768 kHz with a 600 s gap a step is ~1e-7 of the
// value, under half a float ulp, so a float would stall.
//
// Frozen, stage 1 is pulled to the output with a one-pole that settles to
// 1 % in BRAKE_S, so the slope a (stage1 - out) decays smoothly to 0 and the
// output holds; released, stage 1 starts from the output, so the slope
// starts from 0.
#include "FastMath.hpp"
#include "squall/SquallTuning.hpp"

namespace rka {
namespace squall {

class Swell {
public:
	Swell() {
		update();
	}

	void setSampleRate(float fs) {
		if (!(fs > 0.f && fs <= 768000.f))
			return;
		fs_ = fs;
		update();
	}

	void setGap(float averageGapS) {
		if (!(averageGapS > 0.f) || averageGapS > 1e7f)
			return;
		if (averageGapS != gap_) {
			gap_ = averageGapS;
			update();
		}
	}

	void setTarget(float volts) {
		if (volts == volts)
			target_ = volts;
	}

	void setFrozen(bool f) {
		frozen_ = f;
	}

	float process() {
		if (frozen_)
			s1_ += b_ * (out_ - s1_);
		else
			s1_ += a_ * ((double) target_ - s1_);
		out_ += a_ * (s1_ - out_);
		return (float) out_;
	}

	void setState(float stage1, float out) {
		s1_ = stage1 == stage1 ? stage1 : 0.0;
		out_ = out == out ? out : 0.0;
		target_ = (float) s1_;
	}

	float stage1() const {
		return (float) s1_;
	}

	float out() const {
		return (float) out_;
	}

private:
	void update() {
		const double w = (double) SWELL_OMEGA / gap_;
		a_ = 1.0 - fastExp2D(-w / fs_ * 1.4426950408889634);
		// 1 % (e^-4.6) in BRAKE_S.
		b_ = 1.0 - fastExp2D(-4.6 / ((double) BRAKE_S * fs_) * 1.4426950408889634);
	}

	float fs_ = 48000.f, gap_ = 1.f, target_ = 0.f;
	double a_ = 0.0, b_ = 0.0, s1_ = 0.0, out_ = 0.0;
	bool frozen_ = false;
};

} // namespace squall
} // namespace rka
