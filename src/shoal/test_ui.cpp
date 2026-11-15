// Controls and screens across the shoal::Core boundary: buttons and encoders
// through the firmware's own debounce and acceleration, the menu and scope
// frames, the non-blocking app menu and the removed screensaver.
#include <climits>
#include <cstring>

#include "testing.hpp"
#include "test_util.hpp"
#include "CoreTest.hpp"

using shoal::Core;
using shoal::CoreTest;

namespace {

constexpr int kCopierMaschine = 0, kQuantermain = 3;

// Hold button b for `seconds`, release it, and let the release debounce.
void press(Core& c, shoal::Button b, double seconds) {
	c.setButton(b, true);
	run(c, seconds);
	c.setButton(b, false);
	run(c, 0.02);
}

// Turn encoder e by `detents` and let the detents play out.
void turn(Core& c, shoal::Encoder e, int detents, double seconds = 0.02) {
	c.turnEncoder(e, detents);
	run(c, seconds);
}

struct Frame {
	uint8_t bytes[shoal::kFrameBytes];
};

Frame menu(Core& c) {
	Frame f;
	c.drawMenu(f.bytes);
	return f;
}

Frame scope(Core& c) {
	Frame f;
	c.drawScope(f.bytes);
	return f;
}

uint32_t frameHash(const Frame& f) {
	uint32_t h = 2166136261u; // FNV-1a
	for (uint8_t b : f.bytes)
		h = (h ^ b) * 16777619u;
	return h;
}

int lit(const Frame& f) {
	int n = 0;
	for (int y = 0; y < shoal::kFrameH; y++)
		for (int x = 0; x < shoal::kFrameW; x++)
			n += Core::pixel(f.bytes, x, y) ? 1 : 0;
	return n;
}

int settingIndex(Core& c, const char* name) {
	for (int i = 0; i < c.settingCount(); i++)
		if (std::strcmp(c.settingInfo(i).name, name) == 0)
			return i;
	return -1;
}

// Scroll the menu cursor of app 0, or of app 3's selected channel, down onto
// setting `index`, one detent at a time (bounded by the number of settings).
// False if it never got there.
bool scrollTo(Core& c, int index) {
	for (int k = 0; k <= c.settingCount(); k++) {
		if (CoreTest::menuSetting(c) == index)
			return true;
		turn(c, shoal::ENCODER_R, 1);
	}
	return false;
}

// Random CV on all inputs and a clock on every gate, for `seconds`.
void stir(Core& c, Lcg& rng, double seconds) {
	for (int step = 0; step < (int) (seconds / 0.01); step++) {
		for (int ch = 0; ch < shoal::kNumChannels; ch++) {
			c.setCv(ch, rng.volts(-3.f, 6.f));
			c.setGate(ch, (step & 1) ? 10.f : 0.f);
		}
		run(c, 0.01);
	}
	for (int ch = 0; ch < shoal::kNumChannels; ch++)
		c.setGate(ch, 0.f);
}

void testPixelLayout() {
	// weegfx layout: byte (y / 8) * 128 + x, bit y % 8.
	Frame f = {};
	f.bytes[0] = 0x01;
	f.bytes[1 * 128 + 5] = 0x80;  // x 5, y 15
	f.bytes[7 * 128 + 127] = 0x80; // x 127, y 63
	CHECK(Core::pixel(f.bytes, 0, 0), "(0,0)");
	CHECK(!Core::pixel(f.bytes, 0, 1), "(0,1)");
	CHECK(Core::pixel(f.bytes, 5, 15), "(5,15)");
	CHECK(Core::pixel(f.bytes, 127, 63), "(127,63)");
	CHECK(lit(f) == 3, "three pixels, got %d", lit(f));
	CHECK(!Core::pixel(f.bytes, -1, 0) && !Core::pixel(f.bytes, 128, 0)
		&& !Core::pixel(f.bytes, 0, -1) && !Core::pixel(f.bytes, 0, 64), "out of range is dark");
}

void testFramesDraw() {
	for (int app = 0; app < shoal::kNumApps; app++) {
		Core c;
		c.selectApp(app);
		run(c, 0.1);
		const Frame m = menu(c), s = scope(c);
		CHECK(lit(m) > 50, "%s: menu lit %d", Core::appName(app), lit(m));
		CHECK(frameHash(m) != frameHash(s), "%s: menu and scope differ", Core::appName(app));
		Lcg rng;
		stir(c, rng, 1.0);
		const Frame s2 = scope(c);
		CHECK(lit(s2) > 0, "%s: scope lit after input", Core::appName(app));
		CHECK(allFinite(c), "%s: outputs finite", Core::appName(app));
	}
}

// The R turns that put each app's menu cursor on the row where an R press
// opens the app's editor (CopierMaschine, Quantermain, Meta-Q: scale editor;
// References: autotuner). Other apps: the R press toggles editing of row 1.
int editorRow(int app) {
	switch (app) {
	case 3: return 2;
	case 12: return 4;
	default: return 1;
	}
}

// One scripted session per app: seeded CV and clocks, the editor gesture,
// and a few more controls. `draw`: draw both screens every 17 ticks.
void scriptedSession(Core& c, int app, bool draw) {
	c.setEncoderAcceleration(false); // queued detents land on the intended row
	c.selectApp(app);
	Lcg rng;
	auto step = [&](double seconds) {
		const long n = std::lround(seconds * shoal::kTickHz);
		for (long i = 0; i < n; i++) {
			if (i % 50 == 0)
				for (int ch = 0; ch < shoal::kNumChannels; ch++) {
					c.setCv(ch, rng.volts(-3.f, 6.f));
					c.setGate(ch, (i / 50) % 4 == ch ? 10.f : 0.f);
				}
			c.tick();
			if (draw && i % 17 == 0) {
				menu(c);
				scope(c);
			}
		}
	};
	auto hold = [&](shoal::Button b, double seconds) {
		c.setButton(b, true);
		step(seconds);
		c.setButton(b, false);
		step(0.02);
	};
	step(0.1);
	c.turnEncoder(shoal::ENCODER_R, editorRow(app));
	step(0.05);
	hold(shoal::BUTTON_R, 0.05); // open the editor
	c.turnEncoder(shoal::ENCODER_R, 1);
	step(0.05);
	c.turnEncoder(shoal::ENCODER_L, 1);
	step(0.05);
	hold(shoal::BUTTON_UP, 0.05);
	hold(shoal::BUTTON_DOWN, 0.05);
	step(1.5);
	hold(shoal::BUTTON_UP, 0.05);
	hold(shoal::BUTTON_L, 0.05);
	step(0.5);
}

// Drawing (often, or never) changes nothing the firmware does: outputs and
// the final menu frame match between a core that draws and one that never
// does, in every app.
void testFramesIndependent() {
	for (int app = 0; app < shoal::kNumApps; app++) {
		Core drawn, blind;
		scriptedSession(drawn, app, true);
		scriptedSession(blind, app, false);
		for (int ch = 0; ch < shoal::kNumChannels; ch++)
			CHECK(drawn.outCode(ch) == blind.outCode(ch), "%s: ch %d output", Core::appName(app), ch);
		CHECK(frameHash(menu(drawn)) == frameHash(menu(blind)), "%s: menu frame", Core::appName(app));
	}
	// Scope draws leave the menu frame alone.
	Core a;
	run(a, 0.1);
	const uint32_t m0 = frameHash(menu(a));
	for (int i = 0; i < 50; i++)
		scope(a);
	CHECK(frameHash(menu(a)) == m0, "scope draws leave the menu frame");
}

// Meta-Q's scale editor lets UP switch scale slots only after the editor has
// been open a while (upstream counted menu redraws). That must not depend on
// the host drawing.
Frame metaQSlotAfterUp(bool draw, bool pressUp) {
	Core c;
	c.selectApp(4);
	run(c, 0.1);
	turn(c, shoal::ENCODER_R, 1);     // Scale, Active notes
	press(c, shoal::BUTTON_R, 0.05);  // open the scale editor
	for (int i = 0; i < 100; i++) {   // 1.5 s
		run(c, 0.015);
		if (draw)
			menu(c);
	}
	if (pressUp)
		press(c, shoal::BUTTON_UP, 0.05);
	run(c, 0.05);
	return menu(c);
}

void testMetaQSlotSwitchWithoutDrawing() {
	const uint32_t blind = frameHash(metaQSlotAfterUp(false, true));
	const uint32_t drawn = frameHash(metaQSlotAfterUp(true, true));
	const uint32_t still = frameHash(metaQSlotAfterUp(false, false));
	CHECK(blind == drawn, "UP does the same whether or not the host draws");
	CHECK(blind != still, "UP switched the scale slot without any drawing");
}

// References: the autotuner's error state (no frequency ever arrives in
// Shoal) is reached without drawing, so DOWN then resets it the same way
// (DOWN acts only in the error state).
Frame autotunerAfterError(bool draw) {
	Core c;
	c.setEncoderAcceleration(false); // 4 queued detents are 4 rows
	c.selectApp(12);
	run(c, 0.1);
	turn(c, shoal::ENCODER_R, 4);     // the autotune row
	press(c, shoal::BUTTON_R, 0.05);  // open the autotuner
	turn(c, shoal::ENCODER_R, 1);     // cursor to "run"
	press(c, shoal::BUTTON_UP, 0.05); // arm
	press(c, shoal::BUTTON_DOWN, 0.05); // run
	for (int i = 0; i < 200; i++) {   // 2 s, past the error timeout
		run(c, 0.01);
		if (draw)
			menu(c);
	}
	press(c, shoal::BUTTON_DOWN, 0.05); // in the error state: reset
	return menu(c);
}

void testAutotunerErrorWithoutDrawing() {
	CHECK(frameHash(autotunerAfterError(false)) == frameHash(autotunerAfterError(true)),
		"autotuner reaches the same state whether or not the host draws");
}

void testEncoderMovesCursor() {
	Core c;
	run(c, 0.1);
	const uint32_t h0 = frameHash(menu(c));
	turn(c, shoal::ENCODER_R, 1);
	CHECK(frameHash(menu(c)) != h0, "R turn moved the cursor");
	turn(c, shoal::ENCODER_R, -1);
	CHECK(frameHash(menu(c)) == h0, "and back");
}

void testEditValue() {
	Core c;
	// Two detents queued at once arrive 2 ms apart, which the firmware's
	// acceleration (on by default) turns into 3 steps. Off: exactly 2.
	c.setEncoderAcceleration(false);
	run(c, 0.1);
	const int idx = settingIndex(c, "octave");
	CHECK(idx >= 0, "CopierMaschine has an octave setting");
	const int v0 = c.setting(0, idx);
	CHECK(scrollTo(c, idx), "cursor reached the octave row");
	press(c, shoal::BUTTON_R, 0.05);
	turn(c, shoal::ENCODER_R, 2, 0.05);
	CHECK(c.setting(0, idx) == v0 + 2, "octave %d, want %d", c.setting(0, idx), v0 + 2);
}

// "hold (buflen)" (4..63, default 4) has room for 40 unaccelerated steps.
int bufferLengthAfter40(bool acceleration) {
	Core c;
	c.setEncoderAcceleration(acceleration);
	run(c, 0.1);
	const int idx = settingIndex(c, "hold (buflen)");
	if (idx < 0 || !scrollTo(c, idx))
		return -1;
	press(c, shoal::BUTTON_R, 0.05);
	const int v0 = c.setting(0, idx);
	turn(c, shoal::ENCODER_R, 40, 0.2);
	return c.setting(0, idx) - v0;
}

// "> LFSR p" (0..255, default 128; shown with CV source "TM") turned by
// `detents` queued at once.
int lfsrProbabilityAfter(int detents, bool acceleration) {
	Core c;
	c.setEncoderAcceleration(false);
	run(c, 0.1);
	const int source = settingIndex(c, "CV source"), prob = settingIndex(c, "> LFSR p");
	if (source < 0 || prob < 0 || !scrollTo(c, source))
		return -1000;
	press(c, shoal::BUTTON_R, 0.05);
	turn(c, shoal::ENCODER_R, 1);     // CV1 -> TM
	press(c, shoal::BUTTON_R, 0.05);
	if (!scrollTo(c, prob))
		return -1000;
	press(c, shoal::BUTTON_R, 0.05);
	c.setEncoderAcceleration(acceleration);
	const int v0 = c.setting(0, prob);
	turn(c, shoal::ENCODER_R, detents, 0.2);
	return c.setting(0, prob) - v0;
}

void testAcceleration() {
	const int on = bufferLengthAfter40(true), off = bufferLengthAfter40(false);
	CHECK(on > 40, "accelerated: moved %d", on);
	CHECK(off == 40, "unaccelerated: moved %d", off);
	// 40 accelerated detents overrun any range app 0 has; 10 stay well
	// inside "> LFSR p"'s 127 steps of headroom, so this is acceleration,
	// not the clamp.
	const int on10 = lfsrProbabilityAfter(10, true), off10 = lfsrProbabilityAfter(10, false);
	CHECK(on10 > 10 && on10 < 127, "accelerated, unclamped: moved %d", on10);
	CHECK(off10 == 10, "unaccelerated: moved %d", off10);
}

void testLongPressOpensAppMenu() {
	Core c;
	c.setEncoderAcceleration(false); // 3 queued detents are 3 steps
	run(c, 0.1);
	CHECK(!c.appMenuOpen(), "closed at boot");
	const uint32_t appFrame = frameHash(menu(c));
	press(c, shoal::BUTTON_R, 1.2);
	CHECK(c.appMenuOpen(), "R long press opens the app menu");
	CHECK(frameHash(menu(c)) != appFrame, "menu frame shows the app menu");
	turn(c, shoal::ENCODER_R, 3, 0.05);
	CHECK(c.app() == kCopierMaschine, "scrolling does not switch yet");
	press(c, shoal::BUTTON_R, 0.05);
	CHECK(!c.appMenuOpen() && c.app() == kQuantermain, "Quantermain, got %d", c.app());

	// R long press in the menu switches too (upstream also saved).
	press(c, shoal::BUTTON_R, 1.2);
	turn(c, shoal::ENCODER_R, -1, 0.05);
	press(c, shoal::BUTTON_R, 1.2);
	CHECK(!c.appMenuOpen() && c.app() == kQuantermain - 1, "long press switched, got %d", c.app());

	// L does nothing; UP toggles acceleration back on, so 1 + 3 detents in
	// the same direction scroll more than 4 apps.
	press(c, shoal::BUTTON_R, 1.2);
	press(c, shoal::BUTTON_L, 0.05);
	CHECK(c.appMenuOpen(), "L leaves the menu open");
	press(c, shoal::BUTTON_UP, 0.05);
	turn(c, shoal::ENCODER_R, 1, 0.05); // set the direction
	turn(c, shoal::ENCODER_R, 3, 0.05);
	press(c, shoal::BUTTON_R, 0.05);
	CHECK(c.app() > kQuantermain - 1 + 4, "accelerated scroll, got %d", c.app());
}

void testAppMenuTimeout() {
	Core c;
	run(c, 0.1);
	press(c, shoal::BUTTON_R, 1.2);
	CHECK(c.appMenuOpen(), "open");
	turn(c, shoal::ENCODER_R, 2);
	run(c, 24.0);
	CHECK(c.appMenuOpen(), "still open after 24 s idle");
	run(c, 2.0);
	CHECK(!c.appMenuOpen() && c.app() == kCopierMaschine, "closed unchanged, app %d", c.app());
}

// The R long press that opens the app menu ends that main pass's dispatch
// (Ui::DispatchEvents returns), so a turn queued behind it reaches the app
// menu one main pass later: it is pending until then.
void testTurnBehindAppMenuOpen() {
	Core c;
	c.setEncoderAcceleration(false);
	run(c, 0.1);
	// The long press (queued when the button is released) and then a detent,
	// both queued before the next main pass (UI polls only).
	c.setButton(shoal::BUTTON_R, true);
	run(c, 1.1);
	CHECK(!c.appMenuOpen(), "still held");
	c.setButton(shoal::BUTTON_R, false);
	for (int i = 0; i < 12; i++)
		CoreTest::pollUi(c);
	c.turnEncoder(shoal::ENCODER_R, 1);
	for (int i = 0; i < 4; i++)
		CoreTest::pollUi(c);
	CHECK(!c.appMenuOpen(), "no main pass yet");
	int ticks = 0;
	while (!c.appMenuOpen() && ticks < 17) {
		c.tick();
		ticks++;
	}
	CHECK(c.appMenuOpen(), "the long press opened the app menu");
	CHECK(c.encodersPending(), "the turn queued behind the long press is pending");
	run(c, 0.003);
	CHECK(!c.encodersPending(), "nothing pending once the app menu has taken it");
	// It scrolled the app menu by one: an R push selects the next app.
	run(c, 0.05);
	press(c, shoal::BUTTON_R, 0.05);
	CHECK(!c.appMenuOpen() && c.app() == kCopierMaschine + 1, "app %d, want %d", c.app(), kCopierMaschine + 1);
}

void testSelectAppClosesMenu() {
	Core c;
	run(c, 0.1);
	press(c, shoal::BUTTON_R, 1.2);
	c.selectApp(5);
	CHECK(!c.appMenuOpen() && c.app() == 5, "selectApp closes the menu and switches");
	press(c, shoal::BUTTON_R, 1.2);
	c.selectApp(5);
	CHECK(!c.appMenuOpen() && c.app() == 5, "selecting the current app closes the menu");
}

void testNoScreensaver() {
	Core c;
	run(c, 60.0);
	const uint32_t h0 = frameHash(menu(c));
	turn(c, shoal::ENCODER_R, 1);
	const uint32_t h1 = frameHash(menu(c));
	CHECK(h1 != h0, "first input after idle is not swallowed");
	press(c, shoal::BUTTON_UP, 2.0);
	CHECK(frameHash(menu(c)) == h1, "UP long press does nothing");
	turn(c, shoal::ENCODER_R, 1);
	CHECK(frameHash(menu(c)) != h1, "input after UP long press is not swallowed");
}

// Quantermain: put channel 1 on a user scale and open its scale editor (R on
// the "Active notes" row, as the manual describes), then turn R by one.
//   HeldL: L held during the turn. The event's mask carries L, so the turn
//     edits the note under the cursor, and the L release is swallowed.
//   TapLAfter: the turn alone (it rotates the mask), then an L tap (which
//     toggles the note under the cursor). This is exactly what HeldL would
//     produce if the held L were missing from the event's mask.
enum class Chord { HeldL, TapLAfter, Plain };

Frame scaleEditorAfterTurn(Chord chord) {
	Core c;
	c.setEncoderAcceleration(false);
	c.selectApp(kQuantermain);
	run(c, 0.1);
	press(c, shoal::BUTTON_R, 0.05);  // edit "Scale" (row 0): Semitones
	turn(c, shoal::ENCODER_R, -2);     // past "off" to user scale 4
	press(c, shoal::BUTTON_R, 0.05);  // done editing
	turn(c, shoal::ENCODER_R, 2);      // Scale, Root, Active notes
	press(c, shoal::BUTTON_R, 0.05);  // open the scale editor
	if (chord == Chord::HeldL)
		c.setButton(shoal::BUTTON_L, true);
	turn(c, shoal::ENCODER_R, 1, 0.05);
	if (chord == Chord::HeldL)
		c.setButton(shoal::BUTTON_L, false);
	run(c, 0.05);
	if (chord == Chord::TapLAfter)
		press(c, shoal::BUTTON_L, 0.05);
	run(c, 0.05);
	return menu(c);
}

void testHeldButtonChord() {
	const Frame held = scaleEditorAfterTurn(Chord::HeldL);
	const Frame tapped = scaleEditorAfterTurn(Chord::TapLAfter);
	const Frame plain = scaleEditorAfterTurn(Chord::Plain);
	CHECK(lit(held) > 50 && lit(tapped) > 50, "editor drawn");
	CHECK(frameHash(held) != frameHash(tapped), "the held L reached the R turn's event mask");
	CHECK(frameHash(held) != frameHash(plain), "holding L changes what the R turn does");
	CHECK(frameHash(tapped) != frameHash(plain), "the L tap toggled a note");
}

void testSwitchUnderLoad() {
	Lcg rng;
	for (int i = 0; i < shoal::kNumApps; i++) {
		for (int j = 0; j < shoal::kNumApps; j++) {
			Core c;
			c.selectApp(i);
			for (int ch = 0; ch < shoal::kNumChannels; ch++) {
				c.setGate(ch, 10.f);
				c.setCv(ch, rng.volts(-5.f, 8.f));
			}
			run(c, 0.02);
			c.selectApp(j);
			run(c, 0.02);
			CHECK(allFinite(c), "%d -> %d: outputs finite", i, j);
			const Frame m = menu(c);
			CHECK(lit(m) > 50, "%d -> %d: menu lit %d", i, j, lit(m));
		}
	}
}

// A huge turn queues at most 256 detents per encoder (Core::turnEncoder);
// the rest is dropped. Quantermain's "Fine" (-999..999, default 0) has room
// for all of them, so the move shows the cap, not the setting's clamp.
void testEncoderQueueBounded() {
	Core c;
	c.selectApp(kQuantermain);
	c.setEncoderAcceleration(false);
	run(c, 0.1);
	const int idx = settingIndex(c, "Fine");
	CHECK(idx >= 0, "Quantermain has a Fine setting");
	CHECK(scrollTo(c, idx), "cursor reached the Fine row");
	press(c, shoal::BUTTON_R, 0.05);
	const int v0 = c.setting(0, idx);
	c.turnEncoder(shoal::ENCODER_R, 100000);
	run(c, 1.0);
	const int moved = c.setting(0, idx) - v0;
	CHECK(allFinite(c), "outputs finite");
	CHECK(moved == 256, "moved %d, want the 256-detent cap", moved);
}

void testHostileControls() {
	Core c;
	c.turnEncoder(shoal::ENCODER_R, INT_MIN);
	c.turnEncoder(shoal::ENCODER_L, INT_MAX);
	c.setButton((shoal::Button) 200, true);
	c.turnEncoder((shoal::Encoder) 200, 5);
	run(c, 1.0);
	CHECK(allFinite(c), "outputs finite");
	CHECK(lit(menu(c)) > 50, "menu still draws");
}

} // namespace

int main() {
	testPixelLayout();
	testFramesDraw();
	testFramesIndependent();
	testMetaQSlotSwitchWithoutDrawing();
	testAutotunerErrorWithoutDrawing();
	testEncoderMovesCursor();
	testEditValue();
	testAcceleration();
	testLongPressOpensAppMenu();
	testAppMenuTimeout();
	testTurnBehindAppMenuOpen();
	testSelectAppClosesMenu();
	testNoScreensaver();
	testHeldButtonChord();
	testSwitchUnderLoad();
	testHostileControls();
	testEncoderQueueBounded();
	return rkatest::finish("test_ui");
}
