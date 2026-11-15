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

constexpr int kCopier = 0, kH1200 = 1, kAutomatonnetz = 2, kQuantermain = 3, kMetaQ = 4, kQuadraturia = 5, kLowRents = 6;
constexpr int kPingPong = 9, kViznutcracker = 10, kReferences = 12;
// The apps with an adapter of their own (Tasks 1 to 3).
constexpr int kAdapted[] = { kCopier, kH1200, kAutomatonnetz, kQuantermain, kMetaQ, kQuadraturia, kLowRents, kPingPong,
	kViznutcracker, kReferences };

bool adapted(int app) {
	for (int a : kAdapted)
		if (a == app)
			return true;
	return false;
}
constexpr int kExtraId = 256;

// The settings whose value-name array is shorter than their range (the
// survey's "Value names shorter than the setting's range"), with the length
// of the array in the firmware source.
struct ShortNames { int app; const char* label; int count; };
const ShortNames kShortNames[] = {
	{ 7, "Trigger input", 4 },   // OC_strings.cpp:21 trigger_input_names[4], range 0..6
	{ 11, "chord:", 8 },         // APP_CHORDS.ino:984 chords_slots, 8 names, range 0..CHORDS_USER_LAST - 1
};

// Labels an adapter gives in place of the settings table's (allowed where
// the table's name is a one- or two-letter abbreviation).
struct LabelOverride { int app, setting; const char* table; const char* label; };
const LabelOverride kLabelOverrides[] = {
	{ 5, 0, "C", "Coarse" },
	{ 5, 1, "F", "Fine" },
	{ 2, 0, "dx", "Step X" },
	{ 2, 1, "dy", "Step Y" },
};

// Choice rows whose names an adapter gives in place of the table's (a
// firmware array naming the whole range, where the table's is short).
struct NameOverride { int app, setting; const char* last; };
const NameOverride kNameOverrides[] = {
	{ 4, 14, "Turing" },   // Meta-Q CV source 0..4: cv_input_names has 4; channel_input_sources (APP_QQ.ino) has these 5 first
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
	// Busy while the app menu is open, and otherwise only while the adapter
	// is off its settings menu (a sub-editor has the controls).
	const bool busy = (m.flags & shoal::kModelBusy) != 0;
	CHECK(!c.appMenuOpen() || busy, "%s %s: app menu open, model flags %d", what, an, m.flags);
	CHECK(!busy || c.appMenuOpen() || !CoreTest::friendlyAccepting(const_cast<Core&>(c)), "%s %s: busy on its settings menu", what, an);
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
				const NameOverride* names = nullptr;
				for (const NameOverride& o : kNameOverrides)
					if (o.app == app && o.setting == r.id)
						names = &o;
				if (r.kind == RowKind::Choice && !names)
					CHECK(r.names == s.valueNames, "%s row %d (%s): names", an, i, r.label);
				if (names) {
					// The table's names first, then the one it lacks.
					bool same = r.kind == RowKind::Choice && r.names != s.valueNames && r.nameCount == r.max - r.min + 1;
					for (int k = 0; same && k < r.nameCount - 1; k++)
						same = std::strcmp(r.names[k], s.valueNames[k]) == 0;
					CHECK(same && std::strcmp(r.names[r.nameCount - 1], names->last) == 0, "%s row %d (%s): names do not end in '%s'",
						an, i, r.label, names->last);
				}
			}
		}
		// The apps with an adapter have the cursor on a listed row in a new core.
		CHECK(CoreTest::friendlyHasAdapter(app) == adapted(app), "%s: adapter %d", an, CoreTest::friendlyHasAdapter(app));
		if (adapted(app))
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
	// replaySet refuses a menu row the cursor cannot reach: no edit made, and
	// the cursor is not left editing.
	{
		const Snapshot s = snapshot(c);
		const FriendlyEdit far = makeEdit(FriendlyEdit::SetValue, kLowRents, 0, 4, c.setting(0, 4) == 7 ? 9 : 7);
		CHECK(!CoreTest::friendlyFake(c, CoreTest::ReachFails, &far, nullptr), "past the menu's end: SetValue reported done");
		CHECK(sameSnapshot(s, snapshot(c)), "past the menu's end: a setting changed");
		const FriendlyModel after = model(c);
		for (int i = 0; i < after.rows; i++)
			CHECK(!(after.row[i].flags & shoal::kRowEditing), "past the menu's end: row %d left editing", i);
	}
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
	// The storm reached the adapters: most edits on the two one-tab apps
	// without a sub-editor land, and many on every other adapted app (fewer:
	// edits for another tab, sub-editors opened by the panel's presses).
	CHECK(applied[kQuadraturia] > 10000 && applied[kLowRents] > 10000, "storm: %d and %d edits applied",
		applied[kQuadraturia], applied[kLowRents]);
	for (int app : kAdapted)
		CHECK(applied[app] > 2000, "storm: %s: %d edits applied", Core::appName(app), applied[app]);
}


// ---- Task 2: Dialectic Ping Pong, Viznutcracker sweet, References,
// Harrington 1200, Automatonnetz -------------------------------------------

// The firmware's menu row of `setting` on the selected channel; -1: hidden.
int menuRowOf(Core& c, int setting) {
	int list[128];
	const int n = CoreTest::enabledSettings(c, list, 128);
	for (int i = 0; i < n; i++)
		if (list[i] == setting)
			return i;
	return -1;
}

// Quantermain's and Meta-Q's CV source and trigger: the firmware's guard
// against an internal source with a continuous trigger looks at one encoder
// event at a time, so a person turning one detent at a time is stopped where
// one event of several steps would jump past.
bool guardedRow(int app, int id) {
	return (app == kQuantermain && (id == 3 || id == 5)) || (app == kMetaQ && (id == 14 || id == 15));
}

// Settings the list shows that the firmware's menu does not (CopierMaschine's
// scale: the L encoder and the L push).
bool outsideMenu(int app, int id) {
	return app == kCopier && id == 0;
}

// The hardware gesture for a setting of the selected channel's menu, in apps
// whose R push toggles edit mode: scroll to its row, push, turn, push. The
// turn goes one detent at a time on a guarded row.
void listGesture(Core& c, int setting, int target) {
	const int ch = CoreTest::selectedChannel(c);
	CoreTest::encoderEvent(c, shoal::ENCODER_R, menuRowOf(c, setting) - CoreTest::menuRow(c));
	CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
	const int delta = target - c.setting(ch, setting);
	if (guardedRow(c.app(), setting))
		for (int i = 0; i < std::abs(delta); i++)
			CoreTest::encoderEvent(c, shoal::ENCODER_R, delta > 0 ? 1 : -1);
	else
		CoreTest::encoderEvent(c, shoal::ENCODER_R, delta);
	CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
}

// The channel control of Dialectic Ping Pong, Viznutcracker sweet and
// References: the L encoder.
void tabGesture(Core& c, int tab) {
	CoreTest::encoderEvent(c, shoal::ENCODER_L, tab - CoreTest::selectedChannel(c));
}

// A friendly SetValue on core A and the hardware gesture on core B.
void setBoth(Pair& p, int app, int tab, int id, int target) {
	CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, tab, id, target)), "%s: setting %d -> %d on tab %d dropped",
		Core::appName(app), id, target, tab);
	tabGesture(p.b, tab);
	listGesture(p.b, id, target);
}

// Whether the model's setting rows are the firmware's menu, in its order
// (a setting the menu repeats listed once; References' placeholder rows
// "-" left out).
bool rowsAreMenu(Core& c, const FriendlyModel& m) {
	int list[128];
	const int n = CoreTest::enabledSettings(c, list, 128);
	int k = 0;
	for (int i = 0; i < n; i++) {
		bool seen = false;
		for (int j = 0; j < i; j++)
			seen = seen || list[j] == list[i];
		if (seen || (c.app() == kReferences && list[i] == 9))
			continue;
		while (k < m.rows && (m.row[k].id >= kExtraId || outsideMenu(c.app(), m.row[k].id)))
			k++;
		if (k >= m.rows || m.row[k].id != list[i])
			return false;
		k++;
	}
	for (; k < m.rows; k++)
		if (m.row[k].id < kExtraId && !outsideMenu(c.app(), m.row[k].id))
			return false;
	return true;
}

// Every value row of every tab, six targets each, from the last row up (so
// that rows a visibility switch shows are edited while it is still on).
// `prepare` runs on each tab first. A row a visibility switch has hidden
// meanwhile must be dropped. `special`, if given, makes the hardware gesture
// on core b for a row the menu does not have, and says whether it did.
// A guarded row (guardedRow) ends where the gesture ends, not always on the
// target.
void listEquivalence(int app, void (*prepare)(Pair&, int tab), bool (*special)(Core& b, const Row& row, int target) = nullptr) {
	Pair p(app);
	const char* an = Core::appName(app);
	const int tabs = model(p.a).tabs;
	int edits = 0;
	for (int tab = 0; tab < tabs; tab++) {
		if (tabs > 1) {
			CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, app, model(p.a).tab, 0, tab)), "%s: SelectTab %d dropped", an, tab);
			tabGesture(p.b, tab);
			p.checkSame("select tab", "tab", tab);
		}
		if (prepare)
			prepare(p, tab);
		const FriendlyModel first = model(p.a);
		CHECK(first.tab == tab, "%s: on tab %d, want %d", an, first.tab, tab);
		CHECK(rowsAreMenu(p.a, first), "%s tab %d: the rows are not the firmware's menu", an, tab);
		for (int n = first.rows - 1; n >= 0; n--) {
			const Row row = first.row[n];
			if (!isValueRow(row))
				continue;
			int targets[6];
			targetsFor(row, targets);
			for (int target : targets) {
				const bool listed = findRow(model(p.a), row.id) != nullptr;
				const bool taken = p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, tab, row.id, target));
				CHECK(taken == listed, "%s tab %d %s -> %d: %s", an, tab, row.label, target, listed ? "dropped" : "taken while hidden");
				if (!listed)
					continue;
				const bool inMenu = !(special && special(p.b, row, target));
				if (inMenu)
					listGesture(p.b, row.id, target);
				const int want = guardedRow(app, row.id) ? p.b.setting(tab, row.id) : target;
				if (row.id < kExtraId)
					CHECK(p.a.setting(tab, row.id) == want, "%s tab %d %s: %d, want %d", an, tab, row.label, p.a.setting(tab, row.id), want);
				const FriendlyModel after = model(p.a);
				const Row* r = findRow(after, row.id);
				const int flags = inMenu ? shoal::kRowCursor : 0;
				CHECK(r && r->value == want && (r->flags & (shoal::kRowCursor | shoal::kRowEditing)) == flags,
					"%s tab %d %s: the model shows %d, flags %d", an, tab, row.label, r ? (int) r->value : -1, r ? r->flags : -1);
				p.checkSame("edit", row.label, target);
				edits++;
			}
		}
	}
	CHECK(edits >= 4 * 6 * tabs, "%s: only %d edits", an, edits);  // References A-C: four value rows
}

void testPingPongEquivalence() {
	listEquivalence(kPingPong, nullptr);
}

void testViznutcrackerEquivalence() {
	// Loop mode on, so that the six loop rows are listed and edited.
	listEquivalence(kViznutcracker, [](Pair& p, int tab) { setBoth(p, kViznutcracker, tab, 6, 1); });
}

void testReferencesEquivalence() {
	listEquivalence(kReferences, nullptr);
}

void testH1200Equivalence() {
	// Trigger type Eucl, so that the Euclidean rows are listed and edited;
	// Root mode's follow-up (the chord's mode at the next trigger) shows in
	// the outputs.
	listEquivalence(kH1200, [](Pair& p, int) { setBoth(p, kH1200, 0, 14, 2); });
}

void testAutomatonnetzEquivalence() {
	listEquivalence(kAutomatonnetz, nullptr);
}

// Tabs are the firmware's channels: a SelectTab is the L encoder, the L
// encoder shows in model.tab, and an edit for another tab selects it first.
void testTabsFollowFirmware() {
	for (int app : { kPingPong, kViznutcracker, kReferences }) {
		const char* an = Core::appName(app);
		Pair p(app);
		FriendlyModel m = model(p.a);
		CHECK(m.tabs == 4 && m.tab == CoreTest::selectedChannel(p.a), "%s: %d tabs, tab %d, firmware's %d", an, m.tabs, m.tab,
			CoreTest::selectedChannel(p.a));
		for (int t = 0; t < 4 && m.tabs == 4; t++)
			CHECK(m.tabLabel[t] && m.tabLabel[t][0] == 'A' + t && m.tabLabel[t][1] == 0, "%s: tab %d label '%s'", an, t,
				m.tabLabel[t] ? m.tabLabel[t] : "(null)");
		for (int tab : { 3, 0, 2, 1, 1, 3 }) {
			CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, app, model(p.a).tab, 0, tab)), "%s: SelectTab %d dropped", an, tab);
			tabGesture(p.b, tab);
			CHECK(CoreTest::selectedChannel(p.a) == tab && model(p.a).tab == tab, "%s: SelectTab %d: firmware on %d, model on %d", an, tab,
				CoreTest::selectedChannel(p.a), model(p.a).tab);
			Frame fa, fb;
			p.a.drawMenu(fa.bytes);
			p.b.drawMenu(fb.bytes);
			CHECK(std::memcmp(fa.bytes, fb.bytes, sizeof(fa.bytes)) == 0, "%s: SelectTab %d: the menu differs from the L encoder's", an, tab);
			p.checkSame("select tab", "tab", tab);
		}
		// The L encoder on the panel: the model follows.
		for (int delta : { -1, -2, 3, -7 }) {
			CoreTest::encoderEvent(p.a, shoal::ENCODER_L, delta);
			CHECK(model(p.a).tab == CoreTest::selectedChannel(p.a), "%s: L encoder %d: model tab %d, firmware's %d", an, delta,
				model(p.a).tab, CoreTest::selectedChannel(p.a));
		}
		// An edit for a tab that is not selected selects it, as a person would.
		Pair q(app);
		const int from = CoreTest::selectedChannel(q.a), to = from == 1 ? 2 : 1;
		CHECK(q.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, to, 3, 17)), "%s: edit for tab %d dropped", an, to);
		tabGesture(q.b, to);
		listGesture(q.b, 3, 17);
		CHECK(model(q.a).tab == to && q.a.setting(to, 3) == 17 && q.a.setting(from, 3) == q.b.setting(from, 3),
			"%s: edit for tab %d: model on %d, value %d", an, to, model(q.a).tab, q.a.setting(to, 3));
		q.checkSame("edit on another tab", "setting 3", 17);
		CHECK(!q.a.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, app, model(q.a).tab, 0, 4)), "%s: SelectTab 4 taken", an);
	}
	// Per-channel trigger defaults (TR1..TR4 for A..D, from Init): a row's
	// default is the table's, TR1, on every tab, and setting it gives TR1.
	for (int app : { kPingPong, kViznutcracker }) {
		const int trigger = app == kPingPong ? 4 : 13;
		Core c;
		c.selectApp(app);
		for (int tab = 0; tab < 4; tab++) {
			CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, app, model(c).tab, 0, tab)), "SelectTab %d dropped", tab);
			const FriendlyModel m = model(c);
			const Row* r = findRow(m, trigger);
			CHECK(r && r->value == tab && r->def == 0, "%s tab %d: trigger input %d, default %d", Core::appName(app), tab,
				r ? (int) r->value : -1, r ? (int) r->def : -1);
			if (r)
				CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, tab, trigger, r->def)) && c.setting(tab, trigger) == 0,
					"%s tab %d: trigger input %d after setting the default", Core::appName(app), tab, c.setting(tab, trigger));
		}
	}
}

// A visibility switch: after the edit, the model's rows are the firmware's
// menu, on both paths.
void testVisibilityFollows() {
	struct Case { int app, tab, setting, value, rows; };
	const Case cases[] = {
		{ kViznutcracker, 2, 6, 1, 19 }, { kViznutcracker, 2, 6, 0, 13 }, { kViznutcracker, 0, 6, 1, 19 },
		{ kH1200, 0, 14, 2, 37 + 1 }, { kH1200, 0, 14, 0, 15 + 1 }, { kH1200, 0, 14, 2, 37 + 1 }, { kH1200, 0, 14, 1, 15 + 1 },
	};
	Pair viz(kViznutcracker), h(kH1200);
	for (const Case& k : cases) {
		Pair& p = k.app == kH1200 ? h : viz;
		const char* an = Core::appName(k.app);
		setBoth(p, k.app, k.tab, k.setting, k.value);
		const FriendlyModel m = model(p.a);
		CHECK(m.rows == k.rows, "%s: setting %d = %d: %d rows, want %d", an, k.setting, k.value, m.rows, k.rows);
		CHECK(rowsAreMenu(p.a, m), "%s: setting %d = %d: the rows are not the firmware's menu", an, k.setting, k.value);
		p.checkSame("visibility", "switch", k.value);
	}
	// Viznutcracker: each channel keeps its own list; the model shows the
	// selected one's.
	CHECK(viz.a.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, kViznutcracker, model(viz.a).tab, 0, 2)), "SelectTab 2 dropped");
	CHECK(model(viz.a).rows == 13 && rowsAreMenu(viz.a, model(viz.a)), "Viznutcracker tab C: %d rows", model(viz.a).rows);
	// References: channel D has the tuning rows, A to C two placeholder rows
	// ("-", R push does nothing), which are left out.
	Core r;
	r.selectApp(kReferences);
	for (int tab : { 3, 0, 1, 2 }) {
		CHECK(r.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, kReferences, model(r).tab, 0, tab)), "References: SelectTab %d dropped", tab);
		const FriendlyModel m = model(r);
		CHECK(m.rows == (tab == 3 ? 9 : 5) && rowsAreMenu(r, m), "References tab %d: %d rows", tab, m.rows);
	}
}

// Ticks a and b (one edited, one by gesture) and `idle` (neither) on the same
// inputs. same: a and b agree throughout; differs: a and idle do not.
void runThree(Core& a, Core& b, Core& idle, Lcg rng, int ticks, bool& same, bool& differs) {
	same = true;
	differs = false;
	for (int i = 0; i < ticks; i++) {
		if (i % 50 == 0)
			for (int ch = 0; ch < shoal::kNumChannels; ch++) {
				const float v = rng.volts(-3.f, 6.f);
				for (Core* c : { &a, &b, &idle })
					c->setCv(ch, v);
			}
		if (i % 97 == 0) {
			const int ch = (int) (rng.next() >> 30);
			const float g = (rng.next() & 0x10000) ? 10.f : 0.f;
			for (Core* c : { &a, &b, &idle })
				c->setGate(ch, g);
		}
		for (Core* c : { &a, &b, &idle })
			c->tick();
		for (int ch = 0; ch < shoal::kNumChannels; ch++) {
			same = same && a.outCode(ch) == b.outCode(ch);
			differs = differs || a.outCode(ch) != idle.outCode(ch);
		}
	}
}

// Each Action row is its hardware gesture.
void testActions() {
	struct Case { int app; const char* label; shoal::Button button; bool longPress; };
	const Case cases[] = {
		{ kH1200, "Reset", shoal::BUTTON_L, true },
		{ kAutomatonnetz, "Reset", shoal::BUTTON_UP, false },
		{ kAutomatonnetz, "Clock", shoal::BUTTON_DOWN, false },
		{ kAutomatonnetz, "Clear grid", shoal::BUTTON_L, true },
	};
	for (const Case& k : cases) {
		const char* an = Core::appName(k.app);
		Pair p(k.app), idle(k.app);  // idle: the same cores, never pressed
		// Some settings off their defaults first, on all four cores.
		for (Pair* q : { &p, &idle }) {
			if (k.app == kH1200) {
				setBoth(*q, k.app, 0, 14, 2);   // Trigger type Eucl
				setBoth(*q, k.app, 0, 0, 5);    // Transpose
				setBoth(*q, k.app, 0, 4, 1);    // Root mode Min
			} else {
				setBoth(*q, k.app, 0, 6, 0);    // Clr: zero, for Clear grid
				setBoth(*q, k.app, 0, 0, 13);   // dx
			}
			q->checkSame("before", k.label, 0);
		}
		const FriendlyModel m = model(p.a);
		const Row* action = nullptr;
		for (int i = 0; i < m.rows; i++)
			if (std::strcmp(m.row[i].label, k.label) == 0)
				action = &m.row[i];
		CHECK(action && action->kind == RowKind::Action && (action->flags & shoal::kRowExtra), "%s: no Action row '%s'", an, k.label);
		if (!action)
			continue;
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::Press, k.app, 0, action->id, 0)), "%s %s: dropped", an, k.label);
		CoreTest::buttonEvent(p.b, k.button, k.longPress);
		CHECK(sameSettings(p.a, p.b) && modelDiff(model(p.a), model(p.b)) < 0, "%s %s: differs from the gesture", an, k.label);
		bool same = false, differs = false;
		runThree(p.a, p.b, idle.a, p.rng, 4000, same, differs);
		CHECK(same, "%s %s: the row and the gesture give different outputs", an, k.label);
		CHECK(differs, "%s %s: the outputs are those of a core that was not pressed", an, k.label);
		CHECK(modelDiff(model(p.a), model(p.b)) < 0, "%s %s: models differ after running", an, k.label);
		if (k.app == kH1200) {
			// All settings back to their defaults; the firmware does not rebuild
			// its menu, so the Euclidean rows stay listed, as on the hardware.
			for (int i = 0; i < p.a.settingCount(); i++)
				CHECK(p.a.setting(0, i) == p.a.settingInfo(i).def, "%s Reset: setting %d is %d", an, i, p.a.setting(0, i));
			CHECK(model(p.a).rows == 37 + 1 && rowsAreMenu(p.a, model(p.a)), "%s Reset: %d rows", an, model(p.a).rows);
		}
	}
	// Automatonnetz "Cells" is the cell editor, edited in hardware mode only.
	Core c;
	c.selectApp(kAutomatonnetz);
	const FriendlyModel m = model(c);
	const Row* cells = nullptr;
	for (int i = 0; i < m.rows; i++)
		if (std::strcmp(m.row[i].label, "Cells") == 0)
			cells = &m.row[i];
	CHECK(cells && cells->kind == RowKind::SubEditor && (cells->flags & shoal::kRowExtra), "Automatonnetz: no Cells row");
	CHECK(m.rows == 7 + 4, "Automatonnetz: %d rows", m.rows);
}

// Automatonnetz's cell view and References' autotuner take the controls:
// the model is busy and every edit is dropped until they close.
void testSubEditorsBusy() {
	{
		Pair p(kAutomatonnetz);
		CoreTest::buttonEvent(p.a, shoal::BUTTON_L, false);  // the cell view
		const Snapshot before = snapshot(p.a);
		const FriendlyModel m = model(p.a);
		CHECK((m.flags & shoal::kModelBusy) && !CoreTest::friendlyAccepting(p.a), "Automatonnetz cell view: flags %d", m.flags);
		CHECK(std::strcmp(m.status, "CELL 1,1") == 0, "Automatonnetz cell view: status '%s'", m.status);
		// The cell view shows the cell's own cursor: no row of the list has it.
		CHECK(checkWellFormed(p.a, m, "cell view") == 0, "Automatonnetz cell view: a row carries the cursor");
		CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kAutomatonnetz, 0, 3, 2)), "cell view: SetValue taken");
		CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::Focus, kAutomatonnetz, 0, 4, 0)), "cell view: Focus taken");
		CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::Press, kAutomatonnetz, 0, kExtraId, 0)), "cell view: Press taken");
		CHECK(sameSnapshot(before, snapshot(p.a)), "cell view: a dropped edit changed a setting");
		CoreTest::buttonEvent(p.a, shoal::BUTTON_L, false);  // back to the grid
		CHECK(!(model(p.a).flags & shoal::kModelBusy), "Automatonnetz grid view: busy");
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kAutomatonnetz, 0, 3, 2)) && p.a.setting(0, 3) == 2, "grid view: dropped");
		const Row* cells = nullptr;
		const FriendlyModel g = model(p.a);
		for (int i = 0; i < g.rows; i++)
			if (g.row[i].kind == RowKind::SubEditor)
				cells = &g.row[i];
		CHECK(cells && !p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kAutomatonnetz, 0, cells->id, 1)), "Cells: SetValue taken");
	}
	{
		Pair p(kReferences);
		const FriendlyModel m = model(p.a);
		const Row* tune = findRow(m, 8);
		CHECK(tune && tune->kind == RowKind::SubEditor && std::strcmp(tune->label, "--> autotune") == 0, "References: no autotune row");
		CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kReferences, m.tab, 8, 0)), "autotune: SetValue taken");
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::Focus, kReferences, m.tab, 8, 0)) && CoreTest::menuSetting(p.a) == 8,
			"autotune: Focus did not move the cursor there");
		CHECK(!(model(p.a).flags & shoal::kModelBusy), "References: busy with the cursor on autotune");
		CoreTest::buttonEvent(p.a, shoal::BUTTON_R, false);  // opens the autotuner
		CHECK((model(p.a).flags & shoal::kModelBusy) && !CoreTest::friendlyAccepting(p.a), "References autotuner: not busy");
		checkWellFormed(p.a, model(p.a), "autotuner");
		CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kReferences, m.tab, 0, 2)) && p.a.setting(3, 0) == 0,
			"autotuner: SetValue taken");
		CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, kReferences, m.tab, 0, 0)), "autotuner: SelectTab taken");
		CoreTest::buttonEvent(p.a, shoal::BUTTON_R, false);  // closes it
		CHECK(!(model(p.a).flags & shoal::kModelBusy), "References: busy after the autotuner closed");
	}
}

// After a patch load References lists channel D's last two rows, which the
// firmware's cursor cannot reach (REFS_restore sets the menu's end from
// channel A) until the L encoder moves. An edit there turns the L encoder
// away and back first, as a person must.
void testReferencesAfterRestore() {
	Pair p(kReferences);
	static uint8_t image[shoal::kStateBytes];
	p.a.save(image);
	CHECK(p.a.restore(image) && p.b.restore(image), "References: restore failed");
	CHECK(model(p.a).tab == 3, "References after restore: tab %d", model(p.a).tab);
	// PPQN (setting 7, menu row 8) and the A mantissa (6, menu row 7).
	for (int setting : { 7, 6 }) {
		const int target = setting == 7 ? 5 : 33;
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kReferences, 3, setting, target)), "References: setting %d dropped", setting);
		if (setting == 7) {
			// On B: the person scrolls, the list stops two rows short, so they
			// turn the L encoder away and back, then scroll on.
			CoreTest::encoderEvent(p.b, shoal::ENCODER_R, menuRowOf(p.b, setting) - CoreTest::menuRow(p.b));
			CHECK(CoreTest::menuRow(p.b) == 6, "References after restore: the cursor reached row %d", CoreTest::menuRow(p.b));
			CoreTest::encoderEvent(p.b, shoal::ENCODER_L, -1);
			CoreTest::encoderEvent(p.b, shoal::ENCODER_L, 1);
		}
		listGesture(p.b, setting, target);
		CHECK(p.a.setting(3, setting) == target, "References: setting %d is %d, want %d", setting, p.a.setting(3, setting), target);
		p.checkSame("after restore", "tuning", target);
	}
}

// Focus on each row of the new adapters: the cursor goes to a menu row; an
// extra row leaves it.
void testFocusNewApps() {
	for (int app : { kH1200, kAutomatonnetz, kPingPong, kViznutcracker, kReferences, kCopier, kQuantermain, kMetaQ }) {
		const char* an = Core::appName(app);
		Core c;
		c.selectApp(app);
		const FriendlyModel first = model(c);
		for (int pass = 0; pass < 2 * first.rows; pass++) {
			const int n = pass < first.rows ? pass : 2 * first.rows - 1 - pass;
			const Row row = first.row[n];
			const int before = CoreTest::menuSetting(c);
			const Snapshot settings = snapshot(c);
			CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::Focus, app, first.tab, row.id, 0)), "%s: Focus on '%s' dropped", an, row.label);
			CHECK(sameSnapshot(settings, snapshot(c)), "%s: Focus on '%s' changed a setting", an, row.label);
			const int want = menuRowOf(c, row.id) >= 0 ? row.id : before;  // a row the menu does not have: the cursor stays
			CHECK(CoreTest::menuSetting(c) == want, "%s: Focus on '%s': cursor on %d, want %d", an, row.label, CoreTest::menuSetting(c), want);
			checkWellFormed(c, model(c), "focus");
		}
	}
}

// Text the adapters write themselves: title-bar status lines (as the
// firmware's own title bars draw them) and Automatonnetz's dx/dy values.
void testStatusAndText() {
	{
		Core c;
		c.selectApp(kH1200);
		run(c, 0.5);
		// A fresh core: the C major chord, notes shown as names (L push: numbers).
		CHECK(std::strcmp(model(c).status, "C  Maj  C  E  G ") == 0, "Harrington 1200 status '%s'", model(c).status);
		CoreTest::buttonEvent(c, shoal::BUTTON_L, false);
		CHECK(std::strcmp(model(c).status, "C  Maj   0 +4 +7") == 0, "Harrington 1200 status with numbers '%s'", model(c).status);
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kH1200, 0, 4, 1)), "Root mode dropped");
		CHECK(std::strncmp(model(c).status, "C  Min", 6) == 0, "Harrington 1200 status after Root mode '%s'", model(c).status);
	}
	{
		Core c;
		c.selectApp(kAutomatonnetz);
		run(c, 0.5);
		// Three note names (two characters and a space each) and the chord's mode.
		const FriendlyModel m = model(c);
		CHECK(std::strlen(m.status) == 10 && (m.status[9] == '+' || m.status[9] == '-') && m.status[2] == ' ' && m.status[5] == ' ',
			"Automatonnetz status '%s'", m.status);
		// The cell view's title: the selected cell, row and column from 1.
		CoreTest::buttonEvent(c, shoal::BUTTON_L, false);
		CoreTest::encoderEvent(c, shoal::ENCODER_L, 7);
		CHECK(std::strcmp(model(c).status, "CELL 2,3") == 0, "Automatonnetz cell view status '%s'", model(c).status);
		CoreTest::buttonEvent(c, shoal::BUTTON_L, false);
		// dx and dy: whole cells and a fraction, as draw_grid_menu prints them.
		struct Step { int value; const char* text; };
		const Step steps[] = { { 0, "0" }, { 3, "1/6" }, { 8, "1" }, { 12, "1 1/5" }, { 39, "4 1/2" }, { 1, "1/8" } };
		for (int id : { 0, 1 })
			for (const Step& st : steps) {
				CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kAutomatonnetz, 0, id, st.value)), "dx/dy dropped");
				const FriendlyModel g = model(c);
				const Row* r = findRow(g, id);
				CHECK(r && r->value == st.value && std::strcmp(r->text, st.text) == 0, "Automatonnetz %s = %d: text '%s', want '%s'",
					id ? "dy" : "dx", st.value, r ? r->text : "?", st.text);
			}
	}
}

// ---- Task 3: CopierMaschine, Quantermain, Meta-Q ----------------------------

constexpr int kHold = kExtraId, kDelayType = kExtraId + 1;    // CopierMaschine's extra rows
constexpr int kCopyScale = kExtraId, kResetMask = kExtraId + 1;  // Quantermain's and Meta-Q's

// CopierMaschine's rows the menu does not have, as the hardware does them on
// core b: the scale (the L encoder from the scale it shows, then L push),
// Hold / Freeze (DOWN) and Delay type (DOWN long press).
bool copierGesture(Core& b, const Row& row, int target) {
	if (row.id == 0) {  // no preview pending in these tests: the L encoder shows the committed scale
		CoreTest::encoderEvent(b, shoal::ENCODER_L, target - b.setting(0, 0));
		CoreTest::buttonEvent(b, shoal::BUTTON_L, false);
		return true;
	}
	if (row.id == kHold || row.id == kDelayType) {
		const FriendlyModel m = model(b);
		const Row* r = findRow(m, row.id);
		if (r && r->value != target)
			CoreTest::buttonEvent(b, shoal::BUTTON_DOWN, row.id == kDelayType);
		return true;
	}
	return false;
}

void testCopierEquivalence() {
	{
		Core c;
		c.selectApp(kCopier);
		const FriendlyModel m = model(c);
		// The extra rows first, as on the reference: Scale, Hold / Freeze, Delay type.
		CHECK(m.rows == 3 + 9 && m.row[0].id == 0 && m.row[0].kind == RowKind::Choice && m.row[0].value == 5
			&& m.row[1].id == kHold && m.row[1].kind == RowKind::Toggle && std::strcmp(m.row[1].label, "Hold / Freeze") == 0
			&& m.row[2].id == kDelayType && m.row[2].kind == RowKind::Choice && std::strcmp(m.row[2].label, "Delay type") == 0
			&& m.row[2].nameCount == 2 && std::strcmp(m.row[2].names[0], "multiplying") == 0 && std::strcmp(m.row[2].names[1], "basic") == 0,
			"CopierMaschine: %d rows; first rows %d %d %d", m.rows, m.row[0].id, m.row[1].id, m.row[2].id);
		const Row* mask = findRow(m, 3);
		CHECK(mask && mask->kind == RowKind::Mask && mask->max == 12 && mask->value == 65535, "CopierMaschine: mask row");
	}
	// Each CV source shows its rows; CV1 shows none.
	listEquivalence(kCopier, [](Pair& p, int) { setBoth(p, kCopier, 0, 8, 1); }, &copierGesture);
	listEquivalence(kCopier, [](Pair& p, int) { setBoth(p, kCopier, 0, 8, 2); }, &copierGesture);
	listEquivalence(kCopier, [](Pair& p, int) { setBoth(p, kCopier, 0, 8, 3); }, &copierGesture);
}

void testQuantermainEquivalence() {
	// A: CV2 (the aux row shows), B: Turing, C: ByteB, D: IntSq.
	listEquivalence(kQuantermain, [](Pair& p, int tab) {
		static const int source[4] = { 1, 4, 6, 7 };
		setBoth(p, kQuantermain, tab, 3, source[tab]);
	});
	listEquivalence(kQuantermain, [](Pair& p, int tab) { setBoth(p, kQuantermain, tab, 3, 5); });  // Lgstc
}

void testMetaQEquivalence() {
	// A: Turing (its rows show), B: aux output "copy" (aux +/- shows).
	listEquivalence(kMetaQ, [](Pair& p, int tab) {
		if (tab == 0)
			setBoth(p, kMetaQ, 0, 14, 4);
		else
			setBoth(p, kMetaQ, 1, 22, 1);
	});
	// Every slot's scale, root and transpose, reached through "scale #" (its
	// reset_scale shows in the outputs once seq_mode runs the sequence).
	Pair p(kMetaQ);
	for (int tab = 0; tab < 2; tab++) {
		setBoth(p, kMetaQ, tab, 13, 1 + tab);  // seq_mode TR+1, TR+2
		p.checkSame("seq_mode", "seq_mode", 1 + tab);
		for (int slot = 0; slot < 4; slot++) {
			setBoth(p, kMetaQ, tab, 8, slot);
			p.checkSame("scale #", "scale #", slot);
			const FriendlyModel m = model(p.a);
			CHECK(findRow(m, slot) && findRow(m, 4 + slot) && findRow(m, 17 + slot) && findRow(m, 9 + slot)
				&& findRow(m, 9 + slot)->kind == RowKind::Mask, "Meta-Q tab %d slot %d: the slot's rows are not listed", tab, slot);
			setBoth(p, kMetaQ, tab, slot, 6 + slot);
			p.checkSame("slot scale", "scale", 6 + slot);
			setBoth(p, kMetaQ, tab, 4 + slot, 2 + slot);
			p.checkSame("slot root", "root", 2 + slot);
			setBoth(p, kMetaQ, tab, 17 + slot, slot - 2);
			p.checkSame("slot transpose", "transpose", slot - 2);
		}
	}
}

// Sets the scale on both cores: the friendly Scale row on a, the hardware
// gesture on b (CopierMaschine: the L encoder and L push; the others: the
// menu's scale row, slot 1 for Meta-Q).
void setScaleBoth(Pair& p, int app, int scale) {
	const int id = 0;  // ASR_SETTING_SCALE, CHANNEL_SETTING_SCALE, DQ_CHANNEL_SETTING_SCALE1
	if (app == kCopier) {
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, app, 0, id, scale)), "%s: scale %d dropped", Core::appName(app), scale);
		const Row r = { RowKind::Choice, 0, 0, "", 0, 0, 0, 0, nullptr, 0, "" };
		copierGesture(p.b, r, scale);
	} else {
		setBoth(p, app, 0, id, scale);
	}
}

// The scale editor's own toggle of note `bit`, as a person does it: cursor to
// the mask row, R push (the editor opens, its cursor on the first note), the
// L encoder to the note, L push, R push (it closes).
void editorToggle(Core& c, int maskSetting, int bit) {
	CoreTest::encoderEvent(c, shoal::ENCODER_R, menuRowOf(c, maskSetting) - CoreTest::menuRow(c));
	CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
	CHECK(!CoreTest::friendlyAccepting(c), "%s: the scale editor did not open", Core::appName(c.app()));
	CoreTest::encoderEvent(c, shoal::ENCODER_L, bit);
	CoreTest::buttonEvent(c, shoal::BUTTON_L, false);
	CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
	CHECK(CoreTest::friendlyAccepting(c), "%s: the scale editor did not close", Core::appName(c.app()));
}

// The editor's rule, written out here: a note toggles, but the last active
// one stays.
uint16_t expectToggle(uint16_t mask, int bit, int cells) {
	const uint16_t m = (uint16_t) (1u << bit), used = (uint16_t) (cells >= 16 ? 0xffff : (1u << cells) - 1);
	if (!(mask & m))
		return (uint16_t) (mask | m);
	return (mask & used) == m ? mask : (uint16_t) (mask & ~m);
}

// A mask row has one cell per note of its scale, follows a scale change at
// once, and a ToggleBit is the scale editor's toggle (mask and outputs).
void testMaskCells() {
	struct Case { int scale, userNotes, cells; const char* what; };
	const Case cases[] = {
		{ 4, 0, 0, "Off" }, { 5, 0, 12, "Semitone" }, { 15, 0, 5, "Pentatonic major" }, { 6, 0, 7, "Ionian" },
		{ 0, 4, 4, "user scale, 4 notes" }, { 0, 16, 16, "user scale, 16 notes" }, { 4, 0, 0, "Off again" },
	};
	struct App { int app, mask; };
	for (const App& k : { App{ kCopier, 3 }, App{ kQuantermain, 2 }, App{ kMetaQ, 9 } }) {
		const char* an = Core::appName(k.app);
		Pair p(k.app);
		for (const Case& cs : cases) {
			if (cs.userNotes)
				for (Core* c : { &p.a, &p.b }) {
					CoreTest::userScaleNumNotes(*c, 0) = (uint32_t) cs.userNotes;
					for (int i = 0; i < 16; i++)
						CoreTest::userScaleNote(*c, 0, i) = (int16_t) (96 * i);
				}
			setScaleBoth(p, k.app, cs.scale);
			p.checkSame("scale", cs.what, cs.scale);
			const FriendlyModel m = model(p.a);
			const Row* row = findRow(m, k.mask);
			// Off: Quantermain and Meta-Q hide the row; CopierMaschine shows it with no cells.
			if (cs.cells == 0 && k.app != kCopier) {
				CHECK(!row, "%s %s: a mask row is listed", an, cs.what);
				continue;
			}
			CHECK(row && row->kind == RowKind::Mask && row->max == cs.cells && row->value == p.a.setting(0, k.mask),
				"%s %s: mask row of %d cells, want %d", an, cs.what, row ? (int) row->max : -1, cs.cells);
			if (cs.cells == 0) {
				CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::ToggleBit, k.app, 0, k.mask, 0)), "%s %s: a cell toggled", an, cs.what);
				continue;
			}
			CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::ToggleBit, k.app, 0, k.mask, cs.cells)), "%s %s: the cell past the last toggled",
				an, cs.what);
			// Each cell toggled on both paths, then all but one cleared, then
			// the last one (refused), then all on again.
			auto toggle = [&](int bit, const char* what) {
				const uint16_t want = expectToggle((uint16_t) p.a.setting(0, k.mask), bit, cs.cells);
				CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::ToggleBit, k.app, 0, k.mask, bit)), "%s %s: cell %d dropped", an, cs.what, bit);
				editorToggle(p.b, k.mask, bit);
				CHECK(p.a.setting(0, k.mask) == want, "%s %s %s: cell %d: mask %04x, want %04x", an, cs.what, what, bit,
					p.a.setting(0, k.mask), want);
				const FriendlyModel after = model(p.a);
				const Row* r = findRow(after, k.mask);
				CHECK(r && r->value == want && (r->flags & shoal::kRowCursor), "%s %s: cell %d: the model shows %04x", an, cs.what, bit,
					r ? (int) r->value : -1);
				p.checkSame(what, cs.what, bit);
			};
			for (int bit = 0; bit < cs.cells; bit++)
				toggle(bit, "each");
			const uint16_t used = (uint16_t) (cs.cells >= 16 ? 0xffff : (1u << cs.cells) - 1);
			for (int bit = 0; bit < cs.cells; bit++)
				if ((p.a.setting(0, k.mask) & (1 << bit)) && __builtin_popcount(p.a.setting(0, k.mask) & used) > 1)
					toggle(bit, "clear");
			int last = -1;
			for (int bit = 0; bit < cs.cells; bit++)
				if (p.a.setting(0, k.mask) & (1 << bit))
					last = bit;
			CHECK(last >= 0 && __builtin_popcount(p.a.setting(0, k.mask) & used) == 1, "%s %s: %04x is not one note", an, cs.what,
				p.a.setting(0, k.mask));
			const int single = p.a.setting(0, k.mask);
			if (last >= 0)
				toggle(last, "last note");
			CHECK(p.a.setting(0, k.mask) == single, "%s %s: the last note was cleared", an, cs.what);
			for (int bit = 0; bit < cs.cells; bit++)
				if (!(p.a.setting(0, k.mask) & (1 << bit)))
					toggle(bit, "on again");
			CHECK((p.a.setting(0, k.mask) & used) == used, "%s %s: not all notes on: %04x", an, cs.what, p.a.setting(0, k.mask));
		}
		// A user scale's length is shared: changed while it is selected, the
		// row follows without an edit.
		setScaleBoth(p, k.app, 1);
		for (int n : { 9, 4, 16 }) {
			CoreTest::userScaleNumNotes(p.a, 1) = (uint32_t) n;
			const FriendlyModel m = model(p.a);
			const Row* r = findRow(m, k.mask);
			CHECK(r && r->max == n, "%s: user scale of %d notes: %d cells", an, n, r ? (int) r->max : -1);
		}
	}
}

struct Frame3 {
	uint8_t bytes[shoal::kFrameBytes];
};

bool sameFrame(Core& a, Core& b) {
	Frame3 fa, fb;
	a.drawMenu(fa.bytes);
	b.drawMenu(fb.bytes);
	return std::memcmp(fa.bytes, fb.bytes, sizeof(fa.bytes)) == 0;
}

// CopierMaschine's Scale row is the L encoder's preview and the L push's
// commit (set_scale): the setting, the mask's fix-up and the title bar.
void testCopierScaleCommit() {
	Pair p(kCopier);
	auto set = [&](int target, int preview) {  // preview: the scale B's L encoder shows
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kCopier, 0, 0, target)), "Scale %d dropped", target);
		CoreTest::encoderEvent(p.b, shoal::ENCODER_L, target - preview);
		CoreTest::buttonEvent(p.b, shoal::BUTTON_L, false);
		CHECK(p.a.setting(0, 0) == target, "Scale is %d, want %d", p.a.setting(0, 0), target);
		CHECK(sameFrame(p.a, p.b), "Scale %d: the menu (title bar) differs from the gesture's", target);
		CHECK(std::strcmp(model(p.a).status, model(p.b).status) == 0, "Scale %d: status", target);
		p.checkSame("scale", "Scale", target);
	};
	CHECK(std::strcmp(model(p.a).status, "Semitone") == 0, "CopierMaschine status '%s'", model(p.a).status);
	set(6, 5);
	CHECK(std::strcmp(model(p.a).status, "Ionian") == 0, "CopierMaschine status '%s'", model(p.a).status);
	// A preview pending from the panel: the row shows the committed scale,
	// the status the previewed one; the edit turns from the preview.
	for (Core* c : { &p.a, &p.b })
		CoreTest::encoderEvent(*c, shoal::ENCODER_L, 3);  // Lydian (9), not committed
	{
		const FriendlyModel m = model(p.a);
		CHECK(m.row[0].value == 6 && std::strcmp(m.status, "Next: Lydian") == 0, "pending preview: row %d, status '%s'",
			(int) m.row[0].value, m.status);
	}
	set(15, 9);
	// To the committed scale with a preview pending: the preview goes back, nothing else.
	for (Core* c : { &p.a, &p.b })
		CoreTest::encoderEvent(*c, shoal::ENCODER_L, -2);
	set(15, 13);
	CHECK(std::strcmp(model(p.a).status, "Pentatonic maj") == 0, "status '%s' after the preview went back", model(p.a).status);
	// set_scale keeps a note active: a mask with only note 11 (of Semitone)
	// has none of a five-note scale's, so note 1 comes on with the commit.
	set(5, 15);
	for (int bit = 0; bit < 11; bit++)
		for (Core* c : { &p.a, &p.b })
			CHECK(c->friendlyEdit(makeEdit(FriendlyEdit::ToggleBit, kCopier, 0, 3, bit)), "toggle %d dropped", bit);
	CHECK(p.a.setting(0, 3) == 0xf800, "mask %04x before the commit", p.a.setting(0, 3));
	set(15, 5);
	CHECK(p.a.setting(0, 3) == 0xf801 && p.b.setting(0, 3) == 0xf801, "mask %04x after the commit, want f801", p.a.setting(0, 3));
	// The L long press is the same commit and opens the scale editor: the
	// model is busy until it closes.
	CoreTest::encoderEvent(p.a, shoal::ENCODER_L, 1);
	CoreTest::buttonEvent(p.a, shoal::BUTTON_L, true);
	CHECK(p.a.setting(0, 0) == 16 && (model(p.a).flags & shoal::kModelBusy), "L long press: scale %d, flags %d", p.a.setting(0, 0),
		model(p.a).flags);
	CHECK(!p.a.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kCopier, 0, 0, 5)), "Scale taken with the scale editor open");
	CoreTest::buttonEvent(p.a, shoal::BUTTON_R, false);
	CHECK(!(model(p.a).flags & shoal::kModelBusy), "the scale editor did not close");
}

// The firmware's guard against an internal CV source with a continuous
// trigger looks at one encoder event: a person turning one detent at a time
// is stopped at TR4 or CV4, one event of several steps is not. The list is
// stopped where the person is.
void testTriggerSourceGuard() {
	struct App { int app, source, trigger, internal; };
	for (const App& k : { App{ kQuantermain, 3, 5, 7 }, App{ kMetaQ, 14, 15, 4 } }) {
		const char* an = Core::appName(k.app);
		Pair p(k.app);
		Core single;  // the same edits, as one encoder event each
		single.selectApp(k.app);
		auto oneEvent = [&](int setting, int target) {
			CoreTest::encoderEvent(single, shoal::ENCODER_R, menuRowOf(single, setting) - CoreTest::menuRow(single));
			CoreTest::buttonEvent(single, shoal::BUTTON_R, false);
			CoreTest::encoderEvent(single, shoal::ENCODER_R, target - single.setting(0, setting));
			CoreTest::buttonEvent(single, shoal::BUTTON_R, false);
		};
		// An internal source: the trigger stops at TR4, from TR1 and from TR3.
		for (int from : { 0, 2 }) {
			setBoth(p, k.app, 0, k.trigger, from);
			oneEvent(k.trigger, from);
			setBoth(p, k.app, 0, k.source, 4);  // Turing
			oneEvent(k.source, 4);
			setBoth(p, k.app, 0, k.trigger, 5);  // cnt-
			oneEvent(k.trigger, 5);
			CHECK(p.a.setting(0, k.trigger) == 3 && p.b.setting(0, k.trigger) == 3, "%s: trigger from %d to cnt- with Turing: %d and %d, want TR4",
				an, from, p.a.setting(0, k.trigger), p.b.setting(0, k.trigger));
			CHECK(single.setting(0, k.trigger) == 5, "%s: one event from %d gave %d (the guard is per event)", an, from, single.setting(0, k.trigger));
			p.checkSame("guard", "trigger", 5);
			setBoth(p, k.app, 0, k.source, 0);
			oneEvent(k.source, 0);
		}
		// A continuous trigger: the source stops at CV4.
		setBoth(p, k.app, 0, k.trigger, 4);  // cnt+
		oneEvent(k.trigger, 4);
		CHECK(p.a.setting(0, k.trigger) == 4, "%s: trigger cnt+ with CV1: %d", an, p.a.setting(0, k.trigger));
		setBoth(p, k.app, 0, k.source, k.internal);
		oneEvent(k.source, k.internal);
		CHECK(p.a.setting(0, k.source) == 3 && p.b.setting(0, k.source) == 3, "%s: source to %d with cnt+: %d and %d, want CV4", an,
			k.internal, p.a.setting(0, k.source), p.b.setting(0, k.source));
		CHECK(single.setting(0, k.source) == k.internal, "%s: one event gave source %d", an, single.setting(0, k.source));
		p.checkSame("guard", "source", k.internal);
		const FriendlyModel m = model(p.a);
		const Row* r = findRow(m, k.source);
		CHECK(r && r->value == 3, "%s: the list shows source %d", an, r ? (int) r->value : -1);
	}
}

// Quantermain's and Meta-Q's Action rows are their long presses.
void testQuantizerActions() {
	struct Case { int app; int id; const char* label; shoal::Button button; };
	const Case cases[] = {
		{ kQuantermain, kCopyScale, "Copy scale to other channels", shoal::BUTTON_L },
		{ kQuantermain, kResetMask, "Reset mask", shoal::BUTTON_DOWN },
		{ kMetaQ, kCopyScale, "Copy scale to all slots", shoal::BUTTON_L },
		{ kMetaQ, kResetMask, "Reset mask", shoal::BUTTON_DOWN },
	};
	for (const Case& k : cases) {
		const char* an = Core::appName(k.app);
		const int mask = k.app == kQuantermain ? 2 : 9;
		Pair p(k.app), idle(k.app);
		for (Pair* q : { &p, &idle }) {
			setBoth(*q, k.app, 0, 0, 6);   // Ionian
			setBoth(*q, k.app, 0, k.app == kQuantermain ? 1 : 4, 2);  // root D
			for (int bit : { 1, 3, 4 })
				for (Core* c : { &q->a, &q->b })
					CHECK(c->friendlyEdit(makeEdit(FriendlyEdit::ToggleBit, k.app, 0, mask, bit)), "%s: toggle dropped", an);
			q->checkSame("before", k.label, 0);
		}
		const FriendlyModel m = model(p.a);
		const Row* action = findRow(m, k.id);
		CHECK(action && action->kind == RowKind::Action && std::strcmp(action->label, k.label) == 0, "%s: no Action row '%s'", an, k.label);
		CHECK(p.a.friendlyEdit(makeEdit(FriendlyEdit::Press, k.app, 0, k.id, 0)), "%s %s: dropped", an, k.label);
		CoreTest::buttonEvent(p.b, k.button, true);
		CHECK(sameSettings(p.a, p.b) && modelDiff(model(p.a), model(p.b)) < 0, "%s %s: differs from the gesture", an, k.label);
		if (k.id == kResetMask)
			CHECK(p.a.setting(0, mask) == 0xffff, "%s %s: mask %04x", an, k.label, p.a.setting(0, mask));
		else
			CHECK(p.a.setting(1, 0) == 6 && p.a.setting(k.app == kQuantermain ? 3 : 1, 0) == 6, "%s %s: not copied", an, k.label);
		bool same = false, differs = false;
		runThree(p.a, p.b, idle.a, p.rng, 20000, same, differs);
		CHECK(same, "%s %s: the row and the gesture give different outputs", an, k.label);
		CHECK(differs, "%s %s: the outputs are those of a core that was not pressed", an, k.label);
	}
}

// The scale editor takes every event while it is open: the model is busy and
// every edit dropped. And an edit mode carried onto a mask row by a channel
// switch, where an R push opens the editor: edits that would move the cursor
// are dropped.
void testQuantizerBusy() {
	struct App { int app, mask; };
	for (const App& k : { App{ kCopier, 3 }, App{ kQuantermain, 2 }, App{ kMetaQ, 9 } }) {
		const char* an = Core::appName(k.app);
		Core c;
		c.selectApp(k.app);
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::Focus, k.app, 0, k.mask, 0)) && CoreTest::menuSetting(c) == k.mask, "%s: Focus on the mask", an);
		CHECK(CoreTest::friendlyAccepting(c) && !(model(c).flags & shoal::kModelBusy), "%s: busy with the cursor on the mask row", an);
		CoreTest::buttonEvent(c, shoal::BUTTON_R, false);  // opens the scale editor
		const Snapshot before = snapshot(c);
		const FriendlyModel m = model(c);
		CHECK((m.flags & shoal::kModelBusy) && !CoreTest::friendlyAccepting(c), "%s: not busy with the scale editor open", an);
		checkWellFormed(c, m, "scale editor");
		const int other = k.app == kCopier ? 4 : k.app == kQuantermain ? 9 : 21;  // buf.index, Octave, octave
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, k.app, 0, other, 2)), "%s: SetValue taken", an);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::ToggleBit, k.app, 0, k.mask, 3)), "%s: ToggleBit taken", an);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::Focus, k.app, 0, other, 0)), "%s: Focus taken", an);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::Press, k.app, 0, kExtraId, 0)), "%s: Press taken", an);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::SelectTab, k.app, 0, 0, 1)), "%s: SelectTab taken", an);
		CHECK(sameSnapshot(before, snapshot(c)), "%s: a dropped edit changed a setting", an);
		CoreTest::buttonEvent(c, shoal::BUTTON_R, false);  // closes it
		CHECK(!(model(c).flags & shoal::kModelBusy), "%s: busy after the editor closed", an);
		if (k.app == kCopier)
			continue;
		// Channel A on scale Off, the cursor editing its row 2 (Quantermain:
		// Trigger source) or 1 (Meta-Q: octave); the L encoder to B, where
		// that row is the mask.
		const int row = k.app == kQuantermain ? 2 : 1;
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, k.app, 0, 0, 4)), "%s: scale Off dropped", an);
		CoreTest::encoderEvent(c, shoal::ENCODER_R, row - CoreTest::menuRow(c));
		CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
		CoreTest::encoderEvent(c, shoal::ENCODER_L, 1);
		CHECK(CoreTest::menuSetting(c) == k.mask, "%s: the cursor is on setting %d", an, CoreTest::menuSetting(c));
		const FriendlyModel stuck = model(c);
		const Row* r = findRow(stuck, k.mask);
		CHECK(r && (r->flags & shoal::kRowEditing) && !(stuck.flags & shoal::kModelBusy), "%s: editing on the mask row: flags %d", an,
			r ? r->flags : -1);
		const Snapshot held = snapshot(c);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, k.app, 1, other, 2)), "%s: SetValue taken while editing on the mask", an);
		CHECK(!c.friendlyEdit(makeEdit(FriendlyEdit::Focus, k.app, 1, other, 0)), "%s: Focus taken while editing on the mask", an);
		CHECK(sameSnapshot(held, snapshot(c)) && CoreTest::friendlyAccepting(c), "%s: a dropped edit changed the app", an);
		// On the mask row itself the cursor need not move: a cell toggles.
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::ToggleBit, k.app, 1, k.mask, 0)) && c.setting(1, k.mask) == 0xfffe,
			"%s: ToggleBit on the mask row being edited: mask %04x", an, c.setting(1, k.mask));
		// Why: there an R push opens the scale editor, not edit mode's end.
		CoreTest::buttonEvent(c, shoal::BUTTON_R, false);
		CHECK(!CoreTest::friendlyAccepting(c), "%s: R push on the mask row did not open the editor", an);
	}
}

// Title-bar status lines, as QQ_menu, DQ_menu and ASR_menu draw them.
void testQuantizerStatus() {
	{
		Core c;
		c.selectApp(kQuantermain);
		CHECK(std::strcmp(model(c).status, "A  B  C  D") == 0, "Quantermain status '%s'", model(c).status);
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kQuantermain, 1, 9, 1)), "Octave dropped");
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kQuantermain, 3, 9, -2)), "Octave dropped");
		CHECK(std::strcmp(model(c).status, "A  B +1  C  D -2") == 0, "Quantermain status '%s'", model(c).status);
	}
	{
		Core c;
		c.selectApp(kMetaQ);
		run(c, 0.2);
		CHECK(std::strcmp(model(c).status, "A #1 C  B #1 C") == 0, "Meta-Q status '%s'", model(c).status);
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kMetaQ, 1, 4, 4)), "root dropped");
		CHECK(c.friendlyEdit(makeEdit(FriendlyEdit::SetValue, kMetaQ, 0, 21, 2)), "octave dropped");
		CHECK(std::strcmp(model(c).status, "A #1 C +2  B #1 E") == 0, "Meta-Q status '%s'", model(c).status);
	}
	{
		Core c;
		c.selectApp(kCopier);
		CoreTest::encoderEvent(c, shoal::ENCODER_L, -1);
		CHECK(std::strcmp(model(c).status, "Next: Off ") == 0, "CopierMaschine status '%s'", model(c).status);
		CoreTest::buttonEvent(c, shoal::BUTTON_L, false);
		CHECK(std::strcmp(model(c).status, "Off ") == 0 && model(c).row[0].value == 4, "CopierMaschine status '%s'", model(c).status);
	}
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
	testPingPongEquivalence();
	testViznutcrackerEquivalence();
	testReferencesEquivalence();
	testH1200Equivalence();
	testAutomatonnetzEquivalence();
	testTabsFollowFirmware();
	testVisibilityFollows();
	testActions();
	testSubEditorsBusy();
	testReferencesAfterRestore();
	testFocusNewApps();
	testStatusAndText();
	testCopierEquivalence();
	testQuantermainEquivalence();
	testMetaQEquivalence();
	testMaskCells();
	testCopierScaleCommit();
	testTriggerSourceGuard();
	testQuantizerActions();
	testQuantizerBusy();
	testQuantizerStatus();
	testEditStorm();
	return rkatest::finish("test_friendly");
}
