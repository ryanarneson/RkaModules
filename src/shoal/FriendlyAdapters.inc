// Friendly mode (Friendly.hpp): the row model of the current app and the
// edits on it, for shoal::Core::friendlyModel and friendlyEdit. Included by
// Firmware.cpp after SettingsAccess.inc and before CoreImpl.inc, so the app
// classes, their state and their handlers are visible.
//
// Three parts: the helpers every adapter uses (events, rows, the cursor),
// one adapter per app, and the table of adapters with the code common to
// all of them (the checks that drop an edit, and the model's frame).
//
// Rules for an adapter (docs/plans/2026-11-15-shoal-friendly-mode.md, Task 1):
// - A row's label, range, default and value names come from the firmware's
//   settings table; rows follow the app's enabled-settings list if it has one.
// - An edit changes a value only by sending the app's own handlers the events
//   the hardware would (encoder(), button()), with the firmware's cursor put
//   on the row first, so the app's guards and follow-ups run. Nothing here
//   calls apply_value or Core::setSettingRaw.
// - No allocation, no locks, no writable globals: models and edits run on the
//   engine thread, and all state is the firmware's, in the current Context.
#include <cassert>
#include <cstdio>

#include "Friendly.hpp"

namespace shoal {
namespace friendly {
namespace {

typedef menu::ScreenCursor<menu::kScreenLines> Cursor;

constexpr int kExtraId = 256;    // ids of extra rows start here (Row::id)
constexpr int kHeadingId = 512;  // ids of headings start here

// A settings table's name array that is shorter than the setting's range.
struct NameCount { int16_t setting, count; };

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
	// Name arrays shorter than their range, for addSetting(). May be null.
	const NameCount* nameCounts;
	int numNameCounts;
};

inline const Adapter& adapterFor(int app);

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

// |delta| events of one step each, for handlers whose guard looks at single
// steps only (Quantermain and Meta-Q trigger and source).
inline void encoderSteps(OC::UiControl control, int delta) {
	for (; delta > 0; delta--)
		encoder(control, 1);
	for (; delta < 0; delta++)
		encoder(control, -1);
}

// A short or a long press. The app never sees an R or UP long press on the
// hardware (OC_ui.cpp: the app menu, and nothing): do not send those.
inline void button(OC::UiControl control, bool longPress = false) {
	OC::apps::current_app->HandleButtonEvent(
		UI::Event(longPress ? UI::EVENT_BUTTON_LONG_PRESS : UI::EVENT_BUTTON_PRESS, (uint16_t) control, 0, 0));
}

// ---- Rows -------------------------------------------------------------------

// Append a row. A model is never cut short: kMaxRows holds every app's
// largest list (static_assert below, and test_friendly), so the assert only
// guards an adapter written wrong.
inline Row& addRow(FriendlyModel& m, RowKind kind, int id, const char* label) {
	assert(m.rows < kMaxRows);
	if (m.rows >= kMaxRows)
		m.rows = kMaxRows - 1;
	Row& r = m.row[m.rows++];
	r = Row();
	r.kind = kind;
	r.id = (int16_t) id;
	r.label = label;
	return r;
}

// The row's value as text, for a value the list cannot name itself.
inline void setNumberText(Row& r) {
	snprintf(r.text, sizeof(r.text), "%d", (int) r.value);
}

// A row for one entry of a settings table: Choice if it has value names,
// Toggle if it is 0..1, else Number. The row's id is the setting index.
// An adapter may then change r.kind (Mask, SubEditor) or r.text.
inline Row& addSettingRow(FriendlyModel& m, int setting, const settings::value_attr& attr, int value) {
	const RowKind kind = attr.value_names ? RowKind::Choice
		: attr.min_ == 0 && attr.max_ == 1 ? RowKind::Toggle : RowKind::Number;
	Row& r = addRow(m, kind, setting, attr.name);
	r.value = value;
	r.min = attr.min_;
	r.max = attr.max_;
	r.def = attr.default_;
	if (kind == RowKind::Choice) {
		r.names = attr.value_names;
		int count = attr.max_ - attr.min_ + 1;
		const Adapter& a = adapterFor(m.app);
		for (int i = 0; i < a.numNameCounts; i++)
			if (a.nameCounts[i].setting == setting)
				count = a.nameCounts[i].count;
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
	assert(id >= kExtraId);
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

// Put the cursor on menu row `menuRow` (a setting index, or an index into
// the enabled list: whatever the app's cursor counts). Edit mode is kept.
inline void moveCursor(Cursor& c, int menuRow) {
	if (menuRow != c.cursor_pos())
		c.Scroll(menuRow - c.cursor_pos());
}

// A value edit on a menu row, for an app whose R push toggles the cursor's
// edit mode and whose R encoder edits the row under an editing cursor: the
// cursor goes to the row and into edit mode, the app's R-encoder handler
// gets `delta` (target - current) as one event, or as unit steps, and the
// cursor is left on the row, not editing.
inline void replaySet(Cursor& c, int menuRow, int delta, bool unitSteps = false) {
	moveCursor(c, menuRow);
	c.set_editing(true);
	if (unitSteps)
		encoderSteps(OC::CONTROL_ENCODER_R, delta);
	else
		encoder(OC::CONTROL_ENCODER_R, delta);
	c.set_editing(false);
}

// ---- The generic adapter ----------------------------------------------------
// For an app without an adapter of its own yet: the first channel's settings
// (its enabled list if the app keeps one), read-only. It knows neither the
// app's cursor nor its handlers, so it marks no row and drops every edit.

template <class T, int N, T& (*Obj)(int)>
struct GenericTable {
	static_assert(N + 8 <= kMaxRows, "kMaxRows holds the whole table and its extra rows");
	static void rows(FriendlyModel& m, int) { addTable(m, Obj(0), 0, N - 1, nullptr); }
};

template <class T, int N, T& (*Obj)(int)>
struct GenericEnabled {
	static_assert(N + 8 <= kMaxRows, "kMaxRows holds the whole table and its extra rows");
	static void rows(FriendlyModel& m, int) { addEnabled(m, Obj(0), nullptr); }
};

// ---- Quadraturia (APP_POLYLFO.ino) ------------------------------------------
// One settings object, no enabled list: the cursor position is a setting
// index, from Tap tempo on. Coarse and fine are not in the firmware's menu:
// the L encoder edits whichever of the two L push has selected (the title
// bar's "(C)" or "(F)"), and not at all while tap tempo is on.

namespace quadraturia {

enum { kPhaseReset = kExtraId };

void header(FriendlyModel& m) {
	// POLYLFO_menu's title bar.
	if (poly_lfo.get_tap_tempo()) {
		snprintf(m.status, sizeof(m.status), "(T) Ch A: tap tempo");
		return;
	}
	const char* mode = PolyLfo::value_attr(poly_lfo_state.left_edit_mode).name;
	const float freq = poly_lfo.lfo.get_freq_ch1();
	if (freq >= 0.1f)
		snprintf(m.status, sizeof(m.status), "(%s) Ch A: %6.2f Hz", mode, (double) freq);
	else
		snprintf(m.status, sizeof(m.status), "(%s) Ch A: %6.3fs", mode, (double) (1.0f / freq));
}

void rows(FriendlyModel& m, int) {
	addTable(m, poly_lfo, 0, POLYLFO_SETTING_LAST - 1, &poly_lfo_state.cursor);
	addExtra(m, RowKind::Action, kPhaseReset, "Phase reset");  // DOWN long press
}

bool edit(const FriendlyEdit& e, const Row& row) {
	const bool leftSide = row.id == POLYLFO_SETTING_COARSE || row.id == POLYLFO_SETTING_FINE;
	switch (e.type) {
		case FriendlyEdit::SetValue:
			if (leftSide) {
				if (poly_lfo_state.left_edit_mode != row.id)
					button(OC::CONTROL_BUTTON_L);
				encoder(OC::CONTROL_ENCODER_L, e.value - row.value);
			} else {
				replaySet(poly_lfo_state.cursor, row.id, e.value - row.value);
			}
			return true;
		case FriendlyEdit::Press:
			button(OC::CONTROL_BUTTON_DOWN, true);
			return true;
		case FriendlyEdit::Focus:
			if (!leftSide && row.id < kExtraId)
				moveCursor(poly_lfo_state.cursor, row.id);
			return true;
		default:
			return false;
	}
}

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, nullptr, 0 };

} // namespace quadraturia

// ---- Low-rents (APP_LORENZ.ino) ---------------------------------------------
// One settings object, no enabled list: the cursor position is a setting
// index, from Rho/c 1 on. Freq 1 and Freq 2 are not in the firmware's menu:
// the L encoder edits the one L push has selected.

namespace lowrents {

void header(FriendlyModel& m) {
	// LORENZ_menu's title bar: both frequencies with their CV.
	const int32_t freq1 = SCALE8_16(lorenz_generator.get_freq1()) + (lorenz_generator.cv_freq1.value() * 16);
	const int32_t freq2 = SCALE8_16(lorenz_generator.get_freq2()) + (lorenz_generator.cv_freq2.value() * 16);
	snprintf(m.status, sizeof(m.status), "Freq1 %d  Freq2 %d", (int) (USAT16(freq1) >> 8), (int) (USAT16(freq2) >> 8));
}

void rows(FriendlyModel& m, int) {
	addTable(m, lorenz_generator, 0, LORENZ_SETTING_LAST - 1, &lorenz_generator_state.cursor);
}

bool edit(const FriendlyEdit& e, const Row& row) {
	const bool leftSide = row.id == LORENZ_SETTING_FREQ1 || row.id == LORENZ_SETTING_FREQ2;
	switch (e.type) {
		case FriendlyEdit::SetValue:
			if (leftSide) {
				if (lorenz_generator_state.selected_generator != (row.id == LORENZ_SETTING_FREQ2))
					button(OC::CONTROL_BUTTON_L);
				encoder(OC::CONTROL_ENCODER_L, e.value - row.value);
			} else {
				replaySet(lorenz_generator_state.cursor, row.id, e.value - row.value);
			}
			return true;
		case FriendlyEdit::Focus:
			if (!leftSide)
				moveCursor(lorenz_generator_state.cursor, row.id);
			return true;
		default:
			return false;
	}
}

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, nullptr, 0 };

} // namespace lowrents

// ---- The table --------------------------------------------------------------

// Name arrays shorter than their range (survey: "Value names shorter than
// the setting's range"); counts as declared in the firmware source.
constexpr NameCount kMetaQNames[] = {
	{ DQ_CHANNEL_SETTING_SOURCE, 4 },    // 0..4 over OC::Strings::cv_input_names[4]
};
constexpr NameCount kPiquedNames[] = {
	{ ENV_SETTING_TRIGGER_INPUT, 4 },    // 0..6 over OC::Strings::trigger_input_names[4]
};
constexpr NameCount kAcidCurdsNames[] = {
	{ CHORDS_SETTING_CHORD_SLOT, 8 },    // 0..CHORDS_USER_LAST - 1 over chords_slots, 8 names
};

// One adapter per app, in available_apps order (as kAppSettings).
constexpr Adapter kAdapters[] = {
	{ nullptr, &GenericEnabled<ASR, ASR_SETTING_LAST, &asrObj>::rows, nullptr, nullptr, nullptr, 0 },
	{ nullptr, &GenericEnabled<H1200Settings, H1200_SETTING_LAST, &h1200Obj>::rows, nullptr, nullptr, nullptr, 0 },
	{ nullptr, &GenericTable<AutomatonnetzState, GRID_SETTING_LAST, &automatonnetzObj>::rows, nullptr, nullptr, nullptr, 0 },
	{ nullptr, &GenericEnabled<QuantizerChannel, CHANNEL_SETTING_LAST, &qqObj>::rows, nullptr, nullptr, nullptr, 0 },
	{ nullptr, &GenericEnabled<DQ_QuantizerChannel, DQ_CHANNEL_SETTING_LAST, &dqObj>::rows, nullptr, nullptr, kMetaQNames, 1 },
	quadraturia::kAdapter,
	lowrents::kAdapter,
	{ nullptr, &GenericEnabled<EnvelopeGenerator, ENV_SETTING_LAST, &envObj>::rows, nullptr, nullptr, kPiquedNames, 1 },
	{ nullptr, &GenericEnabled<SEQ_Channel, SEQ_CHANNEL_SETTING_LAST, &seqObj>::rows, nullptr, nullptr, nullptr, 0 },
	{ nullptr, &GenericTable<BouncingBall, BB_SETTING_LAST, &bbObj>::rows, nullptr, nullptr, nullptr, 0 },
	{ nullptr, &GenericEnabled<ByteBeat, BYTEBEAT_SETTING_LAST, &bytebeatObj>::rows, nullptr, nullptr, nullptr, 0 },
	{ nullptr, &GenericEnabled<Chords, CHORDS_SETTING_LAST, &chordsObj>::rows, nullptr, nullptr, kAcidCurdsNames, 1 },
	{ nullptr, &GenericEnabled<ReferenceChannel, REF_SETTING_LAST, &refsObj>::rows, nullptr, nullptr, nullptr, 0 },
};
static_assert(sizeof(kAdapters) / sizeof(kAdapters[0]) == NUM_AVAILABLE_APPS, "one friendly adapter per app");

inline const Adapter& adapterFor(int app) {
	return kAdapters[app >= 0 && app < NUM_AVAILABLE_APPS ? app : 0];
}

// ---- Common to every app ----------------------------------------------------

// The model's frame for the current app: app, tabs, labels, status; no rows.
// False if the firmware's current app is not one of the 13.
inline bool header(FriendlyModel& m) {
	m = FriendlyModel();
	const int app = OC::apps::index_of(OC::global_settings.current_app_id);
	if (app < 0 || app >= NUM_AVAILABLE_APPS)
		return false;
	m.app = (uint8_t) app;
	m.tabs = 1;
	const Adapter& a = kAdapters[app];
	if (a.header)
		a.header(m);
	const bool sound = m.tabs >= 1 && m.tabs <= kMaxTabs && m.tab < m.tabs;
	assert(sound);  // an adapter written wrong
	if (!sound)
		m.tabs = 1, m.tab = 0;
	return true;
}

// Core::friendlyModel.
inline void model(FriendlyModel& m) {
	if (header(m))
		kAdapters[m.app].rows(m, m.tab);
}

// Core::friendlyEdit. Drops an edit made for another app, made while the app
// menu is open, for a tab the app does not have, for a row that tab does not
// list now, or of a type the row's kind does not take. What reaches the
// adapter's edit(): e.app is current, e.tab is selected, `row` is the
// current row with e.id, and
//   SetValue   row is a Number, Choice or Toggle; e.value is within its range
//   ToggleBit  row is a Mask; e.value is one of its cells (0 .. row.max - 1)
//   Press      row is an Action
//   Focus      row is anything but a Heading
inline bool edit(const FriendlyEdit& e) {
	FriendlyModel m;
	if (OC::g_ui.app_menu_open() || !header(m) || e.app != m.app || e.tab >= m.tabs)
		return false;
	const Adapter& a = kAdapters[m.app];
	if (e.type == FriendlyEdit::SelectTab) {
		if (e.value < 0 || e.value >= m.tabs)
			return false;
		if (e.value != m.tab && a.selectTab)
			a.selectTab(e.value);
		return true;
	}
	if (!a.edit)
		return false;
	a.rows(m, e.tab);
	const Row* row = nullptr;
	for (int i = 0; i < m.rows && !row; i++)
		if (m.row[i].id == e.id)
			row = &m.row[i];
	if (!row)
		return false;
	FriendlyEdit checked = e;
	switch (e.type) {
		case FriendlyEdit::SetValue:
			if (row->kind != RowKind::Number && row->kind != RowKind::Choice && row->kind != RowKind::Toggle)
				return false;
			checked.value = e.value < row->min ? row->min : e.value > row->max ? row->max : e.value;
			break;
		case FriendlyEdit::ToggleBit:
			if (row->kind != RowKind::Mask || e.value < 0 || e.value >= row->max)
				return false;
			break;
		case FriendlyEdit::Press:
			if (row->kind != RowKind::Action)
				return false;
			break;
		case FriendlyEdit::Focus:
			if (row->kind == RowKind::Heading)
				return false;
			break;
		default:
			return false;
	}
	if (e.tab != m.tab && a.selectTab)
		a.selectTab(e.tab);
	return a.edit(checked, *row);
}

} // namespace
} // namespace friendly
} // namespace shoal
