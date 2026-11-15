// Save, restore and reset to defaults, through the firmware's own storage
// over the instance's 2048-byte fake EEPROM.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "CoreTest.hpp"
#include "testing.hpp"
#include "test_util.hpp"

using shoal::Core;
using shoal::CoreTest;

namespace {

constexpr int kImage = shoal::kStateBytes;

// Settings the firmware legitimately alters on restore (the value after a
// restore differs from the one saved), each justified from the source. The
// test writes values the encoder could not (setSettingRaw skips app guards),
// so an app may normalise them on load. Empty: none found.
struct Exception { int app, channel; const char* name; const char* why; };
const std::vector<Exception> kExceptions = {};

bool isException(int app, int ch, const char* name) {
	for (const Exception& e : kExceptions)
		if (e.app == app && e.channel == ch && std::strcmp(e.name, name) == 0)
			return true;
	return false;
}

// Every setting of every app, in order. Leaves the core on the last app.
std::vector<int> snapshot(Core& c) {
	std::vector<int> v;
	for (int app = 0; app < shoal::kNumApps; app++) {
		c.selectApp(app);
		for (int ch = 0; ch < c.channels(); ch++)
			for (int i = 0; i < c.settingCount(); i++)
				v.push_back(c.setting(ch, i));
	}
	return v;
}

// Set every setting of every app to a value other than its default, then
// leave the core on app `last`.
void scramble(Core& c, int last) {
	for (int app = 0; app < shoal::kNumApps; app++) {
		c.selectApp(app);
		for (int ch = 0; ch < c.channels(); ch++)
			for (int i = 0; i < c.settingCount(); i++) {
				const shoal::SettingInfo s = c.settingInfo(i);
				c.setSettingRaw(ch, i, s.def == s.max ? s.min : s.max);
			}
	}
	c.selectApp(last);
}

uint32_t frameHash(Core& c) {
	uint8_t f[shoal::kFrameBytes];
	c.drawMenu(f);
	uint32_t h = 2166136261u;
	for (uint8_t b : f)
		h = (h ^ b) * 16777619u;
	return h;
}

void press(Core& c, shoal::Button b, double seconds) {
	c.setButton(b, true);
	run(c, seconds);
	c.setButton(b, false);
	run(c, 0.02);
}

void testRoundTripEveryApp() {
	Core a;
	scramble(a, 7);
	std::vector<uint8_t> img(kImage);
	a.save(img.data());
	Core b;
	CHECK(b.restore(img.data()), "restore of a saved image");
	CHECK(b.app() == 7, "app after restore: %d", b.app());
	const std::vector<int> sa = snapshot(a), sb = snapshot(b);
	CHECK(sa.size() == sb.size(), "snapshot sizes %zu, %zu", sa.size(), sb.size());
	size_t k = 0;
	for (int app = 0; app < shoal::kNumApps; app++) {
		a.selectApp(app);
		for (int ch = 0; ch < a.channels(); ch++)
			for (int i = 0; i < a.settingCount(); i++, k++) {
				const char* name = a.settingInfo(i).name;
				if (k < sb.size() && !isException(app, ch, name))
					CHECK(sa[k] == sb[k], "%s ch %d '%s': saved %d, restored %d",
						Core::appName(app), ch, name, sa[k], sb[k]);
			}
	}
	// The scrambled values are not the defaults, so the match is not trivial.
	Core fresh;
	CHECK(snapshot(fresh) != sa, "scrambled settings differ from a fresh core's");
}

void testSaveIsStable() {
	Core a;
	scramble(a, 5);
	std::vector<uint8_t> i1(kImage), i2(kImage), i3(kImage);
	a.save(i1.data());
	a.save(i2.data());
	a.save(i3.data());
	// With nothing changed the storage writes nothing: the images are equal.
	CHECK(i1 == i2 && i2 == i3, "images differ with nothing changed");
	const std::vector<int> sa = snapshot(a);
	for (const std::vector<uint8_t>* img : { &i1, &i2 }) {
		Core b;
		CHECK(b.restore(img->data()), "restore");
		CHECK(snapshot(b) == sa, "restored settings differ");
	}
	// A change makes the next image differ (and restore to the change).
	a.selectApp(0);
	const int v = a.setting(0, 0);
	const shoal::SettingInfo s = a.settingInfo(0);
	a.setSettingRaw(0, 0, v == s.max ? s.min : v + 1);
	std::vector<uint8_t> i4(kImage);
	a.save(i4.data());
	CHECK(i4 != i3, "a change leaves the image unchanged");
	Core b;
	CHECK(b.restore(i4.data()) && b.setting(0, 0) == a.setting(0, 0), "change restored");
}

void testFreshCoreSaves() {
	Core a, fresh;
	std::vector<uint8_t> img(kImage);
	a.save(img.data());
	Core b;
	CHECK(b.restore(img.data()), "a fresh core's image restores");
	CHECK(b.app() == 0, "app %d", b.app());
	CHECK(snapshot(b) == snapshot(fresh), "defaults");
}

void checkDefaultsOrRestored(Core& c, bool ok, const std::vector<int>& freshSettings, const char* what, int n) {
	if (!ok) {
		CHECK(c.app() == 0, "%s %d: rejected but app %d", what, n, c.app());
		CHECK(snapshot(c) == freshSettings, "%s %d: rejected but not defaults", what, n);
	}
}

void testCorruptImage() {
	Core fresh;
	const std::vector<int> freshSettings = snapshot(fresh);
	Core a;
	scramble(a, 7);
	std::vector<uint8_t> valid(kImage);
	a.save(valid.data());

	Core c;
	std::vector<uint8_t> img(kImage);
	const std::vector<int> validSettings = snapshot(a);
	auto attempt = [&](const char* what, int n) {
		const bool ok = c.restore(img.data());
		run(c, 0.5);
		CHECK(allFinite(c), "%s %d: outputs finite", what, n);
		checkDefaultsOrRestored(c, ok, freshSettings, what, n);
		// A flip the checksum does not cover leaves the saved settings intact.
		if (ok && img != valid)
			CHECK(snapshot(c) == validSettings, "%s %d: accepted, but the settings differ from the valid image's", what, n);
		return ok;
	};
	std::fill(img.begin(), img.end(), 0);
	CHECK(!attempt("zeros", 0), "all-zero image accepted");
	std::fill(img.begin(), img.end(), 0xff);
	CHECK(!attempt("ones", 0), "all-0xFF image accepted");
	for (uint32_t seed = 1; seed <= 100; seed++) {
		Lcg g;
		g.state = seed;
		for (uint8_t& b : img)
			b = (uint8_t) (g.next() >> 24);
		attempt("random", (int) seed);
	}
	Lcg g;
	g.state = 777;
	int accepted = 0;
	for (int n = 0; n < 64; n++) {
		img = valid;
		const uint32_t bit = (g.next() >> 8) % (kImage * 8);
		img[bit / 8] ^= (uint8_t) (1u << (bit % 8));
		if (attempt("bit flip", n))
			accepted++;
	}
	// The checksum rejects flips inside the saved pages; flips elsewhere in
	// the image (calibration area, unused bytes) leave it valid.
	std::printf("test_state: %d of 64 single-bit flips still restored\n", accepted);
}

void clockCopier(Core& c, const float* volts, int n) {
	for (int i = 0; i < n; i++) {
		c.setCv(0, volts[i]);
		run(c, 0.01);
		pulse(c, 0);
		run(c, 0.005);
	}
}

// Sequence positions and shift-register contents are not in the image. The
// CopierMaschine only writes its outputs when clocked (a fresh core's outputs
// are the DAC's power-on code until then, and a restored core keeps what the
// DAC held), so the reset shows on the first clock: a fresh core shifts the
// sample in over an empty buffer, and so must the restored one.
void testRunningStateResets() {
	Core fresh;
	const float one[1] = { 5.f };
	clockCopier(fresh, one, 1);
	float want[4];
	for (int ch = 0; ch < 4; ch++)
		want[ch] = fresh.out(ch);
	CHECK(std::fabs(want[0] - 5.f) < 0.01f && std::fabs(want[1]) < 0.01f, "fresh: A %.3f B %.3f", want[0], want[1]);

	Core c;
	const float volts[4] = { 1.f, 2.f, 3.f, 4.f };
	clockCopier(c, volts, 4);
	CHECK(std::fabs(c.out(0) - 4.f) < 0.01f && std::fabs(c.out(3) - 1.f) < 0.01f, "the copier ran: A %.3f D %.3f", c.out(0), c.out(3));
	std::vector<uint8_t> img(kImage);
	c.save(img.data());
	CHECK(c.restore(img.data()), "restore");
	clockCopier(c, one, 1);
	for (int ch = 0; ch < 4; ch++)
		CHECK(std::fabs(c.out(ch) - want[ch]) < 1e-3f, "out %d: %.4f, fresh core %.4f", ch, c.out(ch), want[ch]);
}

// Sequins (app 8): the step position is running state too. After several
// clocks, save and restore, one clock plays what a fresh core given the same
// image plays on its first clock.
void testSequencerPositionResets() {
	Core a;
	a.selectApp(8);
	// Distinct notes, so every step plays something else.
	for (int slot = 0; slot < CoreTest::userPatternSlots(); slot++)
		for (int n = 0; n < 16; n++)
			CoreTest::userPatternNote(a, slot, n) = (int16_t) (n * 128);
	std::vector<uint8_t> img(kImage);
	a.save(img.data());

	auto clockOnce = [](Core& c) {
		c.setCv(0, 0.f);
		run(c, 0.05);
		pulse(c, 0);
		run(c, 0.01);
	};
	Core ref;
	CHECK(ref.restore(img.data()) && ref.app() == 8, "reference restore");
	clockOnce(ref);

	Core c;
	CHECK(c.restore(img.data()), "restore");
	bool differed = false;
	for (int k = 1; k <= 6; k++) {
		clockOnce(c);
		for (int ch = 0; ch < 4; ch++)
			differed = differed || std::fabs(c.out(ch) - ref.out(ch)) > 1e-3f;
	}
	CHECK(differed, "the sequence moved (so the comparison can tell)");
	c.save(img.data());
	CHECK(c.restore(img.data()), "restore after running");
	clockOnce(c);
	for (int ch = 0; ch < 4; ch++)
		CHECK(std::fabs(c.out(ch) - ref.out(ch)) < 1e-3f, "out %d: %.4f, fresh core %.4f", ch, c.out(ch), ref.out(ch));
}

void testResetToDefaults() {
	Core fresh;
	const std::vector<int> freshSettings = snapshot(fresh);
	Core a;
	scramble(a, 7);
	CHECK(a.app() == 7, "app 7");
	a.resetToDefaults();
	CHECK(a.app() == 0, "app after reset %d", a.app());
	CHECK(snapshot(a) == freshSettings, "settings are a fresh core's");
	// And the saved image of the reset core restores to defaults.
	a.selectApp(0);
	std::vector<uint8_t> img(kImage);
	a.save(img.data());
	Core b;
	CHECK(b.restore(img.data()) && snapshot(b) == freshSettings, "reset core saves defaults");
}

// restore re-runs the firmware's start-up, which reloads the calibration
// defaults; the core's input range (not in the image) must survive it.
void testInputRangeSurvives() {
	for (int how = 0; how < 2; how++) {
		Core c;
		std::vector<uint8_t> img(kImage);
		c.save(img.data());
		c.setInputRange(shoal::InputRange::Bipolar5);
		if (how == 0)
			CHECK(c.restore(img.data()), "restore");
		else
			c.resetToDefaults();
		for (float v : { 0.f, 1.f, -5.f }) {
			c.setCv(0, v);
			run(c, 0.02);
			const int32_t p = CoreTest::pitchIn(c, 0);
			CHECK(std::abs(p - (int32_t) std::lround(v * 1536.f)) <= 8, "%s: %.0f V reads pitch %d",
				how == 0 ? "restore" : "reset", (double) v, (int) p);
		}
	}
}

void testRestoreClosesAppMenu() {
	for (int how = 0; how < 2; how++) {
		Core c;
		c.setEncoderAcceleration(false);
		std::vector<uint8_t> img(kImage);
		c.save(img.data());
		run(c, 0.1);
		press(c, shoal::BUTTON_R, 0.05);   // edit mode in the app
		press(c, shoal::BUTTON_R, 1.2);    // long press: app menu
		CHECK(c.appMenuOpen(), "menu opened");
		c.turnEncoder(shoal::ENCODER_L, 3);  // queued, not yet played
		if (how == 0)
			CHECK(c.restore(img.data()), "restore");
		else
			c.resetToDefaults();
		CHECK(!c.appMenuOpen(), "menu closed after %s", how == 0 ? "restore" : "reset");
		run(c, 0.1);
		CHECK(!c.appMenuOpen(), "still closed");
		// UI state as a fresh boot: same screen as a fresh core after the same time.
		Core fresh;
		run(fresh, 0.1);
		CHECK(frameHash(c) == frameHash(fresh), "app screen is a fresh core's (edit mode and cursor reset)");
		// An R press now goes to the app (it enters edit mode), not the menu.
		const uint32_t before = frameHash(c);
		press(c, shoal::BUTTON_R, 0.05);
		CHECK(!c.appMenuOpen(), "an R press does not open the menu");
		CHECK(frameHash(c) != before, "the R press reached the app");
	}
}

// User scales, patterns and chords live outside the apps' settings (in the
// global settings page); each survives a save and restore.
void testUserDataRoundTrip() {
	Core a;
	const int ns = CoreTest::userScaleSlots(), np = CoreTest::userPatternSlots(), nc = CoreTest::userChordSlots();
	for (int slot = 0; slot < ns; slot++) {
		CoreTest::userScaleNumNotes(a, slot) = 5 + slot;
		for (int n = 0; n < 16; n++)
			CoreTest::userScaleNote(a, slot, n) = (int16_t) (100 * slot + 7 * n);
	}
	for (int slot = 0; slot < np; slot++)
		for (int n = 0; n < 16; n++)
			CoreTest::userPatternNote(a, slot, n) = (int16_t) (-300 + 50 * slot + 13 * n);
	for (int slot = 0; slot < nc; slot++) {
		const int8_t v[5] = { (int8_t) (slot % 9), (int8_t) (slot % 4), (int8_t) (slot % 5), (int8_t) (slot % 8), (int8_t) (slot % 9 - 4) };
		for (int f = 0; f < 5; f++)
			CoreTest::userChordField(a, slot, f) = v[f];
	}
	std::vector<uint8_t> img(kImage);
	a.save(img.data());
	Core b;
	CHECK(b.restore(img.data()), "restore");
	for (int slot = 0; slot < ns; slot++) {
		CHECK(CoreTest::userScaleNumNotes(b, slot) == (uint32_t) (5 + slot), "scale %d: %u notes", slot, (unsigned) CoreTest::userScaleNumNotes(b, slot));
		for (int n = 0; n < 16; n++)
			CHECK(CoreTest::userScaleNote(b, slot, n) == CoreTest::userScaleNote(a, slot, n), "scale %d note %d", slot, n);
	}
	for (int slot = 0; slot < np; slot++)
		for (int n = 0; n < 16; n++)
			CHECK(CoreTest::userPatternNote(b, slot, n) == CoreTest::userPatternNote(a, slot, n), "pattern %d note %d", slot, n);
	for (int slot = 0; slot < nc; slot++)
		for (int f = 0; f < 5; f++)
			CHECK(CoreTest::userChordField(b, slot, f) == CoreTest::userChordField(a, slot, f), "chord %d field %d", slot, f);
	// And they differ from a fresh core's, so the check is not trivial.
	Core fresh;
	CHECK(CoreTest::userScaleNote(fresh, 1, 3) != CoreTest::userScaleNote(b, 1, 3), "edited scale differs from default");
}

// A patch file can carry any bytes, with a correct checksum. Edit bytes of
// the app data page, and (separately) of the global settings page, fix the
// checksum with the firmware's own, restore, and run every app on random
// inputs; every setting must stay inside its table range and nothing may
// fault (the sanitizers watch).
void testForgedImage() {
	Core a;
	scramble(a, 7);
	std::vector<uint8_t> valid(kImage);
	a.save(valid.data());
	const int kPerPage = 500;
	Lcg g;
	g.state = 4242;
	Core c;
	int accepted[2] = {};
	for (int page = 0; page < 2; page++) {
		const bool appData = page == 0;
		size_t pageOff, dataOff, dataSize;
		CoreTest::pageLayout(appData, pageOff, dataOff, dataSize);
		for (int n = 0; n < kPerPage; n++) {
			std::vector<uint8_t> img = valid;
			// 1..30 edits; every third image gets 300 (mostly garbage).
			const int edits = n % 3 == 2 ? 300 : 1 + (int) (g.next() >> 8) % 30;
			for (int j = 0; j < edits; j++)
				img[dataOff + (g.next() >> 8) % dataSize] = (uint8_t) (g.next() >> 24);
			CoreTest::fixChecksum(img.data(), appData);
			if (c.restore(img.data()))
				accepted[page]++;
			for (int app = 0; app < shoal::kNumApps; app++) {
				c.selectApp(app);
				for (int ch = 0; ch < 4; ch++) {
					c.setCv(ch, g.volts(-4.f, 7.f));
					c.setGate(ch, (g.next() >> 8) & 1 ? 10.f : 0.f);
				}
				run(c, 0.05);
				uint8_t frame[shoal::kFrameBytes];
				c.drawMenu(frame);
				c.drawScope(frame);
				CHECK(allFinite(c), "%s forged %d, app %d: outputs finite", appData ? "app data" : "global", n, app);
				for (int ch = 0; ch < c.channels(); ch++)
					for (int i = 0; i < c.settingCount(); i++) {
						const shoal::SettingInfo si = c.settingInfo(i);
						const int v = c.setting(ch, i);
						CHECK(v >= si.min && v <= si.max, "%s forged %d, app %d ch %d '%s': %d outside [%d, %d]",
							appData ? "app data" : "global", n, app, ch, si.name, v, si.min, si.max);
					}
			}
		}
	}
	// The forged pages must mostly load, or the test proves nothing.
	CHECK(accepted[0] == kPerPage && accepted[1] == kPerPage, "forged images accepted: app data %d, global %d of %d",
		accepted[0], accepted[1], kPerPage);
}

} // namespace

int main() {
	testRoundTripEveryApp();
	testSaveIsStable();
	testFreshCoreSaves();
	testCorruptImage();
	testRunningStateResets();
	testSequencerPositionResets();
	testUserDataRoundTrip();
	testForgedImage();
	testResetToDefaults();
	testInputRangeSurvives();
	testRestoreClosesAppMenu();
	return rkatest::finish("test_state");
}
