#pragma once
// Squall's engine (spec: docs/specs/2026-11-18-squall-design.md), Rack-free.
// Squall.cpp only maps params, ports, menus and patch state onto it.
//
// Each sample: the controls (every 32 samples, knob + CV / 10 V x att,
// clamped), Clock and Freeze through Schmitt triggers, then, unless frozen,
// the timing:
//   - free-running, a pending replay counts down its stored gap in average
//     gaps (rate / fs a sample); otherwise Gust's process (Events) may fire;
//   - clocked, the same on clock edges: a pending replay counts whole ticks,
//     otherwise each edge asks Events.
// On every event: Step takes the value, the gate and the light fire, Gust is
// excited, the event goes into Memory, and the next one is decided: a
// replay of the event L - 1 back with chance Memory, or a new wait.
// Gaps and gate lengths live in average gaps (1 / rate free; the measured
// tick period / p clocked), so a loop follows Weather and survives a clock
// being patched or unpatched.
#include "FastMath.hpp"
#include "hadal/Random.hpp"
#include "squall/Events.hpp"
#include "squall/Gate.hpp"
#include "squall/Memory.hpp"
#include "squall/SquallTuning.hpp"
#include "squall/Swell.hpp"

#include <cstdint>

namespace rka {

struct SquallPort {
	const float* v = nullptr;
	int channels = 0;

	// Channel 1; unpatched, NaN or inf read as 0.
	float first() const {
		if (channels <= 0 || !v)
			return 0.f;
		const float x = v[0];
		return (x - x == 0.f) ? x : 0.f;
	}
};

struct SquallFrame {
	enum { WEATHER, GUST, MEMORY, WEATHER_ATT, GUST_ATT, MEMORY_ATT, PARAMS_LEN };
	float params[PARAMS_LEN] = {0.5f, 0.3f, 0.f, 0.f, 0.f, 0.f};
	SquallPort cv[3];   // Weather, Gust, Memory
	float clock = 0.f;
	bool clockPatched = false;
	float freeze = 0.f;
	int range = 0;      // 0 +-5 V, 1 0 ... 10 V, 2 +-2 V, 3 +-1 V
	int loopLength = 8;
};

struct SquallResult {
	float step = 0.f, swell = 0.f, gate = 0.f;
	bool light = false;
};

struct SquallState {
	int count = 0;
	squall::Event history[squall::Memory::CAP];
	float step01 = 0.5f, swellStage1 = 0.f, swellOut = 0.f;
};

// Counters and timing the tests read; never used by the module.
struct SquallDebug {
	long long events = 0, replays = 0, gustDuringReplay = 0;
	double waitLeft = 0.0;
};

class SquallEngine {
public:
	explicit SquallEngine(uint32_t seed) {
		rng_.seed(seed);
		events_.seed(seed * 2654435761u + 0x9E3779B9u);
		setSampleRate(48000.f);
		swell_.setState(0.f, 0.f);
	}

	void setSampleRate(float fs) {
		if (!(fs > 0.f && fs <= 768000.f))
			return;
		fs_ = fs;
		events_.setSampleRate(fs);
		gate_.setSampleRate(fs);
		swell_.setSampleRate(fs);
		lightSamples_ = (long long) (LIGHT_S_() * fs + 0.5);
		if (lightSamples_ < 1)
			lightSamples_ = 1;
		ctl_ = 0;
	}

	void reset() {
		memory_.clear();
		gate_.clear();
		step01_ = 0.5f;
		pending_ = false;
		gapAcc_ = 0.0;
		events_.restartWait();
		resetSwell_ = true;
	}

	void process(const SquallFrame& f, SquallResult& r) {
		using namespace squall;
		if (ctl_ <= 0) {
			controls(f);
			ctl_ = 32;
		}
		ctl_--;
		memory_.setLength(f.loopLength);

		// NaN or inf on Clock and Freeze read as 0 V.
		const bool frozen = schmitt(finite(f.freeze), freezeHigh_);
		const bool clocked = f.clockPatched;
		if (clocked != clocked_)
			switchMode(clocked);
		const bool wasHigh = clockHigh_;
		const bool clockNow = clocked ? schmitt(finite(f.clock), clockHigh_) : false;
		const bool edge = clocked && clockNow && !wasHigh;

		// Freeze stops the count since the last edge, so the first edge after
		// a release would measure only the unfrozen samples either side of
		// it: drop that measurement and wait for a fresh pair of edges,
		// keeping the period already known (issue #47).
		if (wasFrozen_ && !frozen) {
			edges_ = 0;
			sinceEdge_ = 0;
		}
		wasFrozen_ = frozen;

		if (!frozen) {
			if (decide_) {
				decide_ = false;
				decideNext();
			}
			if (clocked) {
				sinceEdge_++;
				if (edge) {
					if (edges_ >= 1)
						period_ = sinceEdge_ / (double) fs_;
					edges_++;
					sinceEdge_ = 0;
					gapAcc_ += p_;
					if (pending_) {
						events_.tick(false);
						if (--ticksLeft_ <= 0)
							fire(pend_, true);
					}
					else if (events_.tick()) {
						fire(Event(), false);
					}
				}
			}
			else {
				const double adv = rate_ / fs_;
				gapAcc_ += adv;
				if (pending_) {
					if (events_.stepFree(false))
						dbg_.gustDuringReplay++;
					replayLeft_ -= adv;
					if (replayLeft_ <= 0.0)
						fire(pend_, true);
				}
				else if (events_.stepFree()) {
					fire(Event(), false);
				}
			}
		}

		const float stepV = map(step01_, f.range);
		if (resetSwell_) {
			resetSwell_ = false;
			swell_.setState(stepV, stepV);
		}
		swell_.setTarget(stepV);
		swell_.setFrozen(frozen);
		r.step = stepV;
		r.swell = swell_.process();
		r.gate = gate_.process() ? 10.f : 0.f;
		r.light = lightLeft_ > 0;
		if (lightLeft_ > 0)
			lightLeft_--;
	}

	void getState(SquallState& s) const {
		s.count = memory_.count();
		for (int i = 0; i < s.count; i++)
			s.history[i] = memory_.at(i);
		s.step01 = step01_;
		s.swellStage1 = swell_.stage1();
		s.swellOut = swell_.out();
	}

	// Bad fields load as defaults; the next event is decided afresh.
	void setState(const SquallState& s) {
		memory_.clear();
		const int count = (s.count >= 0 && s.count <= squall::Memory::CAP) ? s.count : 0;
		for (int i = 0; i < count; i++) {
			squall::Event e = s.history[i];
			const squall::Event d;
			if (!(e.gap > 0.f && e.gap <= 1e6f))
				e.gap = d.gap;
			if (!(e.value >= 0.f && e.value <= 1.f))
				e.value = d.value;
			if (!(e.length > 0.f && e.length <= 1e6f))
				e.length = d.length;
			memory_.push(e);
		}
		step01_ = (s.step01 >= 0.f && s.step01 <= 1.f) ? s.step01 : 0.5f;
		const bool swellOk = s.swellStage1 >= -10.f && s.swellStage1 <= 10.f && s.swellOut >= -10.f && s.swellOut <= 10.f;
		if (swellOk) {
			swell_.setState(s.swellStage1, s.swellOut);
			resetSwell_ = false;
		}
		else {
			resetSwell_ = true;
		}
		pending_ = false;
		gapAcc_ = 0.0;
		events_.restartWait();
		decide_ = true;
	}

	SquallDebug debug() const {
		SquallDebug d = dbg_;
		d.waitLeft = events_.waitLeft();
		return d;
	}

private:
	static float LIGHT_S_() {
		return squall::LIGHT_S;
	}

	static float clamp01(float x) {
		return x > 0.f ? (x < 1.f ? x : 1.f) : 0.f;
	}

	static float map(float v01, int range) {
		switch (range) {
			case 1: return v01 * 10.f;
			case 2: return v01 * 4.f - 2.f;
			case 3: return v01 * 2.f - 1.f;
			default: return v01 * 10.f - 5.f;
		}
	}

	static float finite(float v) {
		return (v - v == 0.f) ? v : 0.f;
	}

	// A Schmitt trigger at 0.1 V and 1 V; NaN reads as low.
	static bool schmitt(float v, bool& high) {
		if (high) {
			if (!(v >= 0.1f))
				high = false;
		}
		else if (v >= 1.f) {
			high = true;
		}
		return high;
	}

	void controls(const SquallFrame& f) {
		const float w = clamp01(f.params[SquallFrame::WEATHER] + f.cv[0].first() * 0.1f * f.params[SquallFrame::WEATHER_ATT]);
		const float g = clamp01(f.params[SquallFrame::GUST] + f.cv[1].first() * 0.1f * f.params[SquallFrame::GUST_ATT]);
		m_ = clamp01(f.params[SquallFrame::MEMORY] + f.cv[2].first() * 0.1f * f.params[SquallFrame::MEMORY_ATT]);
		events_.setWeather(w);
		events_.setGust(g);
		rate_ = events_.rate();
		p_ = squall::tickChance(w);
		swell_.setGap(averageGap());
	}

	// In seconds.
	float averageGap() const {
		if (clocked_ && period_ > 0.0)
			return (float) (period_ / p_);
		return (float) (1.0 / rate_);
	}

	void switchMode(bool clocked) {
		if (pending_) {
			if (clocked) {
				const double t = replayLeft_ / p_ + 0.5;
				ticksLeft_ = t >= 1.0 ? (long long) t : 1;
			}
			else {
				replayLeft_ = ticksLeft_ * (double) p_;
			}
		}
		clocked_ = clocked;
		clockHigh_ = false;
		edges_ = 0;
		period_ = 0.0;
		sinceEdge_ = 0;
		swell_.setGap(averageGap());
	}

	void fire(const squall::Event& e, bool replay) {
		using namespace squall;
		Event rec;
		if (replay) {
			rec = e;
		}
		else {
			rec.gap = gapAcc_ > 1e-9 ? (float) gapAcc_ : 1e-9f;
			rec.value = rng_.unit();
			rec.length = GATE_FRACTION;
		}
		gapAcc_ = 0.0;
		step01_ = rec.value;
		float seconds;
		if (clocked_ && period_ <= 0.0) {
			seconds = FIRST_CLOCK_GATE_S;
		}
		else {
			seconds = rec.length * averageGap();
			seconds = seconds > GATE_MIN_S ? (seconds < GATE_MAX_S ? seconds : GATE_MAX_S) : GATE_MIN_S;
		}
		// The retrigger's drop: 1 ms, or a quarter of the average gap when
		// events come faster than that (a 1 kHz clock).
		const float quarter = 0.25f * averageGap();
		gate_.fire(seconds, quarter < RETRIGGER_S ? quarter : RETRIGGER_S);
		lightLeft_ = lightSamples_;
		events_.excite();
		memory_.push(rec);
		dbg_.events++;
		if (replay)
			dbg_.replays++;
		decideNext();
	}

	void decideNext() {
		const float coin = rng_.unit();
		if (memory_.canReplay() && coin < m_) {
			pending_ = true;
			pend_ = memory_.next();
			if (clocked_) {
				const double t = pend_.gap / p_ + 0.5;
				ticksLeft_ = t >= 1.0 ? (long long) t : 1;
			}
			else {
				replayLeft_ = pend_.gap;
			}
		}
		else {
			pending_ = false;
			events_.restartWait();
		}
	}

	rka::hadal::Random rng_;
	squall::Events events_;
	squall::Memory memory_;
	squall::Gate gate_;
	squall::Swell swell_;
	SquallDebug dbg_;

	float fs_ = 48000.f, m_ = 0.f, p_ = 0.0625f, step01_ = 0.5f;
	double rate_ = 0.1, gapAcc_ = 0.0, replayLeft_ = 0.0, period_ = 0.0;
	long long ticksLeft_ = 0, sinceEdge_ = 0, edges_ = 0, lightLeft_ = 0, lightSamples_ = 4800;
	int ctl_ = 0;
	bool clocked_ = false, clockHigh_ = false, freezeHigh_ = false, wasFrozen_ = false;
	bool pending_ = false, decide_ = false, resetSwell_ = false;
	squall::Event pend_;
};

} // namespace rka
