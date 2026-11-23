// The saved-patch rule for Fog's mode, driftRate and lowCut
// (SavedState.hpp): what a value Fog.cpp found in the JSON loads as.
// Values reach the rule as Jansson's json_number_value gives them: a
// double, from a JSON integer or real.
#include <climits>

#include "SavedState.hpp"
#include "testing.hpp"

using rka::fog::savedSetting;

namespace {

// Settings and their ranges, as Fog.cpp reads them. current is a value
// away from both ends where the range allows, so "left as it is" is told
// apart from a clamp.
struct Field {
	const char* name;
	int lo, hi, current;
};
const Field FIELDS[] = {{"mode", 0, 3, 2}, {"driftRate", 0, 1, 1}, {"lowCut", 0, 2, 1}};

void testInRange() {
	for (const Field& f : FIELDS)
		for (int v = f.lo; v <= f.hi; v++)
			CHECK(savedSetting(true, (double) v, f.lo, f.hi, f.current) == v, "%s: %d (an integer, or a real %d.0)",
			      f.name, v, v);
	// Reals between settings round to the nearest.
	CHECK(savedSetting(true, 1.9999, 0, 3, 0) == 2, "mode 1.9999 is 2");
	CHECK(savedSetting(true, 2.4, 0, 3, 0) == 2, "mode 2.4 is 2");
	CHECK(savedSetting(true, 0.6, 0, 2, 0) == 1, "lowCut 0.6 is 1");
}

void testClampedBeforeNarrowing() {
	// 2^32 + 1 cut to an int is 1; clamped first it is the top setting.
	const double big = 4294967297.0;
	CHECK(savedSetting(true, big, 0, 3, 0) == 3, "mode 4294967297 is ICE (3), got %d", savedSetting(true, big, 0, 3, 0));
	CHECK(savedSetting(true, big, 0, 1, 0) == 1, "driftRate 4294967297 is 1");
	CHECK(savedSetting(true, big, 0, 2, 0) == 2, "lowCut 4294967297 is 2");
	// 2^32 cut to an int is 0.
	CHECK(savedSetting(true, 4294967296.0, 0, 3, 0) == 3, "mode 4294967296 is 3");
	CHECK(savedSetting(true, (double) LLONG_MAX, 0, 3, 1) == 3, "mode LLONG_MAX is 3");
	CHECK(savedSetting(true, (double) LLONG_MIN, 0, 3, 1) == 0, "mode LLONG_MIN is 0");
	CHECK(savedSetting(true, -4294967295.0, 0, 3, 2) == 0, "mode -4294967295 is 0");
	CHECK(savedSetting(true, 4.0, 0, 3, 1) == 3, "mode 4 is 3");
	CHECK(savedSetting(true, -1.0, 0, 3, 2) == 0, "mode -1 is 0");
	CHECK(savedSetting(true, 1e300, 0, 2, 0) == 2, "lowCut 1e300 is 2");
	CHECK(savedSetting(true, -1e300, 0, 2, 2) == 0, "lowCut -1e300 is 0");
}

void testNotANumber() {
	for (const Field& f : FIELDS) {
		// Missing, or a string / boolean / null: the setting stays.
		CHECK(savedSetting(false, 0.0, f.lo, f.hi, f.current) == f.current, "%s: not a number keeps %d", f.name,
		      f.current);
		CHECK(savedSetting(true, __builtin_nan(""), f.lo, f.hi, f.current) == f.current, "%s: NaN keeps %d", f.name, f.current);
	}
}

} // namespace

int main() {
	testInRange();
	testClampedBeforeNarrowing();
	testNotANumber();
	return rkatest::finish("test_saved");
}
