#pragma once
// Squall's event process (spec: Weather, Gust): when the next new event
// happens. Rack-free; the engine (SquallEngine.hpp) owns Memory's replays and
// calls excite() for every event, replayed or new.
//
// A self-exciting (Hawkes) process that runs in average-gap time: each
// step advances adv average gaps (rate(w) / fs a sample free-running, p(w)
// a clock tick), and the expected events in a step are
//   adv x ((1 - n) + y),   n = gust x N_MAX,
// where y, Gust's excitation, is dimensionless: it decays by
// d = exp(-adv / K) a step (tau = K average gaps), and each event adds
// n (1 - d) / adv, a bump of area n in average gaps. Because y lives in
// average gaps, the long-run rate is rate(w) whatever Gust is, however
// Weather moves, and the one y carries across a clock being patched or
// unpatched (final review: a per-mode excitation in seconds or ticks piled
// up across a switch and inflated the rate under Weather CV).
//
// Free-running, time is rescaled: Lambda += adv ((1 - n) + y) a sample, and
// an event fires when Lambda reaches an exponential draw E = -ln u. Lambda
// and y are doubles: at 768 kHz a slow rate adds ~1e-8 a sample, which a
// float would lose. Clocked, each tick fires with chance
// clamp(adv ((1 - n) + y), 0, 1).
#include "FastMath.hpp"
#include "hadal/Random.hpp"
#include "squall/SquallTuning.hpp"

#include <cstdint>

namespace rka {
namespace squall {

// rate(w) = (1 / 600 s) x 4800^w events per second.
inline double weatherRate(float w) {
	return fastExp2D((double) w * 12.228818690495881) * (1.0 / 600.0);
}

// p(w) = 256^(w - 1): the chance of an event on each clock tick.
inline float tickChance(float w) {
	return fastExp2(8.f * (w - 1.f));
}

class Events {
public:
	Events() {
		seed(1u);
		update();
	}

	void seed(uint32_t s) {
		rng_.seed(s);
		restartWait();
	}

	void setSampleRate(float fs) {
		if (!(fs > 0.f && fs <= 768000.f))
			return;
		fs_ = fs;
		update();
	}

	void setWeather(float w) {
		w = w > 0.f ? (w < 1.f ? w : 1.f) : 0.f;
		if (w != w_) {
			w_ = w;
			update();
		}
	}

	void setGust(float g) {
		g = g > 0.f ? (g < 1.f ? g : 1.f) : 0.f;
		if (g != g_) {
			g_ = g;
			update();
		}
	}

	// One sample, free-running. mayFire false (a replay owns the timing):
	// the excitation still decays, but nothing fires.
	bool stepFree(bool mayFire = true) {
		const double expected = adv_ * (keep_ + y_);
		decayY(decay_);
		bump_ = bumpFree_;
		if (!mayFire)
			return false;
		acc_ += expected;
		if (acc_ >= wait_) {
			restartWait();
			return true;
		}
		return false;
	}

	// One clock tick.
	bool tick(bool mayFire = true) {
		double chance = advTick_ * (keep_ + y_);
		decayY(decayTick_);
		bump_ = bumpTick_;
		if (!mayFire)
			return false;
		chance = chance < 1.0 ? chance : 1.0;
		return unit32() < chance;
	}

	// Every event, replayed or new, adds its bump.
	// The bump is the one for the step just taken (free or clocked).
	void excite() {
		y_ += bump_;
	}

	// A new wait, from now (after a replay hands the timing back).
	void restartWait() {
		acc_ = 0.0;
		wait_ = -0.69314718 * (double) fastLog2((float) unit32Open());
	}

	double rate() const {
		return rate_;
	}

	// Gust's excitation y (for tests).
	double excitation() const {
		return y_;
	}

	// What is left of the current free-running wait (for tests).
	double waitLeft() const {
		return wait_ - acc_;
	}

private:
	// [0, 1) and (0, 1], from all 32 bits.
	double unit32() {
		return rng_.next() * (1.0 / 4294967296.0);
	}
	double unit32Open() {
		return (rng_.next() + 1.0) * (1.0 / 4294967296.0);
	}

	void decayY(double d) {
		y_ *= d;
		if (y_ < 1e-30)
			y_ = 0.0;   // never subnormal: far below any rate that matters
	}

	void update() {
		rate_ = weatherRate(w_);
		const double n = (double) g_ * N_MAX;
		keep_ = 1.0 - n;
		adv_ = rate_ / fs_;
		decay_ = fastExp2D(-adv_ / K * 1.4426950408889634);
		bumpFree_ = n * (1.0 - decay_) / adv_;
		advTick_ = tickChance(w_);
		decayTick_ = fastExp2D(-advTick_ / K * 1.4426950408889634);
		bumpTick_ = n * (1.0 - decayTick_) / advTick_;
	}

	rka::hadal::Random rng_;
	float fs_ = 48000.f, w_ = 0.5f, g_ = 0.f;
	double rate_ = 0.0, keep_ = 1.0, adv_ = 0.0, decay_ = 1.0, bumpFree_ = 0.0;
	double advTick_ = 0.0, decayTick_ = 0.0, bumpTick_ = 0.0, bump_ = 0.0;
	double y_ = 0.0, acc_ = 0.0, wait_ = 1.0;
};

} // namespace squall
} // namespace rka
