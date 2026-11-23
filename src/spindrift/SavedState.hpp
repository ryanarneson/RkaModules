#pragma once
// Spindrift's patch data (the module's dataToJson / dataFromJson), Rack-free
// so its decode rules are tested (TESTING.md rule 5; test_buffer.cpp): the
// fields as a map from key to a kind and a number, which Spindrift.cpp turns
// into JSON and back (BOOL: true/false, INT: an integer, REAL: a real).
//
// Keys (format version 1): version, quality (Beads' Media, 0..3), seedLatched,
// frozen (a frozen buffer is stored in buffer.bin), synth (SynthPolicy),
// gain (GainMode), manualGain (1..40), gateOutput, keepFrozen.
//
// Decoding: a missing version, a version other than 1 (a newer Spindrift's
// patch) or one of the wrong kind gives the defaults throughout. Otherwise
// each field missing, of the wrong kind or out of range takes its default
// alone; the others load.
#include <cmath>
#include <map>
#include <string>

#include "Controls.hpp"
#include "SpindriftEngine.hpp"

namespace spindrift {

struct SavedState {
	int version = 1;
	Quality quality = DIGITAL;
	bool seedLatched = true;  // Beads latches Seed at power-up
	bool frozen = false;
	SynthPolicy synth = SYNTH_AUTO;
	GainMode gain = GAIN_FIXED;
	float manualGain = 1.f;
	bool gateOutput = false;
	bool keepFrozen = true;
};

struct SavedValue {
	enum Kind { BOOL, INT, REAL };
	Kind kind;
	double value;
};
typedef std::map<std::string, SavedValue> SavedMap;

inline SavedMap savedToMap(const SavedState& s) {
	SavedMap m;
	m["version"] = {SavedValue::INT, double(s.version)};
	m["quality"] = {SavedValue::INT, double(int(s.quality))};
	m["seedLatched"] = {SavedValue::BOOL, s.seedLatched ? 1.0 : 0.0};
	m["frozen"] = {SavedValue::BOOL, s.frozen ? 1.0 : 0.0};
	m["synth"] = {SavedValue::INT, double(int(s.synth))};
	m["gain"] = {SavedValue::INT, double(int(s.gain))};
	m["manualGain"] = {SavedValue::REAL, double(s.manualGain)};
	m["gateOutput"] = {SavedValue::BOOL, s.gateOutput ? 1.0 : 0.0};
	m["keepFrozen"] = {SavedValue::BOOL, s.keepFrozen ? 1.0 : 0.0};
	return m;
}

namespace detail {
inline const SavedValue* field(const SavedMap& m, const char* key, SavedValue::Kind kind) {
	auto it = m.find(key);
	return it != m.end() && it->second.kind == kind ? &it->second : nullptr;
}
inline void readBool(const SavedMap& m, const char* key, bool& out) {
	if (const SavedValue* v = field(m, key, SavedValue::BOOL))
		out = v->value != 0.0;
}
// An integer in lo..hi.
inline bool readInt(const SavedMap& m, const char* key, int lo, int hi, int& out) {
	const SavedValue* v = field(m, key, SavedValue::INT);
	if (!v || !(v->value >= lo && v->value <= hi) || v->value != std::floor(v->value))
		return false;
	out = int(v->value);
	return true;
}
}  // namespace detail

inline SavedState savedFromMap(const SavedMap& m) {
	SavedState s;
	int version = 0;
	if (!detail::readInt(m, "version", 1, 1, version))
		return s;
	int i;
	if (detail::readInt(m, "quality", 0, 3, i))
		s.quality = Quality(i);
	detail::readBool(m, "seedLatched", s.seedLatched);
	detail::readBool(m, "frozen", s.frozen);
	if (detail::readInt(m, "synth", 0, 2, i))
		s.synth = SynthPolicy(i);
	if (detail::readInt(m, "gain", 0, 2, i))
		s.gain = GainMode(i);
	if (const SavedValue* v = detail::field(m, "manualGain", SavedValue::REAL))
		if (v->value >= 1.0 && v->value <= 40.0)
			s.manualGain = float(v->value);
	detail::readBool(m, "gateOutput", s.gateOutput);
	detail::readBool(m, "keepFrozen", s.keepFrozen);
	return s;
}

}  // namespace spindrift
