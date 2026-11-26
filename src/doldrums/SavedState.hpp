#pragma once
// Doldrums' patch data (the module's dataToJson / dataFromJson), Rack-free so
// its decode rules are tested (TESTING.md rule 5; test_saved.cpp), after
// Spindrift's SavedState.hpp: the fields as a map from key to a kind and a
// number, which Doldrums.cpp turns into JSON and back (BOOL: true/false, INT:
// an integer).
//
// Keys (the plan's "Menu and saved state"): grain (0 Short, 1 Medium, 2 Long;
// default 1), wide (default false), atEnd (0 go round, 1 stop; default 0),
// keepTape (default true). The knobs and the Freeze button are Rack's params
// and are saved by Rack; the frozen tape is tape.wav (TapeStore.hpp); the
// seed is not saved. There is no version key: a later format that needs one
// adds it, and its absence then means this one.
//
// Decoding: each field missing, of the wrong kind or out of range takes its
// default alone; the others load. Keys it does not know are ignored.
#include <cmath>
#include <map>
#include <string>

namespace rka { namespace doldrums {

struct SavedState {
	int grain = 1;          // GrainSize
	bool wide = false;
	bool stopAtEnd = false; // "atEnd": 1
	bool keepTape = true;
};

struct SavedValue {
	enum Kind { BOOL, INT, REAL };
	Kind kind;
	double value;
};
typedef std::map<std::string, SavedValue> SavedMap;

inline SavedMap savedToMap(const SavedState& s) {
	SavedMap m;
	m["grain"] = {SavedValue::INT, double(s.grain)};
	m["wide"] = {SavedValue::BOOL, s.wide ? 1.0 : 0.0};
	m["atEnd"] = {SavedValue::INT, s.stopAtEnd ? 1.0 : 0.0};
	m["keepTape"] = {SavedValue::BOOL, s.keepTape ? 1.0 : 0.0};
	return m;
}

namespace saved {
inline const SavedValue* field(const SavedMap& m, const char* key, SavedValue::Kind kind) {
	SavedMap::const_iterator it = m.find(key);
	return it != m.end() && it->second.kind == kind ? &it->second : nullptr;
}
inline void readBool(const SavedMap& m, const char* key, bool& out) {
	if (const SavedValue* v = field(m, key, SavedValue::BOOL))
		out = v->value != 0.0;
}
// A whole number in lo..hi.
inline bool readInt(const SavedMap& m, const char* key, int lo, int hi, int& out) {
	const SavedValue* v = field(m, key, SavedValue::INT);
	if (!v || !(v->value >= lo && v->value <= hi) || v->value != std::floor(v->value))
		return false;
	out = int(v->value);
	return true;
}
} // namespace saved

inline SavedState savedFromMap(const SavedMap& m) {
	SavedState s;
	int i;
	if (saved::readInt(m, "grain", 0, 2, i))
		s.grain = i;
	saved::readBool(m, "wide", s.wide);
	if (saved::readInt(m, "atEnd", 0, 1, i))
		s.stopAtEnd = i == 1;
	saved::readBool(m, "keepTape", s.keepTape);
	return s;
}

}} // namespace rka::doldrums
