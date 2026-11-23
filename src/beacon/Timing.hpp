#pragma once
// Beacon's time law (Attack, Decay, Release) and typed values. Rack-free.
// The per-sample functions use no libm.
#include "FastMath.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace rka {
namespace beacon {

constexpr float kMinSeconds = 0.001f, kMaxSeconds = 60.f;
constexpr float kLog2Span = 15.8726744f;   // log2(60000)

// 1 ms at 0, 60 s at 1, log. A NaN knob reads noon.
inline float stageSeconds(float knob) {
	if (knob != knob)
		knob = 0.5f;
	const float k = knob >= 0.f ? (knob <= 1.f ? knob : 1.f) : 0.f;
	return kMinSeconds * fastExp2(k * kLog2Span);
}

// The knob plus the CV (10 V is the knob's travel); NaN volts are ignored.
inline float stageSecondsCv(float knob, float volts) {
	if (volts != volts)
		volts = 0.f;
	return stageSeconds(knob + volts * 0.1f);
}

// The knob for a time (typed values), clamped to 0..1; NaN reads 0.
inline float stageKnob(float seconds) {
	if (!(seconds > kMinSeconds))
		return 0.f;
	if (seconds >= kMaxSeconds)
		return 1.f;
	return fastLog2(seconds / kMinSeconds) / kLog2Span;
}

// "1.0 ms" .. "999 ms", "1.00 s" .. "9.99 s", "10.0 s" .. "60.0 s".
inline void formatSeconds(float seconds, char* buf, int n) {
	if (!buf || n <= 0)
		return;
	const float s = seconds > kMinSeconds ? (seconds < kMaxSeconds ? seconds : kMaxSeconds) : kMinSeconds;
	const float ms = s * 1000.f;
	if (ms < 9.95f)
		std::snprintf(buf, (std::size_t) n, "%.1f ms", (double) ms);
	else if (ms < 999.5f)
		std::snprintf(buf, (std::size_t) n, "%.0f ms", (double) ms);
	else if (s < 9.995f)
		std::snprintf(buf, (std::size_t) n, "%.2f s", (double) s);
	else
		std::snprintf(buf, (std::size_t) n, "%.1f s", (double) s);
}

// A typed value is milliseconds, or seconds if it ends in "s" alone
// ("1.5 s"); "250" and "250 ms" are both a quarter of a second. The result
// is not clamped (stageKnob clamps). False on anything else.
inline bool parseSeconds(const char* s, float& seconds) {
	if (!s)
		return false;
	char* end = nullptr;
	const float v = std::strtof(s, &end);
	if (end == s || v != v)
		return false;
	while (*end == ' ')
		end++;
	bool secs = false;
	if (*end == 's' || *end == 'S') {
		secs = true;
		end++;
	} else if ((*end == 'm' || *end == 'M') && (end[1] == 's' || end[1] == 'S')) {
		end += 2;
	}
	while (*end == ' ')
		end++;
	if (*end != '\0')
		return false;
	seconds = secs ? v : v / 1000.f;
	return true;
}

} // namespace beacon
} // namespace rka
