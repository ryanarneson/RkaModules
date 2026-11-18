#pragma once
// Squall's event process (spec: Weather, Gust): when the next new event
// happens. Rack-free; the engine (SquallEngine.hpp) owns Memory's replays and
// calls excite() for every event, replayed or new.
//
// Free-running, a self-exciting (Hawkes) process with intensity
//   lambda = rate(w) (1 - n) + x,   n = gust x N_MAX,
// where each event adds a bump of area n to x, decaying with
// tau = K / rate(w). The long-run rate is rate(w) whatever Gust is. Time is
// rescaled: Lambda += lambda / fs each sample, and an event fires when
// Lambda reaches an exponential draw E = -ln u. Lambda and x are doubles: at
// 768 kHz a slow rate adds ~1e-8 a sample, which a float would lose.
//
// Clocked, the same on the tick grid: each tick fires with chance
// clamp(p(w) (1 - n) + xt, 0, 1); xt decays by d = exp(-p / K) a tick and an
// event adds n (1 - d), so a bump's total is n.
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
		const double lambda = base_ + x_;
		x_ *= decay_;
		if (!mayFire)
			return false;
		acc_ += lambda * invFs_;
		if (acc_ >= wait_) {
			restartWait();
			return true;
		}
		return false;
	}

	// One clock tick.
	bool tick(bool mayFire = true) {
		float chance = baseTick_ + xt_;
		xt_ *= decayTick_;
		if (!mayFire)
			return false;
		chance = chance < 1.f ? chance : 1.f;
		return unit32() < (double) chance;
	}

	// Every event, replayed or new, adds its bump.
	void excite() {
		x_ += bump_;
		xt_ += bumpTick_;
	}

	// A new wait, from now (after a replay hands the timing back).
	void restartWait() {
		acc_ = 0.0;
		wait_ = -0.69314718 * (double) fastLog2((float) unit32Open());
	}

	double rate() const {
		return rate_;
	}

private:
	// [0, 1) and (0, 1], from all 32 bits.
	double unit32() {
		return rng_.next() * (1.0 / 4294967296.0);
	}
	double unit32Open() {
		return (rng_.next() + 1.0) * (1.0 / 4294967296.0);
	}

	void update() {
		rate_ = weatherRate(w_);
		const double n = (double) g_ * N_MAX;
		base_ = rate_ * (1.0 - n);
		// Per sample: x decays by exp(-1 / (tau fs)), tau = K / rate; a bump
		// of n (1 - d) fs integrates (sum / fs) to n.
		decay_ = fastExp2D(-rate_ / ((double) K * fs_) * 1.4426950408889634);
		bump_ = n * (1.0 - decay_) * fs_;
		invFs_ = 1.0 / fs_;
		const float p = tickChance(w_);
		baseTick_ = p * (1.f - (float) n);
		decayTick_ = fastExp2(-p / K * 1.44269504f);
		bumpTick_ = (float) n * (1.f - decayTick_);
	}

	rka::hadal::Random rng_;
	float fs_ = 48000.f, w_ = 0.5f, g_ = 0.f;
	double rate_ = 0.0, base_ = 0.0, decay_ = 1.0, bump_ = 0.0, invFs_ = 1.0 / 48000.0;
	double x_ = 0.0, acc_ = 0.0, wait_ = 1.0;
	float baseTick_ = 0.f, decayTick_ = 0.f, bumpTick_ = 0.f, xt_ = 0.f;
};

} // namespace squall
} // namespace rka
