#pragma once
// Neap's saved menu settings, parsed without Rack. Neap.cpp turns each
// json_t* into a JsonValue (missing, boolean, integer or anything else) and
// this decides what to keep. Anything that does not fit gives the default.
#include <stdint.h>

namespace rka {
namespace neap {

struct JsonValue {
	enum Kind { MISSING, BOOLEAN, INTEGER, OTHER };
	Kind kind = MISSING;
	bool b = false;
	int64_t i = 0;

	static JsonValue missing() { return JsonValue(); }
	static JsonValue boolean(bool v) { JsonValue j; j.kind = BOOLEAN; j.b = v; return j; }
	static JsonValue integer(int64_t v) { JsonValue j; j.kind = INTEGER; j.i = v; return j; }
	static JsonValue other() { JsonValue j; j.kind = OTHER; return j; }
};

struct MenuState {
	bool drift = true;
	int wave = 0;  // 0 Triangle, 1 Sine
	int level = 0; // 0 +-5 V, 1 0-10 V
};

// Drift: only a JSON boolean counts; a missing key or any other type keeps
// the default (on). Wave and Level: only the integer 1 selects the second
// choice; everything else, including 2, -1 and a huge integer, is the first.
inline MenuState parseMenu(const JsonValue& drift, const JsonValue& wave, const JsonValue& level) {
	MenuState m;
	if (drift.kind == JsonValue::BOOLEAN)
		m.drift = drift.b;
	if (wave.kind == JsonValue::INTEGER && wave.i == 1)
		m.wave = 1;
	if (level.kind == JsonValue::INTEGER && level.i == 1)
		m.level = 1;
	return m;
}

} // namespace neap
} // namespace rka
