#pragma once
// Hadal's engine (spec: docs/specs/2026-11-16-hadal-design.md). Rack-free;
// Hadal.cpp only maps ports, params and patch state onto it.
//
// Per sample:
//   controls = clamp(knob + CV / 10 V x attenuverter, 0, 1)  (channel 1 of a CV)
//   smoothed (30 ms one-poles): Depth, the three level gains (knob^2), Drift
//   every 32 samples: Depth and Drift's colour and density into the layers
//   the layers that are needed: a layer's output is patched, or L or R is
//   layer outputs: the layer's left side x 1.6 V (Rumble: x 3.2 V; Crackle: soft-clipped by
//     Crackle::shape, then x 5 V) x
//     its level gain x Drift's level wander for that side
//   the mix: each layer x MIX_GAIN x its mix weight (fading to 0 over 10 ms
//     when its own output is patched), summed per side
//   L: the left mix, or (L + R) / sqrt 2 when R is unpatched (crossfaded
//     over 10 ms); R: the right mix
//   a soft limit, knee 8 V, ceiling 10 V, on every output
// With nothing patched nothing is computed.
//
// No allocation and no libm in process(): FastMath only; std::sqrt and
// std::exp run on a sample-rate change. Unusable sample rates (not finite,
// <= 0, above 768 kHz) are ignored and the last good one kept.
#include "Crackle.hpp"
#include "Drift.hpp"
#include "FastMath.hpp"
#include "NoiseLayer.hpp"
#include "Random.hpp"

#include <cmath>
#include <cstdint>

namespace rka {

// NaN and +-inf read 0 (NaN fails both comparisons, even under
// -funsafe-math-optimizations' assumptions about the rest).
inline float hadalFinite(float x) {
	return (x > -1e30f && x < 1e30f) ? x : 0.f;
}

struct HadalPort {
	const float* v = nullptr;
	int channels = 0;
	float first() const {
		return channels > 0 && v ? hadalFinite(v[0]) : 0.f;
	}
};

struct HadalFrame {
	enum {
		DEPTH,
		RUMBLE,
		HISS,
		CRACKLE,
		DRIFT,
		DEPTH_ATT,
		RUMBLE_ATT,
		HISS_ATT,
		CRACKLE_ATT,
		DRIFT_ATT,
		PARAMS_LEN
	};
	// Outputs, in id order.
	enum { OUT_RUMBLE, OUT_HISS, OUT_CRACKLE, OUT_L, OUT_R, OUTPUTS_LEN };
	float params[PARAMS_LEN] = {};
	HadalPort cv[5];
	bool patched[OUTPUTS_LEN] = {};
};

struct HadalResult {
	float rumble = 0, hiss = 0, crackle = 0, l = 0, r = 0;
};

class HadalEngine {
public:
	static constexpr float FULL_V = 1.6f;                            // Hiss RMS at full
	static constexpr float RUMBLE_V = 3.2f;                          // Rumble RMS at full: a +-5 V sine's loudness, peaks into the soft limit
	static constexpr float CRACKLE_GAIN = 5.f;                       // V per shaped unit
	static constexpr float CRACKLE_FULL_RMS = 0.138f;                // the jack (mono sum), measured at noon Depth, 10/s
	static constexpr float MIX_GAIN = 0.5f;
	static constexpr float KNEE = 8.f, CEIL = 10.f;
	static constexpr float DRIFT_LEVEL_DB = 4.f, DRIFT_COLOUR_OCT = 1.f / 3.f;
	static constexpr float DENSITY = 10.f, DRIFT_DENSITY_RATIO = 2.2f;

	explicit HadalEngine(uint32_t seed) : seed_(seed) {
		seedAll();
		setSampleRate(48000.f);
	}

	void setSampleRate(float fs) {
		if (!(fs > 0.f && fs <= 768000.f))
			return;   // keep the last good rate (48 kHz from construction)
		rate_ = fs;
		rumble_.setRate(fs);
		hiss_.setRate(fs);
		crackle_.setRate(fs);
		for (hadal::Wander& w : wanders_)
			w.setRate(fs);
		smooth_ = (float) (1.0 - std::exp(-1.0 / (0.03 * fs)));
		fadeStep_ = 1.f / (0.01f * fs);
		snap_ = true;
		ctl_ = 0;
	}

	// Initialize: reseed and clear every state, keeping the gain tables
	// (Rack holds its engine lock while this runs).
	void reset() {
		seedAll();
		rumble_.clear();
		hiss_.clear();
		ctl_ = 0;
	}

	void process(const HadalFrame& f, HadalResult& out) {
		for (int i = 0; i < 5; i++) {
			const float k = hadalFinite(f.params[i]) + f.cv[i].first() * 0.1f * hadalFinite(f.params[HadalFrame::DEPTH_ATT + i]);
			control_[i] = k >= 0.f ? (k <= 1.f ? k : 1.f) : 0.f;
		}
		const float depth = control_[0], drift = control_[4];
		float gain[3];
		for (int k = 0; k < 3; k++)
			gain[k] = control_[1 + k] * control_[1 + k];
		const bool lr = f.patched[HadalFrame::OUT_L] || f.patched[HadalFrame::OUT_R];
		float wTarget[3];
		for (int k = 0; k < 3; k++)
			wTarget[k] = f.patched[k] ? 0.f : 1.f;
		const float monoTarget = f.patched[HadalFrame::OUT_R] ? 0.f : 1.f;
		if (snap_) {
			depthS_ = depth;
			driftS_ = drift;
			for (int k = 0; k < 3; k++) {
				gainS_[k] = gain[k];
				w_[k] = wTarget[k];
			}
			mono_ = monoTarget;
			snap_ = false;
			ctl_ = 0;
		}
		depthS_ += (depth - depthS_) * smooth_;
		driftS_ += (drift - driftS_) * smooth_;
		for (int k = 0; k < 3; k++) {
			gainS_[k] += (gain[k] - gainS_[k]) * smooth_;
			w_[k] = ramp(w_[k], wTarget[k]);
		}
		mono_ = ramp(mono_, monoTarget);

		bool need[3];
		bool any = false;
		for (int k = 0; k < 3; k++) {
			need[k] = f.patched[k] || lr;
			any = any || need[k];
		}
		if (!any) {
			out = HadalResult();
			mixL_ = mixR_ = 0.f;
			return;
		}
		work_++;

		// Drift: the wanders move every sample; their effect scales with
		// the smoothed Drift amount.
		float lvl[3][2];
		for (int k = 0; k < 3; k++)
			for (int s = 0; s < 2; s++)
				lvl[k][s] = fastDbToGain(DRIFT_LEVEL_DB * driftS_ * wanders_[2 * k + s].next());
		float colour[3];
		for (int k = 0; k < 3; k++)
			colour[k] = DRIFT_COLOUR_OCT * driftS_ * wanders_[6 + k].next();
		const float densityW = wanders_[9].next();
		if (ctl_ == 0) {
			rumble_.setDepthColour(depthS_, colour[0]);
			hiss_.setDepthColour(depthS_, colour[1]);
			crackle_.setDepthColour(depthS_, colour[2]);
			crackle_.setDensity(DENSITY * fastExp2(driftS_ * densityW * 1.1375035f));   // log2(2.2)
		}
		ctl_ = ctl_ + 1 < 32 ? ctl_ + 1 : 0;

		float side[3][2] = {};
		if (need[0])
			rumble_.process(side[0][0], side[0][1]);
		if (need[1])
			hiss_.process(side[1][0], side[1][1]);
		if (need[2]) {
			crackle_.process(side[2][0], side[2][1]);
			side[2][0] = hadal::Crackle::shape(side[2][0]);
			side[2][1] = hadal::Crackle::shape(side[2][1]);
			ticks_ += crackle_.ticked() ? 1 : 0;
		}
		const float scale[3] = {RUMBLE_V, FULL_V, CRACKLE_GAIN};
		float mixL = 0.f, mixR = 0.f, mono[3];
		for (int k = 0; k < 3; k++) {
			const float g = scale[k] * gainS_[k];
			const float l = side[k][0] * g * lvl[k][0], r = side[k][1] * g * lvl[k][1];
			// A layer's own jack: its left side (Rumble and Hiss, whose sides
			// are alike), or Crackle's mono sum, so no panned tick goes missing.
			mono[k] = k == 2 ? (l + r) * 0.70710678f : l;
			mixL += MIX_GAIN * w_[k] * l;
			mixR += MIX_GAIN * w_[k] * r;
		}
		mixL_ = mixL;
		mixR_ = mixR;
		const float l = mono_ * (mixL + mixR) * 0.70710678f + (1.f - mono_) * mixL;
		out.rumble = limit(mono[0]);
		out.hiss = limit(mono[1]);
		out.crackle = limit(mono[2]);
		out.l = limit(l);
		out.r = limit(mixR);
	}

	// For the tests and the bench.
	float control(int i) const {
		return control_[i];
	}
	float mixL() const {
		return mixL_;
	}
	float mixR() const {
		return mixR_;
	}
	long work() const {
		return work_;
	}
	long limited() const {
		return limited_;
	}
	long tickCount() const {
		return ticks_;
	}

private:
	float ramp(float v, float target) const {
		if (v < target)
			return v + fadeStep_ < target ? v + fadeStep_ : target;
		if (v > target)
			return v - fadeStep_ > target ? v - fadeStep_ : target;
		return v;
	}

	float limit(float x) {
		const float a = x >= 0.f ? x : -x;
		if (a <= KNEE)
			return x;
		limited_++;
		const float room = CEIL - KNEE;
		const float y = KNEE + room * fastTanh((a - KNEE) / room);
		return x >= 0.f ? y : -y;
	}

	void seedAll() {
		// Distinct streams per part, all from the one instance seed.
		rumble_.seed(seed_ * 2654435761u + 1u);
		hiss_.seed(seed_ * 2654435761u + 2u);
		crackle_.seed(seed_ * 2654435761u + 3u);
		for (int i = 0; i < 10; i++)
			wanders_[i].seed(seed_ * 2654435761u + 11u + (uint32_t) i);
		snap_ = true;
		work_ = limited_ = ticks_ = 0;
	}

	uint32_t seed_;
	float rate_ = 48000.f;
	hadal::Rumble rumble_;
	hadal::Hiss hiss_;
	hadal::Crackle crackle_;
	// 0-5: level, per layer per side; 6-8: colour per layer; 9: density.
	hadal::Wander wanders_[10];
	float control_[5] = {};
	float depthS_ = 0.5f, driftS_ = 0.f, gainS_[3] = {}, w_[3] = {1, 1, 1}, mono_ = 0.f;
	float smooth_ = 0.001f, fadeStep_ = 0.002f;
	float mixL_ = 0.f, mixR_ = 0.f;
	bool snap_ = true;
	int ctl_ = 0;
	long work_ = 0, limited_ = 0, ticks_ = 0;
};

} // namespace rka
