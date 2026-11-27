#pragma once
// What Doldrums' panel says in words and in the window, Rack-free so it is
// tested (TESTING.md, "UI logic is Rack-free too"; test_readout.cpp): the
// tooltips' texts and their inverses for a typed value, and the part of the
// tape the window draws bright. Control rate only (snprintf, libm): the UI
// thread's, never the audio thread's.
//
// The tooltips say what is heard:
// - Stretch: Laws.hpp's stretchText ("x8.0", "x128", "held"); typed, a
//   figure ("8", "x8", "128") or "held" / "inf", through dialForStretch.
// - Root: the comb's note, "C2" (C4 at 0 semitones; sharps, never flats);
//   typed, a note ("C2", "f#3", "Bb1") or a number of semitones from C4.
// - Width: the tooth's width in cents, "25 cents" (Laws.hpp's widthCents);
//   typed, a number of cents ("25", "25 cents"), through parseWidth.
// - Harmonics: Rack's display in percent with CV's value beside it; typed,
//   a percentage ("50", "50 %"), through parseHarmonics.
// - Shift: semitones with a sign, "+7.0", "-12.0"; "0" inside the detent
//   (Laws.hpp's shiftRatio: exactly no shift within +-0.25).
// - Position: how far back the anchor is, "now" at the right end, else
//   "12.3 s ago" (the lag Laws.hpp's lagFrames makes, at the rate and grain
//   in force); typed, seconds back.
// Partials (a whole number), Tilt (dB per octave), Blur (octaves) and Mix
// (percent; Mix "wet") need no help: Rack's own display with a unit and a
// multiplier says them. Every parser here refuses text that does not begin
// with a number (or one of its words), so the knob is left as it was.
//
// The window's bright part (toPlay): what the pass will still play, as
// fractions of the window (0 = 30 s ago, 1 = now; TapeView.hpp):
// - stopped: nothing (the pass is over until TRIG);
// - held: the held slice itself, [readStart, readEnd];
// - frozen: from the read point to the end of the tape, [readEnd, 1] (the
//   pass ends when the read point arrives at now);
// - recording: from the read point as far as it will get before the oldest
//   sound, about to be recorded over, catches it. The read point moves
//   through the sound at `pace` while the tape's oldest edge (fraction 0)
//   moves at 1; from readEnd = r they meet after r / (1 - pace) of the
//   tape's length, by which time the read point has passed pace r / (1 -
//   pace) of sound: [r, r + pace r / (1 - pace)], cut at 1. At pace 1
//   (Stretch 1) it never returns: [r, 1].
// A span with nothing in it is from > to.
#include "Laws.hpp"
#include "TapeView.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace rka { namespace doldrums {

// ---- Stretch.

// The dial fraction for a stretch: the inverse of paceFromDial. 1 or less is
// the dial's start; 32 is 5/6; above, 1 - sqrt(1 / (1.125 stretch)); infinity
// (or NaN) the stop.
inline float dialForStretch(double stretch) {
	if (!(stretch < 1e300))
		return 1.f;
	if (!(stretch > 1.0))
		return 0.f;
	if (stretch <= 32.0)
		return float(std::log2(stretch) / 6.0);
	return float(1.0 - std::sqrt(1.0 / (1.125 * stretch)));
}

// A typed stretch: "8", "x8", "×8", "8x", "held", "inf"; false if none.
inline bool parseStretch(const std::string& text, float& dial) {
	std::string s;
	for (char c : text)
		if (!std::isspace((unsigned char) c))
			s += char(std::tolower((unsigned char) c));
	if (s == "held" || s == "inf" || s == "infinity" || s == "stop") {
		dial = 1.f;
		return true;
	}
	size_t at = 0;
	if (s.compare(0, 2, "\xc3\x97") == 0)   // the multiplication sign
		at = 2;
	else if (!s.empty() && s[0] == 'x')
		at = 1;
	const char* begin = s.c_str() + at;
	char* end = nullptr;
	const double v = std::strtod(begin, &end);
	if (end == begin || !(v == v))
		return false;
	dial = dialForStretch(v);
	return true;
}

// ---- Root.

// The note `semis` from C4, rounded to a semitone: "C2", "F#5".
inline std::string noteName(float semis) {
	static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	const int n = int(std::floor(double(detail::clampLo(semis, -1000.f, 1000.f)) + 0.5));
	const int midi = 60 + n;
	const int pc = ((midi % 12) + 12) % 12;
	const int octave = (midi - pc) / 12 - 1;
	return std::string(kNames[pc]) + std::to_string(octave);
}

// A typed note ("C2", "c#3", "Bb1", "A-1") or a number of semitones from C4
// ("-24"), as semitones from C4; false if neither.
inline bool parseNote(const std::string& text, float& semis) {
	std::string s;
	for (char c : text)
		if (!std::isspace((unsigned char) c))
			s += c;
	if (s.empty())
		return false;
	static const int kSteps[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
	const char letter = char(std::toupper((unsigned char) s[0]));
	if (letter >= 'A' && letter <= 'G') {
		int pc = kSteps[letter - 'A'];
		size_t i = 1;
		if (i < s.size() && s[i] == '#') {
			pc++;
			i++;
		}
		else if (i < s.size() && (s[i] == 'b' || s[i] == 'B') && i + 1 < s.size()) {
			pc--;
			i++;
		}
		const char* begin = s.c_str() + i;
		char* end = nullptr;
		const long octave = std::strtol(begin, &end, 10);
		if (end == begin || *end != '\0')
			return false;
		semis = float((octave + 1) * 12 + pc - 60);
		return true;
	}
	char* end = nullptr;
	const double v = std::strtod(s.c_str(), &end);
	if (end == s.c_str() || !(v == v))
		return false;
	semis = float(v);
	return true;
}

// ---- Width.

inline std::string widthText(float width) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.0f cents", double(widthCents(width)));
	return buf;
}
// The leading number of a typed value (spaces allowed before it); false if
// there is none.
inline bool leadingNumber(const std::string& text, double& v) {
	const char* begin = text.c_str();
	char* end = nullptr;
	v = std::strtod(begin, &end);
	return end != begin && v == v;
}

// The knob for a width in cents (5 to 300).
inline float widthForCents(double cents) {
	if (!(cents > 5.0))
		return 0.f;
	if (!(cents < 300.0))
		return 1.f;
	return float(std::log(cents / 5.0) / std::log(60.0));
}

// A typed width in cents ("25", "25 cents"); false if none.
inline bool parseWidth(const std::string& text, float& width) {
	double c;
	if (!leadingNumber(text, c))
		return false;
	width = widthForCents(c);
	return true;
}

// ---- Harmonics.

// A typed Harmonics in percent ("50", "50 %"): the knob's 0..1; false if none.
inline bool parseHarmonics(const std::string& text, float& value) {
	double v;
	if (!leadingNumber(text, v) || !(std::fabs(v) < 1e30))
		return false;
	value = float(v / 100.0);
	return true;
}

// ---- Shift.

inline std::string shiftText(float semis) {
	if (shiftRatio(semis) == 1.f)
		return "0";
	char buf[32];
	std::snprintf(buf, sizeof buf, "%+.1f", double(detail::volts(semis, 24.f)));
	return buf;
}

// ---- Position.

// Seconds back from now to the anchor a Position asks for.
inline double positionSeconds(float position, long long capacity, int grain, float rate) {
	if (!(rate > 0.f))
		return 0.0;
	return double(lagFrames(position, capacity, grain)) / double(rate);
}
inline std::string positionText(float position, long long capacity, int grain, float rate) {
	const double s = positionSeconds(position, capacity, grain, rate);
	if (!(s > 0.0))
		return "now";
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.1f s ago", s);
	return buf;
}
// The Position for an anchor `seconds` back ("now" or 0: 1).
inline float positionForSeconds(double seconds, long long capacity, int grain, float rate) {
	const double room = double(capacity - grain);
	if (!(room > 0.0) || !(rate > 0.f) || !(seconds > 0.0))
		return 1.f;
	const double p = 1.0 - seconds * double(rate) / room;
	return float(p < 0.0 ? 0.0 : p);
}
inline bool parsePosition(const std::string& text, long long capacity, int grain, float rate, float& position) {
	std::string s;
	for (char c : text)
		if (!std::isspace((unsigned char) c))
			s += char(std::tolower((unsigned char) c));
	if (s == "now") {
		position = 1.f;
		return true;
	}
	char* end = nullptr;
	const double v = std::strtod(s.c_str(), &end);
	if (end == s.c_str() || !(v == v))
		return false;
	position = positionForSeconds(v, capacity, grain, rate);
	return true;
}

// ---- The window's bright part (see the top of this file).

inline void toPlay(const TapeSnapshot& s, double pace, float& from, float& to) {
	from = 1.f;
	to = 0.f;
	if (s.stopped)
		return;
	if (s.held) {
		from = s.readStart;
		to = s.readEnd;
		return;
	}
	from = s.readEnd;
	if (s.frozen || !(pace < 1.0)) {
		to = 1.f;
		return;
	}
	const double p = pace > 0.0 ? pace : 0.0;
	const double r = s.readEnd > 0.f ? double(s.readEnd) : 0.0;
	const double end = r + p * r / (1.0 - p);
	to = float(end < 1.0 ? end : 1.0);
}

}} // namespace rka::doldrums
