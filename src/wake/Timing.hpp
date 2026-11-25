#pragma once
// Wake's time laws: the Time knob, the clock's multiples, the Ratio between
// the two sides, and the clock follower. Rack-free; no libm.
#include "FastMath.hpp"
#include "wake/WakeTuning.hpp"

#include <cstdint>

namespace rka {
namespace wake {

// 10 ms at 0, 10 s at 1, log.
inline float timeSeconds(float knob) {
	const float k = knob >= 0.f ? (knob <= 1.f ? knob : 1.f) : 0.f;   // NaN -> 0
	return TIME_MIN_S * fastExp2(k * 9.96578428f);                     // log2(1000)
}

// The knob for a time (typed values).
inline float timeKnob(float seconds) {
	if (!(seconds > TIME_MIN_S))
		return 0.f;
	if (seconds >= TIME_MAX_S)
		return 1.f;
	return fastLog2(seconds / TIME_MIN_S) / 9.96578428f;
}

constexpr int kMultiples = 10, kRatios = 7;

inline float clockMultiple(int step) {
	static const float m[kMultiples] = {0.25f, 1.f / 3.f, 0.5f, 2.f / 3.f, 0.75f, 1.f, 1.5f, 2.f, 3.f, 4.f};
	return m[step < 0 ? 0 : step >= kMultiples ? kMultiples - 1 : step];
}

inline const char* clockName(int step) {
	static const char* const n[kMultiples] = {"1/4", "1/3", "1/2", "2/3", "3/4", "x1", "x1.5", "x2", "x3", "x4"};
	return n[step < 0 ? 0 : step >= kMultiples ? kMultiples - 1 : step];
}

// The step a typed multiple names, as the tooltip shows it: a name from
// clockName ("x2", "1/2"), case aside, with " clock" after it or not and
// spaces around it ("x1.5 clock"). -1 if it names none.
inline int clockStepNamed(const char* s) {
	if (!s)
		return -1;
	auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; };
	// Matches word at p (case aside); returns the end, or null.
	auto match = [&](const char* p, const char* word) -> const char* {
		for (; *word; p++, word++)
			if (lower(*p) != *word)
				return nullptr;
		return p;
	};
	while (*s == ' ')
		s++;
	for (int step = 0; step < kMultiples; step++) {
		const char* p = match(s, clockName(step));
		if (!p || (*p != '\0' && *p != ' '))
			continue;
		while (*p == ' ')
			p++;
		if (const char* q = match(p, "clock"))
			p = q;
		while (*p == ' ')
			p++;
		if (*p == '\0')
			return step;
	}
	return -1;
}

// The clock's steps on the knob. x1 is centred on noon, 0.45 to 0.55, so a
// knob set to noon by eye, and the default (300 ms, 0.4924), are x1. The
// five slower steps share 0 to 0.45 (0.1 each, half that for 1/4 at the
// end); the four faster share 0.55 to 1 (0.1286 each, half for x4).
// stepPosition is the knob as a position along the steps: step n is
// n - 0.5 .. n + 0.5.
constexpr float kX1Top = 0.55f, kFastSlope = 3.5f / 0.45f;

inline float stepPosition(float k) {
	return k <= kX1Top ? k * 10.f : 5.5f + (k - kX1Top) * kFastSlope;
}

// The knob for a step (typed multiples): the middle of the step's travel
// (the end, for the steps at the ends).
inline float clockKnob(int step) {
	const int st = step < 0 ? 0 : step >= kMultiples ? kMultiples - 1 : step;
	if (st >= kMultiples - 1)
		return 1.f;
	return st <= 5 ? (float) st * 0.1f : kX1Top + ((float) st - 5.5f) / kFastSlope;
}

// The step for the knob plus the CV (volts times the attenuverter; 10 V is
// the knob's travel). Noon is x1.
inline int clockStep(float knob, float cvTimesAtt) {
	float k = knob + cvTimesAtt * 0.1f;
	k = k >= 0.f ? (k <= 1.f ? k : 1.f) : 0.f;   // NaN -> 0
	const int st = (int) (stepPosition(k) + 0.5f);
	return st < kMultiples ? st : kMultiples - 1;
}

// The two sides' times for a Ratio setting 0..6 (3 is equal). Left of 3 the
// left side is shorter, right of it the right side; neither under 10 ms.
inline void ratioTimes(int ratio, float t, float& left, float& right) {
	static const float f[3] = {0.5f, 2.f / 3.f, 0.75f};
	const int r = ratio < 0 ? 0 : ratio >= kRatios ? kRatios - 1 : ratio;
	left = right = t;
	if (r < 3)
		left = t * f[r];
	else if (r > 3)
		right = t * f[6 - r];
	left = left >= TIME_MIN_S ? left : TIME_MIN_S;
	right = right >= TIME_MIN_S ? right : TIME_MIN_S;
}

inline const char* ratioName(int ratio) {
	static const char* const n[kRatios] = {"L 1:2", "L 2:3", "L 3:4", "1:1", "R 3:4", "R 2:3", "R 1:2"};
	return n[ratio < 0 ? 0 : ratio >= kRatios ? kRatios - 1 : ratio];
}

// A time brought into 10 ms .. tMax by octaves: halved while over, doubled
// while under. NaN and nonsense read 10 ms.
inline float fitTime(float seconds, float tMax) {
	if (!(tMax >= TIME_MIN_S))
		tMax = TIME_MIN_S;
	if (!(seconds > 0.f))
		return TIME_MIN_S;
	if (!(seconds < 1e6f))
		return tMax;
	while (seconds > tMax)
		seconds *= 0.5f;
	while (seconds < TIME_MIN_S)
		seconds *= 2.f;
	return seconds <= tMax ? seconds : tMax;
}

// The clock's period: samples between the last two rising edges (0.1 V /
// 1 V Schmitt trigger). A stopped clock keeps its period, and when it
// starts again the pause is not taken for a period: an edge more than
// PAUSE_PERIODS periods after the last one only starts the count afresh.
// A clock that really slows by more than that is followed a tick late: a
// second long interval in a row, within SAME_TEMPO of the first, is the
// new period.
struct ClockFollower {
	void reset() {
		high_ = seen_ = locked_ = false;
		count_ = 0;
		period_ = longGap_ = 0.f;
	}

	static constexpr uint32_t COUNT_MAX = 1u << 30;
	static constexpr float PAUSE_PERIODS = 4.f, SAME_TEMPO = 2.f;

	void process(float volts) {
		if (count_ < COUNT_MAX)
			count_++;
		if (high_) {
			if (volts <= 0.1f)
				high_ = false;
			return;
		}
		if (volts >= 1.f) {
			high_ = true;
			const float n = (float) count_;
			const bool pause = count_ >= COUNT_MAX || (locked_ && n > PAUSE_PERIODS * period_);
			if (seen_ && !pause) {
				period_ = n;
				locked_ = true;
				longGap_ = 0.f;
			}
			else if (pause && count_ < COUNT_MAX) {
				if (longGap_ > 0.f && n <= SAME_TEMPO * longGap_ && n * SAME_TEMPO >= longGap_) {
					period_ = n;   // twice running: the clock has slowed
					longGap_ = 0.f;
				}
				else {
					longGap_ = n;
				}
			}
			seen_ = true;
			count_ = 0;
		}
	}

	bool locked() const {
		return locked_;
	}
	float periodSamples() const {
		return period_;
	}

private:
	bool high_ = false, seen_ = false, locked_ = false;
	uint32_t count_ = 0;   // 32 bits everywhere (long is 32 bits on Windows)
	float period_ = 0.f;
	float longGap_ = 0.f;   // the last interval taken for a pause; 0 if none
};

} // namespace wake
} // namespace rka
