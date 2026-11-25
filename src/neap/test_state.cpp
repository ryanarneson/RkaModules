// Neap's saved menu settings (NeapState.hpp): good values, wrong types,
// missing keys and out-of-range integers.
#include "neap/NeapState.hpp"
#include "testing.hpp"

#include <stdint.h>

namespace {

using rka::neap::JsonValue;
using rka::neap::MenuState;
using rka::neap::parseMenu;

const JsonValue M = JsonValue::missing();

void good_values() {
	MenuState s = parseMenu(JsonValue::boolean(false), JsonValue::integer(1), JsonValue::integer(1));
	CHECK(!s.drift && s.wave == 1 && s.level == 1, "false/1/1 -> %d %d %d", s.drift, s.wave, s.level);
	s = parseMenu(JsonValue::boolean(true), JsonValue::integer(0), JsonValue::integer(0));
	CHECK(s.drift && s.wave == 0 && s.level == 0, "true/0/0 -> %d %d %d", s.drift, s.wave, s.level);
}

void missing_keys_give_defaults() {
	MenuState s = parseMenu(M, M, M);
	CHECK(s.drift && s.wave == 0 && s.level == 0, "missing -> defaults");
}

void drift_only_a_boolean_counts() {
	// 0, 1, a string, null-ish and a real are not booleans: Drift stays on.
	CHECK(parseMenu(JsonValue::integer(0), M, M).drift, "drift 0 stays on");
	CHECK(parseMenu(JsonValue::integer(1), M, M).drift, "drift 1 stays on");
	CHECK(parseMenu(JsonValue::other(), M, M).drift, "drift string stays on");
	CHECK(!parseMenu(JsonValue::boolean(false), M, M).drift, "drift false is off");
	CHECK(parseMenu(JsonValue::boolean(true), M, M).drift, "drift true is on");
}

void wave_and_level_out_of_range() {
	const int64_t bad[] = {2, -1, 256, 4294967297LL, INT64_MAX, INT64_MIN};
	for (int64_t v : bad) {
		MenuState s = parseMenu(M, JsonValue::integer(v), JsonValue::integer(v));
		CHECK(s.wave == 0 && s.level == 0, "int %lld -> wave %d level %d", (long long)v, s.wave, s.level);
	}
	// 4294967297 is 2^32 + 1: a narrowing parse would read it as 1.
	MenuState s = parseMenu(M, JsonValue::other(), JsonValue::other());
	CHECK(s.wave == 0 && s.level == 0, "strings/reals give the first choice");
	s = parseMenu(M, JsonValue::boolean(true), JsonValue::boolean(true));
	CHECK(s.wave == 0 && s.level == 0, "booleans are not integers");
}

void keys_are_independent() {
	MenuState s = parseMenu(JsonValue::boolean(false), JsonValue::other(), JsonValue::integer(1));
	CHECK(!s.drift && s.wave == 0 && s.level == 1, "each key on its own");
}

} // namespace

int main() {
	good_values();
	missing_keys_give_defaults();
	drift_only_a_boolean_counts();
	wave_and_level_out_of_range();
	keys_are_independent();
	return rkatest::finish("neap/test_state");
}
