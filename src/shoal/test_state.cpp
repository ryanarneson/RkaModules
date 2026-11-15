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
	auto attempt = [&](const char* what, int n) {
		const bool ok = c.restore(img.data());
		run(c, 0.5);
		CHECK(allFinite(c), "%s %d: outputs finite", what, n);
		checkDefaultsOrRestored(c, ok, freshSettings, what, n);
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

} // namespace

int main() {
	testRoundTripEveryApp();
	testSaveIsStable();
	testFreshCoreSaves();
	testCorruptImage();
	testRunningStateResets();
	testResetToDefaults();
	testInputRangeSurvives();
	testRestoreClosesAppMenu();
	return rkatest::finish("test_state");
}
