// Friendly mode in the core: the row model of every app, and edits carried
// out through the app's own handlers. The equivalence tests give one core a
// FriendlyEdit and a second core the hardware gesture the app survey
// describes (docs/specs/2026-11-15-shoal-app-survey.md), as single events
// straight to the app's handlers, and require the two to end up identical.
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "testing.hpp"
#include "test_util.hpp"
#include "CoreTest.hpp"

using shoal::Core;
using shoal::CoreTest;
using shoal::FriendlyEdit;
using shoal::FriendlyModel;
using shoal::Row;
using shoal::RowKind;

namespace {

constexpr int kQuantermain = 3, kQuadraturia = 5, kLowRents = 6;
constexpr int kExtraId = 256;

// The settings whose value-name array is shorter than their range (the
// survey's "Value names shorter than the setting's range"), with the length
// of the array in the firmware source.
struct ShortNames { int app; const char* label; int count; };
const ShortNames kShortNames[] = {
	{ 4, "CV source", 4 },       // OC_strings.cpp:25 cv_input_names[4], range 0..4
	{ 7, "Trigger input", 4 },   // OC_strings.cpp:21 trigger_input_names[4], range 0..6
	{ 11, "chord:", 8 },         // APP_CHORDS.ino:984 chords_slots, 8 names, range 0..CHORDS_USER_LAST - 1
};

// Labels an adapter gives in place of the settings table's (allowed where
// the table's name is a one- or two-letter abbreviation).
struct LabelOverride { int app, setting; const char* table; const char* label; };
const LabelOverride kLabelOverrides[] = {
	{ 5, 0, "C", "Coarse" },
	{ 5, 1, "F", "Fine" },
};

const ShortNames* shortNamesFor(int app, const char* label) {
	for (const ShortNames& s : kShortNames)
		if (s.app == app && std::strcmp(s.label, label) == 0)
			return &s;
	return nullptr;
}

FriendlyModel model(const Core& c) {
	FriendlyModel m;
	std::memset(&m, 0xa5, sizeof(m));  // the core must fill every field it publishes
	c.friendlyModel(m);
	return m;
}

const Row* findRow(const FriendlyModel& m, int id) {
	for (int i = 0; i < m.rows; i++)
		if (m.row[i].id == id)
			return &m.row[i];
	return nullptr;
}

bool isValueRow(const Row& r) {
	return r.kind == RowKind::Number || r.kind == RowKind::Choice || r.kind == RowKind::Toggle;
}

bool terminated(const char* s, int size) {
	return std::memchr(s, 0, (size_t) size) != nullptr;
}

// The checks every model must pass at any time. `what` names the caller.
// Returns the number of rows under the cursor.
int checkWellFormed(const Core& c, const FriendlyModel& m, const char* what) {
	const int app = c.app();
	const char* an = Core::appName(app);
	CHECK(m.app == app, "%s %s: model app %d, core app %d", what, an, m.app, app);
	CHECK(m.rows >= 1 && m.rows <= shoal::kMaxRows, "%s %s: %d rows", what, an, m.rows);
	CHECK(m.tabs <= shoal::kMaxTabs, "%s %s: %d tabs", what, an, m.tabs);
	CHECK(m.tab < (m.tabs ? m.tabs : 1), "%s %s: tab %d of %d", what, an, m.tab, m.tabs);
	CHECK((m.flags & ~shoal::kModelBusy) == 0, "%s %s: model flags %d (2: rows were cut off)", what, an, m.flags);
	CHECK(((m.flags & shoal::kModelBusy) != 0) == c.appMenuOpen(), "%s %s: model flags %d, app menu %s", what, an, m.flags,
		c.appMenuOpen() ? "open" : "closed");
	for (int t = 0; m.tabs > 1 && t < m.tabs && t < shoal::kMaxTabs; t++)  // one tab: no tab bar, no label
		CHECK(m.tabLabel[t] != nullptr && std::strlen(m.tabLabel[t]) > 0, "%s %s: tab %d has no label", what, an, t);
	CHECK(terminated(m.status, shoal::kStatusText), "%s %s: status not terminated", what, an);
	int cursors = 0;
	for (int i = 0; i < m.rows && i < shoal::kMaxRows; i++) {
		const Row& r = m.row[i];
		CHECK((int) r.kind <= (int) RowKind::Heading, "%s %s row %d: kind %d", what, an, i, (int) r.kind);
		CHECK((r.flags & ~(shoal::kRowCursor | shoal::kRowEditing | shoal::kRowExtra)) == 0,
			"%s %s row %d: flags %d", what, an, i, r.flags);
		CHECK(r.label != nullptr, "%s %s row %d: null label", what, an, i);
		CHECK(terminated(r.text, shoal::kRowText), "%s %s row %d: text not terminated", what, an, i);
		CHECK(((r.flags & shoal::kRowExtra) != 0) == (r.id >= kExtraId), "%s %s row %d: id %d, flags %d",
			what, an, i, r.id, r.flags);
		if (r.flags & shoal::kRowCursor)
			cursors++;
		else
			CHECK(!(r.flags & shoal::kRowEditing), "%s %s row %d: editing without the cursor", what, an, i);
		if (isValueRow(r))
			CHECK(r.min <= r.value && r.value <= r.max && r.min <= r.def && r.def <= r.max,
				"%s %s row %d (%s): value %d, default %d, range %d..%d", what, an, i, r.label ? r.label : "?",
				(int) r.value, (int) r.def, (int) r.min, (int) r.max);
		if (r.kind == RowKind::Toggle)
			CHECK(r.min == 0 && r.max == 1, "%s %s row %d: toggle range %d..%d", what, an, i, (int) r.min, (int) r.max);
		if (r.kind == RowKind::Mask)
			CHECK(r.max >= 0 && r.max <= 32, "%s %s row %d: %d mask cells", what, an, i, (int) r.max);
		if (r.kind == RowKind::Choice) {
			const int range = r.max - r.min + 1;
			const ShortNames* s = r.label && !(r.flags & shoal::kRowExtra) ? shortNamesFor(app, r.label) : nullptr;
			CHECK(r.names != nullptr, "%s %s row %d: choice without names", what, an, i);
			CHECK(r.nameCount >= 1 && r.nameCount <= range, "%s %s row %d (%s): %d names for %d values",
				what, an, i, r.label, r.nameCount, range);
			if (s)
				CHECK(r.nameCount == s->count, "%s %s row %d (%s): %d names, the array has %d",
					what, an, i, r.label, r.nameCount, s->count);
			else
				CHECK(r.nameCount == range, "%s %s row %d (%s): %d names for %d values",
					what, an, i, r.label, r.nameCount, range);
			// Every published name is readable (the address sanitizer faults
			// on a count past the end of the firmware's array).
			for (int k = 0; r.names && k < r.nameCount && k < range; k++)
				CHECK(r.names[k] != nullptr && std::strlen(r.names[k]) < 64, "%s %s row %d: name %d", what, an, i, k);
			// A value without a name shows as its number.
			if (r.value - r.min >= r.nameCount)
				CHECK(std::atoi(r.text) == r.value && r.text[0] != 0, "%s %s row %d (%s): value %d has no name; text '%s'",
					what, an, i, r.label, (int) r.value, r.text);
		}
		for (int k = 0; k < i; k++)
			CHECK(m.row[k].id != r.id, "%s %s: rows %d and %d share id %d", what, an, k, i, r.id);
	}
	CHECK(cursors <= 1, "%s %s: %d rows under the cursor", what, an, cursors);
	// An app with an adapter of its own: whenever the firmware's cursor is on
	// a setting the model lists, that row and no other carries the cursor.
	const int under = CoreTest::friendlyHasAdapter(app) ? CoreTest::menuSetting(const_cast<Core&>(c)) : -1;
	const Row* at = under >= 0 ? findRow(m, under) : nullptr;
	if (at)
		CHECK(cursors == 1 && (at->flags & shoal::kRowCursor), "%s %s: the cursor is on setting %d; %d rows marked, that row's flags %d",
			what, an, under, cursors, at->flags);
	return cursors;
}

bool sameRow(const Row& a, const Row& b) {
	return a.kind == b.kind && a.flags == b.flags && a.id == b.id && a.label == b.label
		&& a.value == b.value && a.min == b.min && a.max == b.max && a.def == b.def
		&& a.names == b.names && a.nameCount == b.nameCount && std::strcmp(a.text, b.text) == 0;
}

// -1 when equal, else the first differing row (or kMaxRows for the header).
int modelDiff(const FriendlyModel& a, const FriendlyModel& b) {
	if (a.app != b.app || a.tabs != b.tabs || a.tab != b.tab || a.rows != b.rows || std::strcmp(a.status, b.status) != 0)
		return shoal::kMaxRows;
	for (int t = 0; t < a.tabs; t++)
		if (a.tabLabel[t] != b.tabLabel[t])
			return shoal::kMaxRows;
	for (int i = 0; i < a.rows; i++)
		if (!sameRow(a.row[i], b.row[i]))
			return i;
	return -1;
}

bool sameSettings(const Core& a, const Core& b) {
	if (a.app() != b.app())
		return false;
	for (int ch = 0; ch < a.channels(); ch++)
		for (int i = 0; i < a.settingCount(); i++)
			if (a.setting(ch, i) != b.setting(ch, i))
				return false;
	return true;
}

struct Snapshot {
	int value[shoal::kNumChannels][128];
};

Snapshot snapshot(const Core& c) {
	Snapshot s = {};
	for (int ch = 0; ch < c.channels(); ch++)
		for (int i = 0; i < c.settingCount() && i < 128; i++)
			s.value[ch][i] = c.setting(ch, i);
	return s;
}

bool sameSnapshot(const Snapshot& a, const Snapshot& b) {
	return std::memcmp(&a, &b, sizeof(a)) == 0;
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

// Tick both cores `ticks` times over the same random CV and gates; true when
// all four output codes matched after every tick.
bool runTogether(Core& a, Core& b, Lcg& rng, int ticks) {
	bool same = true;
	for (int i = 0; i < ticks; i++) {
		if (i % 50 == 0)
			for (int ch = 0; ch < shoal::kNumChannels; ch++) {
				const float v = rng.volts(-3.f, 6.f);
				a.setCv(ch, v);
				b.setCv(ch, v);
			}
		if (i % 97 == 0) {
			const int ch = (int) (rng.next() >> 30);
			const float g = (rng.next() & 0x10000) ? 10.f : 0.f;
			a.setGate(ch, g);
			b.setGate(ch, g);
		}
		a.tick();
		b.tick();
		for (int ch = 0; ch < shoal::kNumChannels; ch++)
			same = same && a.outCode(ch) == b.outCode(ch);
	}
	return same;
}

// The six targets the equivalence tests set each row to.
void targetsFor(const Row& r, int out[6]) {
	out[0] = r.min;
	out[1] = r.max;
	out[2] = r.def;
	for (int k = 1; k <= 3; k++)
		out[2 + k] = r.min + (int) ((int64_t) (r.max - r.min) * k / 4);
}

// A pair of cores on `app`, identically seeded and warmed up.
struct Pair {
	Core a, b;
	Lcg rng;
	explicit Pair(int app) {
		a.seed(4242);
		b.seed(4242);
		a.selectApp(app);
		b.selectApp(app);
		CHECK(runTogether(a, b, rng, 3000), "%s: two fresh cores diverge", Core::appName(app));
	}
	// After an edit on each core: same settings, same model, same outputs.
	void checkSame(const char* what, const char* label, int target) {
		const char* an = Core::appName(a.app());
		CHECK(sameSettings(a, b), "%s %s (%s -> %d): settings differ", an, what, label, target);
		const FriendlyModel ma = model(a), mb = model(b);
		const int d = modelDiff(ma, mb);
		CHECK(d < 0, "%s %s (%s -> %d): models differ at %d", an, what, label, target, d);
		checkWellFormed(a, ma, what);
		CHECK(runTogether(a, b, rng, 2000), "%s %s (%s -> %d): outputs differ", an, what, label, target);
		const int d2 = modelDiff(model(a), model(b));
		CHECK(d2 < 0, "%s %s (%s -> %d): models differ at %d after running", an, what, label, target, d2);
	}
};

// The hardware gesture for a row of the settings menu, in apps whose R push
// toggles edit mode: scroll to the row, push, turn by the difference, push.
// `editing`: the cursor is in edit mode beforehand (an R push on the panel);
// a person then pushes R to leave it before scrolling to another row, and
// does not push again to edit the row it is on.
void menuGesture(Core& c, int row, int target, bool editing = false) {
	const int at = CoreTest::menuSetting(c);
	if (editing && at != row) {
		CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
		editing = false;
	}
	CoreTest::encoderEvent(c, shoal::ENCODER_R, row - at);
	if (!editing)
		CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
	CoreTest::encoderEvent(c, shoal::ENCODER_R, target - c.setting(0, row));
	CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
}

// ---- Tests ------------------------------------------------------------------

void testGenericModel() {
	for (int app = 0; app < shoal::kNumApps; app++) {
		Core c;
		c.selectApp(app);
		const char* an = Core::appName(app);
		const FriendlyModel m = model(c);
		const int cursors = checkWellFormed(c, m, "new core");
		// Each row is a setting of the table, with the table's label and range.
		for (int i = 0; i < m.rows; i++) {
			const Row& r = m.row[i];
			if (r.id >= kExtraId || r.kind == RowKind::Heading)
				continue;
			CHECK(r.id >= 0 && r.id < c.settingCount(), "%s row %d: id %d", an, i, r.id);
			const shoal::SettingInfo s = c.settingInfo(r.id);
			const LabelOverride* over = nullptr;
			for (const LabelOverride& o : kLabelOverrides)
				if (o.app == app && o.setting == r.id)
					over = &o;
			if (over)
				CHECK(std::strcmp(s.name, over->table) == 0 && std::strlen(s.name) <= 2 && std::strcmp(r.label, over->label) == 0,
					"%s row %d: label '%s' for the table's '%s', want '%s' for '%s'", an, i, r.label, s.name, over->label, over->table);
			else
				CHECK(r.label == s.name, "%s row %d: label '%s', table '%s'", an, i, r.label, s.name);
			if (isValueRow(r)) {
				CHECK(r.min == s.min && r.max == s.max && r.def == s.def, "%s row %d (%s): range", an, i, r.label);
				CHECK(r.value == c.setting(m.tab, r.id), "%s row %d (%s): value %d, setting %d", an, i, r.label,
					(int) r.value, c.setting(m.tab, r.id));
				const RowKind want = s.valueNames ? RowKind::Choice
					: s.min == 0 && s.max == 1 ? RowKind::Toggle : RowKind::Number;
				CHECK(r.kind == want, "%s row %d (%s): kind %d, want %d", an, i, r.label, (int) r.kind, (int) want);
				if (r.kind == RowKind::Choice)
					CHECK(r.names == s.valueNames, "%s row %d (%s): names", an, i, r.label);
			}
		}
		// The two apps with an adapter always have the cursor on a listed row.
		CHECK(CoreTest::friendlyHasAdapter(app) == (app == kQuadraturia || app == kLowRents), "%s: adapter %d", an,
			CoreTest::friendlyHasAdapter(app));
		if (app == kQuadraturia || app == kLowRents)
			CHECK(cursors == 1, "%s: %d rows under the cursor", an, cursors);
		// A list is never cut short: the largest table plus its extras fits.
		CHECK(c.settingCount() + 8 <= shoal::kMaxRows, "%s: %d settings do not leave room in %d rows", an,
			c.settingCount(), shoal::kMaxRows);
	}
	// A value past the end of a short name array: the model shows the number
	// and publishes only the names that exist.
	for (const ShortNames& s : kShortNames) {
		Core c;
		c.selectApp(s.app);
		const char* an = Core::appName(s.app);
		int index = -1;
		for (int i = 0; i < c.settingCount(); i++)
			if (std::strcmp(c.settingInfo(i).name, s.label) == 0)
				index = i;
		CHECK(index >= 0, "%s: no setting '%s'", an, s.label);
		if (index < 0)
			continue;
		const shoal::SettingInfo info = c.settingInfo(index);
		CHECK(info.max - info.min + 1 > s.count, "%s '%s': range %d..%d is covered by %d names", an, s.label,
			info.min, info.max, s.count);
		c.setSettingRaw(0, index, info.max);  // only to reach the state; friendly mode never writes raw
		const FriendlyModel m = model(c);
		checkWellFormed(c, m, "short names");
		const Row* r = findRow(m, index);
		if (r) {  // the generic adapter may not list it while it is hidden
			CHECK(r->value == info.max && std::atoi(r->text) == info.max && r->nameCount == s.count,
				"%s '%s': value %d, text '%s', %d names", an, s.label, (int) r->value, r->text, r->nameCount);
		}
	}
}

void testLowRentsEquivalence() {
	Pair p(kLowRents);
	bool second = false;  // B's selected generator, as the title bar would show it
	const FriendlyModel first = model(p.a);
	CHECK(first.rows == 10 && first.tabs <= 1, "Low-rents: %d rows, %d tabs", first.rows, first.tabs);
	CHECK(std::strncmp(first.status, "Freq1 ", 6) == 0, "Low-rents status '%s'", first.status);
	for (int n = 0; n < first.rows; n++) {
		const Row row = first.row[n];
		CHECK(row.id == n, "Low-rents row %d has id %d", n, row.id);
		int targets[6];
		targetsFor(row, targets);
		for (int target : targets) {
			CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kLowRents, 0, row.id, target)),
				"Low-rents %s -> %d: dropped", row.label, target);
			if (row.id <= 1) {
				// Freq 1 and Freq 2: L push selects the generator, the L encoder edits it.
				if (second != (row.id == 1)) {
					CoreTest::buttonEvent(p.b, shoal::BUTTON_L, false);
					second = !second;
				}
				CoreTest::encoderEvent(p.b, shoal::ENCODER_L, target - p.b.setting(0, row.id));
			} else {
				menuGesture(p.b, row.id, target);
			}
			CHECK(p.a.setting(0, row.id) == target, "Low-rents %s: %d, want %d", row.label, p.a.setting(0, row.id), target);
			const FriendlyModel m = model(p.a);
			const Row* r = findRow(m, row.id);
			CHECK(r && r->value == target, "Low-rents %s: the model does not show %d", row.label, target);
			if (r && row.id > 1)
				CHECK((r->flags & (shoal::kRowCursor | shoal::kRowEditing)) == shoal::kRowCursor,
					"Low-rents %s: flags %d after an edit", row.label, r->flags);
			p.checkSame("edit", row.label, target);
		}
	}
}

void testQuadraturiaEquivalence() {
	Pair p(kQuadraturia);
	bool fine = false;  // B's left edit mode, as the title bar would show it
	const FriendlyModel first = model(p.a);
	CHECK(first.rows == 22 && first.tabs <= 1, "Quadraturia: %d rows, %d tabs", first.rows, first.tabs);
	CHECK(std::strncmp(first.status, "(C) Ch A:", 9) == 0, "Quadraturia status '%s'", first.status);
	// B's gesture for coarse (0) and fine (1): L push picks which of the two
	// the L encoder edits, then the encoder; both dead while tap tempo is on.
	auto leftGesture = [&](int id, int target) {
		const bool tap = p.b.setting(0, 2) != 0;
		if (fine != (id == 1)) {
			CoreTest::buttonEvent(p.b, shoal::BUTTON_L, false);
			if (!tap)
				fine = !fine;
		}
		CoreTest::encoderEvent(p.b, shoal::ENCODER_L, target - p.b.setting(0, id));
	};
	for (int n = 0; n < first.rows; n++) {
		const Row row = first.row[n];
		if (row.id >= kExtraId)
			continue;
		CHECK(row.id == n, "Quadraturia row %d has id %d", n, row.id);
		int targets[6];
		targetsFor(row, targets);
		for (int target : targets) {
			CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kQuadraturia, 0, row.id, target)),
				"Quadraturia %s -> %d: dropped", row.label, target);
			if (row.id <= 1)
				leftGesture(row.id, target);
			else
				menuGesture(p.b, row.id, target);
			CHECK(p.a.setting(0, row.id) == target, "Quadraturia %s: %d, want %d", row.label, p.a.setting(0, row.id), target);
			p.checkSame("edit", row.label, target);
		}
	}
	CHECK(std::strncmp(model(p.a).status, "(F) Ch A:", 9) == 0, "Quadraturia status '%s' after a fine edit", model(p.a).status);
	// Tap tempo on: the firmware refuses coarse and fine, on both paths.
	CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kQuadraturia, 0, 2, 1)), "tap tempo on: dropped");
	menuGesture(p.b, 2, 1);
	p.checkSame("tap tempo", "Tap tempo", 1);
	CHECK(std::strcmp(model(p.a).status, "(T) Ch A: tap tempo") == 0, "Quadraturia status '%s' in tap tempo", model(p.a).status);
	for (int id = 0; id <= 1; id++) {
		const int before = p.a.setting(0, id);
		const int target = before > 0 ? before - 7 : before + 7;
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kQuadraturia, 0, id, target)),
			"tap tempo, setting %d: dropped", id);
		leftGesture(id, target);
		CHECK(p.a.setting(0, id) == before, "tap tempo: setting %d moved from %d to %d", id, before, p.a.setting(0, id));
		p.checkSame("refused", id ? "F" : "C", target);
	}
	// Off again: the left side works, and both cores agree on which of
	// coarse and fine the encoder has.
	CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kQuadraturia, 0, 2, 0)), "tap tempo off: dropped");
	menuGesture(p.b, 2, 0);
	for (int id = 1; id >= 0; id--) {
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kQuadraturia, 0, id, 33)), "setting %d: dropped", id);
		leftGesture(id, 33);
		CHECK(p.a.setting(0, id) == 33, "setting %d is %d, want 33", id, p.a.setting(0, id));
		p.checkSame("after tap tempo", id ? "F" : "C", 33);
	}
}

void testQuadraturiaPhaseReset() {
	Pair p(kQuadraturia);
	Core idle;  // the same core with no reset
	idle.seed(4242);
	idle.selectApp(kQuadraturia);
	{
		// Bring `idle` to the pair's state: the same inputs for the same time.
		Core twin;
		twin.seed(4242);
		twin.selectApp(kQuadraturia);
		Lcg rng;
		CHECK(runTogether(twin, idle, rng, 3000), "Quadraturia: two fresh cores diverge");
	}
	// A high TR2 freezes the LFO, and a frozen LFO takes no reset: gates low.
	for (Core* c : { &p.a, &p.b, &idle })
		for (int ch = 0; ch < shoal::kNumChannels; ch++)
			c->setGate(ch, 0.f);
	const FriendlyModel m = model(p.a);
	const Row* reset = findRow(m, kExtraId);
	CHECK(reset != nullptr, "Quadraturia: no extra row");
	if (!reset)
		return;
	CHECK(reset->kind == RowKind::Action && (reset->flags & shoal::kRowExtra) && std::strcmp(reset->label, "Phase reset") == 0,
		"Quadraturia extra row: kind %d, flags %d, label '%s'", (int) reset->kind, reset->flags, reset->label);
	CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::Press, kQuadraturia, 0, kExtraId, 0)), "Phase reset: dropped");
	CoreTest::buttonEvent(p.b, shoal::BUTTON_DOWN, true);
	bool differs = false;
	Lcg rng = p.rng;
	bool same = true;
	for (int i = 0; i < 4000; i++) {
		for (int ch = 0; ch < shoal::kNumChannels; ch++) {
			const float v = rng.volts(-1.f, 1.f);
			p.a.setCv(ch, v);
			p.b.setCv(ch, v);
			idle.setCv(ch, v);
		}
		p.a.tick();
		p.b.tick();
		idle.tick();
		for (int ch = 0; ch < shoal::kNumChannels; ch++) {
			same = same && p.a.outCode(ch) == p.b.outCode(ch);
			differs = differs || p.a.outCode(ch) != idle.outCode(ch);
		}
	}
	CHECK(same, "Phase reset: the row and the DOWN long press give different outputs");
	CHECK(differs, "Phase reset: the outputs are those of a core that was not reset");
	CHECK(sameSettings(p.a, idle), "Phase reset changed a setting");
}

struct Frame {
	uint8_t bytes[shoal::kFrameBytes];
};

// Menu lines (0..3 below the title bar) drawn inverted: more than half lit.
int highlightedLines(const Frame& f, int& which) {
	int n = 0;
	for (int line = 0; line < 4; line++) {
		const int y0 = (line + 1) * 12 + 3;  // menu::CalcLineY, kMenuLineH
		int lit = 0;
		for (int y = y0; y < y0 + 11; y++)
			for (int x = 0; x < shoal::kFrameW; x++)
				lit += Core::pixel(f.bytes, x, y) ? 1 : 0;
		if (lit > 11 * shoal::kFrameW / 2) {
			n++;
			which = line;
		}
	}
	return n;
}

void testFocusMovesCursor() {
	for (int app : { kQuadraturia, kLowRents }) {
		const char* an = Core::appName(app);
		Core a, b;
		a.selectApp(app);
		b.selectApp(app);
		const FriendlyModel first = model(a);
		// Down the list, then back up it.
		for (int pass = 0; pass < 2 * first.rows; pass++) {
			const int n = pass < first.rows ? pass : 2 * first.rows - 1 - pass;
			const Row row = first.row[n];
			const int before = CoreTest::menuSetting(a);
			const Snapshot settings = snapshot(a);
			CHECK(!(model(a).flags & shoal::kModelBusy), "%s: busy on its settings menu", an);
			const bool inMenu = row.id >= 2 && row.id < kExtraId;  // both apps' menus start at setting 2
			CHECK(a.friendlyEdit(makeEdit(FriendlyEdit::Focus, app, 0, row.id, 0)), "%s: Focus on '%s' dropped", an, row.label);
			CHECK(sameSnapshot(settings, snapshot(a)), "%s: Focus on '%s' changed a setting", an, row.label);
			const FriendlyModel m = model(a);
			if (!inMenu) {
				// A row the firmware's menu does not have: the cursor stays.
				CHECK(CoreTest::menuSetting(a) == before, "%s: Focus on '%s' moved the cursor to %d", an, row.label,
					CoreTest::menuSetting(a));
				continue;
			}
			CHECK(CoreTest::menuSetting(a) == row.id, "%s: Focus on '%s': cursor on %d, want %d", an, row.label,
				CoreTest::menuSetting(a), row.id);
			for (int i = 0; i < m.rows; i++)
				CHECK(((m.row[i].flags & shoal::kRowCursor) != 0) == (i == n), "%s: Focus on row %d: row %d flags %d", an, n, i,
					m.row[i].flags);
			// The menu shows one highlighted line, and the frame is the one a
			// single encoder turn of the same size gives.
			CoreTest::encoderEvent(b, shoal::ENCODER_R, row.id - CoreTest::menuSetting(b));
			Frame fa, fb;
			a.drawMenu(fa.bytes);
			b.drawMenu(fb.bytes);
			int line = -1;
			CHECK(highlightedLines(fa, line) == 1, "%s: Focus on '%s': %d highlighted lines", an, row.label, highlightedLines(fa, line));
			CHECK(std::memcmp(fa.bytes, fb.bytes, sizeof(fa.bytes)) == 0, "%s: Focus on '%s': the menu differs from the encoder's",
				an, row.label);
		}
		// While the cursor is editing: Focus on its own row changes nothing;
		// Focus on another row leaves edit mode first, as a person must (an
		// R push, then the turn).
		const int here = CoreTest::menuSetting(a);
		const int there = here == 4 ? 5 : 4;
		CoreTest::buttonEvent(a, shoal::BUTTON_R, false);
		CHECK(a.friendlyEdit(makeEdit(FriendlyEdit::Focus, app, 0, here, 0)), "%s: Focus on the row being edited dropped", an);
		const FriendlyModel same = model(a);
		const Row* r = findRow(same, here);
		CHECK(r && (r->flags & shoal::kRowCursor) && (r->flags & shoal::kRowEditing), "%s: Focus on the row being edited: flags %d", an,
			r ? r->flags : -1);
		CHECK(a.friendlyEdit(makeEdit(FriendlyEdit::Focus, app, 0, there, 0)), "%s: Focus while editing dropped", an);
		const FriendlyModel moved = model(a);
		r = findRow(moved, there);
		CHECK(r && (r->flags & (shoal::kRowCursor | shoal::kRowEditing)) == shoal::kRowCursor, "%s: Focus while editing another row: flags %d",
			an, r ? r->flags : -1);
	}
}

// An edit that arrives while the cursor is editing (R pushed on the panel):
// it applies, and the cursor ends on the edited row, not editing.
void testEditWhileEditing() {
	for (int app : { kQuadraturia, kLowRents }) {
		const char* an = Core::appName(app);
		Pair p(app);
		const FriendlyModel first = model(p.a);
		// From each menu row in edit mode, an edit on another row and one on that row.
		for (int from = 2; from < p.a.settingCount(); from++) {
			for (int to : { from == 2 ? p.a.settingCount() - 1 : from - 1, from }) {
				for (Core* c : { &p.a, &p.b }) {
					CoreTest::encoderEvent(*c, shoal::ENCODER_R, from - CoreTest::menuSetting(*c));
					CoreTest::buttonEvent(*c, shoal::BUTTON_R, false);
				}
				const FriendlyModel before = model(p.a);
				const Row* held = findRow(before, from);
				CHECK(held && (held->flags & shoal::kRowEditing), "%s: setting %d is not being edited", an, from);
				const Row* row = findRow(first, to);
				const int target = p.a.setting(0, to) == row->max ? row->min : row->max;
				const int fromValue = p.a.setting(0, from);
				CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, 0, to, target)), "%s: edit of %d while editing %d dropped",
					an, to, from);
				menuGesture(p.b, to, target, true);
				CHECK(p.a.setting(0, to) == target, "%s: setting %d is %d, want %d", an, to, p.a.setting(0, to), target);
				if (to != from)
					CHECK(p.a.setting(0, from) == fromValue, "%s: the row being edited (%d) changed", an, from);
				const FriendlyModel after = model(p.a);
				for (int i = 0; i < after.rows; i++) {
					const int want = after.row[i].id == to ? shoal::kRowCursor : 0;
					CHECK((after.row[i].flags & (shoal::kRowCursor | shoal::kRowEditing)) == want, "%s: after an edit of %d while editing %d, row %d has flags %d",
						an, to, from, i, after.row[i].flags);
				}
				CHECK(sameSettings(p.a, p.b) && modelDiff(after, model(p.b)) < 0, "%s: edit of %d while editing %d differs from the gesture",
					an, to, from);
			}
		}
		p.checkSame("while editing", "all rows", 0);
	}
}

// The drops no real adapter can show yet, through stand-ins for the
// Low-rents adapter (CoreTest::friendlyFake).
void testFrameworkDrops() {
	Core c;
	c.selectApp(kLowRents);
	const Snapshot before = snapshot(c);
	const int cursor = CoreTest::menuSetting(c);
	const FriendlyEdit set = makeEdit(FriendlyEdit::SetValue, kLowRents, 0, 4, 1);
	const FriendlyEdit focus = makeEdit(FriendlyEdit::Focus, kLowRents, 0, 5, 0);
	auto untouched = [&](const char* what) {
		CHECK(sameSnapshot(before, snapshot(c)) && CoreTest::menuSetting(c) == cursor, "%s: a dropped edit changed the app", what);
	};
	FriendlyModel m;
	// Not on its settings menu (a sub-editor has the controls): busy, all dropped.
	CHECK(!CoreTest::friendlyFake(c, CoreTest::NotAccepting, &set, &m), "not accepting: SetValue applied");
	CHECK(m.flags == shoal::kModelBusy && m.rows == 10, "not accepting: flags %d, %d rows", m.flags, m.rows);
	CHECK(!CoreTest::friendlyFake(c, CoreTest::NotAccepting, &focus, nullptr), "not accepting: Focus applied");
	const FriendlyEdit tab0 = makeEdit(FriendlyEdit::SelectTab, kLowRents, 0, 0, 0);
	CHECK(!CoreTest::friendlyFake(c, CoreTest::NotAccepting, &tab0, nullptr), "not accepting: SelectTab applied");
	untouched("not accepting");
	// Several tabs and no selectTab: nothing is taken, even on the selected tab.
	CHECK(!CoreTest::friendlyFake(c, CoreTest::TabsNoSelect, &set, &m), "tabs without selectTab: SetValue applied");
	CHECK(m.tabs == 2 && m.tab == 0 && m.flags == 0, "tabs without selectTab: %d tabs, flags %d", m.tabs, m.flags);
	const FriendlyEdit tab1 = makeEdit(FriendlyEdit::SelectTab, kLowRents, 0, 0, 1);
	CHECK(!CoreTest::friendlyFake(c, CoreTest::TabsNoSelect, &tab1, nullptr), "tabs without selectTab: SelectTab applied");
	untouched("tabs without selectTab");
	// The app does not follow selectTab: an edit for the other tab is dropped
	// and never reaches a row of the tab that stayed selected.
	const FriendlyEdit other = makeEdit(FriendlyEdit::SetValue, kLowRents, 1, 4, 1);
	CHECK(!CoreTest::friendlyFake(c, CoreTest::SelectFails, &other, nullptr), "selectTab failed: SetValue for tab 1 applied");
	CHECK(!CoreTest::friendlyFake(c, CoreTest::SelectFails, &tab1, nullptr), "selectTab failed: SelectTab reported done");
	const FriendlyEdit otherFocus = makeEdit(FriendlyEdit::Focus, kLowRents, 1, 5, 0);
	CHECK(!CoreTest::friendlyFake(c, CoreTest::SelectFails, &otherFocus, nullptr), "selectTab failed: Focus for tab 1 applied");
	untouched("selectTab failed");
	// The same stand-in takes an edit for the tab that is selected.
	CHECK(CoreTest::friendlyFake(c, CoreTest::SelectFails, &tab0, nullptr), "SelectTab of the selected tab dropped");
	CHECK(CoreTest::friendlyFake(c, CoreTest::SelectFails, &set, nullptr), "an edit on the selected tab dropped");
	CHECK(c.setting(0, 4) == 1 && CoreTest::menuSetting(c) == 4, "the edit left %d, cursor on %d", c.setting(0, 4), CoreTest::menuSetting(c));
	// Too many rows: the model says so, and holds kMaxRows sound rows.
	CoreTest::friendlyFake(c, CoreTest::TooManyRows, nullptr, &m);
	CHECK(m.flags == shoal::kModelTruncated && m.rows == shoal::kMaxRows, "too many rows: flags %d, %d rows", m.flags, m.rows);
	for (int i = 0; i < m.rows; i++)
		CHECK(m.row[i].id == kExtraId + i && m.row[i].label != nullptr, "too many rows: row %d has id %d", i, m.row[i].id);
	// The app menu makes a real model busy, and it clears when the menu closes.
	c.setButton(shoal::BUTTON_R, true);
	run(c, 1.1);
	c.setButton(shoal::BUTTON_R, false);
	run(c, 0.05);
	CHECK(c.appMenuOpen() && (model(c).flags & shoal::kModelBusy), "app menu open: flags %d", model(c).flags);
	c.selectApp(kLowRents);
	CHECK(model(c).flags == 0, "app menu closed: flags %d", model(c).flags);
}

// The status texts' number formatting against printf, away from rounding ties.
void testFixedText() {
	char got[24], want[24];
	Lcg rng;
	for (int n = 0; n < 20000; n++) {
		const int decimals = n % 4, width = (n / 4) % 9;
		// A value on a fine grid, pushed off the half-way points.
		const double scale[] = { 1.0, 10.0, 100.0, 1000.0 };
		const double grid = (double) ((int32_t) rng.next() >> (8 + n % 20)) / scale[decimals];
		const double v = grid + (n & 1 ? 0.2 : -0.3) / scale[decimals];
		CoreTest::fixedText(v, decimals, width, got);
		std::snprintf(want, sizeof(want), "%*.*f", width, decimals, v);
		CHECK(std::strcmp(got, want) == 0, "fixed(%.9g, %d, %d) is '%s', printf gives '%s'", v, decimals, width, got, want);
	}
	CoreTest::fixedText(0.0 / 1e-300 * 0.0, 2, 6, got);  // 0
	CHECK(std::strcmp(got, "  0.00") == 0, "fixed(0) is '%s'", got);
	CoreTest::fixedText(1e300 * 1e300, 3, 6, got);  // infinity: the limit, cut to the buffer
	CHECK(std::strncmp(got, "1000000000000.000", 17) == 0 && std::strlen(got) == 17, "fixed(inf) is '%s'", got);
	CoreTest::fixedText(1e300 * 1e300 - 1e300 * 1e300, 3, 6, got);
	CHECK(std::strcmp(got, "nan") == 0, "fixed(nan) is '%s'", got);
	// The same text the firmware's own title bar prints.
	Core c;
	c.selectApp(kQuadraturia);
	run(c, 0.2);
	const FriendlyModel m = model(c);
	CHECK(std::strlen(m.status) == 19 && std::strcmp(m.status + 16, " Hz") == 0 && m.status[13] == '.', "Quadraturia status '%s'", m.status);
}

void testStaleEditDropped() {
	// Built for Low-rents (app 6), applied to Quantermain (app 3).
	{
		Core c;
		c.selectApp(kLowRents);
		const FriendlyModel m = model(c);
		const Row row = m.row[3];
		const FriendlyEdit e = makeEdit(FriendlyEdit::SetValue, m.app, m.tab, row.id, row.max);
		c.selectApp(kQuantermain);
		const Snapshot before = snapshot(c);
		const FriendlyModel mBefore = model(c);
		CHECK(!c.friendlyEdit(e), "an edit for app 6 was applied to app 3");
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::Focus, kLowRents, 0, row.id, 0)), "a Focus for app 6 was applied to app 3");
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, kLowRents, 0, 0, 0)), "a SelectTab for app 6 was applied to app 3");
		CHECK(sameSnapshot(before, snapshot(c)), "an edit for another app changed a setting");
		CHECK(modelDiff(mBefore, model(c)) < 0, "an edit for another app changed the model");
		// Back on its own app the same edit is taken.
		c.selectApp(kLowRents);
		CHECK(c.friendlyEdit(e), "the edit was dropped on its own app");
		CHECK(c.setting(0, row.id) == row.max, "the edit left %d, want %d", c.setting(0, row.id), (int) row.max);
	}
	for (int app : { kQuadraturia, kLowRents }) {
		const char* an = Core::appName(app);
		Core c;
		c.selectApp(app);
		const FriendlyModel m = model(c);
		const Row row = m.row[4];
		const int other = row.value == row.max ? row.min : row.max;
		const Snapshot before = snapshot(c);
		auto dropped = [&](const FriendlyEdit& e, const char* what) {
			CHECK(!c.friendlyEdit(e), "%s: %s was applied", an, what);
			CHECK(sameSnapshot(before, snapshot(c)), "%s: %s changed a setting", an, what);
			CHECK(modelDiff(m, model(c)) < 0, "%s: %s changed the model", an, what);
		};
		dropped(makeEdit(FriendlyEdit::SetValue, app, 1, row.id, other), "a tab out of range");
		dropped(makeEdit(FriendlyEdit::SetValue, app, 255, row.id, other), "tab 255");
		dropped(makeEdit(FriendlyEdit::SelectTab, app, 0, 0, 1), "SelectTab past the last tab");
		dropped(makeEdit(FriendlyEdit::SelectTab, app, 0, 0, -1), "SelectTab -1");
		dropped(makeEdit(FriendlyEdit::SetValue, app, 0, 999, other), "an id not in the model");
		dropped(makeEdit(FriendlyEdit::SetValue, app, 0, -1, other), "id -1");
		dropped(makeEdit(FriendlyEdit::SetValue, app, 0, c.settingCount(), other), "the id after the last setting");
		dropped(makeEdit(FriendlyEdit::Focus, app, 0, 999, 0), "a Focus on an id not in the model");
		dropped(makeEdit(FriendlyEdit::Press, app, 0, row.id, 0), "a Press on a value row");
		dropped(makeEdit(FriendlyEdit::ToggleBit, app, 0, row.id, 0), "a ToggleBit on a value row");
		dropped(makeEdit((FriendlyEdit::Type) 77, app, 0, row.id, other), "an unknown edit type");
		if (app == kQuadraturia)
			dropped(makeEdit(FriendlyEdit::SetValue, app, 0, kExtraId, 1), "a SetValue on an action row");
		// With the app menu open (an R long press) nothing is taken.
		c.setButton(shoal::BUTTON_R, true);
		run(c, 1.1);
		c.setButton(shoal::BUTTON_R, false);
		run(c, 0.05);
		CHECK(c.appMenuOpen(), "%s: the app menu did not open", an);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, 0, row.id, other)), "%s: an edit was applied under the app menu", an);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::Focus, app, 0, row.id, 0)), "%s: a Focus was applied under the app menu", an);
		CHECK(sameSnapshot(before, snapshot(c)), "%s: an edit under the app menu changed a setting", an);
		checkWellFormed(c, model(c), "app menu open");
		// Closed again, the edit is taken.
		c.selectApp(app);
		CHECK(!c.appMenuOpen(), "%s: the app menu is still open", an);
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, 0, row.id, other)), "%s: a valid edit was dropped", an);
		CHECK(c.setting(0, row.id) == other, "%s: %s is %d, want %d", an, row.label, c.setting(0, row.id), other);
	}
	// A target outside the row's range lands on the nearest end.
	{
		Core c;
		c.selectApp(kLowRents);
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kLowRents, 0, 2, INT_MAX)), "INT_MAX dropped");
		CHECK(c.setting(0, 2) == 127, "Rho 1 is %d after INT_MAX, want 127", c.setting(0, 2));
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kLowRents, 0, 2, INT_MIN)), "INT_MIN dropped");
		CHECK(c.setting(0, 2) == 4, "Rho 1 is %d after INT_MIN, want 4", c.setting(0, 2));
	}
}

void testEditStorm() {
	int applied[shoal::kNumApps] = {};
	for (int app = 0; app < shoal::kNumApps; app++) {
		Core c;
		c.selectApp(app);
		Lcg rng;
		rng.state = 9000u + (uint32_t) app;
		int held = -1, heldFor = 0;
		for (int n = 0; n < 20000; n++) {
			const FriendlyModel m = model(c);
			checkWellFormed(c, m, "storm");
			if (rkatest::failures > 20)
				return;
			FriendlyEdit e;
			const uint32_t r = rng.next() >> 8;
			e.type = (FriendlyEdit::Type) ((r % 16) < 9 ? 0u : (r % 16) - 9);  // SetValue most often; up to 6, one past Focus
			e.app = (uint8_t) ((rng.next() >> 8) % 12 ? app : (rng.next() >> 8) % 16);
			e.tab = (uint8_t) ((rng.next() >> 8) % 12 ? m.tab : (rng.next() >> 8) % 6);
			const Row& row = m.row[(rng.next() >> 8) % (uint32_t) m.rows];
			e.id = (int16_t) ((rng.next() >> 8) % 12 ? row.id : (int) ((rng.next() >> 8) % 600) - 20);
			switch ((rng.next() >> 8) % 8) {
				case 0: e.value = INT_MAX; break;
				case 1: e.value = INT_MIN; break;
				case 2: e.value = (int32_t) rng.next(); break;
				case 3: e.value = (int32_t) ((rng.next() >> 8) % 40) - 4; break;
				default: e.value = row.min + (int32_t) ((rng.next() >> 8) % (uint32_t) (row.max - row.min + 1 > 0 ? row.max - row.min + 1 : 1));
			}
			if (c.friendlyEdit(e))
				applied[app]++;
			// The panel stays live: encoder turns and short presses now and then.
			const uint32_t p = (rng.next() >> 8) % 64;
			if (p < 2)
				c.turnEncoder(p ? shoal::ENCODER_R : shoal::ENCODER_L, (int) ((rng.next() >> 8) % 7) - 3);
			else if (p < 4 && held < 0) {
				held = (int) ((rng.next() >> 8) % 4);
				heldFor = 200 + (int) ((rng.next() >> 8) % 400);  // 12..36 ms: a short press
				c.setButton((shoal::Button) held, true);
			}
			// Clocked throughout.
			const int ticks = 1 + (int) ((rng.next() >> 8) % 12);
			for (int t = 0; t < ticks; t++) {
				if ((rng.next() >> 8) % 40 == 0) {
					const int ch = (int) ((rng.next() >> 8) % 4);
					c.setGate(ch, (rng.next() & 0x100) ? 10.f : 0.f);
					c.setCv(ch, rng.volts(-6.f, 8.f));
				}
				c.tick();
				if (held >= 0 && --heldFor <= 0) {
					c.setButton((shoal::Button) held, false);
					held = -1;
				}
			}
			if (c.app() != app || c.appMenuOpen())  // a press held across a slow stretch: stay on this app
				c.selectApp(app);
		}
		CHECK(allFinite(c), "%s: an output is not finite after the storm", Core::appName(app));
	}
	// The storm reached the adapters: most edits on the two adapted apps land.
	CHECK(applied[kQuadraturia] > 10000 && applied[kLowRents] > 10000, "storm: %d and %d edits applied",
		applied[kQuadraturia], applied[kLowRents]);
}

} // namespace

int main() {
	testGenericModel();
	testLowRentsEquivalence();
	testQuadraturiaEquivalence();
	testQuadraturiaPhaseReset();
	testFocusMovesCursor();
	testEditWhileEditing();
	testFrameworkDrops();
	testFixedText();
	testStaleEditDropped();
	testEditStorm();
	return rkatest::finish("test_friendly");
}
