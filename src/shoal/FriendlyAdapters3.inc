// Friendly-mode adapters, file 3: the quantizer apps, CopierMaschine,
// Quantermain and Meta-Q. Included by Friendly.inc between its helpers and
// its table of adapters; see that file for the Adapter struct, the helpers
// used here and the rules an adapter follows.

// ---- Shared by the quantizer apps -------------------------------------------
// Each keeps a note mask per scale, edited by the scale editor
// (OC_scale_edit.h), which the R push on the mask row opens and which takes
// every encoder and button event until its own R push closes it: so the mask
// row is never given to replaySet or moveCursor's R push, and each adapter's
// accepting() is false while the editor is open. In the list the mask is a
// Mask row: one cell per note of the scale (OC::Scales::GetScale(scale)
// .num_notes; none for Off, 12 for Semitone, 4 to 16 for a user scale, which
// every app and channel share), and its value is the stored mask, the one
// the editor edits (the firmware's menu draws it rotated by CV). ToggleBit
// does what the editor's L push does to the cell under its cursor, through
// the function the editor writes the mask with (update_scale_mask).

// The number of cells of `scale`'s mask row.
inline int scaleCells(int scale) {
	const uint32_t n = OC::Scales::GetScale(scale).num_notes;
	return n > 16 ? 16 : (int) n;
}

// Make the listed row `setting` (if listed) the mask row of `scale`.
inline void makeMaskRow(FriendlyModel& m, int setting, int scale) {
	for (int i = 0; i < m.rows; i++)
		if (m.row[i].id == setting) {
			Row& r = m.row[i];
			r.kind = RowKind::Mask;
			r.min = 0;
			r.max = scaleCells(scale);
			r.text[0] = 0;
		}
}

// The scale editor's L push on note `bit` of a scale with `cells` notes
// (ScaleEditor::handleButtonLeft): the note toggles, except that the last
// active note is never cleared.
inline uint16_t toggledMask(uint16_t mask, int bit, int cells) {
	const uint16_t m = (uint16_t) (1u << bit);
	if (mask & m) {
		if ((mask & ~(0xffff << cells)) != m)
			mask &= (uint16_t) ~m;
	} else {
		mask |= m;
	}
	return mask;
}

// ToggleBit on the mask of `slot` (Meta-Q's scale slot; 0 elsewhere), as the
// editor writes it (ScaleEditor::apply_mask: only a change is written).
template <class Owner>
inline void toggleMaskBit(Owner& owner, int slot, int bit, int cells) {
	const uint16_t mask = owner.get_scale_mask((uint8_t) slot);
	const uint16_t toggled = toggledMask(mask, bit, cells);
	if (toggled != mask)
		owner.update_scale_mask(toggled, (uint8_t) slot);
}

// ---- CopierMaschine (APP_ASR.ino) -------------------------------------------
// One settings object with an enabled list (CV source shows its source's
// rows; the R-encoder handler rebuilds it). The scale is not a menu row: the
// L encoder previews a scale (the title bar shows its name) and an L push
// commits it (set_scale, which also keeps a note of the mask active). The
// "Scale" row shows the committed scale; a SetValue turns the L encoder from
// whatever the preview is to the target and pushes L. The extra rows come
// first, as on the reference: Scale, "Hold / Freeze" (DOWN; TR2 also sets
// and clears it) and "Delay type" (DOWN long press: the manual's "toggle
// delay type: multiplying / basic"; the firmware calls it the delay
// mechanics). UP toggles the octave up and down, a shortcut to the octave
// row. The mask row opens the scale editor (R push, or L long press after
// committing the previewed scale).

namespace copier {

enum { kHold = kExtraId, kDelayType };

// The manual's names for the two delay types, in the order of delay_type_.
constexpr const char* kDelayTypes[2] = { "multiplying", "basic" };

void header(FriendlyModel& m) {
	// ASR_menu's title bar: the name of the scale the L encoder shows, which
	// is a preview until an L push commits it.
	const int shown = asr_state.left_encoder_value;
	Text status(m.status);
	if (shown != asr.get_scale(DUMMY))
		status.str("Next: ");
	status.str(OC::scale_names[shown]);
}

void rows(FriendlyModel& m, int) {
	addSetting(m, asr, ASR_SETTING_SCALE);
	Row& hold = addExtra(m, RowKind::Toggle, kHold, "Hold / Freeze");
	hold.value = asr.freeze_state() ? 1 : 0;
	hold.max = 1;
	Row& delay = addExtra(m, RowKind::Choice, kDelayType, "Delay type");
	delay.value = asr.get_delay_type() ? 1 : 0;
	delay.max = 1;
	delay.names = kDelayTypes;
	delay.nameCount = 2;
	addEnabled(m, asr, &asr_state.cursor);
	makeMaskRow(m, ASR_SETTING_MASK, asr.get_scale(DUMMY));
}

bool accepting() {
	return !asr_state.scale_editor.active();
}

bool edit(const FriendlyEdit& e, const Row& row) {
	Cursor& cursor = asr_state.cursor;
	const bool set = e.type == FriendlyEdit::SetValue;
	switch (row.id) {
		case ASR_SETTING_SCALE:  // not in the menu: Focus leaves the cursor where it is
			if (set) {
				encoder(OC::CONTROL_ENCODER_L, e.value - asr_state.left_encoder_value);
				button(OC::CONTROL_BUTTON_L);
			}
			return set || e.type == FriendlyEdit::Focus;
		case kHold:
			if (set && e.value != row.value)
				button(OC::CONTROL_BUTTON_DOWN);
			return set || e.type == FriendlyEdit::Focus;
		case kDelayType:
			if (set && e.value != row.value)
				button(OC::CONTROL_BUTTON_DOWN, true);
			return set || e.type == FriendlyEdit::Focus;
		default:
			break;
	}
	const int menuRow = enabledRowOf(asr, row.id);
	if (e.type == FriendlyEdit::ToggleBit) {  // the mask row (the framework checked)
		if (menuRow < 0)
			return false;
		moveCursor(cursor, menuRow);
		toggleMaskBit(asr, 0, e.value, row.max);
		return true;
	}
	return menuEdit(e, row, cursor, menuRow);
}

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, &accepting, nullptr, 0, nullptr, 0 };

} // namespace copier

// ---- Shared by Quantermain and Meta-Q ---------------------------------------
// Both select a channel with the L encoder (clamped) and keep an enabled
// list per channel, rebuilt by the R-encoder handler after the edits that
// change it. Their guard against an internal CV source with a continuous
// trigger only looks at one encoder event: the trigger cannot step up from
// TR4 while the source is internal, nor the source up from CV4 while the
// trigger is continuous, but one event of several steps jumps past. A person
// turning one detent at a time cannot, so SetValue on those two rows sends
// unit steps and the guard stops it where it stops the person.
//
// The list can be rebuilt under an editing cursor. Neither app rebuilds a
// channel's list when the L encoder selects it (Quantermain) or when L push
// or "Copy scale" changes it (both), so a channel can show a stale list
// until an edit rebuilds it; that edit's first step can then put another
// setting, the mask row included, under the cursor. And edit mode survives
// a channel switch, so the panel can leave the cursor editing on a mask row.
// On a mask row an R push opens the scale editor instead of leaving edit
// mode, and the R encoder does nothing there (Meta-Q's nudges the mask as a
// number, an upstream slip in its guard). Both apps pass a Menu, so the
// cursor helpers stop the steps when the setting under the cursor changes,
// leave edit mode by suspending and resuming the app where an R push would
// open the editor, and finish the turn on the setting's new row
// (Friendly.inc).

// The edits common to both apps, on `channel` (the selected one): the mask
// row's ToggleBit (on `slot`), and SetValue (unit steps for `guarded`
// settings) and Focus on a menu row. First, a cursor the panel left editing
// on a mask row leaves edit mode (suspendAndResume).
template <class Channel>
inline bool quantizerEdit(const FriendlyEdit& e, const Row& row, Channel& channel, Cursor& cursor, int slot,
		const Menu& menu, bool (*guarded)(int)) {
	const int menuRow = enabledRowOf(channel, row.id);
	if (menuRow < 0)  // an extra row: Focus leaves the cursor where it is
		return e.type == FriendlyEdit::Focus;
	if (editingOnEditorRow(cursor, &menu))
		suspendAndResume();
	switch (e.type) {
		case FriendlyEdit::ToggleBit:  // the mask row (the framework checked)
			moveCursor(cursor, menuRow, &menu);
			toggleMaskBit(channel, slot, e.value, row.max);
			return true;
		case FriendlyEdit::SetValue:
			return replaySet(cursor, menuRow, e.value - row.value, guarded(row.id), &menu);
		case FriendlyEdit::Focus:
			moveCursor(cursor, menuRow, &menu);
			return true;
		default:
			return false;
	}
}

// ---- Quantermain (APP_QQ.ino) -----------------------------------------------
// Four channels A to D, the L encoder (or L push, which wraps) selects one.
// Scale Off hides Root and the mask row; Scale, CV Source and Trigger
// source change which rows are shown. Extra rows: "Copy scale to other
// channels" (L long press: the selected channel's scale and root to the
// other three, through set_scale; upstream does not rebuild their lists)
// and "Reset mask" (DOWN long press: every note of the selected channel's
// mask on). UP and DOWN are Octave +-1, a shortcut to the Octave row.

namespace quantermain {

enum { kCopyScale = kExtraId, kResetMask };

bool isMask(int setting) {
	return setting == CHANNEL_SETTING_MASK;
}

bool guarded(int setting) {
	return setting == CHANNEL_SETTING_SOURCE || setting == CHANNEL_SETTING_TRIGGER;
}

void header(FriendlyModel& m) {
	quadTabs(m, qq_state.selected_channel);
	// QQ_menu's title bar: each channel's letter, and its octave if not 0.
	Text status(m.status);
	for (int i = 0; i < 4; i++) {
		if (i)
			status.str("  ");
		status.ch((char) ('A' + i));
		const int octave = quantizer_channels[i].get_octave();
		if (octave)
			prettyNum(status.ch(' '), octave);
	}
}

void rows(FriendlyModel& m, int tab) {
	const QuantizerChannel& channel = quantizer_channels[tab];
	addEnabled(m, channel, tab == m.tab ? &qq_state.cursor : nullptr);
	makeMaskRow(m, CHANNEL_SETTING_MASK, channel.get_scale(DUMMY));
	addExtra(m, RowKind::Action, kCopyScale, "Copy scale to other channels");  // L long press
	addExtra(m, RowKind::Action, kResetMask, "Reset mask");                    // DOWN long press
}

void selectTab(int tab) {
	encoder(OC::CONTROL_ENCODER_L, tab - qq_state.selected_channel);
}

int settingAt(int menuRow) {
	return (int) quantizer_channels[qq_state.selected_channel].enabled_setting_at(menuRow);
}

int rowOf(int setting) {
	return enabledRowOf(quantizer_channels[qq_state.selected_channel], setting);
}

constexpr Menu kMenu = { &settingAt, &isMask, &rowOf };

bool accepting() {
	return !qq_state.scale_editor.active();
}

bool edit(const FriendlyEdit& e, const Row& row) {
	if (e.type == FriendlyEdit::Press) {
		switch (row.id) {
			case kCopyScale: button(OC::CONTROL_BUTTON_L, true); return true;
			case kResetMask: button(OC::CONTROL_BUTTON_DOWN, true); return true;
			default: return false;
		}
	}
	return quantizerEdit(e, row, quantizer_channels[qq_state.selected_channel], qq_state.cursor, 0, kMenu, &guarded);
}

constexpr Adapter kAdapter = { &header, &rows, &selectTab, &edit, &accepting, nullptr, 0, nullptr, 0 };

} // namespace quantermain

// ---- Meta-Q (APP_DQ.ino) ----------------------------------------------------
// Two channels A and B, the L encoder (or L push) selects one. Each has four
// scale slots (scale, root, mask, transpose each); "scale #" selects the one
// the menu shows, so the list shows that slot's rows, and "scale #" and
// "seq_mode" reset the scale sequence (reset_scale) in the R-encoder
// handler. Scale Off hides the slot's other rows and "scale #" too. The
// mask row is the shown slot's; the scale editor edits that slot's mask
// (it can switch slots itself, inside). CV source runs to 4 (Turing), past
// the firmware's four names: its names are Quantermain's source names, whose
// first five are these five. Extra rows: "Copy scale to all slots" (L long
// press: the shown slot's scale, mask, root and transpose to all four slots
// of both channels) and "Reset mask" (DOWN long press: the shown slot's mask,
// every note on). UP and DOWN are octave +-1, a shortcut to a row.

namespace metaq {

enum { kCopyScale = kExtraId, kResetMask };

constexpr const char* kDualTabs[2] = { "A", "B" };

bool isMask(int setting) {
	return setting >= DQ_CHANNEL_SETTING_MASK1 && setting <= DQ_CHANNEL_SETTING_MASK4;
}

bool guarded(int setting) {
	return setting == DQ_CHANNEL_SETTING_SOURCE || setting == DQ_CHANNEL_SETTING_TRIGGER;
}

void header(FriendlyModel& m) {
	m.tabs = 2;
	m.tab = (uint8_t) (dq_state.selected_channel == 1 ? 1 : 0);
	m.tabLabel[0] = kDualTabs[0];
	m.tabLabel[1] = kDualTabs[1];
	// DQ_menu's title bar: per channel the letter, the playing slot, its root
	// (the CV-modulated one when CV aux. is root) and the octave if not 0.
	Text status(m.status);
	for (int i = 0; i < NUMCHANNELS; i++) {
		const DQ_QuantizerChannel& channel = dq_quantizer_channels[i];
		const int slot = channel.get_display_scale();
		const int root = channel.get_aux_cv_dest() == DQ_DEST_ROOT ? channel.get_display_root() : channel.get_root((uint8_t) slot);
		if (i)
			status.str("  ");
		status.ch((char) ('A' + i)).str(" #").num(slot + 1).ch(' ').str(OC::Strings::note_names_unpadded[((root % 12) + 12) % 12]);
		const int octave = channel.get_octave();
		if (octave)
			prettyNum(status.ch(' '), octave);
	}
}

void rows(FriendlyModel& m, int tab) {
	const DQ_QuantizerChannel& channel = dq_quantizer_channels[tab];
	addEnabled(m, channel, tab == m.tab ? &dq_state.cursor : nullptr);
	const int slot = channel.get_scale_select();
	makeMaskRow(m, DQ_CHANNEL_SETTING_MASK1 + slot, channel.get_scale((uint8_t) slot));
	addExtra(m, RowKind::Action, kCopyScale, "Copy scale to all slots");  // L long press
	addExtra(m, RowKind::Action, kResetMask, "Reset mask");               // DOWN long press
}

void selectTab(int tab) {
	encoder(OC::CONTROL_ENCODER_L, tab - dq_state.selected_channel);
}

int settingAt(int menuRow) {
	return (int) dq_quantizer_channels[dq_state.selected_channel].enabled_setting_at(menuRow);
}

int rowOf(int setting) {
	return enabledRowOf(dq_quantizer_channels[dq_state.selected_channel], setting);
}

constexpr Menu kMenu = { &settingAt, &isMask, &rowOf };

bool accepting() {
	return !dq_state.scale_editor.active();
}

bool edit(const FriendlyEdit& e, const Row& row) {
	if (e.type == FriendlyEdit::Press) {
		switch (row.id) {
			case kCopyScale: button(OC::CONTROL_BUTTON_L, true); return true;
			case kResetMask: button(OC::CONTROL_BUTTON_DOWN, true); return true;
			default: return false;
		}
	}
	DQ_QuantizerChannel& channel = dq_quantizer_channels[dq_state.selected_channel];
	return quantizerEdit(e, row, channel, dq_state.cursor, channel.get_scale_select(), kMenu, &guarded);
}

// CV source: 0..4 (CV1 to CV4, Turing) over cv_input_names, which has four
// names; channel_input_sources (APP_QQ.ino) starts with the same four and
// "Turing".
constexpr NameCount kNames[] = {
	{ DQ_CHANNEL_SETTING_SOURCE, 5, channel_input_sources },
};

constexpr Adapter kAdapter = { &header, &rows, &selectTab, &edit, &accepting, kNames, 1, nullptr, 0 };

} // namespace metaq
