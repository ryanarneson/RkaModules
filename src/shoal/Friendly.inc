// Friendly mode (Friendly.hpp): the row model of the current app and the
// edits on it, for shoal::Core::friendlyModel and friendlyEdit. Included by
// Firmware.cpp after SettingsAccess.inc and before CoreImpl.inc, so the app
// classes, their state and their handlers are visible.
//
// This file is the framework: the Adapter struct, the helpers every adapter
// uses (events, text, rows, the cursor), the table of
// adapters and the code common to all of them (the checks that drop an
// edit, and the model's frame). The adapters themselves are in
// FriendlyAdapters<n>.inc, included in the middle of this file.
//
// Rules for an adapter (docs/plans/2026-11-15-shoal-friendly-mode.md, Task 1):
// - A row's label, range, default and value names come from the firmware's
//   settings table; rows follow the app's enabled-settings list if it has one.
// - An edit changes a value only by sending the app's own handlers the events
//   the hardware would (encoder(), button()), with the firmware's cursor put
//   on the row first, so the app's guards and follow-ups run. Nothing here
//   calls apply_value or Core::setSettingRaw.
// - No allocation, no locks, no libc formatting (Text), no writable globals:
//   models and edits run on the engine thread, and all state is the
//   firmware's, in the current Context.

#include <cstring>   // std::memcpy (FriendlyAdapters4.inc)

#include "Friendly.hpp"

namespace shoal {
namespace friendly {
namespace {

typedef menu::ScreenCursor<menu::kScreenLines> Cursor;

constexpr int kExtraId = 256;    // ids of extra rows start here (Row::id)
constexpr int kHeadingId = 512;  // ids of headings start here

// A settings table's name array that is shorter than the setting's range:
// `count` names from the minimum on. With `names` given, the row's names
// are that array's instead of the table's (a firmware array that names the
// whole range), indexed by value as the table's are; static storage.
struct NameCount {
	int16_t setting, count;
	const char* const* names;
};
// A label in place of the settings table's, allowed where the table's name
// is a one- or two-letter abbreviation or an arrow to an editor ("--> edit").
struct Label { int16_t setting; const char* text; };

// One app's adapter. Every function runs with the core's Context current and
// the app current. Null members take the default given.
struct Adapter {
	// Fill m.tabs, m.tab, m.tabLabel and m.status. They arrive as one
	// unlabelled tab (tabs 1, tab 0) and an empty status. Null: leave that.
	void (*header)(FriendlyModel& m);
	// Append the rows of `tab` (0 .. tabs - 1; not always the selected one,
	// which is m.tab) with the add...() helpers. Rows under the firmware's
	// cursor are marked only when tab == m.tab.
	void (*rows)(FriendlyModel& m, int tab);
	// Make `tab` the app's selected channel, as its channel control would
	// (the events of that control). Null: the app has one tab.
	void (*selectTab)(int tab);
	// Carry out e on `row` (the model's row with e.id; its tab is already
	// selected, see edit() below for what has been checked). False: dropped.
	// Null: every edit on a row is dropped.
	bool (*edit)(const FriendlyEdit& e, const Row& row);
	// Whether the app is on its settings menu, so that events sent to its
	// handlers mean what the helpers assume. False while a sub-editor (scale,
	// sequence, chord, segment, cell) has the controls: the model is then
	// kModelBusy and every edit is dropped. Null: always (no sub-editor).
	bool (*accepting)();
	// Name arrays shorter than their range, or replacing the table's, for
	// addSetting(). May be null.
	const NameCount* nameCounts;
	int numNameCounts;
	// Labels replacing the table's, for addSetting(). May be null.
	const Label* labels;
	int numLabels;
};

inline const Adapter& adapterFor(int app);  // the table's entry, for the overrides

// ---- Events -----------------------------------------------------------------
// Straight to the current app's handlers, as OC::Ui::DispatchEvents sends
// them once the debounce and the acceleration have run.

// One encoder event carrying `delta` (nothing for 0, as the hardware). A
// delta beyond an event's 16 bits goes as several events.
inline void encoder(OC::UiControl control, int delta) {
	while (delta) {
		const int step = delta > INT16_MAX ? INT16_MAX : delta < -INT16_MAX ? -INT16_MAX : delta;
		OC::apps::current_app->HandleEncoderEvent(UI::Event(UI::EVENT_ENCODER, (uint16_t) control, (int16_t) step, 0));
		delta -= step;
	}
}

// A short or a long press. The app never sees an R or UP long press on the
// hardware (OC_ui.cpp: the app menu, and nothing): do not send those.
inline void button(OC::UiControl control, bool longPress = false) {
	OC::apps::current_app->HandleButtonEvent(
		UI::Event(longPress ? UI::EVENT_BUTTON_LONG_PRESS : UI::EVENT_BUTTON_PRESS, (uint16_t) control, 0, 0));
}

// ---- Text -------------------------------------------------------------------
// Writes into a fixed char array with integer arithmetic only (printf may
// allocate or lock), always terminated, cut off at the array's end.
//   Text(m.status).str("Freq1 ").num(128);
//   Text(r.text).fixed(12.345f, 2, 6).str(" Hz");     "12.35 Hz" after one pad space
struct Text {
	char* p;
	char* end;  // the last byte, kept for the terminator

	template <size_t N>
	explicit Text(char (&buffer)[N]) : p(buffer), end(buffer + N - 1) { *p = 0; }

	Text& ch(char c) {
		if (p < end) {
			*p++ = c;
			*p = 0;
		}
		return *this;
	}
	Text& str(const char* s) {
		while (*s)
			ch(*s++);
		return *this;
	}
	// As printf("%*d"): right-aligned in `width`.
	Text& num(int64_t v, int width = 0) { return digits(v < 0, v < 0 ? 0 - (uint64_t) v : (uint64_t) v, 0, width); }
	// As printf("%*.*f"), 0 to 6 decimals, rounded half away from zero.
	// Beyond +-1e12 it prints that limit; not-a-number prints "nan".
	Text& fixed(double v, int decimals, int width = 0) {
		if (v != v)
			return str("nan");
		decimals = decimals < 0 ? 0 : decimals > 6 ? 6 : decimals;
		double a = v < 0 ? -v : v;
		if (!(a < 1e12))
			a = 1e12;
		for (int i = 0; i < decimals; i++)
			a *= 10.0;
		return digits(v < 0, (uint64_t) (a + 0.5), decimals, width);
	}

private:
	Text& digits(bool negative, uint64_t magnitude, int decimals, int width) {
		char buf[32];  // filled from the end
		int n = 0;
		do {
			buf[n++] = (char) ('0' + magnitude % 10);
			magnitude /= 10;
			if (n == decimals)
				buf[n++] = '.';
		} while (magnitude || n < (decimals ? decimals + 2 : 1));  // at least one digit before the point
		if (negative)
			buf[n++] = '-';
		for (int i = n; i < width; i++)
			ch(' ');
		while (n)
			ch(buf[--n]);
		return *this;
	}
};

// ---- Rows -------------------------------------------------------------------

// Append a row. kMaxRows holds every app's largest list (Sequins, 46 rows;
// test_friendly fails on a cut model), so a model is never cut short; if an adapter
// written wrong adds more, the model says so (kModelTruncated) and the rows
// past the end go to a scratch row nobody reads.
inline Row& addRow(FriendlyModel& m, RowKind kind, int id, const char* label) {
	Row* r = &current()->friendly_scratch;
	if (m.rows < kMaxRows)
		r = &m.row[m.rows++];
	else
		m.flags |= kModelTruncated;
	*r = Row();
	r->kind = kind;
	r->id = (int16_t) id;
	r->label = label;
	return *r;
}

// The row's value as text, for a value the list cannot name itself.
inline void setNumberText(Row& r) {
	Text(r.text).num(r.value);
}

// A row for one entry of a settings table: Choice if it has value names,
// Toggle if it is 0..1, else Number. The row's id is the setting index. The
// app's adapter entry may replace the label and give the length of a short
// name array (Adapter::labels, nameCounts). An adapter may then change
// r.kind (Mask, SubEditor) or r.text.
inline Row& addSettingRow(FriendlyModel& m, int setting, const settings::value_attr& attr, int value) {
	const RowKind kind = attr.value_names ? RowKind::Choice
		: attr.min_ == 0 && attr.max_ == 1 ? RowKind::Toggle : RowKind::Number;
	const Adapter& a = adapterFor(m.app);
	const char* label = attr.name;
	for (int i = 0; i < a.numLabels; i++)
		if (a.labels[i].setting == setting)
			label = a.labels[i].text;
	Row& r = addRow(m, kind, setting, label);
	r.value = value;
	r.min = attr.min_;
	r.max = attr.max_;
	r.def = attr.default_;
	if (kind == RowKind::Choice) {
		// The firmware names value v value_names[v] (OC_menus.h,
		// SettingsListItem::DrawDefault), whatever the minimum; the row
		// reads names[v - min]. No named setting has a minimum below 0.
		const char* const* names = attr.value_names;
		int count = attr.max_ - attr.min_ + 1;
		for (int i = 0; i < a.numNameCounts; i++)
			if (a.nameCounts[i].setting == setting) {
				count = a.nameCounts[i].count;
				if (a.nameCounts[i].names)
					names = a.nameCounts[i].names;
			}
		r.names = names + attr.min_;
		r.nameCount = (int16_t) count;
		if (value - attr.min_ >= count)
			setNumberText(r);
	}
	return r;
}

// The same for setting `setting` of settings object `obj`.
template <class T>
inline Row& addSetting(FriendlyModel& m, const T& obj, int setting) {
	return addSettingRow(m, setting, T::value_attr((size_t) setting), obj.get_value((size_t) setting));
}

// A row that is not a setting: an encoder-only function. id >= kExtraId.
inline Row& addExtra(FriendlyModel& m, RowKind kind, int id, const char* label) {
	Row& r = addRow(m, kind, id, label);
	r.flags |= kRowExtra;
	return r;
}

// A section title (one per menu page). id >= kHeadingId.
inline Row& addHeading(FriendlyModel& m, int id, const char* label) {
	return addExtra(m, RowKind::Heading, id, label);
}

// Mark r as the row under cursor c.
inline void markCursor(Row& r, const Cursor& c) {
	r.flags |= c.editing() ? kRowCursor | kRowEditing : kRowCursor;
}

// Rows for settings first..last of obj, in table order: for an app without
// an enabled-settings list, whose cursor position is a setting index.
// cursor: the app's cursor, or null to mark no row (a tab that is not the
// selected one).
template <class T>
inline void addTable(FriendlyModel& m, const T& obj, int first, int last, const Cursor* cursor) {
	for (int i = first; i <= last; i++) {
		Row& r = addSetting(m, obj, i);
		if (cursor && cursor->cursor_pos() == i)
			markCursor(r, *cursor);
	}
}

// Rows for obj's enabled-settings list, in menu order: for an app whose
// cursor position is an index into that list. cursor as for addTable. A
// setting the list repeats (References pads its menu with the same dummy
// setting twice) gets one row, which takes the cursor from either place.
template <class T>
inline void addEnabled(FriendlyModel& m, const T& obj, const Cursor* cursor) {
	const int first = m.rows, n = obj.num_enabled_settings();
	for (int i = 0; i < n; i++) {
		const int setting = (int) obj.enabled_setting_at(i);
		Row* r = nullptr;
		for (int k = first; k < m.rows && !r; k++)
			if (m.row[k].id == setting)
				r = &m.row[k];
		if (!r)
			r = &addSetting(m, obj, setting);
		if (cursor && cursor->cursor_pos() == i)
			markCursor(*r, *cursor);
	}
}

// Where `setting` is in obj's enabled-settings list: its menu row (the first,
// if the list repeats it). -1: hidden.
template <class T>
inline int enabledRowOf(const T& obj, int setting) {
	const int n = obj.num_enabled_settings();
	for (int i = 0; i < n; i++)
		if ((int) obj.enabled_setting_at(i) == setting)
			return i;
	return -1;
}

// ---- The cursor -------------------------------------------------------------
// Both helpers are for a menu row whose R push toggles the cursor's edit
// mode (whatever else the app's handler does with it), and both go through
// that handler, so what the app does on entering and leaving edit mode
// happens. Not for a row whose R push opens a sub-editor.
//
// An app whose menu has such rows (a mask row opens the scale editor), or
// whose list can be rebuilt under an editing cursor (a stale list the edit
// rebuilds), passes a Menu. The rule the helpers then keep: a friendly edit
// never opens a sub-editor, and a SetValue ends on the settings menu with
// the cursor not editing. Where an R push would not leave edit mode (it
// opens a sub-editor, or switches page), they leave it by suspending and
// resuming the app instead (suspendAndResume).

// What the cursor helpers need to know of an app's menu, for the selected
// channel, as its handlers see it now.
struct Menu {
	// The setting at a menu row, read as the app's R-push and R-encoder
	// handlers read it (enabled_setting_at(row), past the list's end too).
	int (*settingAt)(int menuRow);
	// Whether the R push on that setting's row does something other than
	// toggle edit mode: opens a sub-editor (or switches menu page).
	bool (*opensEditor)(int setting);
	// The setting's menu row; -1: hidden.
	int (*rowOf)(int setting);
};

// Leave edit mode where an R push would open a sub-editor: the app gets
// SUSPEND then RESUME, as when Core::selectApp leaves and re-enters it. On
// RESUME each app that passes a Menu leaves edit mode and closes its
// editors. This is what the app sees of the way out a person takes (an R
// long press opens the app menu, an R push closes it on the same app), less
// the app menu itself: nothing is drawn, and UI events the panel queued
// meanwhile are kept (the app menu's close would drop them).
inline void suspendAndResume() {
	OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_SUSPEND);
	OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_RESUME);
}

// Whether the cursor is editing on a row whose R push opens a sub-editor
// (edit mode carried there by a channel switch, or a list rebuilt under it).
inline bool editingOnEditorRow(const Cursor& c, const Menu* menu) {
	return c.editing() && menu && menu->opensEditor(menu->settingAt(c.cursor_pos()));
}

// Leave edit mode: an R push, or suspendAndResume where the R push would
// open a sub-editor. Not editing: nothing.
inline void leaveEditing(const Cursor& c, const Menu* menu) {
	if (editingOnEditorRow(c, menu))
		suspendAndResume();
	else if (c.editing())
		button(OC::CONTROL_BUTTON_R);
}

// Put the cursor on menu row `menuRow` (a setting index, or an index into
// the enabled list: whatever the app's cursor counts), as a person would:
// if it is editing another row, it leaves edit mode first (leaveEditing).
// On the row already: nothing, and edit mode stays as it is. This is all of
// Focus.
inline void moveCursor(Cursor& c, int menuRow, const Menu* menu = nullptr) {
	if (menuRow == c.cursor_pos())
		return;
	leaveEditing(c, menu);
	c.Scroll(menuRow - c.cursor_pos());
}

// A value edit on a menu row, as the hardware gesture: cursor to the row,
// R push to enter edit mode (unless the cursor is editing that row already),
// the R encoder turned by `delta` (target - current), R push to leave. The
// cursor ends on the row, not editing. unitSteps: the turn goes as |delta|
// events of one step, for handlers whose guard looks at single steps only;
// otherwise as one event. False, with no edit made, if the cursor cannot
// reach the row (the menu's end is short of it: References after a patch
// load), the row's R push opens a sub-editor (menu), or the R push did not
// put the cursor in edit mode; the cursor may have moved on the way (and
// left edit mode on another row).
//
// With a menu: if a step rebuilds the list so that another setting comes
// under the cursor, the steps stop there (the rest would go to that
// setting), edit mode is left (leaveEditing: never an R push that opens a
// sub-editor), and the rest of the turn goes to the setting's new row, as
// a person would go on; if it is hidden now, the edit ends where it got.
inline bool replaySet(Cursor& c, int menuRow, int delta, bool unitSteps = false, const Menu* menu = nullptr) {
	moveCursor(c, menuRow, menu);
	if (c.cursor_pos() != menuRow)
		return false;
	const int setting = menu ? menu->settingAt(menuRow) : -1;
	if (menu && menu->opensEditor(setting))
		return false;
	if (!c.editing())
		button(OC::CONTROL_BUTTON_R);
	if (!c.editing())
		return false;
	int left = delta;
	if (!unitSteps) {
		encoder(OC::CONTROL_ENCODER_R, left);
		left = 0;
	}
	for (; left && (!menu || menu->settingAt(c.cursor_pos()) == setting); left += left > 0 ? -1 : 1)
		encoder(OC::CONTROL_ENCODER_R, left > 0 ? 1 : -1);
	leaveEditing(c, menu);
	if (left && menu) {  // each pass sends a step at least, so this ends
		const int row = menu->rowOf(setting);
		if (row >= 0)
			replaySet(c, row, left, unitSteps, menu);
	}
	return true;
}

// SetValue and Focus on a row of a plain settings menu (every row's R push
// toggles edit mode), at menu row `menuRow`: -1 for a row the menu does not
// have (an extra row, or a setting the app keeps elsewhere), on which Focus
// does nothing and SetValue is dropped. Other edit types are dropped.
inline bool menuEdit(const FriendlyEdit& e, const Row& row, Cursor& c, int menuRow) {
	switch (e.type) {
		case FriendlyEdit::SetValue:
			return menuRow >= 0 && replaySet(c, menuRow, e.value - row.value);
		case FriendlyEdit::Focus:
			if (menuRow >= 0)
				moveCursor(c, menuRow);
			return true;
		default:
			return false;
	}
}

// ---- The adapters -----------------------------------------------------------
// One file per group of apps; each defines `constexpr Adapter kAdapter` in a
// namespace named after its app, for the table below.

#include "FriendlyAdapters1.inc"
#include "FriendlyAdapters2.inc"
#include "FriendlyAdapters3.inc"
#include "FriendlyAdapters4.inc"

// ---- The table --------------------------------------------------------------

// One adapter per app, in available_apps order (as kAppSettings).
constexpr Adapter kAdapters[] = {
	copier::kAdapter,
	harrington::kAdapter,
	automaton::kAdapter,
	quantermain::kAdapter,
	metaq::kAdapter,
	quadraturia::kAdapter,
	lowrents::kAdapter,
	piqued::kAdapter,
	sequins::kAdapter,
	pingpong::kAdapter,
	viznut::kAdapter,
	acidcurds::kAdapter,
	references::kAdapter,
};
static_assert(sizeof(kAdapters) / sizeof(kAdapters[0]) == NUM_AVAILABLE_APPS, "one friendly adapter per app");

inline const Adapter& adapterFor(int app) {
	return kAdapters[app >= 0 && app < NUM_AVAILABLE_APPS ? app : 0];
}

// ---- Common to every app ----------------------------------------------------
// `a` is the current app's entry in kAdapters (or, in the tests, a stand-in
// for it).

// The model's frame for the current app: app, flags, tabs, labels, status;
// no rows. False (an empty model) if the firmware's current app is not one
// of the 13.
inline bool header(const Adapter& a, FriendlyModel& m) {
	const int app = OC::apps::index_of(OC::global_settings.current_app_id);
	const bool known = app >= 0 && app < NUM_AVAILABLE_APPS;
	m.app = (uint8_t) (known ? app : 0);
	m.tabs = 1;
	m.tab = 0;
	m.flags = 0;
	for (const char*& label : m.tabLabel)
		label = nullptr;
	m.status[0] = 0;
	m.rows = 0;
	if (!known)
		return false;
	if (a.header)
		a.header(m);
	if (m.tabs < 1 || m.tabs > kMaxTabs || m.tab >= m.tabs)  // an adapter written wrong
		m.tabs = 1, m.tab = 0;
	if (OC::g_ui.app_menu_open() || (a.accepting && !a.accepting()))
		m.flags |= kModelBusy;
	return true;
}

// Core::friendlyModel.
inline void model(const Adapter& a, FriendlyModel& m) {
	if (header(a, m))
		a.rows(m, m.tab);
}

// Make `tab` the selected one. False if the app did not follow.
inline bool selectTab(const Adapter& a, const FriendlyModel& m, int tab) {
	if (tab == m.tab)
		return true;
	a.selectTab(tab);
	FriendlyModel after;
	return header(a, after) && after.tab == tab && !(after.flags & kModelBusy);
}

// Core::friendlyEdit. An edit is dropped (false) when
// - it was made for another app, or for a tab the app does not have;
// - the app is not on its settings menu (kModelBusy: the app menu is open,
//   or the adapter is not accepting);
// - the app has several tabs and its adapter no selectTab;
// - its row is not in that tab's list now, or does not take that type;
// - the app did not select the edit's tab when asked.
// What reaches the adapter's edit(): e.app is current, e.tab is selected,
// `row` is a copy of the current row with e.id, and
//   SetValue   row is a Number, Choice or Toggle; e.value is within its range
//   ToggleBit  row is a Mask; e.value is one of its cells (0 .. row.max - 1)
//   Press      row is an Action
//   Focus      row is anything but a Heading
inline bool edit(const Adapter& a, const FriendlyEdit& e) {
	FriendlyModel m;
	if (!header(a, m) || (m.flags & kModelBusy) || e.app != m.app || e.tab >= m.tabs)
		return false;
	if (m.tabs > 1 && !a.selectTab)
		return false;
	if (e.type == FriendlyEdit::SelectTab)
		return e.value >= 0 && e.value < m.tabs && selectTab(a, m, e.value);
	if (!a.edit)
		return false;
	a.rows(m, e.tab);
	const Row* found = nullptr;
	for (int i = 0; i < m.rows && !found; i++)
		if (m.row[i].id == e.id)
			found = &m.row[i];
	if (!found)
		return false;
	const Row row = *found;
	FriendlyEdit checked = e;
	switch (e.type) {
		case FriendlyEdit::SetValue:
			if (row.kind != RowKind::Number && row.kind != RowKind::Choice && row.kind != RowKind::Toggle)
				return false;
			checked.value = e.value < row.min ? row.min : e.value > row.max ? row.max : e.value;
			break;
		case FriendlyEdit::ToggleBit:
			if (row.kind != RowKind::Mask || e.value < 0 || e.value >= row.max)
				return false;
			break;
		case FriendlyEdit::Press:
			if (row.kind != RowKind::Action)
				return false;
			break;
		case FriendlyEdit::Focus:
			if (row.kind == RowKind::Heading)
				return false;
			break;
		default:
			return false;
	}
	if (!selectTab(a, m, e.tab))
		return false;
	return a.edit(checked, row);
}

// ---- Stand-in adapters for the tests (CoreTest::friendlyFake) ----------------
// Each is the Low-rents adapter with one thing wrong, to reach the drops no
// real adapter can show yet. Only meaningful while Low-rents is current.

namespace fake {

bool never() { return false; }
void twoTabs(FriendlyModel& m) {
	m.tabs = 2;
	m.tabLabel[0] = "1";
	m.tabLabel[1] = "2";
}
void selectNothing(int) { }
// Asks replaySet for the row after the menu's last, which the cursor cannot
// reach.
bool pastTheEnd(const FriendlyEdit& e, const Row& row) {
	return replaySet(lorenz_generator_state.cursor, LORENZ_SETTING_LAST, e.value - row.value);
}
void tooManyRows(FriendlyModel& m, int) {
	for (int i = 0; i < kMaxRows + 5; i++)
		addExtra(m, RowKind::Action, kExtraId + i, "row");
}

constexpr Adapter kFakes[] = {
	{ &lowrents::header, &lowrents::rows, nullptr, &lowrents::edit, &never, nullptr, 0, nullptr, 0 },    // NotAccepting
	{ &twoTabs, &lowrents::rows, nullptr, &lowrents::edit, nullptr, nullptr, 0, nullptr, 0 },            // TabsNoSelect
	{ &twoTabs, &lowrents::rows, &selectNothing, &lowrents::edit, nullptr, nullptr, 0, nullptr, 0 },     // SelectFails
	{ nullptr, &tooManyRows, nullptr, &lowrents::edit, nullptr, nullptr, 0, nullptr, 0 },                // TooManyRows
	{ &lowrents::header, &lowrents::rows, nullptr, &pastTheEnd, nullptr, nullptr, 0, nullptr, 0 },       // ReachFails
};

} // namespace fake

} // namespace
} // namespace friendly
} // namespace shoal
