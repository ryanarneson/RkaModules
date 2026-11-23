#pragma once
// The rule for Fog's settings in a saved patch, without Rack. The module
// (Fog.cpp) reads the JSON; this decides what a value it found means.
// Header-only, Rack-free.
//
// The patch data (the keys and their meaning never change):
//   { "mode": 0..3, "driftRate": 0..1, "lowCut": 0..2, "dryKill": bool }

namespace rka {
namespace fog {

// One of mode, driftRate or lowCut. isNumber: the field is a JSON integer
// or real (value: Jansson's json_number_value, so a 64-bit integer arrives
// whole, not cut to an int). The value is clamped to lo .. hi before it
// becomes an int, then rounded to the nearest setting, so 2.0 is 2 and
// 4294967297 is hi. A field that is missing or not a number (a string,
// a boolean, null) leaves the setting as it is (current).
inline int savedSetting(bool isNumber, double value, int lo, int hi, int current) {
	if (!isNumber || !(value == value))
		return current;
	if (value <= (double) lo)
		return lo;
	if (value >= (double) hi)
		return hi;
	return lo + (int) (value - (double) lo + 0.5);
}

} // namespace fog
} // namespace rka
