// shoal::Host, the engine-side wrapper: the tick rate at engine sample rates,
// control events and restores applied before the next tick, frames and state
// snapshots published for another thread, and the two threads together.
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <thread>
#include <vector>

#include "testing.hpp"
#include "Host.hpp"
#include "CoreTest.hpp"

// Heap allocations and frees made by operator new / delete on this thread
// (a Core and its firmware Context, among everything else).
static thread_local long tAllocs = 0, tFrees = 0;

void* operator new(std::size_t n) {
	tAllocs++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
	if (p)
		tFrees++;
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	operator delete(p);
}

namespace shoal {
// Test-only hooks into Host (friend of Host).
struct HostTest {
	static Core& core(Host& h) { return *h.core_; }
	static void disableSave(Host& h) { h.saveEnabled_ = false; }
	static std::mutex& stateMutex(Host& h) { return h.stateMutex_; }
};
}

using shoal::ControlEvent;
using shoal::CoreTest;
using shoal::FriendlyEdit;
using shoal::FriendlyModel;
using shoal::Host;
using shoal::HostTest;
using shoal::InputRange;
using shoal::Row;
using shoal::RowKind;

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

constexpr int kQuantermain = 3, kLowRents = 6, kSequins = 8;

// Engine time for `frames` frame periods.
double framePeriods(int frames) {
	return frames * Host::kFrameTicks / shoal::kTickHz;
}

FriendlyEdit makeEdit(FriendlyEdit::Type type, int app, int tab, int id, int value) {
	FriendlyEdit e;
	e.type = type;
	e.app = (uint8_t) app;
	e.tab = (uint8_t) tab;
	e.id = (int16_t) id;
	e.value = value;
	return e;
}

const Row* findRow(const FriendlyModel& m, int id) {
	for (int i = 0; i < m.rows; i++)
		if (m.row[i].id == id)
			return &m.row[i];
	return nullptr;
}

// The first row of kind k at or after model row `from` with at least
// `span` + 1 values; null if none.
const Row* findKind(const FriendlyModel& m, RowKind k, int span = 0, int from = 0) {
	for (int i = from; i < m.rows; i++)
		if (m.row[i].kind == k && m.row[i].max - m.row[i].min >= span)
			return &m.row[i];
	return nullptr;
}

// The model row under the firmware's cursor; -1 if none.
int cursorRow(const FriendlyModel& m) {
	for (int i = 0; i < m.rows; i++)
		if (m.row[i].flags & shoal::kRowCursor)
			return i;
	return -1;
}

// A value of r other than its current one.
int otherValue(const Row& r) {
	return r.value == r.min ? r.max : r.min;
}

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

	// A new core's image: the snapshot as soon as the request is made.
	Image boot;
	{
		shoal::Core c;
		c.save(boot.bytes);
	}

	// The rejected image, a null image and a Reset each replace a core that
	// has moved away from the defaults (app 3, a setting changed).
	const char* const how[3] = { "junk", "null", "Reset" };
	for (int mode = 0; mode < 3; mode++) {
		Host h(1);
		run(h, 0.6);
		h.post(selectApp(3));
		run(h, 0.1);
		shoal::Core& before = HostTest::core(h);
		CHECK(h.app() == 3, "%s: app %d before the request", how[mode], h.app());
		CHECK(before.setSettingRaw(0, 0, before.setting(0, 0) == before.settingInfo(0).min
			? before.settingInfo(0).max : before.settingInfo(0).min), "%s: a setting changed", how[mode]);
		run(h, 0.6);  // a save with the change published
		Image junk;
		std::memset(junk.bytes, 0x5a, sizeof junk.bytes);
		if (mode == 2)
			h.post(ControlEvent{ ControlEvent::Reset, 0, 0 });
		else
			h.requestRestore(mode == 1 ? nullptr : junk.bytes, InputRange::Hardware);
		Image got;
		// Before any process(): the defaults, never the rejected bytes.
		h.snapshot(got.bytes, &r);
		CHECK(std::memcmp(got.bytes, boot.bytes, shoal::kStateBytes) == 0, "%s: defaults snapshot at once", how[mode]);
		CHECK(std::memcmp(got.bytes, junk.bytes, shoal::kStateBytes) != 0, "%s: not the rejected image", how[mode]);
		run(h, 1.0);
		CHECK(h.app() == 0, "%s: app %d", how[mode], h.app());
		h.snapshot(got.bytes, &r);
		CHECK(std::memcmp(got.bytes, want.bytes, shoal::kStateBytes) == 0, "%s: snapshot is a fresh host's", how[mode]);
		CHECK(r == InputRange::Hardware, "range");
		CHECK(sameSettings(HostTest::core(h), HostTest::core(fresh)), "%s: settings are a fresh host's", how[mode]);
	}

	// Straight after a null request the snapshot is a new core's image; then
	// the engine runs a new core.
	Host h(1);
	run(h, 0.6);
	h.post(selectApp(3));
	run(h, 0.6);
	CHECK(h.app() == 3, "app 3 before the null request");
	h.requestRestore(nullptr, InputRange::Bipolar5);
	Image got;
	h.snapshot(got.bytes, &r);
	CHECK(std::memcmp(got.bytes, boot.bytes, shoal::kStateBytes) == 0, "null: a new core's image at once");
	CHECK(r == InputRange::Bipolar5, "null: the requested range");
	run(h, 1.0);
	CHECK(h.app() == 0, "null: app %d after the restore ran", h.app());
	h.snapshot(got.bytes, &r);
	CHECK(r == InputRange::Bipolar5, "null: the range is kept by the engine");
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

// Events posted after a restore request are applied to the restored core
// even when the engine first finds the UI holding the lock.
void testEventsWaitForRestore() {
	Host h(1);
	run(h, 0.1);
	h.requestRestore(nullptr, InputRange::Hardware);
	h.post(selectApp(3));
	std::atomic<int> step(0);
	std::thread holder([&] {
		std::lock_guard<std::mutex> lock(HostTest::stateMutex(h));
		step.store(1);
		while (step.load() != 2) {}
	});
	while (step.load() != 1) {}
	run(h, 0.05);  // the swap and the events must both wait
	CHECK(h.app() == 0, "app %d while the restore waits", h.app());
	step.store(2);
	holder.join();
	run(h, 0.1);
	CHECK(h.app() == 3, "app %d: SelectApp applied after the restore", h.app());
}

// A new Host has a model before the engine runs; the model follows the app.
void testModelPublished() {
	Host h(1);
	FriendlyModel m;
	std::memset(&m, 0xa5, sizeof m);
	h.model(m);
	CHECK(m.app == 0 && m.rows > 0, "a model for app %d with %d rows before the first process()", m.app, m.rows);
	CHECK(!(m.flags & shoal::kModelTruncated), "not truncated");
	h.post(selectApp(kQuantermain));
	run(h, framePeriods(2));
	h.model(m);
	CHECK(m.app == kQuantermain && m.tabs == 4, "model for app %d with %d tabs after SelectApp", m.app, m.tabs);
	FriendlyModel direct;
	HostTest::core(h).friendlyModel(direct);
	CHECK(m.rows == direct.rows, "published %d rows, the core has %d", m.rows, direct.rows);
}

// A posted SetValue shows in the published model within two frame periods.
void testEditsApplied() {
	Host h(1);
	h.post(selectApp(kQuantermain));
	run(h, 0.1);
	FriendlyModel m;
	h.model(m);
	const Row* r = findKind(m, RowKind::Number, 2);
	CHECK(r != nullptr, "Quantermain has a Number row");
	if (!r)
		return;
	const int id = r->id, target = otherValue(*r);
	CHECK(h.postEdit(makeEdit(FriendlyEdit::SetValue, kQuantermain, m.tab, id, target)), "edit accepted");
	h.model(m);
	CHECK(findRow(m, id)->value != target, "not shown before the engine runs");
	run(h, framePeriods(2));
	h.model(m);
	CHECK(findRow(m, id)->value == target, "row %d shows %d, want %d", id, findRow(m, id)->value, target);

	const Row* mask = findKind(m, RowKind::Mask);
	CHECK(mask && mask->max > 1, "Quantermain has a mask row with cells");
	if (!mask)
		return;
	const int maskId = mask->id, before = mask->value;
	CHECK(h.postEdit(makeEdit(FriendlyEdit::ToggleBit, kQuantermain, m.tab, maskId, 1)), "toggle accepted");
	run(h, framePeriods(2));
	h.model(m);
	CHECK(findRow(m, maskId)->value == (before ^ 2), "mask %#x, want %#x", findRow(m, maskId)->value, before ^ 2);

	// The edit queue is the control event queue: 64 slots between them.
	int accepted = 0;
	for (int i = 0; i < 40; i++)
		accepted += h.post(turn(shoal::ENCODER_L, 0)) ? 1 : 0;
	for (int i = 0; i < 24; i++)
		accepted += h.postEdit(makeEdit(FriendlyEdit::Focus, kQuantermain, m.tab, id, 0)) ? 1 : 0;
	CHECK(accepted == 64, "%d of 64 posts accepted", accepted);
	CHECK(!h.postEdit(makeEdit(FriendlyEdit::Focus, kQuantermain, m.tab, id, 0)), "the 65th is refused");
	run(h, 0.01);
	CHECK(h.postEdit(makeEdit(FriendlyEdit::Focus, kQuantermain, m.tab, id, 0)), "accepted again once drained");
}

// A Turn, an edit and a Turn are applied in posting order: the edit waits
// for the first turn to reach the app, and the second turn follows it.
void testEditOrder() {
	Host h(1);
	h.post(acceleration(false));
	h.post(selectApp(kQuantermain));
	run(h, 0.1);
	FriendlyModel m;
	h.model(m);
	// The rows the R encoder walks through, in menu order.
	int menu[shoal::kMaxRows], n = 0;
	for (int i = 0; i < m.rows; i++)
		if (!(m.row[i].flags & shoal::kRowExtra) && m.row[i].kind != RowKind::Heading)
			menu[n++] = i;
	CHECK(n >= 7 && cursorRow(m) == menu[0], "%d menu rows, cursor on row %d", n, cursorRow(m));
	if (n < 7)
		return;
	h.post(turn(shoal::ENCODER_R, 1));
	h.postEdit(makeEdit(FriendlyEdit::Focus, kQuantermain, m.tab, m.row[menu[4]].id, 0));
	h.post(turn(shoal::ENCODER_R, 1));
	run(h, 0.1);
	h.model(m);
	// In order: row 1, then 4, then 5. The edit first: 4, then 6. The edit last: 2, then 4.
	CHECK(cursorRow(m) == menu[5], "cursor on model row %d, want %d (menu row 5)", cursorRow(m), menu[5]);
}

// An edit posted for the old app before a restore to another app is dropped:
// the restore runs first, and the edit names the old app.
void testEditAfterRestore() {
	Host lowRents(1);
	lowRents.post(selectApp(kLowRents));
	run(lowRents, 1.0);
	Image img;
	InputRange r;
	lowRents.snapshot(img.bytes, &r);
	FriendlyModel want;
	lowRents.model(want);
	const Row* row = findKind(want, RowKind::Number, 2);
	CHECK(row != nullptr, "Low-rents has a Number row");
	if (!row)
		return;
	const int id = row->id, target = otherValue(*row);

	Host h(1);
	h.post(selectApp(kQuantermain));
	run(h, 0.1);
	CHECK(h.postEdit(makeEdit(FriendlyEdit::SetValue, kQuantermain, 0, id, target)), "old-app edit accepted");
	h.requestRestore(img.bytes, InputRange::Hardware);
	FriendlyModel m;
	run(h, 0.1);
	h.model(m);
	CHECK(m.app == kLowRents, "model for app %d after the restore", m.app);
	CHECK(findRow(m, id) && findRow(m, id)->value == row->value, "row %d is %d, want %d (the edit dropped)",
		id, findRow(m, id) ? findRow(m, id)->value : -1, row->value);

	// The same edit drawn for the restored app is applied.
	CHECK(h.postEdit(makeEdit(FriendlyEdit::SetValue, kLowRents, 0, id, target)), "new-app edit accepted");
	run(h, framePeriods(2));
	h.model(m);
	CHECK(findRow(m, id)->value == target, "row %d is %d, want %d", id, findRow(m, id)->value, target);

	// An edit for the app the image holds, posted before the restore, is
	// applied to the restored core (events posted before a restore are
	// applied after it).
	{
		Host same(1);
		same.post(selectApp(kQuantermain));
		run(same, 0.1);
		CHECK(same.postEdit(makeEdit(FriendlyEdit::SetValue, kLowRents, 0, id, target)), "same-app edit accepted");
		same.requestRestore(img.bytes, InputRange::Hardware);
		run(same, 0.1);
		same.model(m);
		CHECK(m.app == kLowRents, "model for app %d after the restore", m.app);
		CHECK(findRow(m, id) && findRow(m, id)->value == target, "row %d is %d, want %d (the edit applied to the restored core)",
			id, findRow(m, id) ? findRow(m, id)->value : -1, target);
	}

	// The next model published after the swap is the new core's, at its
	// first tick, wherever the frame schedule was (two phases half a frame
	// apart).
	for (int half = 0; half < 2; half++) {
		Host g(1);
		g.post(selectApp(kQuantermain));
		run(g, 0.1 + half * framePeriods(1) / 2);
		g.requestRestore(img.bytes, InputRange::Hardware);
		run(g, 3.0 / shoal::kTickHz);
		g.model(m);
		CHECK(m.app == kLowRents, "phase %d: model for app %d straight after the swap", half, m.app);
	}
}

// Encoder turns and an R push show in the model: the cursor row, edit mode
// and the value.
void testModelFollowsEncoders() {
	Host h(1);
	h.post(acceleration(false));
	h.post(selectApp(kQuantermain));
	run(h, 0.1);
	FriendlyModel m;
	h.model(m);
	int menu[shoal::kMaxRows], n = 0;
	for (int i = 0; i < m.rows; i++)
		if (!(m.row[i].flags & shoal::kRowExtra) && m.row[i].kind != RowKind::Heading)
			menu[n++] = i;
	// A Number row past the first menu row, below its maximum.
	int k = -1;
	for (int i = 1; i < n && k < 0; i++)
		if (m.row[menu[i]].kind == RowKind::Number && m.row[menu[i]].value < m.row[menu[i]].max)
			k = i;
	CHECK(k > 0 && cursorRow(m) == menu[0], "a Number row at menu row %d; cursor on %d", k, cursorRow(m));
	if (k <= 0)
		return;
	const int id = m.row[menu[k]].id, before = m.row[menu[k]].value;
	h.post(turn(shoal::ENCODER_R, k));
	run(h, 0.05);
	h.model(m);
	CHECK(cursorRow(m) == menu[k], "cursor on model row %d after %d detents, want %d", cursorRow(m), k, menu[k]);
	CHECK(!(m.row[menu[k]].flags & shoal::kRowEditing), "not editing yet");
	h.setButton(shoal::BUTTON_R, true);
	run(h, 0.05);
	h.setButton(shoal::BUTTON_R, false);
	run(h, 0.05);
	h.model(m);
	CHECK(m.row[menu[k]].flags & shoal::kRowEditing, "editing after an R push");
	h.post(turn(shoal::ENCODER_R, 1));
	run(h, 0.05);
	h.model(m);
	CHECK(findRow(m, id)->value == before + 1, "row %d is %d after a detent, want %d", id, findRow(m, id)->value, before + 1);
}

// Heap operations on this thread while running `seconds` of samples.
long engineHeapOps(Host& h, double seconds) {
	const long before = tAllocs + tFrees;
	run(h, seconds);
	return tAllocs + tFrees - before;
}

// The engine path never allocates, frees or boots firmware, also while it
// swaps in a restored or reset core: those are built, and the retired ones
// deleted, on the requesting thread.
void testNoEngineAllocation() {
	Host a(1);
	a.post(selectApp(3));
	run(a, 1.0);
	Image img;
	InputRange r;
	a.snapshot(img.bytes, &r);

	Host h(1);
	h.setButton(shoal::BUTTON_UP, false);
	CHECK(engineHeapOps(h, 1.0) == 0, "running");
	h.post(acceleration(false));
	h.post(turn(shoal::ENCODER_R, 2));
	CHECK(engineHeapOps(h, 0.1) == 0, "events");

	shoal::Core* old = &HostTest::core(h);
	long allocs = tAllocs;
	h.requestRestore(img.bytes, InputRange::Bipolar5);
	CHECK(tAllocs > allocs, "the restored core is built by the requester");
	CHECK(engineHeapOps(h, 0.1) == 0, "swapping in a restored core");
	CHECK(&HostTest::core(h) != old, "the engine runs a new core");
	CHECK(h.app() == 3, "restored app %d", h.app());
	long frees = tFrees;
	h.snapshot(img.bytes, &r);
	CHECK(tFrees > frees, "the retired core is deleted by snapshot()");
	CHECK(r == InputRange::Bipolar5, "restored range");

	old = &HostTest::core(h);
	allocs = tAllocs;
	CHECK(h.post(ControlEvent{ ControlEvent::Reset, 0, 0 }), "Reset accepted");
	CHECK(tAllocs > allocs, "the reset core is built by the requester");
	CHECK(engineHeapOps(h, 0.6) == 0, "swapping in a reset core");
	CHECK(&HostTest::core(h) != old, "the engine runs a new core after Reset");
	CHECK(h.app() == 0, "app %d after Reset", h.app());
	h.snapshot(img.bytes, &r);
	CHECK(r == InputRange::Bipolar5, "Reset keeps the input range");

	// A replacement that was never picked up is deleted by the next request.
	frees = tFrees;
	h.requestRestore(nullptr, InputRange::Hardware);
	h.requestRestore(nullptr, InputRange::Hardware);
	CHECK(tFrees > frees, "an unused replacement is deleted");
	CHECK(engineHeapOps(h, 0.1) == 0, "swapping in the last one");

	// Models built and published, and edits applied: Quantermain (tabs, a
	// mask) and Sequins (two pages: a row of the other page builds its list
	// on a copy of the channel, and switches page).
	FriendlyModel m;
	h.post(selectApp(kQuantermain));
	CHECK(engineHeapOps(h, 0.1) == 0, "Quantermain models");
	h.model(m);
	CHECK(m.app == kQuantermain, "Quantermain's model");
	const Row* num = findKind(m, RowKind::Number, 2);
	const Row* mask = findKind(m, RowKind::Mask);
	CHECK(num && mask, "Quantermain has a Number and a Mask row");
	if (!num || !mask)
		return;
	h.postEdit(makeEdit(FriendlyEdit::SetValue, kQuantermain, m.tab, num->id, otherValue(*num)));
	h.postEdit(makeEdit(FriendlyEdit::ToggleBit, kQuantermain, m.tab, mask->id, 0));
	h.postEdit(makeEdit(FriendlyEdit::SelectTab, kQuantermain, m.tab, 0, 2));
	h.postEdit(makeEdit(FriendlyEdit::Focus, kQuantermain, 2, num->id, 0));
	CHECK(engineHeapOps(h, 0.1) == 0, "Quantermain edits");
	h.model(m);
	CHECK(m.tab == 2, "the edits ran (tab %d)", m.tab);

	h.post(selectApp(kSequins));
	CHECK(engineHeapOps(h, 0.1) == 0, "Sequins models");
	h.model(m);
	int heading = -1;
	for (int i = 0, seen = 0; i < m.rows && heading < 0; i++)
		if (m.row[i].kind == RowKind::Heading && ++seen == 2)
			heading = i;
	const Row* other = heading < 0 ? nullptr : findKind(m, RowKind::Choice, 1, heading);
	const Row* steps = findKind(m, RowKind::Mask, 1);
	CHECK(m.app == kSequins && other && steps, "Sequins has a second page with a Choice row, and a mask");
	if (!other || !steps)
		return;
	const int otherId = other->id, target = otherValue(*other);
	h.postEdit(makeEdit(FriendlyEdit::SetValue, kSequins, m.tab, otherId, target));
	h.postEdit(makeEdit(FriendlyEdit::ToggleBit, kSequins, m.tab, steps->id, 0));
	CHECK(engineHeapOps(h, 0.1) == 0, "Sequins edits");
	h.model(m);
	CHECK(findRow(m, otherId)->value == target, "the Sequins edit ran");
}

void testThreads() {
	Host h(1);
	h.post(acceleration(false));
	run(h, 0.05);
	// Move the cursor off both ends of the menu: the cursor clamps there,
	// which would hide a lost or extra detent. (menuSetting is a setting
	// index, not a menu position, so the ends are found by turning.)
	shoal::Core& c = HostTest::core(h);
	const int first = CoreTest::menuSetting(c);
	h.post(turn(shoal::ENCODER_R, 60));
	run(h, 0.2);
	const int last = CoreTest::menuSetting(c);
	h.post(turn(shoal::ENCODER_R, -60));
	run(h, 0.2);
	CHECK(CoreTest::menuSetting(c) == first && first >= 0 && last != first, "menu ends %d, %d", first, last);
	h.post(turn(shoal::ENCODER_R, 2));
	run(h, 0.05);
	const int start = CoreTest::menuSetting(c);
	CHECK(start != first && start != last, "cursor on %d is off both ends (%d, %d)", start, first, last);
	for (int dir = -1; dir <= 1; dir += 2) {
		h.post(turn(shoal::ENCODER_R, dir));
		run(h, 0.02);
		const int moved = CoreTest::menuSetting(c);
		h.post(turn(shoal::ENCODER_R, -dir));
		run(h, 0.02);
		CHECK(moved != start && CoreTest::menuSetting(c) == start, "a %+d detent moves the cursor and back", dir);
	}

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
	const int end = CoreTest::menuSetting(c);
	CHECK(end == start, "cursor %d after %ld +1/-1 pairs, started at %d", end, pairs, start);
}

// A UI thread posting edits and reading models for a second while the
// engine runs: no race, no torn model, and the row ends on the last value
// the queue accepted.
void testEditsFromUiThread() {
	Host h(1);
	h.post(selectApp(kQuantermain));
	run(h, 0.1);
	FriendlyModel start;
	h.model(start);
	const Row* row = findKind(start, RowKind::Number, 2);
	CHECK(row != nullptr, "Quantermain has a Number row");
	if (!row)
		return;
	const int id = row->id, lo = row->min, span = row->max - row->min + 1;

	const long n = std::lround(1.0 * kRate);
	std::atomic<bool> done(false), uiFinished(false);
	long posted = 0, refused = 0, reads = 0, bad = 0;
	int last = row->value;
	std::thread ui([&] {
		FriendlyModel m;
		Frames f;
		uint32_t rng = 12345;
		while (!done.load()) {
			h.model(m);
			h.frames(f.menu, f.scope);
			reads++;
			const Row* r = findRow(m, id);
			if (m.app != kQuantermain || m.rows <= 0 || m.rows > shoal::kMaxRows || (m.flags & shoal::kModelTruncated)
					|| !r || r->value < r->min || r->value > r->max)
				bad++;
			rng = rng * 1664525u + 1013904223u;
			const int target = lo + (int) ((rng >> 8) % (uint32_t) span);
			if (h.postEdit(makeEdit(FriendlyEdit::SetValue, kQuantermain, m.tab, id, target))) {
				last = target;
				posted++;
			} else {
				refused++;
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
	}
	ui.join();
	run(h, framePeriods(2));
	FriendlyModel m;
	h.model(m);
	CHECK(posted > 100 && reads > 100, "%ld edits posted (%ld refused), %ld reads", posted, refused, reads);
	CHECK(bad == 0, "%ld malformed models read", bad);
	CHECK(findRow(m, id) && findRow(m, id)->value == last, "row %d ends on %d, the last accepted is %d",
		id, findRow(m, id) ? findRow(m, id)->value : -1, last);
}

} // namespace

// --- The output history published for the upper screen (ScopeView.hpp). ---

struct Scoped {
	shoal::ScopeView view;
	shoal::ScopeHistory hist;
};

// Heap-held: a history is 16 kB.
std::unique_ptr<Scoped> scoped(Host& h) {
	std::unique_ptr<Scoped> s(new Scoped);
	h.scope(s->view, s->hist);
	return s;
}

// The published columns and points against the test's own record of every
// tick's outputs. The host is run at one tick a sample, so the record is
// complete; where the host's windows start is found, not assumed: exactly
// one alignment must fit.
void testHistoryMatchesOutputs() {
	Host h(1);
	h.setSampleRate(shoal::kTickHz);
	// Viznutcracker sweet: outputs that jump both ways within a column.
	CHECK(h.post(selectApp(10)), "select Viznutcracker sweet");
	// 6 s: longer than either ring (4 s each), so both have wrapped.
	const long n = (long) (6.0 * shoal::kTickHz);
	std::vector<float> rec[shoal::kNumChannels];
	for (long i = 0; i < n; i++) {
		h.process();
		for (int ch = 0; ch < shoal::kNumChannels; ch++)
			rec[ch].push_back(h.out(ch));
	}
	const auto s = scoped(h);
	CHECK(s->view.app == 10, "view.app %d", s->view.app);
	CHECK(s->hist.columns == shoal::kTraceColumns, "columns %d", s->hist.columns);
	CHECK(s->hist.points == shoal::kXyPoints, "points %d", s->hist.points);
	// Columns: the newest ends at tick `end`; column k from the end covers
	// the 260 ticks ending at end - 260 k.
	const int cols = 250;
	int fits = 0;
	// The outputs must turn round inside most columns, or a column's last
	// value would pass for its highest.
	int turning = 0;
	for (int col = 6; col < 250; col++) {
		const long e = n - 1 - 260L * col;
		float hi = rec[0][e];
		for (long t = e - 259; t <= e; t++)
			hi = std::max(hi, rec[0][t]);
		turning += hi > rec[0][e] + 0.1f ? 1 : 0;
	}
	CHECK(turning > 100, "output A turned round inside only %d columns: the test proves little", turning);
	bool moved = false;
	for (long end = n - 1; end > n - 1 - 600; end--) {
		bool ok = true;
		for (int k = 0; k < cols && ok; k++) {
			const int col = s->hist.columns - 1 - k;
			for (int ch = 0; ch < shoal::kNumChannels && ok; ch++) {
				float lo = rec[ch][end - 260L * k], hi = lo;
				for (long t = end - 260L * k - 259; t <= end - 260L * k; t++) {
					lo = std::min(lo, rec[ch][t]);
					hi = std::max(hi, rec[ch][t]);
				}
				ok = s->hist.lo[ch][col] == lo && s->hist.hi[ch][col] == hi;
				moved = moved || hi - lo > 0.01f;
			}
		}
		fits += ok ? 1 : 0;
	}
	CHECK(moved, "the outputs did not move: the test proves nothing");
	CHECK(fits == 1, "the last %d columns fit the recorded outputs at %d alignments, want 1", cols, fits);
	const int pts = 500;
	CHECK(n > (long) shoal::kXyPointTicks * (pts + 4), "the run is shorter than the X/Y ring");
	int fitsXy = 0;
	for (long end = n - 1; end > n - 1 - 400; end--) {
		bool ok = true;
		for (int k = 0; k < pts && ok; k++)
			for (int ch = 0; ch < shoal::kNumChannels && ok; ch++)
				ok = s->hist.xy[ch][s->hist.points - 1 - k] == rec[ch][end - (long) shoal::kXyPointTicks * k];
		fitsXy += ok ? 1 : 0;
	}
	CHECK(fitsXy == 1, "the last %d X/Y points fit the recorded outputs at %d alignments, want 1", pts, fitsXy);
}

// Empty on a new host, filling to 256 columns and 512 points, then scrolling.
void testHistoryFillsThenScrolls() {
	Host h(1);
	h.setSampleRate(shoal::kTickHz);
	CHECK(scoped(h)->hist.columns == 0 && scoped(h)->hist.points == 0, "a new host's history is not empty");
	int lastCols = 0, lastPts = 0;
	bool grows = true;
	for (int step = 0; step < 60; step++) {
		run(h, 0.1, shoal::kTickHz);
		const auto s = scoped(h);
		grows = grows && s->hist.columns >= lastCols && s->hist.points >= lastPts;
		lastCols = s->hist.columns;
		lastPts = s->hist.points;
		if (step == 4)
			CHECK(lastCols >= 28 && lastCols <= 33 && lastPts >= 8333 / shoal::kXyPointTicks - 3 && lastPts <= 8333 / shoal::kXyPointTicks + 1,
			      "after 0.5 s: %d columns, %d points", lastCols, lastPts);
	}
	CHECK(grows, "the history shrank while running");
	CHECK(lastCols == shoal::kTraceColumns && lastPts == shoal::kXyPoints, "after 6 s: %d columns, %d points", lastCols, lastPts);
}

// One Piqued envelope, at engine rates where ticks and samples do not line
// up: it shows in lane A as one run of columns, about as long as the
// envelope, and nowhere else.
void testSpikeShows() {
	for (double rate : {8000.0, 48000.0, 768000.0}) {
		Host h(1);
		h.setSampleRate(rate);
		CHECK(h.post(selectApp(7)), "select Piqued");
		// Piqued plays every envelope once when it is selected: let that
		// leave the 4 s history first.
		run(h, 5.0, rate);
		h.setGate(0, 10.f);
		double above = 0.0;   // seconds output A spent over 1 V
		const long n = std::lround(1.0 * rate);
		for (long i = 0; i < n; i++) {
			if (i == std::lround(0.002 * rate))
				h.setGate(0, 0.f);
			h.process();
			above += h.out(0) > 1.f ? 1.0 / rate : 0.0;
		}
		const auto s = scoped(h);
		CHECK(above > 0.001, "at %g Hz the envelope never passed 1 V", rate);
		int first = -1, last = -1, count = 0;
		bool others = false;
		for (int col = 0; col < s->hist.columns; col++) {
			if (s->hist.hi[0][col] > 1.f) {
				first = first < 0 ? col : first;
				last = col;
				count++;
			}
			for (int ch = 1; ch < shoal::kNumChannels; ch++)
				others = others || s->hist.hi[ch][col] > 1.f;
		}
		const double column = 260.0 / shoal::kTickHz;
		CHECK(count >= 1 && count == last - first + 1, "at %g Hz: %d columns over 1 V, from %d to %d", rate, count, first, last);
		CHECK(count >= (int) (above / column) && count <= (int) (above / column) + 2, "at %g Hz: %d columns for %g s over 1 V", rate,
		      count, above);
		CHECK(!others, "at %g Hz the envelope shows in another lane", rate);
	}
}

// Nothing from before an app change, a restore or a reset is published
// after it.
void testHistoryClearedOnChange() {
	Host h(1);
	h.setSampleRate(shoal::kTickHz);
	CHECK(h.post(selectApp(5)), "select Quadraturia");
	run(h, 2.0, shoal::kTickHz);
	CHECK(scoped(h)->hist.columns > 100, "no history to clear");
	CHECK(h.post(selectApp(7)), "select Piqued");
	bool seen = false;
	for (int i = 0; i < 2000 && !seen; i++) {
		h.process();
		const auto s = scoped(h);
		if (s->view.app == 7) {
			seen = true;
			CHECK(s->hist.columns <= 1 && s->hist.points <= 278 / shoal::kXyPointTicks + 1, "the first Piqued view has %d columns, %d points", s->hist.columns,
			      s->hist.points);
		}
		else {
			CHECK(s->view.app == 5, "view.app %d between the apps", s->view.app);
		}
	}
	CHECK(seen, "Piqued's view was never published");
	for (int mode = 0; mode < 2; mode++) {
		run(h, 2.0, shoal::kTickHz);
		CHECK(scoped(h)->hist.columns > 100, "no history to clear");
		if (mode == 0) {
			Image img;
			InputRange r;
			h.snapshot(img.bytes, &r);
			h.requestRestore(img.bytes, r);
		}
		else {
			h.requestReset();
		}
		run(h, 0.05, shoal::kTickHz);   // 833 ticks: three frames
		const auto s = scoped(h);
		CHECK(s->hist.columns <= 4 && s->hist.points <= 834 / shoal::kXyPointTicks + 1, "%s: %d columns, %d points left", mode ? "reset" : "restore", s->hist.columns,
		      s->hist.points);
	}
}

// Another thread reading as fast as it can gets whole histories.
void testHistoryNotTorn() {
	Host h(1);
	h.setSampleRate(kRate);
	CHECK(h.post(selectApp(5)), "select Quadraturia");
	run(h, 0.1);
	std::atomic<bool> stop{false};
	std::atomic<long> reads{0}, bad{0};
	std::thread ui([&]() {
		std::unique_ptr<Scoped> s(new Scoped);
		while (!stop.load()) {
			h.scope(s->view, s->hist);
			bool ok = s->view.app == 5 && s->hist.columns >= 0 && s->hist.columns <= shoal::kTraceColumns && s->hist.points >= 0 &&
			          s->hist.points <= shoal::kXyPoints;
			for (int ch = 0; ch < shoal::kNumChannels && ok; ch++) {
				for (int c = 0; c < s->hist.columns && ok; c++)
					ok = s->hist.lo[ch][c] <= s->hist.hi[ch][c] && s->hist.lo[ch][c] > -3.76f && s->hist.hi[ch][c] < 6.26f;
				for (int p = 0; p < s->hist.points && ok; p++)
					ok = s->hist.xy[ch][p] > -3.76f && s->hist.xy[ch][p] < 6.26f;
			}
			bad += ok ? 0 : 1;
			reads++;
		}
	});
	run(h, 2.0);
	stop.store(true);
	ui.join();
	CHECK(reads.load() > 100, "the reader ran only %ld times", reads.load());
	CHECK(bad.load() == 0, "%ld of %ld histories were torn or out of range", bad.load(), reads.load());
}

int main() {
	testTickRate();
	testCopierThroughHost();
	testEventsApplied();
	testButtonsAreEdges();
	testFramesPublished();
	testSnapshotBeforeProcess();
	testRestoreRejects();
	testSaveDoesNotDisturb();
	testNoEngineAllocation();
	testEventsWaitForRestore();
	testThreads();
	testModelPublished();
	testEditsApplied();
	testEditOrder();
	testEditAfterRestore();
	testModelFollowsEncoders();
	testEditsFromUiThread();
	testHistoryMatchesOutputs();
	testHistoryFillsThenScrolls();
	testSpikeShows();
	testHistoryClearedOnChange();
	testHistoryNotTorn();
	return rkatest::finish("test_host");
}
