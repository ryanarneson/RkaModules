// Friendly-mode adapters, file 4: Piqued, Sequins and Acid Curds, the apps
// with two menu pages and the most guards. Included by Friendly.inc between
// its helpers and its table of adapters; see that file for the Adapter
// struct, the helpers used here and the rules an adapter follows, and
// FriendlyAdapters3.inc for the mask helpers (scaleCells, makeMaskRow,
// toggleMaskBit) and metaq::kDualTabs.

// ---- Piqued (APP_ENVGEN.ino) ------------------------------------------------
// Four channels A to D; the L encoder selects one, except while the
// Euclidean editor is open (below). Two pages: the envelope page (a new
// core's) edits the segment values S1-S4, and the settings page has an
// enabled list per channel (Tr delay mode and Eucl length show their rows;
// the R-encoder handler rebuilds it). L push switches page, so an edit on a
// settings row from the envelope page starts with one.
//
// The envelope page: the R encoder selects a segment (as many as the type
// has, num_editable_segments), R push starts and ends editing it, during
// which the R encoder edits its value: the list is busy then. Its segments
// are the first rows, Numbers named as the page names them (segment_names),
// the selected one under the cursor while the page shows. A SetValue is the
// page's gesture: L push to the page (from the settings page), the R encoder
// to the segment, R push, the R encoder, R push, and L push back to the
// settings page, where every other edit leaves the app. A Focus shows the
// page with the segment selected. Nothing else is on the page but the
// envelope's picture, so it has no hardware-only row.
//
// The Euclidean editor is edit mode on the Offset row: the R encoder edits
// the offset (its guard keeps it <= length, one event at a time), the L
// encoder the fill (guard: <= length + 1, one event at a time) or, after an
// L push, the length (1 and up, pulling fill and offset down with it); R
// push closes it. Its R push is a plain edit toggle (it also ends the length
// mode), but while it is open the L encoder and L push do not select the
// channel or the page, so those edits close it first. The fill is not in
// the firmware's menu; its row comes after Eucl length, where upstream
// commented it out, and is edited with the editor's L encoder. A length from
// one length to another goes the same way, so that fill and offset follow
// it down as the firmware's editor makes them; to or from Off (which the
// editor cannot reach) it is the menu row, which leaves them as they are.
// UP and DOWN change the selected segment by +-32 on either page: a
// shortcut to the envelope page, not a row.

namespace piqued {

bool isSegment(int setting) {
	return setting >= ENV_SETTING_SEG1_VALUE && setting <= ENV_SETTING_SEG4_VALUE;
}

// Trigger input 0..6: the table's TR1..TR4 (the first four entries mirror
// the firmware's OC::Strings::trigger_input_names in OC_strings.cpp, which
// has only those four), then the other three channels' end of cycle in letter order
// (trigger_setting_to_channel_index skips the channel itself), which the
// menu draws as "B EOC" and so on.
constexpr const char* kTriggerNames[4][7] = {
	{ "TR1", "TR2", "TR3", "TR4", "B end of cycle", "C end of cycle", "D end of cycle" },
	{ "TR1", "TR2", "TR3", "TR4", "A end of cycle", "C end of cycle", "D end of cycle" },
	{ "TR1", "TR2", "TR3", "TR4", "A end of cycle", "B end of cycle", "D end of cycle" },
	{ "TR1", "TR2", "TR3", "TR4", "A end of cycle", "B end of cycle", "C end of cycle" },
};

bool onEnvelopePage() {
	return envgen.ui.edit_mode == QuadEnvelopeGenerator::MODE_EDIT_SEGMENTS;
}

void header(FriendlyModel& m) {
	quadTabs(m, envgen.ui.selected_channel);
}

void rows(FriendlyModel& m, int tab) {
	const EnvelopeGenerator& env = envgen.envelopes_[tab];
	const bool shown = tab == m.tab;
	for (int s = 0; s < env.num_editable_segments(); s++) {
		Row& r = addSetting(m, env, ENV_SETTING_SEG1_VALUE + s);
		r.label = segment_names[s];  // the table's are "S1" to "S4"
		if (shown && onEnvelopePage() && envgen.ui.selected_segment == s)
			r.flags |= envgen.ui.segment_editing ? kRowCursor | kRowEditing : kRowCursor;
	}
	const Cursor* cursor = shown && !onEnvelopePage() ? &envgen.ui.cursor : nullptr;
	const int n = env.num_enabled_settings();
	for (int i = 0; i < n; i++) {
		const int setting = env.enabled_setting_at(i);
		Row& r = addSetting(m, env, setting);
		if (cursor && cursor->cursor_pos() == i)
			markCursor(r, *cursor);
		if (setting == ENV_SETTING_TRIGGER_INPUT)
			r.names = kTriggerNames[tab];
		if (setting == ENV_SETTING_EUCLIDEAN_LENGTH && env.get_euclidean_length())
			addSetting(m, env, ENV_SETTING_EUCLIDEAN_FILL);
	}
}

// Close the Euclidean editor, if open, with its R push.
void closeEuclidean() {
	if (envgen.euclidean_edit_active())
		button(OC::CONTROL_BUTTON_R);
}

void selectTab(int tab) {
	closeEuclidean();
	encoder(OC::CONTROL_ENCODER_L, tab - envgen.ui.selected_channel);
}

bool accepting() {
	return !(onEnvelopePage() && envgen.ui.segment_editing);
}

// The Euclidean editor's L encoder: the cursor to the Offset row, R push to
// open the editor (unless open), L push if it edits the other of length and
// fill, the turn (a step at a time for the fill, whose guard looks at one
// event), R push to close it.
void euclideanTurn(Cursor& c, int offsetRow, bool length, int delta) {
	moveCursor(c, offsetRow);
	if (!c.editing())
		button(OC::CONTROL_BUTTON_R);
	if (envgen.ui.euclidean_edit_length != length)
		button(OC::CONTROL_BUTTON_L);
	if (length)
		encoder(OC::CONTROL_ENCODER_L, delta);
	else
		for (; delta; delta += delta > 0 ? -1 : 1)
			encoder(OC::CONTROL_ENCODER_L, delta > 0 ? 1 : -1);
	button(OC::CONTROL_BUTTON_R);
}

bool edit(const FriendlyEdit& e, const Row& row) {
	Cursor& c = envgen.ui.cursor;
	if (e.type != FriendlyEdit::SetValue && e.type != FriendlyEdit::Focus)
		return false;
	if (isSegment(row.id)) {  // the envelope page's gesture
		closeEuclidean();
		if (!onEnvelopePage())
			button(OC::CONTROL_BUTTON_L);
		encoder(OC::CONTROL_ENCODER_R, row.id - ENV_SETTING_SEG1_VALUE - envgen.ui.selected_segment);  // not editing: selects
		if (e.type == FriendlyEdit::SetValue) {
			button(OC::CONTROL_BUTTON_R);  // segment editing on
			encoder(OC::CONTROL_ENCODER_R, e.value - row.value);
			button(OC::CONTROL_BUTTON_R);  // and off
			button(OC::CONTROL_BUTTON_L);  // back to the settings page
		}
		return true;
	}
	if (onEnvelopePage())
		button(OC::CONTROL_BUTTON_L);
	const EnvelopeGenerator& env = envgen.selected();
	const int offsetRow = enabledRowOf(env, ENV_SETTING_EUCLIDEAN_OFFSET);
	const bool set = e.type == FriendlyEdit::SetValue;
	if (row.id == ENV_SETTING_EUCLIDEAN_FILL) {  // listed only while the Offset row is
		if (offsetRow < 0)
			return false;
		if (set)
			euclideanTurn(c, offsetRow, false, e.value - row.value);
		else
			moveCursor(c, offsetRow);
		return true;
	}
	if (row.id == ENV_SETTING_EUCLIDEAN_LENGTH && set && e.value > 0 && row.value > 0 && offsetRow >= 0) {
		euclideanTurn(c, offsetRow, true, e.value - row.value);
		moveCursor(c, enabledRowOf(env, ENV_SETTING_EUCLIDEAN_LENGTH));
		return true;
	}
	const int menuRow = enabledRowOf(env, row.id);
	if (set && row.id == ENV_SETTING_EUCLIDEAN_OFFSET)  // its guard looks at one event: a step at a time
		return menuRow >= 0 && replaySet(c, menuRow, e.value - row.value, true);
	return menuEdit(e, row, c, menuRow);
}

constexpr NameCount kNames[] = {
	{ ENV_SETTING_TRIGGER_INPUT, 7, kTriggerNames[0] },  // rows() gives each channel its own
};

constexpr Adapter kAdapter = { &header, &rows, &selectTab, &edit, &accepting, kNames, 1, nullptr, 0 };

} // namespace piqued

// ---- Shared by Sequins and Acid Curds: two menu pages -----------------------
// Each keeps one enabled list, built for the page it shows (menu_page_, per
// channel in Sequins). DOWN switches page; UP on the CV mapping page goes
// back to the parameters page (on the parameters page UP is the octave
// toggle). A page switch rebuilds the list and ends edit mode, and leaves
// the cursor's row and range as they were. Neither app swaps a second
// cursor in with the page (Sequins declares cursor_state and never uses it).
//
// The list shows both pages, each under a heading, the parameters first; a
// setting both pages list has its row in the first. The page shown comes
// from the app's own list; the other page from the list the app's own
// builder makes on a copy of the settings object, so that building a model
// changes nothing in the app: not its list (entries past the end included,
// which a cursor past the end shows), not the page, not the cursor.
//
// An edit on a row of the other page shows that page first, as DOWN or UP,
// then works on its row; the app stays on that page afterwards, where a
// person making the edit would be and where the list's cursor row is.

constexpr int kPageListMax = 64;
// A page lists at most every setting once: a list longer than kPageListMax
// would be clamped silently by readList.
static_assert(SEQ_CHANNEL_SETTING_LAST <= kPageListMax, "a Sequins page list fits a PageList");
static_assert(CHORDS_SETTING_LAST <= kPageListMax, "an Acid Curds page list fits a PageList");
struct PageList {
	int n;
	int16_t setting[kPageListMax];
};

constexpr const char* kPageNames[2] = { "Parameters", "CV mapping" };

// obj's enabled list as it is.
template <class T>
inline void readList(const T& obj, PageList& out) {
	const int n = obj.num_enabled_settings();
	out.n = n < kPageListMax ? n : kPageListMax;
	for (int i = 0; i < out.n; i++)
		out.setting[i] = (int16_t) obj.enabled_setting_at(i);
}

// The list `build` makes for page `page` of a copy of obj: the app's own
// builder (update_enabled_settings), which reads only the settings and the
// page and writes only the list. The copy is a byte copy in local storage
// (the settings objects hold a braids::Quantizer, which cannot be copied
// as an object), never destroyed; obj is not written. This relies on the two
// builders it is used with reading only setting values and the menu page:
// SEQ_Channel::update_enabled_settings (APP_A_SEQ.ino) and
// Chords::update_enabled_settings (APP_CHORDS.ino). A firmware change to
// either (reading other members, or anything through a pointer the copy
// shares) needs this re-checked.
template <class T, class Build>
inline void otherPageList(const T& obj, int page, PageList& out, Build build) {
	alignas(T) unsigned char bytes[sizeof(T)];
	std::memcpy(bytes, static_cast<const void*>(&obj), sizeof(T));
	T& copy = *reinterpret_cast<T*>(bytes);
	copy.set_menu_page((uint8_t) page);
	build(copy);
	readList(copy, out);
}

// The first row of `list` that stands for row `id` (idOf: the row a list
// entry stands for); -1 if none.
template <class IdOf>
inline int rowIn(const PageList& list, int id, IdOf idOf) {
	for (int i = 0; i < list.n; i++)
		if (idOf(list.setting[i]) == id)
			return i;
	return -1;
}

// Whether the model has a row `id` already.
inline bool hasRow(const FriendlyModel& m, int id) {
	for (int i = 0; i < m.rows; i++)
		if (m.row[i].id == id)
			return true;
	return false;
}

// Mark the row `id`, if any, as under cursor c.
inline void markRow(FriendlyModel& m, int id, const Cursor& c) {
	for (int i = 0; i < m.rows; i++)
		if (m.row[i].id == id)
			markCursor(m.row[i], c);
}

// The menu row of row `id` with its page shown: the page shown if it lists
// the row, else the other page, switched to as the app's controls do (DOWN
// from the parameters page, UP back). -1 if neither page lists it (the page
// is then left alone). shownPage() and list(page, out) read the selected
// channel.
template <class IdOf>
inline int showRow(int id, int (*shownPage)(), void (*list)(int page, PageList& out), IdOf idOf) {
	PageList l;
	list(shownPage(), l);
	const int row = rowIn(l, id, idOf);
	if (row >= 0)
		return row;
	const int other = 1 - shownPage();
	list(other, l);
	if (rowIn(l, id, idOf) < 0)
		return -1;
	button(other ? OC::CONTROL_BUTTON_DOWN : OC::CONTROL_BUTTON_UP);
	list(shownPage(), l);
	return rowIn(l, id, idOf);
}

// ---- Sequins (APP_A_SEQ.ino) ------------------------------------------------
// Two channels A and B; the L encoder selects one (and puts the cursor at
// the top, not editing). Each channel has its own page. Rows whose R push
// does not toggle edit mode: the scale mask (opens the scale editor), the
// step mask (opens the pattern editor on the selected sequence) and the
// placeholder rows "-" (back to the parameters page), which the list leaves
// out. The Scale row's R push also re-reads the scale (update_scale(true)),
// on entering and on leaving edit mode.
//
// The step mask row is the selected sequence's ("sequence #"): one cell per
// step of its length (2 to 16), and a ToggleBit does what the pattern
// editor's L push does (an empty mask is allowed), through the functions it
// writes with: update_pattern_mask, then pattern_changed, forced when that
// sequence is the one playing, which the arpeggiator needs. The firmware
// does not rebuild its list when "sequence #" changes, so its row can stand
// for the old sequence's mask; its R push opens the editor on the selected
// sequence all the same, which is the row the list shows. The scale mask is
// a Mask row as in the quantizer apps. Sequence lengths and step pitches
// are the pattern editor's: hardware mode only.
//
// Extra rows: "Sync" (L push: both channels restart on their next clock),
// "Copy scale to other channel" (L long press), "Wait for end of sequence"
// (DOWN long press on the parameters page, both channels: a new sequence
// waits for the end of the playing one; stored in the DUMMY setting) and
// "Clear CV mapping" (DOWN long press on the CV mapping page). UP on the
// parameters page is an octave +1/-1 toggle (the title bar's "+"), a
// shortcut to the octave row.
//
// Upstream as it is: the R-encoder handler's mask guard joins its tests
// with || and is always true (no mask row is ever in edit mode, so it never
// matters), and force_update() sets a flag nothing reads.

namespace sequins {

enum { kSync = kExtraId, kCopyScale, kWaitEoS, kClearCv };

bool isStepMask(int setting) {
	return setting >= SEQ_CHANNEL_SETTING_MASK1 && setting <= SEQ_CHANNEL_SETTING_MASK4;
}

// The row a list entry of channel ch stands for: a step mask, whichever
// sequence's, is the selected sequence's row.
int idOf(const SEQ_Channel& ch, int setting) {
	return isStepMask(setting) ? SEQ_CHANNEL_SETTING_MASK1 + ch.get_sequence() : setting;
}

// Page `page`'s list of channel ch.
void pageList(const SEQ_Channel& ch, int page, PageList& out) {
	if (page == ch.get_menu_page()) {
		readList(ch, out);
		return;
	}
	otherPageList(ch, page, out, [](SEQ_Channel& copy) { copy.update_enabled_settings(0); });  // its argument is unused
}

SEQ_Channel& selected() {
	return seq_channel[seq_state.selected_channel];
}

int shownPage() {
	return selected().get_menu_page();
}

void selectedList(int page, PageList& out) {
	pageList(selected(), page, out);
}

void header(FriendlyModel& m) {
	m.tabs = 2;
	m.tab = (uint8_t) (seq_state.selected_channel == 1 ? 1 : 0);
	m.tabLabel[0] = metaq::kDualTabs[0];
	m.tabLabel[1] = metaq::kDualTabs[1];
	// SEQ_menu's title bar: per channel "#A/n" (n the sequence playing) and
	// "+" while the octave toggle is up.
	Text status(m.status);
	for (int i = 0; i < 2; i++) {
		const SEQ_Channel& ch = seq_channel[i];
		if (i)
			status.str("  ");
		status.ch('#').ch((char) ('A' + i)).ch('/').num(1 + ch.get_display_num_sequence());
		if (ch.poke_octave_toggle())
			status.ch('+');
	}
}

// The row of setting `id` of channel ch.
void addRowOf(FriendlyModel& m, const SEQ_Channel& ch, int id) {
	Row& r = addSetting(m, ch, id);
	if (id == SEQ_CHANNEL_SETTING_SCALE) {
		r.names = OC::scale_names + r.min;  // as the menu row draws it; the table has the short names
	} else if (id == SEQ_CHANNEL_SETTING_SCALE_MASK) {
		makeMaskRow(m, id, ch.get_scale(DUMMY));
	} else if (isStepMask(id)) {
		r.kind = RowKind::Mask;
		r.min = 0;
		r.max = ch.get_sequence_length((uint8_t) (id - SEQ_CHANNEL_SETTING_MASK1));
		r.text[0] = 0;
	} else if (id == SEQ_CHANNEL_SETTING_PULSEWIDTH && (r.value == 0 || r.value == PULSEW_MAX)) {
		Text(r.text).str(r.value ? "50%" : "echo");  // as Draw_PW_Value
	}
}

void rows(FriendlyModel& m, int tab) {
	const SEQ_Channel& ch = seq_channel[tab];
	for (int page = 0; page < 2; page++) {
		PageList list;
		pageList(ch, page, list);
		addHeading(m, kHeadingId + page, kPageNames[page]);
		for (int i = 0; i < list.n; i++) {
			const int id = idOf(ch, list.setting[i]);
			if (id != SEQ_CHANNEL_SETTING_DUMMY && !hasRow(m, id))
				addRowOf(m, ch, id);
		}
		if (page == PARAMETERS) {
			addExtra(m, RowKind::Action, kSync, "Sync");                                // L push
			addExtra(m, RowKind::Action, kCopyScale, "Copy scale to other channel");    // L long press
			Row& eos = addExtra(m, RowKind::Toggle, kWaitEoS, "Wait for end of sequence");  // DOWN long press
			eos.value = ch.get_value(SEQ_CHANNEL_SETTING_DUMMY);
			eos.max = 1;
		} else {
			addExtra(m, RowKind::Action, kClearCv, "Clear CV mapping").flags |= kRowConfirm;  // DOWN long press
		}
	}
	const Cursor& c = seq_state.cursor;
	if (tab == m.tab && c.cursor_pos() < ch.num_enabled_settings()) {
		const int under = idOf(ch, ch.enabled_setting_at(c.cursor_pos()));
		if (under != SEQ_CHANNEL_SETTING_DUMMY)
			markRow(m, under, c);
	}
}

void selectTab(int tab) {
	encoder(OC::CONTROL_ENCODER_L, tab - seq_state.selected_channel);
}

bool accepting() {
	return !seq_state.pattern_editor.active() && !seq_state.scale_editor.active();
}

int settingAt(int menuRow) {
	return (int) selected().enabled_setting_at(menuRow);
}

// R push on these opens an editor (the masks) or switches page ("-").
bool notToggle(int setting) {
	return setting == SEQ_CHANNEL_SETTING_SCALE_MASK || isStepMask(setting) || setting == SEQ_CHANNEL_SETTING_DUMMY;
}

int rowOf(int setting) {
	return enabledRowOf(selected(), setting);
}

constexpr Menu kMenu = { &settingAt, &notToggle, &rowOf };

// The pattern editor's L push on step `bit` of the selected sequence
// (PatternEditor::handleButtonLeft and apply_mask).
void toggleStep(SEQ_Channel& ch, int bit) {
	const uint8_t seq = (uint8_t) ch.get_sequence();
	const uint16_t mask = (uint16_t) (ch.get_mask(seq) ^ (1u << bit));
	ch.update_pattern_mask(mask, seq);
	ch.pattern_changed(mask, ch.get_current_sequence() == seq);
}

bool edit(const FriendlyEdit& e, const Row& row) {
	Cursor& c = seq_state.cursor;
	const bool focus = e.type == FriendlyEdit::Focus;
	switch (row.id) {
		case kSync:
			if (e.type == FriendlyEdit::Press)
				button(OC::CONTROL_BUTTON_L);
			return e.type == FriendlyEdit::Press || focus;
		case kCopyScale:
			if (e.type == FriendlyEdit::Press)
				button(OC::CONTROL_BUTTON_L, true);
			return e.type == FriendlyEdit::Press || focus;
		case kWaitEoS:
			if (e.type == FriendlyEdit::SetValue && e.value != row.value) {
				if (shownPage() != PARAMETERS)
					button(OC::CONTROL_BUTTON_UP);
				button(OC::CONTROL_BUTTON_DOWN, true);
			}
			return e.type == FriendlyEdit::SetValue || focus;
		case kClearCv:
			if (e.type == FriendlyEdit::Press) {
				if (shownPage() != CV_MAPPING)
					button(OC::CONTROL_BUTTON_DOWN);
				button(OC::CONTROL_BUTTON_DOWN, true);
			}
			return e.type == FriendlyEdit::Press || focus;
		default:
			break;
	}
	// A cursor left editing on an editor row leaves edit mode first, before
	// showRow reads the menu row (as quantizerEdit does).
	if (editingOnEditorRow(c, &kMenu))
		suspendAndResume();
	const int menuRow = showRow(row.id, &shownPage, &selectedList, [](int s) { return idOf(selected(), s); });
	if (menuRow < 0)
		return false;
	switch (e.type) {
		case FriendlyEdit::ToggleBit:  // a mask row (the framework checked)
			moveCursor(c, menuRow, &kMenu);
			if (row.id == SEQ_CHANNEL_SETTING_SCALE_MASK)
				toggleMaskBit(selected(), 0, e.value, row.max);
			else
				toggleStep(selected(), e.value);
			return true;
		case FriendlyEdit::SetValue:
			return replaySet(c, menuRow, e.value - row.value, false, &kMenu);
		case FriendlyEdit::Focus:
			moveCursor(c, menuRow, &kMenu);
			return true;
		default:
			return false;
	}
}

// The table's two "--> edit": the scale mask (the scale editor) and the
// step mask of each sequence (the pattern editor).
constexpr Label kLabels[] = {
	{ SEQ_CHANNEL_SETTING_SCALE_MASK, "Active notes" },
	{ SEQ_CHANNEL_SETTING_MASK1, "Steps" },
	{ SEQ_CHANNEL_SETTING_MASK2, "Steps" },
	{ SEQ_CHANNEL_SETTING_MASK3, "Steps" },
	{ SEQ_CHANNEL_SETTING_MASK4, "Steps" },
};

constexpr Adapter kAdapter = { &header, &rows, &selectTab, &edit, &accepting, nullptr, 0, kLabels, 5 };

} // namespace sequins

// ---- Acid Curds (APP_CHORDS.ino) --------------------------------------------
// One settings object, two pages. Rows whose R push does not toggle edit
// mode: the mask ("scale  -->", opens the scale editor), "chords -->" (opens
// the chord editor; on both pages) and the placeholders "-" and " " (back
// to the parameters page), which the list leaves out (" " stands in for
// root only with the scale Off, which Acid Curds' range rules out). The
// scale is not a menu row: as in CopierMaschine, the L encoder previews a
// scale (the title bar shows it) and L push commits it (set_scale, which
// also keeps a note of the mask active); the "Scale" row, first under the
// parameters heading, turns the L encoder from the preview to the target
// and pushes L. The mask is a Mask row as in the quantizer apps. Chord
// contents, the progressions' lengths (num.chords) and the editor's
// starting chord (CHORD_SLOT, which no control sets) are the chord editor's
// or nobody's: not listed; "chords -->" is a SubEditor row. Extra row:
// "Clear CV mapping" (DOWN long press, either page). UP on the parameters
// page is an octave +1/-1 toggle (the title bar's "+"), a shortcut to the
// octave row.

namespace acidcurds {

enum { kClearCv = kExtraId };

bool placeholder(int setting) {
	return setting == CHORDS_SETTING_DUMMY || setting == CHORDS_SETTING_MORE_DUMMY;
}

int idOf(int setting) {
	return setting;
}

void pageList(int page, PageList& out) {
	if (page == chords.get_menu_page()) {
		readList(chords, out);
		return;
	}
	otherPageList(chords, page, out, [](Chords& copy) { copy.update_enabled_settings(); });
}

int shownPage() {
	return chords.get_menu_page();
}

void header(FriendlyModel& m) {
	// CHORDS_menu's title bar: "#n" ("+n" while the octave toggle is up), n
	// the progression playing, and the name of the scale the L encoder
	// shows, a preview until L push commits it.
	const int shown = chords_state.left_encoder_value;
	Text status(m.status);
	status.ch(chords.poke_octave_toggle() ? '+' : '#').num(chords.get_active_progression() + 1).ch(' ');
	if (shown != chords.get_scale(DUMMY))
		status.str("Next: ");
	status.str(OC::scale_names[shown]);
}

void addRowOf(FriendlyModel& m, int id) {
	Row& r = addSetting(m, chords, id);
	if (id == CHORDS_SETTING_MASK)
		makeMaskRow(m, id, chords.get_scale(DUMMY));
	else if (id == CHORDS_SETTING_CHORD_EDIT)
		r.kind = RowKind::SubEditor;
}

void rows(FriendlyModel& m, int) {
	for (int page = 0; page < 2; page++) {
		PageList list;
		pageList(page, list);
		addHeading(m, kHeadingId + page, kPageNames[page]);
		if (page == MENU_PARAMETERS)
			addSetting(m, chords, CHORDS_SETTING_SCALE);  // the L encoder and L push
		for (int i = 0; i < list.n; i++)
			if (!placeholder(list.setting[i]) && !hasRow(m, list.setting[i]))
				addRowOf(m, list.setting[i]);
		if (page == MENU_CV_MAPPING)
			addExtra(m, RowKind::Action, kClearCv, "Clear CV mapping").flags |= kRowConfirm;  // DOWN long press
	}
	const Cursor& c = chords_state.cursor;
	if (c.cursor_pos() < chords.num_enabled_settings() && !placeholder(chords.enabled_setting_at(c.cursor_pos())))
		markRow(m, chords.enabled_setting_at(c.cursor_pos()), c);
}

bool accepting() {
	return !chords_state.scale_editor.active() && !chords_state.chord_editor.active();
}

int settingAt(int menuRow) {
	return (int) chords.enabled_setting_at(menuRow);
}

// R push on these opens an editor or switches page.
bool notToggle(int setting) {
	return setting == CHORDS_SETTING_MASK || setting == CHORDS_SETTING_CHORD_EDIT || placeholder(setting);
}

int rowOf(int setting) {
	return enabledRowOf(chords, setting);
}

constexpr Menu kMenu = { &settingAt, &notToggle, &rowOf };

bool edit(const FriendlyEdit& e, const Row& row) {
	Cursor& c = chords_state.cursor;
	if (row.id == CHORDS_SETTING_SCALE) {  // not in the menu: Focus leaves the cursor where it is
		if (e.type == FriendlyEdit::SetValue) {
			encoder(OC::CONTROL_ENCODER_L, e.value - chords_state.left_encoder_value);
			button(OC::CONTROL_BUTTON_L);
		}
		return e.type == FriendlyEdit::SetValue || e.type == FriendlyEdit::Focus;
	}
	if (row.id == kClearCv) {
		if (e.type == FriendlyEdit::Press)
			button(OC::CONTROL_BUTTON_DOWN, true);
		return e.type == FriendlyEdit::Press || e.type == FriendlyEdit::Focus;
	}
	if (editingOnEditorRow(c, &kMenu))  // before showRow reads the menu row
		suspendAndResume();
	const int menuRow = showRow(row.id, &shownPage, &pageList, &idOf);
	if (menuRow < 0)
		return false;
	switch (e.type) {
		case FriendlyEdit::ToggleBit:  // the mask row (the framework checked)
			moveCursor(c, menuRow, &kMenu);
			toggleMaskBit(chords, 0, e.value, row.max);
			return true;
		case FriendlyEdit::SetValue:
			return replaySet(c, menuRow, e.value - row.value, false, &kMenu);
		case FriendlyEdit::Focus:
			moveCursor(c, menuRow, &kMenu);
			return true;
		default:
			return false;
	}
}

// CHORD_SLOT 0..31 over chords_slots' 8 names ("#1".."#8"): a value past
// them shows as its number. No control sets it and the list does not show
// it; the count keeps any model from reading past the array.
constexpr NameCount kNames[] = {
	{ CHORDS_SETTING_CHORD_SLOT, 8, nullptr },
};

// The table's "scale  -->" is the scale mask (the scale editor), and
// "chords -->" the chord editor.
constexpr Label kLabels[] = {
	{ CHORDS_SETTING_MASK, "Active notes" },
	{ CHORDS_SETTING_CHORD_EDIT, "Chords" },
};

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, &accepting, kNames, 1, kLabels, 2 };

} // namespace acidcurds
