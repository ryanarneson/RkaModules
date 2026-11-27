// What Doldrums' panel says (Readout.hpp): the tooltips' texts and their
// inverses, and the part of the tape the window draws bright, against
// values worked out by hand from the design and the plan's laws.
//
// - stretch: the tooltip at the lettered marks and the stop; a typed figure
//   lands on the dial where paceFromDial gives that figure back.
// - root: C2 at the default, C4 at 0, C1 and C7 at the ends, sharps; typed
//   notes and numbers.
// - width_shift: 25 cents at the default; 5 and 300 at the ends; typed cents
//   back to the knob; Shift's sign and its detent.
// - position: "now" at 1; seconds back at 0.5 and 0 from the lag law; typed
//   seconds back to the knob.
// - to_play: stopped, held, frozen, recording at x8 and x1, by hand.
#include "doldrums/Readout.hpp"
#include "testing.hpp"

#include <cmath>
#include <string>

namespace {

using namespace rka::doldrums;

std::string stretchAt(float dial) {
	char b[16];
	stretchText(paceFromDial(dial), b, sizeof b);
	return b;
}

void stretch() {
	CHECK(stretchAt(0.f) == "x1.0", "x1.0 at the start: %s", stretchAt(0.f).c_str());
	CHECK(stretchAt(0.5f) == "x8.0", "x8.0 at the default: %s", stretchAt(0.5f).c_str());
	CHECK(stretchAt(5.f / 6.f) == "x32.0", "x32.0 at 5/6: %s", stretchAt(5.f / 6.f).c_str());
	CHECK(stretchAt(1.f) == "held", "held at the stop: %s", stretchAt(1.f).c_str());
	// Typed figures come back as the same figure.
	const double figures[] = {1.0, 2.0, 8.0, 16.0, 32.0, 64.0, 128.0, 1000.0};
	for (double s : figures) {
		const float t = dialForStretch(s);
		const double back = 1.0 / paceFromDial(t);
		CHECK(std::fabs(back / s - 1.0) < 1e-4, "typed x%g reads back as x%g", s, back);
	}
	float d = -1.f;
	CHECK(parseStretch("x8", d) && std::fabs(d - 0.5f) < 1e-6f, "\"x8\" is the default: %g", d);
	CHECK(parseStretch(" 128 ", d) && std::fabs(1.0 / paceFromDial(d) - 128.0) < 0.01, "\"128\": %g", d);
	CHECK(parseStretch("\xc3\x97" "4", d) && std::fabs(d - 1.f / 3.f) < 1e-6f, "\"\xc3\x97" "4\": %g", d);
	CHECK(parseStretch("held", d) && d == 1.f && parseStretch("INF", d) && d == 1.f, "held and inf are the stop");
	CHECK(parseStretch("0.5", d) && d == 0.f, "under 1 is the start: %g", d);
	CHECK(!parseStretch("fast", d), "words it does not know are refused");
}

void root() {
	CHECK(noteName(-24.f) == "C2", "the default is C2: %s", noteName(-24.f).c_str());
	CHECK(noteName(0.f) == "C4", "0 is C4: %s", noteName(0.f).c_str());
	CHECK(noteName(-36.f) == "C1" && noteName(36.f) == "C7", "the ends are C1 and C7: %s %s", noteName(-36.f).c_str(),
	      noteName(36.f).c_str());
	CHECK(noteName(9.f) == "A4" && noteName(-23.f) == "C#2" && noteName(-13.f) == "B2", "A4, C#2, B2: %s %s %s",
	      noteName(9.f).c_str(), noteName(-23.f).c_str(), noteName(-13.f).c_str());
	CHECK(noteName(-24.4f) == "C2" && noteName(-23.4f) == "C#2", "rounded to the semitone");
	float s = 0.f;
	CHECK(parseNote("C2", s) && s == -24.f, "C2: %g", s);
	CHECK(parseNote("a4", s) && s == 9.f, "a4: %g", s);
	CHECK(parseNote("F#3", s) && s == -6.f, "F#3: %g", s);
	CHECK(parseNote("Bb1", s) && s == -26.f, "Bb1: %g", s);
	CHECK(parseNote("C-1", s) && s == -60.f, "C-1: %g", s);
	CHECK(parseNote("-24", s) && s == -24.f, "a number of semitones: %g", s);
	CHECK(!parseNote("H2", s) && !parseNote("C", s) && !parseNote("", s), "not notes are refused");
}

void width_shift() {
	CHECK(widthText(0.393f) == "25 cents", "the default is 25 cents: %s", widthText(0.393f).c_str());
	CHECK(widthText(0.f) == "5 cents" && widthText(1.f) == "300 cents", "5 and 300 at the ends: %s %s", widthText(0.f).c_str(),
	      widthText(1.f).c_str());
	CHECK(std::fabs(widthCents(widthForCents(25.0)) - 25.f) < 0.05f, "25 cents typed: %g", double(widthCents(widthForCents(25.0))));
	CHECK(widthForCents(1.0) == 0.f && widthForCents(1000.0) == 1.f, "typed outside the range: the ends");
	// Typed (M2 of the final review): what does not parse leaves the knob alone.
	float w = -1.f;
	CHECK(parseWidth("25", w) && std::fabs(widthCents(w) - 25.f) < 0.05f && parseWidth(" 100 cents", w) &&
	          std::fabs(widthCents(w) - 100.f) < 0.1f,
	      "a width typed in cents");
	w = -1.f;
	CHECK(!parseWidth("wide", w) && !parseWidth("", w) && !parseWidth("nan", w) && w == -1.f, "a width that is no number is refused");
	float h = -1.f;
	CHECK(parseHarmonics("50", h) && h == 0.5f && parseHarmonics("100 %", h) && h == 1.f, "a Harmonics typed in percent");
	h = -1.f;
	CHECK(!parseHarmonics("full", h) && !parseHarmonics("", h) && !parseHarmonics("nan", h) && h == -1.f, "a Harmonics that is no number is refused");
	CHECK(shiftText(0.f) == "0" && shiftText(0.2f) == "0" && shiftText(-0.25f) == "0", "0 in the detent");
	CHECK(shiftText(7.f) == "+7.0" && shiftText(-12.f) == "-12.0", "signed outside it: %s %s", shiftText(7.f).c_str(),
	      shiftText(-12.f).c_str());
}

void position() {
	const float rate = 48000.f;
	const long long cap = tapeFrames(rate);
	const int grain = grainFrames(GRAIN_MEDIUM, rate);   // 16384
	CHECK(positionText(1.f, cap, grain, rate) == "now", "1 is now: %s", positionText(1.f, cap, grain, rate).c_str());
	// 0.5: lag = round(0.5 (1440000 - 16384)) = 711808 frames = 14.829 s.
	CHECK(positionText(0.5f, cap, grain, rate) == "14.8 s ago", "0.5: %s", positionText(0.5f, cap, grain, rate).c_str());
	// 0: 1423616 frames = 29.659 s.
	CHECK(positionText(0.f, cap, grain, rate) == "29.7 s ago", "0: %s", positionText(0.f, cap, grain, rate).c_str());
	float p = -1.f;
	CHECK(parsePosition("14.829", cap, grain, rate, p) && std::fabs(p - 0.5f) < 1e-4f, "14.829 s typed: %g", p);
	CHECK(parsePosition("now", cap, grain, rate, p) && p == 1.f, "now typed");
	CHECK(parsePosition("100", cap, grain, rate, p) && p == 0.f, "too far back: the oldest");
	CHECK(!parsePosition("soon", cap, grain, rate, p), "words refused");
}

TapeSnapshot snap(float readEnd, bool frozen, bool held, bool stopped) {
	TapeSnapshot s = {};
	s.readEnd = readEnd;
	s.readStart = readEnd - 0.01f;
	s.anchor = 0.5f;
	s.frozen = frozen;
	s.held = held;
	s.stopped = stopped;
	return s;
}

void to_play() {
	float a, b;
	toPlay(snap(0.4f, true, false, true), 0.125, a, b);
	CHECK(a > b, "stopped: nothing bright (%g..%g)", a, b);
	toPlay(snap(0.4f, true, true, false), 0.0, a, b);
	CHECK(a == 0.4f - 0.01f && b == 0.4f, "held: the slice (%g..%g)", a, b);
	toPlay(snap(0.4f, true, false, false), 0.125, a, b);
	CHECK(a == 0.4f && b == 1.f, "frozen: to now (%g..%g)", a, b);
	// Recording at x8 from 0.4: meets the oldest edge after 0.4 / (7/8) of the
	// tape, having passed 0.4 / 7 of sound: to 0.4571.
	toPlay(snap(0.4f, false, false, false), 0.125, a, b);
	CHECK(a == 0.4f && std::fabs(b - 0.457142857f) < 1e-6f, "recording at x8: to 0.4571 (%g)", b);
	toPlay(snap(0.4f, false, false, false), 1.0, a, b);
	CHECK(b == 1.f, "recording at x1: to now (%g)", b);
	// Following (While recording: Follow), the read point stays on the flag
	// at any pace: to now, as at x1.
	TapeSnapshot fo = snap(0.4f, false, false, false);
	fo.following = true;
	toPlay(fo, 0.125, a, b);
	CHECK(a == 0.4f && b == 1.f, "following at x8: to now (%g..%g)", a, b);
	toPlay(fo, 0.0, a, b);
	CHECK(a == 0.4f && b == 1.f, "following at the Stretch stop: to now, not the held slice (%g..%g)", a, b);
	toPlay(snap(0.8f, false, false, false), 0.5, a, b);
	CHECK(b == 1.f, "recording at x2 from 0.8: cut at now (%g)", b);
	toPlay(snap(-0.2f, false, false, false), 0.125, a, b);
	CHECK(b <= 0.f || a > b, "a read point off the edge plays nothing on the glass (%g..%g)", a, b);
}

// The window's timer: while frozen, the time the pass will still play.
void pass_time() {
	char t[16];
	auto text = [&](const TapeSnapshot& s, double pace, double secs = 30.0) {
		passTimeText(t, sizeof t, s, pace, secs);
		return std::string(t);
	};
	// Frozen, travelling: from the read point to the end, over the pace.
	CHECK(text(snap(0.0f, true, false, false), 0.125) == "4:00", "the whole tape at x8: 4:00 (%s)", t);
	CHECK(text(snap(0.5f, true, false, false), 0.125) == "2:00", "half of it at x8: 2:00 (%s)", t);
	CHECK(text(snap(0.9f, true, false, false), 1.0) == "0:03", "3 s at x1: 0:03 (%s)", t);
	CHECK(text(snap(0.9f, true, false, false), 0.125) == "0:24", "3 s at x8: 0:24 (%s)", t);
	CHECK(text(snap(0.99f, true, false, false), 1.0) == "0:01", "0.3 s left: rounded up, 0:01 (%s)", t);
	CHECK(text(snap(1.f, true, false, false), 0.125) == "0:00", "at the end: 0:00 (%s)", t);
	CHECK(text(snap(0.f, true, false, false), 1.0 / 128.0) == "1:04:00", "the whole tape at x128: 1:04:00 (%s)", t);
	CHECK(text(snap(0.f, true, false, false), 30.0 / 359999.0) == "99:59:59", "just under 100 hours (%s)", t);
	CHECK(text(snap(0.f, true, false, false), 1e-9) == ">99 h", "100 hours or more: >99 h (%s)", t);
	CHECK(text(snap(0.5f, true, false, false), 0.125, 27.5625) == "1:51", "a 27.56 s tape (a rate with a shorter one): 1:51 (%s)", t);
	// Stopped: the pass a trigger would play, from the anchor (0.5 in snap()).
	CHECK(text(snap(0.2f, true, false, true), 0.125) == "2:00", "stopped: from the anchor, 2:00 (%s)", t);
	// Held, not frozen, no pace, no tape: nothing.
	CHECK(text(snap(0.4f, true, true, false), 0.125).empty(), "held: nothing (%s)", t);
	CHECK(text(snap(0.4f, true, false, false), 0.0).empty(), "the Stretch stop: nothing (%s)", t);
	CHECK(text(snap(0.4f, false, false, false), 0.125).empty(), "not frozen: nothing (%s)", t);
	CHECK(text(snap(0.4f, false, false, true), 0.125).empty(), "stopped, not frozen: nothing (%s)", t);
	CHECK(text(snap(0.4f, true, false, false), 0.125, 0.0).empty(), "no tape: nothing (%s)", t);
	// Hostile figures: off the glass is held to it; NaN says nothing; a pace
	// above 1 reads 1.
	CHECK(text(snap(-0.5f, true, false, false), 0.125) == "4:00", "a read point left of the glass: the whole tape (%s)", t);
	CHECK(text(snap(1.5f, true, false, false), 0.125) == "0:00", "right of it: 0:00 (%s)", t);
	CHECK(text(snap(std::nanf(""), true, false, false), 0.125).empty(), "NaN: nothing (%s)", t);
	CHECK(text(snap(0.f, true, false, false), 5.0) == "0:30", "a pace above 1 reads 1 (%s)", t);
	CHECK(text(snap(0.f, true, false, false), std::nan("")).empty(), "a NaN pace: nothing (%s)", t);
	char small[4] = "zzz";
	passTimeText(small, 1, snap(0.f, true, false, false), 0.125, 30.0);
	CHECK(small[0] == 0 && small[1] == 'z', "a buffer of one char: empty, nothing written past it");
}

} // namespace

int main() {
	stretch();
	root();
	width_shift();
	position();
	to_play();
	pass_time();
	return rkatest::finish("doldrums/test_readout");
}
