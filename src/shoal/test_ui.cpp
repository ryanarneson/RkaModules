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

// Scroll app 0's menu cursor down onto setting `index`, one detent at a time
// (bounded by the number of settings). False if it never got there.
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

// Drawing one screen leaves the other's frame and the menu state alone, and
// drawing never (or often) changes nothing the firmware sees.
void testFramesIndependent() {
	Core a, b;
	run(a, 0.1);
	run(b, 0.1);
	const uint32_t m0 = frameHash(menu(a));
	for (int i = 0; i < 50; i++)
		scope(a);
	CHECK(frameHash(menu(a)) == m0, "scope draws leave the menu frame");
	// b never draws; both get the same input and must stay in step.
	turn(a, shoal::ENCODER_R, 1);
	turn(b, shoal::ENCODER_R, 1);
	CHECK(frameHash(menu(a)) == frameHash(menu(b)), "drawing does not change the firmware's state");
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

void testAcceleration() {
	const int on = bufferLengthAfter40(true), off = bufferLengthAfter40(false);
	CHECK(on > 40, "accelerated: moved %d", on);
	CHECK(off == 40, "unaccelerated: moved %d", off);
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
// the "Active notes" row, as the manual describes), then turn R either with
// L held (L + R turn edits the note under the cursor, and the L release is
// swallowed) or without (R turn rotates the mask).
Frame scaleEditorAfterTurn(bool holdL) {
	Core c;
	c.setEncoderAcceleration(false);
	c.selectApp(kQuantermain);
	run(c, 0.1);
	press(c, shoal::BUTTON_R, 0.05);  // edit "Scale" (row 0): Semitones
	turn(c, shoal::ENCODER_R, -2);     // past "off" to user scale 4
	press(c, shoal::BUTTON_R, 0.05);  // done editing
	turn(c, shoal::ENCODER_R, 2);      // Scale, Root, Active notes
	press(c, shoal::BUTTON_R, 0.05);  // open the scale editor
	if (holdL)
		c.setButton(shoal::BUTTON_L, true);
	turn(c, shoal::ENCODER_R, 1, 0.05);
	if (holdL)
		c.setButton(shoal::BUTTON_L, false);
	run(c, 0.05);
	return menu(c);
}

void testHeldButtonChord() {
	const Frame held = scaleEditorAfterTurn(true), plain = scaleEditorAfterTurn(false);
	CHECK(lit(held) > 50 && lit(plain) > 50, "editor drawn");
	CHECK(frameHash(held) != frameHash(plain), "holding L changes what the R turn does");
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
	testEncoderMovesCursor();
	testEditValue();
	testAcceleration();
	testLongPressOpensAppMenu();
	testAppMenuTimeout();
	testSelectAppClosesMenu();
	testNoScreensaver();
	testHeldButtonChord();
	testSwitchUnderLoad();
	testHostileControls();
	return rkatest::finish("test_ui");
}
