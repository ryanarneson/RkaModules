// Friendly-mode adapters, file 2: Harrington 1200, Automatonnetz, Dialectic
// Ping Pong, Viznutcracker sweet and References. Included by Friendly.inc
// between its helpers and its table of adapters; see that file for the
// Adapter struct, the helpers used here and the rules an adapter follows.

// ---- Shared by the four-channel apps ----------------------------------------
// Dialectic Ping Pong, Viznutcracker sweet and References: four settings
// objects, A to D, one shown at a time; the L encoder selects it (clamped
// to 0..3, no wrap) and does nothing else.

constexpr const char* kQuadTabs[4] = { "A", "B", "C", "D" };

inline void quadTabs(FriendlyModel& m, int selected) {
	m.tabs = 4;
	m.tab = (uint8_t) (selected >= 0 && selected < 4 ? selected : 0);
	for (int i = 0; i < 4; i++)
		m.tabLabel[i] = kQuadTabs[i];
}

// A number as weegfx's pretty_print writes it: "+n", "-n", " 0".
inline Text& prettyNum(Text& t, int v) {
	if (v > 0)
		t.ch('+');
	else if (v == 0)
		t.ch(' ');
	return t.num(v);
}

// ---- Dialectic Ping Pong (APP_BBGEN.ino) ------------------------------------
// No enabled list: all 11 settings, the cursor position is a setting index.
// UP and DOWN are Gravity +-32 on the selected channel, a shortcut to a row;
// L push and every long press do nothing. No sub-editor.

namespace pingpong {

void header(FriendlyModel& m) {
	quadTabs(m, bbgen.ui.selected_channel);
}

void rows(FriendlyModel& m, int tab) {
	addTable(m, bbgen.balls_[tab], 0, BB_SETTING_LAST - 1, tab == m.tab ? &bbgen.ui.cursor : nullptr);
}

void selectTab(int tab) {
	encoder(OC::CONTROL_ENCODER_L, tab - bbgen.ui.selected_channel);
}

bool edit(const FriendlyEdit& e, const Row& row) {
	return menuEdit(e, row, bbgen.ui.cursor, row.id < kExtraId ? row.id : -1);
}

constexpr Adapter kAdapter = { &header, &rows, &selectTab, &edit, nullptr, nullptr, 0, nullptr, 0 };

} // namespace pingpong

// ---- Viznutcracker sweet (APP_BYTEBEATGEN.ino) ------------------------------
// Each channel keeps an enabled list: Loop mode on shows the six loop rows.
// The R-encoder handler rebuilds it after a Loop mode edit, so the list
// follows. The cursor position indexes the selected channel's list; after
// the L encoder moves to a channel with a shorter list it can be past the
// end (an upstream quirk), and then no row carries it. UP and DOWN are
// Equation +-1, a shortcut to a row. No sub-editor.

namespace viznut {

void header(FriendlyModel& m) {
	quadTabs(m, bytebeatgen.ui.selected_channel);
}

void rows(FriendlyModel& m, int tab) {
	addEnabled(m, bytebeatgen.bytebeats_[tab], tab == m.tab ? &bytebeatgen.ui.cursor : nullptr);
}

void selectTab(int tab) {
	encoder(OC::CONTROL_ENCODER_L, tab - bytebeatgen.ui.selected_channel);
}

bool edit(const FriendlyEdit& e, const Row& row) {
	return menuEdit(e, row, bytebeatgen.ui.cursor, enabledRowOf(bytebeatgen.selected(), row.id));
}

constexpr Adapter kAdapter = { &header, &rows, &selectTab, &edit, nullptr, nullptr, 0, nullptr, 0 };

} // namespace viznut

// ---- References (APP_REFS.ino) ----------------------------------------------
// Enabled list per channel: Octave, Semitone, Mod range, Mod rate, autotune,
// then on channel D (DAC_CHANNEL_FTM) the four tuning rows, and on A to C
// the placeholder REF_SETTING_DUMMY twice. A placeholder row ("-") has no
// value and its R push does nothing: it only pads A to C's menu, so it is
// left out of the list (the cursor on it marks no row). "--> autotune" opens
// the autotuner (R push), a sub-editor that takes both encoders and every
// button until its own R push closes it: a SubEditor row, and accepting()
// is false while it is open.
//
// REFS_restore sets the cursor's end from channel A's list, though D (two
// rows longer) is selected, so after a patch load D's last two rows are out
// of the cursor's reach until the L encoder moves. An edit there turns the L
// encoder away and back first, as a person must.

namespace references {

void header(FriendlyModel& m) {
	quadTabs(m, references_app.ui.selected_channel);
}

void rows(FriendlyModel& m, int tab) {
	const ReferenceChannel& channel = references_app.channels_[tab];
	const Cursor* cursor = tab == m.tab ? &references_app.ui.cursor : nullptr;
	const int n = channel.num_enabled_settings();
	for (int i = 0; i < n; i++) {
		const int setting = (int) channel.enabled_setting_at(i);
		if (setting == REF_SETTING_DUMMY)
			continue;
		Row& r = addSetting(m, channel, setting);
		if (setting == REF_SETTING_AUTOTUNE)
			r.kind = RowKind::SubEditor;
		if (cursor && cursor->cursor_pos() == i)
			markCursor(r, *cursor);
	}
}

void selectTab(int tab) {
	encoder(OC::CONTROL_ENCODER_L, tab - references_app.ui.selected_channel);
}

bool accepting() {
	return !references_app.autotuner.active();
}

bool edit(const FriendlyEdit& e, const Row& row) {
	Cursor& cursor = references_app.ui.cursor;
	const int menuRow = enabledRowOf(references_app.selected_channel(), row.id);
	if (menuRow >= 0 && (e.type == FriendlyEdit::SetValue || e.type == FriendlyEdit::Focus)) {
		moveCursor(cursor, menuRow);
		if (cursor.cursor_pos() != menuRow) {  // the menu's end is short of the row: away and back
			const int away = references_app.ui.selected_channel > 0 ? -1 : 1;
			encoder(OC::CONTROL_ENCODER_L, away);
			encoder(OC::CONTROL_ENCODER_L, -away);
		}
	}
	return menuEdit(e, row, cursor, menuRow);
}

// The table's "--> autotune" opens the DAC autotuner.
constexpr Label kLabels[] = { { REF_SETTING_AUTOTUNE, "Autotune" } };

constexpr Adapter kAdapter = { &header, &rows, &selectTab, &edit, &accepting, nullptr, 0, kLabels, 1 };

} // namespace references

// ---- Harrington 1200 (APP_H1200.ino) ----------------------------------------
// One settings object with an enabled list: settings 0..14, then with
// Trigger type Eucl the 22 Euclidean ones; always a prefix of the table, so
// the handler's use of the cursor position as a setting index is right. The
// R-encoder handler rebuilds the list after a Trigger type edit and calls
// mode_change(true) after a Root mode edit, so replaying the gesture does
// both. The L encoder (Inversion) and UP/DOWN (Octave +-1) are shortcuts to
// rows; L push switches the title bar between note names and numbers (a
// view toggle, followed by the status). Extra row: "Reset all settings" (L long press:
// every setting to its default and a chord reset; the firmware does not
// rebuild its menu then, so the list keeps whatever rows it had). No
// sub-editor.

namespace harrington {

enum { kReset = kExtraId };

void header(FriendlyModel& m) {
	// H1200_menu's title bar: root note and the Root mode setting, then the
	// other three chord notes as names or as semitone numbers.
	int outputs[4];
	h1200_state.tonnetz_state.get_outputs(outputs);
	Text status(m.status);
	status.str(note_name(outputs[0])).ch(' ').str(mode_names[h1200_settings.mode()]).ch(' ');
	for (int i = 1; i < 4; i++) {
		status.ch(' ');
		if (h1200_state.display_notes)
			status.str(note_name(outputs[i]));
		else
			prettyNum(status, outputs[i]);
	}
}

void rows(FriendlyModel& m, int) {
	addEnabled(m, h1200_settings, &h1200_state.cursor);
	// L long press: every setting to its default (InitDefaults) and a chord
	// reset, so the list asks first.
	addExtra(m, RowKind::Action, kReset, "Reset all settings").flags |= kRowConfirm;
}

bool edit(const FriendlyEdit& e, const Row& row) {
	if (e.type == FriendlyEdit::Press) {
		if (row.id != kReset)
			return false;
		button(OC::CONTROL_BUTTON_L, true);
		return true;
	}
	return menuEdit(e, row, h1200_state.cursor, enabledRowOf(h1200_settings, row.id));
}

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, nullptr, nullptr, 0, nullptr, 0 };

} // namespace harrington

// ---- Automatonnetz (APP_AUTOMATONNETZ.ino) ----------------------------------
// Two views, switched by L push: the grid view (the 7 grid settings, its own
// cursor, positions are setting indices) and the cell view (the 4 settings
// of the cell the L encoder selects). The list is the grid view's; in the
// cell view the encoders and R push address the cell, so accepting() is
// false there. Extra rows: Reset (UP), Clock (DOWN: one manual grid step),
// Clear grid (L long press: all 25 cells cleared by the Clr setting, then a
// reset) and Cells, the cell view (a SubEditor row: L encoder and L push
// are its controls).

namespace automaton {

enum { kReset = kExtraId, kClock, kClearGrid, kCells };

void header(FriendlyModel& m) {
	Text status(m.status);
	if (automatonnetz_state.ui.edit_cell) {
		// draw_cell_menu's title bar.
		const int cell = automatonnetz_state.ui.selected_cell;
		status.str("CELL ").num(cell / 5 + 1).ch(',').num(cell % 5 + 1);
		return;
	}
	// draw_grid_menu's title bar: the three chord notes and the chord's mode.
	int outputs[4];
	automatonnetz_state.tonnetz_state.get_outputs(outputs);
	for (int i = 1; i < 4; i++)
		status.str(note_name(outputs[i])).ch(' ');
	status.ch(MODE_MAJOR == automatonnetz_state.tonnetz_state.current_chord().mode() ? '+' : '-');
}

// dx and dy as draw_grid_menu prints them (whole cells and a fraction:
// 12 is "1 1/5"), without the leading padding.
void setStepText(Row& r) {
	const int integral = r.value / 8, fraction = r.value % 8;
	char text[6];
	memcpy(text, clock_fraction_names[fraction], sizeof(text));
	if (integral || !fraction)
		text[0] = (char) ('0' + integral);
	const char* p = text;
	while (*p == ' ')
		p++;
	Text(r.text).str(p);
}

void rows(FriendlyModel& m, int) {
	// The grid view's cursor; in the cell view no row of this list has it.
	const Cursor* cursor = automatonnetz_state.ui.edit_cell ? nullptr : &automatonnetz_state.ui.grid_cursor;
	for (int i = 0; i < GRID_SETTING_LAST; i++) {
		Row& r = addSetting(m, automatonnetz_state, i);
		if (i == GRID_SETTING_DX || i == GRID_SETTING_DY)
			setStepText(r);
		if (cursor && cursor->cursor_pos() == i)
			markCursor(r, *cursor);
	}
	addExtra(m, RowKind::Action, kReset, "Reset");          // UP
	addExtra(m, RowKind::Action, kClock, "Clock");          // DOWN
	addExtra(m, RowKind::Action, kClearGrid, "Clear grid").flags |= kRowConfirm;  // L long press: all 25 cells
	addExtra(m, RowKind::SubEditor, kCells, "Cells");       // L push, L encoder
}

bool accepting() {
	return !automatonnetz_state.ui.edit_cell;
}

bool edit(const FriendlyEdit& e, const Row& row) {
	if (e.type == FriendlyEdit::Press) {
		switch (row.id) {
			case kReset: button(OC::CONTROL_BUTTON_UP); return true;
			case kClock: button(OC::CONTROL_BUTTON_DOWN); return true;
			case kClearGrid: button(OC::CONTROL_BUTTON_L, true); return true;
			default: return false;
		}
	}
	return menuEdit(e, row, automatonnetz_state.ui.grid_cursor, row.id < kExtraId ? row.id : -1);
}

// The table's names for these two are "dx" and "dy": how far the grid's
// playhead moves on each clock, across and down.
constexpr Label kLabels[] = {
	{ GRID_SETTING_DX, "Step X" },
	{ GRID_SETTING_DY, "Step Y" },
};

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, &accepting, nullptr, 0, kLabels, 2 };

} // namespace automaton
