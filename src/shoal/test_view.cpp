// Core::scopeView: each app's idle view as data (ScopeView.hpp), checked
// against the firmware's own scope frame and the outputs.
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "testing.hpp"
#include "test_util.hpp"

using shoal::Core;
using shoal::Lanes;
using shoal::ScopeView;
using shoal::ViewKind;

namespace {

struct Frame {
	uint8_t bytes[shoal::kFrameBytes];
	bool at(int x, int y) const { return Core::pixel(bytes, x, y); }
};

Frame scope(Core& c) {
	Frame f;
	c.drawScope(f.bytes);
	return f;
}

ScopeView view(Core& c) {
	ScopeView v;
	std::memset(&v, 0xAA, sizeof v);   // scopeView must write every field
	c.scopeView(v);
	return v;
}

// A trigger on input ch, then time for the app to act and the UI to settle.
void trig(Core& c, int ch) {
	pulse(c, ch, 0.002);
	run(c, 0.03);
}

// The design's table (docs/specs/2026-11-19-shoal-scope-design.md, "The views").
void kinds_and_lanes() {
	const struct { ViewKind kind; Lanes lanes; } want[shoal::kNumApps] = {
		{ViewKind::Pixels, Lanes::Columns4},     // CopierMaschine
		{ViewKind::CircleText, Lanes::None},     // Harrington 1200
		{ViewKind::GridCircle, Lanes::None},     // Automatonnetz
		{ViewKind::Pixels, Lanes::Columns4},     // Quantermain
		{ViewKind::Pixels, Lanes::Halves},       // Meta-Q
		{ViewKind::Traces, Lanes::Rows4},        // Quadraturia
		{ViewKind::XY, Lanes::Halves},           // Low-rents
		{ViewKind::Traces, Lanes::Rows4},        // Piqued
		{ViewKind::Pixels, Lanes::Halves},       // Sequins
		{ViewKind::Traces, Lanes::Rows4},        // Dialectic Ping Pong
		{ViewKind::Pixels, Lanes::Rows4},        // Viznutcracker sweet
		{ViewKind::Pixels, Lanes::None},         // Acid Curds
		{ViewKind::Pixels, Lanes::Columns4Top},  // References
	};
	for (int app = 0; app < shoal::kNumApps; app++) {
		Core c;
		c.seed(1);
		c.selectApp(app);
		run(c, 0.06);
		const ScopeView v = view(c);
		const char* n = Core::appName(app);
		CHECK(v.app == app, "%s: view.app %d", n, v.app);
		CHECK(v.kind == want[app].kind, "%s: kind %d, want %d", n, (int) v.kind, (int) want[app].kind);
		CHECK(v.lanes == want[app].lanes, "%s: lanes %d, want %d", n, (int) v.lanes, (int) want[app].lanes);
		// A trace lane's range is the app's own: the envelopes rest at 0 V
		// (Piqued inverted rests at +5 V and falls to 0), Ping Pong reaches
		// the top of the output range, and Quadraturia swings both sides.
		const float wantLo = app == 5 ? -3.75f : 0.f, wantHi = app == 7 ? 5.f : app == 5 || app == 9 ? 6.25f : 0.f;
		CHECK(v.laneLo == (v.kind == ViewKind::Traces ? wantLo : 0.f) && v.laneHi == wantHi, "%s: lane range %g to %g V", n, v.laneLo,
		      v.laneHi);
		if (v.kind != ViewKind::GridCircle && v.kind != ViewKind::CircleText) {
			ScopeView zero = ScopeView();
			zero.laneLo = v.laneLo;
			zero.laneHi = v.laneHi;
			zero.app = v.app;
			zero.kind = v.kind;
			zero.lanes = v.lanes;
			CHECK(std::memcmp(&v, &zero, sizeof v) == 0, "%s: unused fields must be zero", n);
		}
	}
}

// Automatonnetz draws a disc at the newest cell and a ring at each older
// one (APP_AUTOMATONNETZ.ino: kScreenSaverGridX = 6, Y = 8, cells 12 apart,
// an 8 x 8 bitmap at -3). Both bitmaps light (cx - 2, cy); only the disc's
// own pixels light (cx - 1, cy + 1) unless a path line crosses it.
void automatonnetz_path_matches_frame() {
	Core c;
	c.seed(1);
	c.selectApp(2);
	run(c, 0.06);
	ScopeView prev = view(c);
	int moved = 0, discOnly = 0;
	for (int n = 0; n < 12; n++) {
		trig(c, 0);
		const ScopeView v = view(c);
		const Frame f = scope(c);
		for (int i = 0; i < shoal::kViewPath; i++) {
			CHECK(v.pathX[i] < 5 && v.pathY[i] < 5, "clock %d: path %d off the grid", n, i);
			const int cx = 6 + v.pathX[i] * 12, cy = 8 + v.pathY[i] * 12;
			CHECK(f.at(cx - 2, cy), "clock %d: no disc or ring at path %d (%d, %d)", n, i, v.pathX[i], v.pathY[i]);
		}
		const int cx = 6 + v.pathX[0] * 12, cy = 8 + v.pathY[0] * 12;
		CHECK(f.at(cx - 1, cy + 1) && f.at(cx + 1, cy - 1), "clock %d: the newest cell (%d, %d) has no filled disc", n,
		      v.pathX[0], v.pathY[0]);
		// An older cell that is not also the newest shows a ring, hollow at
		// those two pixels unless a line runs through them.
		for (int i = 1; i < shoal::kViewPath; i++) {
			if (v.pathX[i] == v.pathX[0] && v.pathY[i] == v.pathY[0])
				continue;
			const int ox = 6 + v.pathX[i] * 12, oy = 8 + v.pathY[i] * 12;
			if (!f.at(ox - 1, oy + 1) || !f.at(ox + 1, oy - 1))
				discOnly++;
		}
		if (v.pathX[0] != prev.pathX[0] || v.pathY[0] != prev.pathY[0]) {
			moved++;
			for (int i = 1; i < shoal::kViewPath; i++)
				CHECK(v.pathX[i] == prev.pathX[i - 1] && v.pathY[i] == prev.pathY[i - 1],
				      "clock %d: path %d is not the cell before", n, i);
		}
		prev = v;
	}
	CHECK(moved >= 8, "the playhead moved on only %d of 12 clocks", moved);
	CHECK(discOnly >= 8, "older cells looked like discs: only %d hollow rings seen", discOnly);
}

// The firmware draws a disc at each chord note's place on the circle. The
// place is worked out here from the angle (C at the top, clockwise), not
// from the firmware's table.
void circle_notes_match_frame() {
	const struct { int app, cx, cy; } cases[2] = {{1, 32, 32}, {2, 96, 32}};
	for (const auto& k : cases) {
		Core c;
		c.seed(1);
		c.selectApp(k.app);
		run(c, 0.06);
		int distinct = 0;
		uint8_t last[3] = {255, 255, 255};
		for (int n = 0; n < 12; n++) {
			trig(c, k.app == 1 ? 1 + n % 3 : 0);
			const ScopeView v = view(c);
			const Frame f = scope(c);
			for (int i = 0; i < 3; i++) {
				CHECK(v.notes[i] < 12, "%s: note %d out of range", Core::appName(k.app), v.notes[i]);
				const double a = 6.283185307179586 * v.notes[i] / 12.0;
				const int x = k.cx + (int) std::lround(28.0 * std::sin(a)), y = k.cy - (int) std::lround(28.0 * std::cos(a));
				int litNear = 0;
				for (int dy = -2; dy <= 2; dy++)
					for (int dx = -2; dx <= 2; dx++)
						litNear += f.at(x + dx, y + dy) ? 1 : 0;
				// A disc is 20 pixels; the bare circle and a chord line light
				// far fewer of the 25.
				CHECK(litNear >= 14, "%s trigger %d: no disc at note %d (%d lit near %d, %d)", Core::appName(k.app), n,
				      v.notes[i], litNear, x, y);
			}
			if (std::memcmp(last, v.notes, 3) != 0)
				distinct++;
			std::memcpy(last, v.notes, 3);
		}
		CHECK(distinct >= 6, "%s: the chord changed on only %d of 12 triggers", Core::appName(k.app), distinct);
	}
}

int pitchClassOf(const char* s, int* octave) {
	static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	for (int n = 11; n >= 0; n--) {   // sharps before their naturals
		const size_t len = std::strlen(names[n]);
		if (std::strncmp(s, names[n], len) == 0 && (len == 2 || s[1] != '#')) {
			*octave = std::atoi(s + len);
			return n;
		}
	}
	return -1;
}

int settingIndex(Core& c, const char* name) {
	for (int i = 0; i < c.settingCount(); i++)
		if (std::strcmp(c.settingInfo(i).name, name) == 0)
			return i;
	return -1;
}

// The chord's names against the output voltages (B, C, D), and the
// transforms against the triggers that made them.
void harrington_text_matches_outputs() {
	Core c;
	c.seed(1);
	c.selectApp(1);
	run(c, 0.06);
	const char letter[3] = {'P', 'L', 'R'};
	int octaves[3] = {0, 0, 0};
	for (int n = 0; n < 9; n++) {
		const int which = n % 3;
		trig(c, 1 + which);
		const ScopeView v = view(c);
		CHECK((v.transforms[0][0] == '+' || v.transforms[0][0] == '-') && v.transforms[0][1] == letter[which] &&
		      v.transforms[0][2] == 0, "trigger %d: newest transform \"%s\", want %c", n, v.transforms[0], letter[which]);
		if (n > 0)
			CHECK(v.transforms[1][1] == letter[(n - 1) % 3], "trigger %d: second transform \"%s\"", n, v.transforms[1]);
		for (int i = 0; i < 3; i++) {
			const int semis = (int) std::lround(c.out(i + 1) * 12.f);
			int oct = 0;
			const int pc = pitchClassOf(v.chord[i], &oct);
			CHECK(pc == ((semis % 12) + 12) % 12 && pc == v.notes[i], "trigger %d: chord[%d] \"%s\" but output %d is %d semitones",
			      n, i, v.chord[i], i + 1, semis);
			octaves[i] = oct;
		}
	}
	// One octave up on the setting: each printed octave rises by one.
	const int oct = settingIndex(c, "Octave");
	CHECK(oct >= 0, "Harrington has no Octave setting");
	const ScopeView before = view(c);
	c.setSettingRaw(0, oct, c.setting(0, oct) + 1);
	trig(c, 0);   // TRIG 1: back to the root's triad, an octave higher
	const ScopeView reset = view(c);
	c.setSettingRaw(0, oct, c.setting(0, oct) - 1);
	trig(c, 0);
	const ScopeView low = view(c);
	(void) before;
	(void) octaves;
	for (int i = 0; i < 3; i++) {
		int hi = 0, lo = 0;
		const int a = pitchClassOf(reset.chord[i], &hi), b = pitchClassOf(low.chord[i], &lo);
		CHECK(a == b && hi == lo + 1, "chord[%d]: \"%s\" an octave up, \"%s\" back down", i, reset.chord[i], low.chord[i]);
	}
}

} // namespace

int main() {
	kinds_and_lanes();
	automatonnetz_path_matches_frame();
	circle_notes_match_frame();
	harrington_text_matches_outputs();
	return rkatest::finish("shoal test_view");
}
