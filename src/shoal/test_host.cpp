// shoal::Host, the engine-side wrapper: the tick rate at engine sample rates,
// control events and restores applied before the next tick, frames and state
// snapshots published for another thread, and the two threads together.
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

#include "testing.hpp"
#include "Host.hpp"
#include "CoreTest.hpp"

namespace shoal {
// Test-only hooks into Host (friend of Host).
struct HostTest {
	static Core& core(Host& h) { return h.core_; }
	static void disableSave(Host& h) { h.saveEnabled_ = false; }
};
}

using shoal::ControlEvent;
using shoal::CoreTest;
using shoal::Host;
using shoal::HostTest;
using shoal::InputRange;

namespace {

constexpr double kRate = 48000.0;

// Run `seconds` of engine samples at `rate`.
void run(Host& h, double seconds, double rate = kRate) {
	const long n = std::lround(seconds * rate);
	for (long i = 0; i < n; i++)
		h.process();
}

ControlEvent turn(shoal::Encoder e, int detents) {
	ControlEvent ev = { ControlEvent::Turn, (int8_t) e, (int16_t) detents };
	return ev;
}

ControlEvent selectApp(int app) {
	ControlEvent ev = { ControlEvent::SelectApp, 0, (int16_t) app };
	return ev;
}

ControlEvent acceleration(bool on) {
	ControlEvent ev = { ControlEvent::SetAcceleration, 0, (int16_t) (on ? 1 : 0) };
	return ev;
}

struct Image {
	uint8_t bytes[shoal::kStateBytes];
};

struct Frames {
	uint8_t menu[shoal::kFrameBytes];
	uint8_t scope[shoal::kFrameBytes];
};

bool blank(const uint8_t* frame) {
	for (int i = 0; i < shoal::kFrameBytes; i++)
		if (frame[i])
			return false;
	return true;
}

void testTickRate() {
	const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
	const long want = std::lround(10.0 * shoal::kTickHz);
	for (double rate : rates) {
		Host h(1);
		h.setSampleRate(rate);
		run(h, 10.0, rate);
		const long got = (long) CoreTest::ticks(HostTest::core(h));
		CHECK(std::labs(got - want) <= 1, "%.0f Hz: %ld ticks in 10 s, want %ld", rate, got, want);
	}
	// A mid-run rate change: the next 10 s run at the new rate.
	Host h(1);
	h.setSampleRate(48000.0);
	run(h, 10.0, 48000.0);
	h.setSampleRate(44100.0);
	const long before = (long) CoreTest::ticks(HostTest::core(h));
	run(h, 10.0, 44100.0);
	const long got = (long) CoreTest::ticks(HostTest::core(h)) - before;
	CHECK(std::labs(got - want) <= 1, "48000 -> 44100 Hz: %ld ticks in 10 s, want %ld", got, want);
}

void testCopierThroughHost() {
	const float code = 1.f / 6553.6f;
	Host h(1);
	h.setCv(0, 2.f);
	run(h, 0.01);
	h.setGate(0, 5.f);
	run(h, 0.005);
	h.setGate(0, 0.f);
	run(h, 0.01);
	CHECK(std::fabs(h.out(0) - 2.f) <= code, "5 ms pulse: out(0) = %.5f V, want 2.0", h.out(0));

	// A pulse shorter than a tick is still latched as a clock.
	h.setCv(0, 3.f);
	run(h, 0.01);
	h.setGate(0, 5.f);
	h.process();
	h.setGate(0, 0.f);
	run(h, 0.01);
	CHECK(std::fabs(h.out(0) - 3.f) <= code, "1-sample pulse: out(0) = %.5f V, want 3.0", h.out(0));
	CHECK(std::fabs(h.out(1) - 2.f) <= code, "1-sample pulse: out(1) = %.5f V, want 2.0", h.out(1));

	// gateHigh follows the Schmitt trigger at 0.1 V / 1 V.
	h.setGate(2, 1.5f);
	CHECK(h.gateHigh(2), "1.5 V is high");
	h.setGate(2, 0.5f);
	CHECK(h.gateHigh(2), "0.5 V stays high");
	h.setGate(2, 0.05f);
	CHECK(!h.gateHigh(2), "0.05 V is low");
}

void testEventsApplied() {
	Host h(1);
	CHECK(h.app() == 0, "boots into app 0");
	CHECK(h.post(turn(shoal::ENCODER_R, 3)), "turn accepted");
	CHECK(h.post(selectApp(3)), "select accepted");
	run(h, 0.2);
	CHECK(h.app() == 3, "app %d after SelectApp 3", h.app());
	CHECK(HostTest::core(h).app() == 3, "the core runs app %d", HostTest::core(h).app());

	int accepted = 0;
	for (int i = 0; i < 64; i++)
		accepted += h.post(turn(shoal::ENCODER_L, 0)) ? 1 : 0;
	CHECK(accepted == 64, "%d of 64 posts accepted", accepted);
	CHECK(!h.post(turn(shoal::ENCODER_L, 0)), "the 65th post is refused");
	run(h, 0.001);  // a few samples: events are applied before a tick
	CHECK(h.post(turn(shoal::ENCODER_L, 0)), "accepted again once drained");
}

void testButtonsAreEdges() {
	Host h(1);
	shoal::Core c;
	c.seed(1);
	const long n = std::lround(1.2 * kRate);
	for (long i = 0; i < n; i++) {
		h.setButton(shoal::BUTTON_R, true);
		h.process();
	}
	c.setButton(shoal::BUTTON_R, true);
	for (long t = std::lround(1.2 * shoal::kTickHz); t > 0; t--)
		c.tick();
	h.setButton(shoal::BUTTON_R, false);
	c.setButton(shoal::BUTTON_R, false);
	run(h, 0.05);
	for (long t = std::lround(0.05 * shoal::kTickHz); t > 0; t--)
		c.tick();
	CHECK(c.appMenuOpen(), "a held R opens the app menu on a bare core");
	CHECK(h.appMenuOpen(), "and through the host");
	CHECK(HostTest::core(h).appMenuOpen(), "the host's core has it open");
}

void testFramesPublished() {
	Host h(1);
	Frames a;
	h.frames(a.menu, a.scope);
	CHECK(!blank(a.menu), "a menu frame before the first process()");
	h.post(selectApp(3));
	run(h, 0.1);
	Frames b;
	h.frames(b.menu, b.scope);
	CHECK(std::memcmp(a.menu, b.menu, shoal::kFrameBytes) != 0, "the menu frame changed after SelectApp");
}

void testSnapshotBeforeProcess() {
	Host a(1);
	run(a, 1.0);
	a.post(selectApp(3));
	run(a, 1.0);
	Image img;
	InputRange r = InputRange::Bipolar5;
	a.snapshot(img.bytes, &r);
	CHECK(r == InputRange::Hardware, "a's range is Hardware");

	Host b(2);
	b.requestRestore(img.bytes, InputRange::Bipolar5);
	Image out;
	r = InputRange::Hardware;
	b.snapshot(out.bytes, &r);
	CHECK(std::memcmp(out.bytes, img.bytes, shoal::kStateBytes) == 0, "snapshot is the requested image");
	CHECK(r == InputRange::Bipolar5, "snapshot range is the requested one");
	run(b, 1.0);
	CHECK(b.app() == 3, "restored app %d, want 3", b.app());
	b.snapshot(out.bytes, &r);
	CHECK(r == InputRange::Bipolar5, "range kept after the engine publishes");
}

// Settings of the current app, channel by channel, equal on two cores.
bool sameSettings(shoal::Core& x, shoal::Core& y) {
	if (x.app() != y.app() || x.settingCount() != y.settingCount() || x.channels() != y.channels())
		return false;
	for (int ch = 0; ch < x.channels(); ch++)
		for (int i = 0; i < x.settingCount(); i++)
			if (x.setting(ch, i) != y.setting(ch, i))
				return false;
	return true;
}

void testRestoreRejects() {
	Host fresh(1);
	run(fresh, 1.0);
	Image want;
	InputRange r;
	fresh.snapshot(want.bytes, &r);

	for (int nullImage = 0; nullImage < 2; nullImage++) {
		Host h(1);
		Image junk;
		std::memset(junk.bytes, 0x5a, sizeof junk.bytes);
		h.requestRestore(nullImage ? nullptr : junk.bytes, InputRange::Hardware);
		run(h, 1.0);
		CHECK(h.app() == 0, "%s: app %d", nullImage ? "null" : "junk", h.app());
		Image got;
		h.snapshot(got.bytes, &r);
		CHECK(std::memcmp(got.bytes, want.bytes, shoal::kStateBytes) == 0,
			"%s: snapshot is a fresh host's", nullImage ? "null" : "junk");
		CHECK(r == InputRange::Hardware, "range");
		CHECK(sameSettings(HostTest::core(h), HostTest::core(fresh)), "%s: settings are a fresh host's",
			nullImage ? "null" : "junk");
	}

	// Straight after a null request the snapshot is a new core's image.
	Host h(1);
	run(h, 0.6);
	h.post(selectApp(3));
	run(h, 0.6);
	h.requestRestore(nullptr, InputRange::Bipolar5);
	Image got, boot;
	shoal::Core c;
	c.save(boot.bytes);
	h.snapshot(got.bytes, &r);
	CHECK(std::memcmp(got.bytes, boot.bytes, shoal::kStateBytes) == 0, "null: a new core's image at once");
	CHECK(r == InputRange::Bipolar5, "null: the requested range");
}

void testSaveDoesNotDisturb() {
	Host a(7), b(7);
	HostTest::disableSave(b);
	a.post(selectApp(3));
	b.post(selectApp(3));
	const long n = std::lround(5.0 * kRate);
	const long period = std::lround(kRate / 100.0);
	long mismatches = 0;
	for (long i = 0; i < n; i++) {
		const float cv = -2.f + 6.f * (float) i / (float) n;
		const float gate = (i % period) < period / 2 ? 10.f : 0.f;
		Host* hs[2] = { &a, &b };
		for (Host* h : hs) {
			h->setCv(0, cv);
			h->setCv(1, -cv);
			h->setGate(0, gate);
			h->process();
		}
		for (int ch = 0; ch < shoal::kNumChannels; ch++)
			if (a.out(ch) != b.out(ch))
				mismatches++;
	}
	CHECK(a.app() == 3 && b.app() == 3, "both run Quantermain");
	CHECK(mismatches == 0, "%ld output samples differ", mismatches);
	CHECK(a.out(0) != 0.f, "the outputs moved");
}

void testThreads() {
	Host h(1);
	h.post(acceleration(false));
	run(h, 0.05);
	const int start = CoreTest::menuSetting(HostTest::core(h));
	CHECK(start >= 0, "a menu cursor on CopierMaschine");

	const long n = std::lround(5.0 * kRate);
	const long pacing = std::lround(0.005 * kRate);  // a detent plays out in 2 ms
	std::atomic<long> samples(0);
	std::atomic<bool> done(false), uiFinished(false);
	long pairs = 0, reads = 0;
	// The UI thread posts a -1 only once its +1 is in the ring, and paces the
	// pairs by engine time so the core's detent queue never overflows.
	std::thread ui([&] {
		Frames f;
		Image img;
		InputRange r;
		long next = 0;
		while (!done.load()) {
			h.frames(f.menu, f.scope);
			h.snapshot(img.bytes, &r);
			(void) h.app();
			(void) h.appMenuOpen();
			reads++;
			if (samples.load() >= next) {
				while (!h.post(turn(shoal::ENCODER_R, 1))) {}
				while (!h.post(turn(shoal::ENCODER_R, -1))) {}
				pairs++;
				next += pacing;
			}
		}
		uiFinished.store(true);
	});
	long i = 0;
	for (; i < n || !uiFinished.load(); i++) {
		if (i == n)
			done.store(true);
		h.setGate(0, i % 480 < 24 ? 10.f : 0.f);
		h.setCv(0, (float) (i % 4800) / 1000.f);
		h.process();
		samples.store(i + 1);
	}
	ui.join();
	run(h, 0.1);  // drain the last queued events and detents
	CHECK(pairs > 100, "only %ld turn pairs posted", pairs);
	CHECK(reads > 100, "only %ld reads", reads);
	const int end = CoreTest::menuSetting(HostTest::core(h));
	CHECK(end == start, "cursor %d after %ld +1/-1 pairs, started at %d", end, pairs, start);
}

} // namespace

int main() {
	testTickRate();
	testCopierThroughHost();
	testEventsApplied();
	testButtonsAreEdges();
	testFramesPublished();
	testSnapshotBeforeProcess();
	testRestoreRejects();
	testSaveDoesNotDisturb();
	testThreads();
	return rkatest::finish("test_host");
}
