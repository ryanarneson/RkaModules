// Friendly-mode adapters, file 1: Quadraturia and Low-rents. Included by
// Friendly.inc between its helpers and its table of adapters; see that file
// for the Adapter struct, the helpers used here and the rules an adapter
// follows. Each later group of apps gets a file of its own beside this one
// (FriendlyAdapters2.inc, ...) and its lines in the table.

// ---- Quadraturia (APP_POLYLFO.ino) ------------------------------------------
// One settings object, no enabled list: the cursor position is a setting
// index, from Tap tempo on. Coarse and fine are not in the firmware's menu:
// the L encoder edits whichever of the two L push has selected (the title
// bar's "(C)" or "(F)"), and not at all while tap tempo is on. No sub-editor:
// the app is always on its settings menu.

namespace quadraturia {

enum { kPhaseReset = kExtraId };

void header(FriendlyModel& m) {
	// POLYLFO_menu's title bar.
	Text status(m.status);
	if (poly_lfo.get_tap_tempo()) {
		status.str("(T) Ch A: tap tempo");
		return;
	}
	status.str("(").str(PolyLfo::value_attr(poly_lfo_state.left_edit_mode).name).str(") Ch A: ");
	const float freq = poly_lfo.lfo.get_freq_ch1();
	if (freq >= 0.1f)
		status.fixed(freq, 2, 6).str(" Hz");
	else
		status.fixed(1.0f / freq, 3, 6).str("s");
}

void rows(FriendlyModel& m, int) {
	addTable(m, poly_lfo, 0, POLYLFO_SETTING_LAST - 1, &poly_lfo_state.cursor);
	addExtra(m, RowKind::Action, kPhaseReset, "Phase reset");  // DOWN long press
}

bool edit(const FriendlyEdit& e, const Row& row) {
	const bool leftSide = row.id == POLYLFO_SETTING_COARSE || row.id == POLYLFO_SETTING_FINE;
	switch (e.type) {
		case FriendlyEdit::SetValue:
			if (!leftSide)
				return replaySet(poly_lfo_state.cursor, row.id, e.value - row.value);
			if (poly_lfo_state.left_edit_mode != row.id)
				button(OC::CONTROL_BUTTON_L);
			encoder(OC::CONTROL_ENCODER_L, e.value - row.value);
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

// The table's names for these two are "C" and "F".
constexpr Label kLabels[] = {
	{ POLYLFO_SETTING_COARSE, "Coarse" },
	{ POLYLFO_SETTING_FINE, "Fine" },
};

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, nullptr, nullptr, 0, kLabels, 2 };

} // namespace quadraturia

// ---- Low-rents (APP_LORENZ.ino) ---------------------------------------------
// One settings object, no enabled list: the cursor position is a setting
// index, from Rho/c 1 on. Freq 1 and Freq 2 are not in the firmware's menu:
// the L encoder edits the one L push has selected. No sub-editor.

namespace lowrents {

void header(FriendlyModel& m) {
	// LORENZ_menu's title bar: both frequencies with their CV.
	const int32_t freq1 = SCALE8_16(lorenz_generator.get_freq1()) + (lorenz_generator.cv_freq1.value() * 16);
	const int32_t freq2 = SCALE8_16(lorenz_generator.get_freq2()) + (lorenz_generator.cv_freq2.value() * 16);
	Text(m.status).str("Freq1 ").num(USAT16(freq1) >> 8).str("  Freq2 ").num(USAT16(freq2) >> 8);
}

void rows(FriendlyModel& m, int) {
	addTable(m, lorenz_generator, 0, LORENZ_SETTING_LAST - 1, &lorenz_generator_state.cursor);
}

bool edit(const FriendlyEdit& e, const Row& row) {
	const bool leftSide = row.id == LORENZ_SETTING_FREQ1 || row.id == LORENZ_SETTING_FREQ2;
	switch (e.type) {
		case FriendlyEdit::SetValue:
			if (!leftSide)
				return replaySet(lorenz_generator_state.cursor, row.id, e.value - row.value);
			if (lorenz_generator_state.selected_generator != (row.id == LORENZ_SETTING_FREQ2))
				button(OC::CONTROL_BUTTON_L);
			encoder(OC::CONTROL_ENCODER_L, e.value - row.value);
			return true;
		case FriendlyEdit::Focus:
			if (!leftSide)
				moveCursor(lorenz_generator_state.cursor, row.id);
			return true;
		default:
			return false;
	}
}

constexpr Adapter kAdapter = { &header, &rows, nullptr, &edit, nullptr, nullptr, 0, nullptr, 0 };

} // namespace lowrents
