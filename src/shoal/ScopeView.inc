// The current app's idle view as data (ScopeView.hpp), for
// shoal::Core::scopeView. Included by Firmware.cpp after Friendly.inc, so
// the app classes and their state are visible. Everything here only reads:
// the same state each app's own screensaver function reads, nothing more.
// No allocation, no locks, no libc formatting, no writable globals.

#include "ScopeView.hpp"

namespace shoal {
namespace view {
namespace {

void kindOf(int app, ScopeView& v) {
	// The app order is OC_apps.ino's available_apps (Core::appName).
	switch (app) {
		case 0:   // CopierMaschine: ASR_screensaver, four columns of 32
		case 3:   // Quantermain: QQ_screensaver, four columns of 32
			v.kind = ViewKind::Pixels;
			v.lanes = Lanes::Columns4;
			break;
		case 12:  // References: four columns over two lines of text (rows 44, 56)
			v.kind = ViewKind::Pixels;
			v.lanes = Lanes::Columns4Top;
			break;
		case 4:   // Meta-Q: DQ_screensaver, channels at x 0 and 64 (outputs A, B)
		case 8:   // Sequins: SEQ_screensaver, the same
			v.kind = ViewKind::Pixels;
			v.lanes = Lanes::Halves;
			break;
		case 10:  // Viznutcracker sweet: four bands of 16 rows
			v.kind = ViewKind::Pixels;
			v.lanes = Lanes::Rows4;
			break;
		case 5:   // Quadraturia, Piqued, Dialectic Ping Pong: OC::scope_render
		case 7:
		case 9:
			v.kind = ViewKind::Traces;
			v.lanes = Lanes::Rows4;
			// Quadraturia swings both sides of 0 V, over the whole output
			// range. Piqued's envelopes run 0 to +5 V (inverted: +5 V down
			// to 0, APP_ENVGEN.ino). Ping Pong's rise from 0 V to the top
			// of the output range (APP_BBGEN.ino).
			v.laneLo = app == 5 ? -3.75f : 0.f;
			v.laneHi = app == 7 ? 5.f : 6.25f;
			break;
		case 6:   // Low-rents: OC::vectorscope_render
			v.kind = ViewKind::XY;
			v.lanes = Lanes::Halves;
			break;
		case 2:
			v.kind = ViewKind::GridCircle;
			v.lanes = Lanes::None;
			break;
		case 1:
			v.kind = ViewKind::CircleText;
			v.lanes = Lanes::None;
			break;
		default:  // Acid Curds (11): one picture
			v.kind = ViewKind::Pixels;
			v.lanes = Lanes::None;
			break;
	}
}

// As Automatonnetz_screensaver reads them.
void automatonnetz(ScopeView& v) {
	int outputs[4];
	automatonnetz_state.tonnetz_state.get_outputs(outputs);
	for (int i = 0; i < 3; i++)
		v.notes[i] = (uint8_t) ((((outputs[i + 1] + 120) % 12) + 12) % 12);
	uint32_t history = automatonnetz_state.history();
	for (int i = 0; i < kViewPath; i++, history >>= 8) {
		const vec2<size_t> pos = extract_pos(history);
		v.pathX[i] = (uint8_t) (pos.x < (size_t) kGridSide ? pos.x : kGridSide - 1);
		v.pathY[i] = (uint8_t) (pos.y < (size_t) kGridSide ? pos.y : kGridSide - 1);
	}
}

// As H1200_screensaver prints them.
void harrington(ScopeView& v) {
	int outputs[4];
	h1200_state.tonnetz_state.get_outputs(outputs);
	for (int i = 0; i < 3; i++) {
		const int note = outputs[i + 1];
		const int octave = note / 12;
		const int name = ((note % 12) + 12) % 12;
		v.notes[i] = (uint8_t) name;
		friendly::Text(v.chord[i]).str(OC::Strings::note_names_unpadded[name]).num(octave + 1);
	}
	uint32_t history = h1200_state.tonnetz_state.history();
	for (int i = 0; i < 4; i++, history >>= 8) {
		const uint32_t type = history & 0x7f;
		friendly::Text(v.transforms[i])
			.ch(history & 0x80 ? '+' : '-')
			.ch(type <= tonnetz::TRANSFORM_LAST ? tonnetz::transform_names[type] : '?');
	}
}

} // namespace

void fill(int app, ScopeView& v) {
	v = ScopeView();
	v.app = (uint8_t) (app < 0 ? 0 : app);
	kindOf(app, v);
	if (v.kind == ViewKind::GridCircle)
		automatonnetz(v);
	else if (v.kind == ViewKind::CircleText)
		harrington(v);
}

} // namespace view
} // namespace shoal
