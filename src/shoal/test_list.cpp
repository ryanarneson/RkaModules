// shoal::list (ListLayout.hpp): the friendly list's geometry, hit testing,
// slider and scroll arithmetic, and the bookkeeping of its gestures.
#include <cmath>
#include <cstring>
#include <vector>

#include "ListLayout.hpp"
#include "testing.hpp"

using shoal::FriendlyEdit;
using shoal::FriendlyModel;
using shoal::Row;
using shoal::RowKind;
using namespace shoal::list;

namespace {

// The lower screen at Rack's 100 % zoom (83.2 x 41.6 mm), and an odd size.
const float kSizes[][2] = { { 245.67f, 122.83f }, { 301.f, 177.f } };
const float kEps = 1e-3f;

bool near(float a, float b) {
	return std::fabs(a - b) <= kEps;
}

bool inside(const Box& b, const Box& outer) {
	return b.x >= outer.x - kEps && b.y >= outer.y - kEps && b.right() <= outer.right() + kEps && b.bottom() <= outer.bottom() + kEps;
}

bool overlap(const Box& a, const Box& b) {
	return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps && b.y < a.bottom() - kEps;
}

Row makeRow(RowKind kind, int id, int32_t value = 0, int32_t min = 0, int32_t max = 0) {
	Row r = {};
	r.kind = kind;
	r.id = (int16_t) id;
	r.label = "row";
	r.value = value;
	r.min = min;
	r.max = max;
	return r;
}

// `rows` Number rows 0..255, ids from 0.
FriendlyModel makeModel(int tabs, int rows, bool busy = false) {
	static const char* const kTabs[] = { "A", "B", "C", "D" };
	FriendlyModel m = {};
	m.tabs = (uint8_t) tabs;
	m.flags = busy ? shoal::kModelBusy : 0;
	for (int t = 0; t < tabs && tabs > 1; t++)
		m.tabLabel[t] = kTabs[t];
	m.rows = (int16_t) rows;
	for (int i = 0; i < rows; i++)
		m.row[i] = makeRow(RowKind::Number, i, 0, 0, 255);
	return m;
}

// Everything tiles the screen: no overlap, nothing outside, no gap.
void testTiling() {
	for (const float* size : kSizes) {
		const float w = size[0], h = size[1];
		const Box screen = { 0.f, 0.f, w, h };
		for (int tabs : { 1, 4 }) {
			for (int rows : { 1, 8, shoal::kMaxRows }) {
				for (int busy = 0; busy < 2; busy++) {
					const FriendlyModel m = makeModel(tabs, rows, busy != 0);
					for (int scroll : { 0, 3, 1000 }) {
						const Layout l(w, h, m, scroll);
						const char* what = tabs > 1 ? "tabs" : "no tabs";
						CHECK(l.visible == kLines - (tabs > 1 ? 1 : 0) - busy, "%s busy %d: %d rows visible", what, busy, l.visible);
						CHECK(l.scroll == clampScroll(scroll, rows, l.visible), "%s rows %d: scroll %d", what, rows, l.scroll);

						// The bands stack from the top to the bottom edge.
						std::vector<Box> bands;
						bands.push_back(l.header());
						if (busy)
							bands.push_back(l.banner());
						else
							CHECK(l.banner().h == 0.f, "%s: a banner when not busy", what);
						if (tabs > 1)
							bands.push_back(l.tabBar());
						else
							CHECK(l.tabBar().h == 0.f, "a tab bar with one channel");
						bands.push_back(l.rowsArea());
						float y = 0.f;
						for (const Box& b : bands) {
							CHECK(near(b.y, y) && near(b.x, 0.f) && near(b.w, w) && b.h > 0.f, "%s rows %d: band at y %g, want %g (w %g)", what, rows, b.y, y, b.w);
							y = b.bottom();
						}
						CHECK(near(y, h), "%s rows %d: the bands end at %g, want %g", what, rows, y, h);

						// Tabs tile the tab bar.
						float x = 0.f;
						for (int t = 0; t < l.tabs; t++) {
							const Box b = l.tab(t);
							CHECK(near(b.x, x) && near(b.y, l.tabBar().y) && near(b.h, l.tabBar().h), "tab %d at %g, want %g", t, b.x, x);
							x = b.right();
						}
						CHECK(l.tabs == 0 || near(x, w), "the tabs end at %g, want %g", x, w);

						// Rows: the visible ones stack in the rows area beside
						// the scrollbar; the others have no box.
						const Box area = l.rowsArea();
						const Box bar = l.scrollbar();
						CHECK((bar.w > 0.f) == (rows > l.visible), "%s rows %d: scrollbar width %g", what, rows, bar.w);
						CHECK(near(l.rowWidth() + bar.w, w), "rows and scrollbar are %g wide, want %g", l.rowWidth() + bar.w, w);
						if (bar.w > 0.f) {
							CHECK(near(bar.right(), w) && near(bar.y, area.y) && near(bar.h, area.h), "scrollbar beside the rows");
							CHECK(inside(l.thumb(), bar) && l.thumb().h > 0.f, "thumb inside the scrollbar");
						}
						y = area.y;
						int shown = 0;
						for (int i = 0; i < rows; i++) {
							const Box b = l.row(i);
							if (i < l.scroll || i >= l.scroll + l.visible) {
								CHECK(b.w == 0.f && b.h == 0.f, "row %d is scrolled out but has a box", i);
								continue;
							}
							shown++;
							CHECK(near(b.y, y) && near(b.x, 0.f) && near(b.w, l.rowWidth()) && near(b.h, l.lineH()), "%s rows %d: row %d at y %g, want %g", what, rows, i, b.y, y);
							CHECK(inside(b, area) && inside(b, screen) && !overlap(b, bar), "row %d leaves the rows area", i);
							y = b.bottom();
							// Label and control split the row.
							const Box lab = l.label(m.row[i], i), con = l.control(m.row[i], i);
							CHECK(near(lab.x, b.x) && near(lab.right(), con.x) && near(con.right(), b.right()) && lab.w > 0.f && con.w > 0.f,
								"row %d: label %g..%g, control %g..%g", i, lab.x, lab.right(), con.x, con.right());
						}
						CHECK(shown == (rows < l.visible ? rows : l.visible), "%s rows %d: %d shown", what, rows, shown);
					}
				}
			}
		}
	}
}

// Each kind of row gives its space to the label and the control as it needs.
void testRowParts() {
	FriendlyModel m = makeModel(1, 6);
	m.row[1] = makeRow(RowKind::Heading, 512);
	m.row[2] = makeRow(RowKind::Action, 256);
	m.row[3] = makeRow(RowKind::Toggle, 3, 0, 0, 1);
	m.row[4] = makeRow(RowKind::Choice, 4, 0, 0, 3);
	m.row[5] = makeRow(RowKind::SubEditor, 5);
	const Layout l(kSizes[0][0], kSizes[0][1], m, 0);
	const Box heading = l.row(1), action = l.row(2);
	CHECK(l.control(m.row[1], 1).w == 0.f && near(l.label(m.row[1], 1).w, heading.w), "a heading is all label");
	CHECK(l.label(m.row[2], 2).w == 0.f && near(l.control(m.row[2], 2).w, action.w), "an action is all control");
	CHECK(l.control(m.row[3], 3).w < l.control(m.row[0], 0).w, "a toggle's control is narrower than a slider's");
	CHECK(near(l.control(m.row[4], 4).x, l.control(m.row[0], 0).x) && near(l.control(m.row[5], 5).x, l.control(m.row[0], 0).x), "sliders, dropdowns and sub-editors line up");
}

void checkHit(const Layout& l, const FriendlyModel& m, float x, float y, Part part, int index, int cell, const char* what) {
	const Hit hit = l.hit(m, x, y);
	CHECK(hit.part == part && hit.index == index && hit.cell == cell, "%s (%g, %g): part %d index %d cell %d, want %d %d %d",
		what, x, y, (int) hit.part, hit.index, hit.cell, (int) part, index, cell);
}

void testHit() {
	const float w = kSizes[0][0], h = kSizes[0][1], in = 0.01f;
	FriendlyModel m = makeModel(4, 20);
	m.row[4] = makeRow(RowKind::Heading, 512);
	m.row[5] = makeRow(RowKind::Action, 256);
	const Layout l(w, h, m, 2);

	checkHit(l, m, 5.f, in, Part::Header, -1, -1, "header top");
	checkHit(l, m, w - in, l.header().bottom() - in, Part::Header, -1, -1, "header bottom right");
	for (int t = 0; t < 4; t++) {
		const Box b = l.tab(t);
		checkHit(l, m, b.x + in, b.y + in, Part::Tab, t, -1, "tab left edge");
		checkHit(l, m, b.right() - in, b.bottom() - in, Part::Tab, t, -1, "tab right edge");
	}
	// Row boundaries: the first and last pixel of each visible row.
	for (int i = l.scroll; i < l.scroll + l.visible; i++) {
		const Box b = l.row(i);
		const Box lab = l.label(m.row[i], i), con = l.control(m.row[i], i);
		if (m.row[i].kind == RowKind::Heading) {
			checkHit(l, m, b.x + in, b.y + in, Part::None, -1, -1, "heading");
			continue;
		}
		if (m.row[i].kind == RowKind::Action) {
			checkHit(l, m, b.x + in, b.y + in, Part::Control, i, -1, "action left");
			checkHit(l, m, b.right() - in, b.bottom() - in, Part::Control, i, -1, "action right");
			continue;
		}
		checkHit(l, m, lab.x + in, b.y + in, Part::Label, i, -1, "label top left");
		checkHit(l, m, lab.right() - in, b.bottom() - in, Part::Label, i, -1, "label bottom right");
		checkHit(l, m, con.x + in, b.y + in, Part::Control, i, -1, "control top left");
		checkHit(l, m, con.right() - in, b.bottom() - in, Part::Control, i, -1, "control bottom right");
	}
	const Box bar = l.scrollbar();
	checkHit(l, m, bar.x + in, bar.y + in, Part::Scrollbar, -1, -1, "scrollbar top");
	checkHit(l, m, bar.right() - in, bar.bottom() - in, Part::Scrollbar, -1, -1, "scrollbar bottom");
	// Outside the screen.
	checkHit(l, m, -1.f, 5.f, Part::None, -1, -1, "left of the screen");
	checkHit(l, m, 5.f, h + 1.f, Part::None, -1, -1, "below the screen");
	checkHit(l, m, w, 5.f, Part::None, -1, -1, "the right edge is outside");

	// Fewer rows than fit: the space under the last row is nothing.
	const FriendlyModel few = makeModel(1, 3);
	const Layout lf(w, h, few, 0);
	checkHit(lf, few, 5.f, lf.row(2).bottom() - in, Part::Label, 2, -1, "last row");
	checkHit(lf, few, 5.f, lf.row(2).bottom() + in, Part::None, -1, -1, "under the last row");

	// Busy: the banner under the header.
	const FriendlyModel busy = makeModel(4, 20, true);
	const Layout lb(w, h, busy, 0);
	checkHit(lb, busy, 5.f, lb.banner().y + in, Part::Banner, -1, -1, "banner");
	checkHit(lb, busy, 5.f, lb.tabBar().y + in, Part::Tab, 0, -1, "tab under the banner");
	checkHit(lb, busy, 5.f, lb.rowsArea().y + in, Part::Label, 0, -1, "first row under the tabs");
}

// A mask row: one cell per note, tiling the control.
void testMaskCells() {
	const float in = 0.01f;
	for (int cells : { 4, 12, 16 }) {
		FriendlyModel m = makeModel(1, 3);
		m.row[1] = makeRow(RowKind::Mask, 1, 0xffff, 0, cells);
		const Layout l(kSizes[0][0], kSizes[0][1], m, 0);
		const Box con = l.control(m.row[1], 1);
		float x = con.x;
		for (int n = 0; n < cells; n++) {
			const Box c = l.cell(m.row[1], 1, n);
			CHECK(near(c.x, x) && near(c.y, con.y) && near(c.h, con.h) && c.w > 0.f, "%d cells: cell %d at %g, want %g", cells, n, c.x, x);
			x = c.right();
			checkHit(l, m, c.x + in, c.y + in, Part::Cell, 1, n, "cell left edge");
			checkHit(l, m, c.x + c.w / 2, c.y + c.h / 2, Part::Cell, 1, n, "cell centre");
			checkHit(l, m, c.right() - in, c.bottom() - in, Part::Cell, 1, n, "cell right edge");
			CHECK(l.cellAt(m.row[1], 1, c.x + c.w / 2) == n, "%d cells: cellAt the centre of %d", cells, n);
		}
		CHECK(near(x, con.right()), "%d cells end at %g, want %g", cells, x, con.right());
		// Painting past either end stays on the end cell.
		CHECK(l.cellAt(m.row[1], 1, con.x - 50.f) == 0 && l.cellAt(m.row[1], 1, con.right() + 50.f) == cells - 1, "%d cells: cellAt clamps", cells);
		checkHit(l, m, con.x - in, con.y + in, Part::Label, 1, -1, "left of the cells");
	}
	// A mask with no cells (scale Off) has a control and no cell.
	FriendlyModel m = makeModel(1, 2);
	m.row[1] = makeRow(RowKind::Mask, 1, 0, 0, 0);
	const Layout l(kSizes[0][0], kSizes[0][1], m, 0);
	const Box con = l.control(m.row[1], 1);
	checkHit(l, m, con.x + 5.f, con.y + 2.f, Part::Control, 1, -1, "empty mask");
	CHECK(l.cellAt(m.row[1], 1, con.x + 5.f) == -1, "no cell in an empty mask");
}

void testSlider() {
	const float w = 137.f;
	const int32_t ranges[][2] = { { 0, 1 }, { -3, 3 }, { 0, 255 }, { 0, 65535 } };
	for (const int32_t* r : ranges) {
		const int32_t lo = r[0], hi = r[1], mid = lo + (hi - lo) / 2;
		CHECK(sliderX(lo, w, lo, hi) == 0.f, "%d..%d: min is the left edge", lo, hi);
		CHECK(near(sliderX(hi, w, lo, hi), w), "%d..%d: max is the right edge", lo, hi);
		for (int32_t v : { lo, mid, hi })
			CHECK(sliderValue(sliderX(v, w, lo, hi), w, lo, hi) == v, "%d..%d: %d comes back as %d", lo, hi, v, sliderValue(sliderX(v, w, lo, hi), w, lo, hi));
		CHECK(sliderValue(-10.f, w, lo, hi) == lo && sliderValue(w + 10.f, w, lo, hi) == hi, "%d..%d: clamps past the ends", lo, hi);

		// A drag starts at the value and follows the pointer; fine moves a
		// quarter as far, finer a sixteenth.
		SliderDrag d;
		d.begin(lo, w, lo, hi);
		CHECK(d.move(0.f, 1.f, w, lo, hi) == lo, "%d..%d: no movement, no change", lo, hi);
		CHECK(d.move(w, 1.f, w, lo, hi) == hi, "%d..%d: the whole width is the whole range", lo, hi);
		CHECK(d.move(50.f, 1.f, w, lo, hi) == hi && d.move(-w / 2, 1.f, w, lo, hi) == sliderValue(w / 2, w, lo, hi), "%d..%d: the end holds the pointer's overshoot back", lo, hi);
		SliderDrag coarse, fine, finer;
		coarse.begin(lo, w, lo, hi);
		fine.begin(lo, w, lo, hi);
		finer.begin(lo, w, lo, hi);
		coarse.move(20.f, 1.f, w, lo, hi);
		fine.move(80.f, kFine, w, lo, hi);
		finer.move(320.f, kFiner, w, lo, hi);
		CHECK(near(coarse.x, 20.f) && near(fine.x, 20.f) && near(finer.x, 20.f), "%d..%d: fine moved %g and finer %g for coarse's %g", lo, hi, fine.x, finer.x, coarse.x);
	}
	CHECK(kFine == 0.25f, "fine is a quarter speed");
	// Degenerate ranges and widths give the minimum, not a division by zero.
	CHECK(sliderValue(5.f, w, 7, 7) == 7 && sliderX(7, w, 7, 7) == 0.f, "an empty range");
	CHECK(sliderValue(5.f, 0.f, 0, 10) == 0 && sliderX(5, 0.f, 0, 10) == 0.f, "a bar with no width");
	// The whole int32 range does not overflow.
	CHECK(sliderValue(w, w, -2147483647 - 1, 2147483647) == 2147483647, "full range, right edge");
	CHECK(sliderValue(0.f, w, -2147483647 - 1, 2147483647) == -2147483647 - 1, "full range, left edge");
}

void testScroll() {
	CHECK(clampScroll(5, 8, 8) == 0 && clampScroll(5, 3, 8) == 0 && clampScroll(-2, 3, 8) == 0, "no scroll when everything fits");
	CHECK(clampScroll(-1, 20, 8) == 0 && clampScroll(0, 20, 8) == 0, "clamps at the top");
	CHECK(clampScroll(12, 20, 8) == 12 && clampScroll(13, 20, 8) == 12 && clampScroll(1000, 20, 8) == 12, "clamps at the bottom");
	CHECK(clampScroll(5, 20, 8) == 5, "in range is unchanged");
	CHECK(clampScroll(3, 0, 8) == 0 && clampScroll(3, 5, 0) == 0, "no rows, or no room");

	// Bringing a row into view moves as little as possible.
	CHECK(scrollToShow(0, 3, 20, 8) == 0 && scrollToShow(0, 7, 20, 8) == 0, "already in view");
	CHECK(scrollToShow(0, 8, 20, 8) == 1 && scrollToShow(0, 19, 20, 8) == 12, "below: it becomes the last row shown");
	CHECK(scrollToShow(10, 4, 20, 8) == 4, "above: it becomes the first row shown");
	CHECK(scrollToShow(5, -1, 20, 8) == 5 && scrollToShow(5, 20, 20, 8) == 5, "no such row: unchanged");

	// The thumb covers the share of rows shown and maps back to the scroll.
	const FriendlyModel m = makeModel(1, 24);
	const float w = kSizes[0][0], h = kSizes[0][1];
	const Layout top(w, h, m, 0), bottom(w, h, m, 1000);
	const Box bar = top.scrollbar();
	CHECK(near(top.thumb().y, bar.y) && near(bottom.thumb().bottom(), bar.bottom()), "thumb at the ends");
	CHECK(near(top.thumb().h, bar.h * 8 / 24), "thumb height %g, want %g", top.thumb().h, bar.h * 8 / 24);
	for (int s = 0; s <= 16; s++) {
		const Layout l(w, h, m, s);
		CHECK(top.scrollForThumbTop(l.thumb().y) == s, "thumb for scroll %d maps back to %d", s, top.scrollForThumbTop(l.thumb().y));
	}
	CHECK(top.scrollForThumbTop(-500.f) == 0 && top.scrollForThumbTop(5000.f) == 16, "dragging the thumb past the ends clamps");
	// A long list keeps a thumb big enough to grab.
	const FriendlyModel longList = makeModel(1, shoal::kMaxRows);
	const Layout ll(w, h, longList, 0);
	CHECK(ll.thumb().h >= ll.lineH() - kEps, "thumb %g high, want at least a row (%g)", ll.thumb().h, ll.lineH());
	// Nothing to scroll: no thumb, and any thumb position is scroll 0.
	const FriendlyModel few = makeModel(1, 3);
	const Layout lf(w, h, few, 2);
	CHECK(lf.scroll == 0 && lf.thumb().h == 0.f && lf.scrollForThumbTop(40.f) == 0, "three rows do not scroll");
}

FriendlyEdit makeEdit(FriendlyEdit::Type type, int app, int tab, int id, int value) {
	FriendlyEdit e = {};
	e.type = type;
	e.app = (uint8_t) app;
	e.tab = (uint8_t) tab;
	e.id = (int16_t) id;
	e.value = value;
	return e;
}

// What a host with room for `room` more entries received.
struct FakeHost {
	int room = 0;
	std::vector<int> apps;
	std::vector<FriendlyEdit> edits;
	bool takeApp(int app) {
		if (room <= 0)
			return false;
		room--;
		apps.push_back(app);
		return true;
	}
	bool takeEdit(const FriendlyEdit& e) {
		if (room <= 0)
			return false;
		room--;
		edits.push_back(e);
		return true;
	}
	void flush(Outbox& o) {
		o.flush([this](int app) { return takeApp(app); }, [this](const FriendlyEdit& e) { return takeEdit(e); });
	}
};

// The outbox keeps what the host's queue refused: the latest target per
// row, the latest tab and the latest app, in order.
void testOutbox() {
	Outbox o;
	FakeHost h;
	CHECK(o.empty(), "starts empty");
	// A full queue: a drag's steps collapse into the latest target per row.
	CHECK(o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, 7, 10)), "first target");
	CHECK(o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, 7, 11)), "second target");
	CHECK(o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, 8, 50)), "another row");
	CHECK(o.add(makeEdit(FriendlyEdit::SetValue, 3, 1, 7, 99)), "the same row on another tab");
	CHECK(o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, 7, 12)), "third target");
	h.flush(o);
	CHECK(h.edits.empty() && o.count == 3, "nothing sent, %d waiting (want 3)", o.count);
	h.room = 1;
	h.flush(o);
	CHECK(h.edits.size() == 1 && h.edits[0].id == 7 && h.edits[0].tab == 0 && h.edits[0].value == 12, "the latest target of the first row goes first");
	h.room = 10;
	h.flush(o);
	CHECK(h.edits.size() == 3 && h.edits[1].id == 8 && h.edits[1].value == 50 && h.edits[2].tab == 1 && h.edits[2].value == 99, "the rest follow in order");
	CHECK(o.empty(), "empty once sent");
	h.flush(o);
	CHECK(h.edits.size() == 3, "nothing is sent twice");

	// Toggles and presses are each kept; a tab selection replaces the last.
	h.edits.clear();
	h.room = 0;
	o.add(makeEdit(FriendlyEdit::ToggleBit, 3, 0, 2, 4));
	o.add(makeEdit(FriendlyEdit::ToggleBit, 3, 0, 2, 4));
	o.add(makeEdit(FriendlyEdit::SelectTab, 3, 1, 0, 1));
	o.add(makeEdit(FriendlyEdit::Press, 3, 1, 256, 0));
	o.add(makeEdit(FriendlyEdit::SelectTab, 3, 2, 0, 2));
	CHECK(o.count == 4, "%d waiting, want 4", o.count);
	h.room = 10;
	h.flush(o);
	CHECK(h.edits.size() == 4 && h.edits[0].type == FriendlyEdit::ToggleBit && h.edits[1].type == FriendlyEdit::ToggleBit
		&& h.edits[2].type == FriendlyEdit::SelectTab && h.edits[2].value == 2 && h.edits[3].type == FriendlyEdit::Press, "toggles kept, the last tab wins");

	// An app selection goes first and drops what was drawn for the old app;
	// the latest selection wins.
	h.edits.clear();
	h.room = 0;
	o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, 7, 1));
	o.selectApp(5);
	CHECK(o.count == 0 && !o.empty(), "the old app's edits are gone, the selection waits");
	o.selectApp(6);
	o.add(makeEdit(FriendlyEdit::SetValue, 6, 0, 1, 2));
	h.flush(o);
	CHECK(h.apps.empty() && h.edits.empty(), "queue full: nothing sent");
	h.room = 1;
	h.flush(o);
	CHECK(h.apps.size() == 1 && h.apps[0] == 6 && h.edits.empty(), "the app selection first");
	h.room = 1;
	h.flush(o);
	CHECK(h.apps.size() == 1 && h.edits.size() == 1 && h.edits[0].app == 6, "then the edit made after it");
	CHECK(o.empty(), "empty");

	// Full: a new edit is refused, a new target for a waiting row is not.
	h.room = 0;
	for (int i = 0; i < Outbox::kSlots; i++)
		CHECK(o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, i, i)), "slot %d", i);
	CHECK(!o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, 100, 1)), "a new row does not fit");
	CHECK(!o.add(makeEdit(FriendlyEdit::Press, 3, 0, 256, 0)), "nor does a press");
	CHECK(o.add(makeEdit(FriendlyEdit::SetValue, 3, 0, 5, 77)) && o.edits[5].value == 77, "a waiting row takes its new target");
}

// Dragging across a mask paints the state the first cell was given.
void testMaskPaint() {
	MaskPaint p;
	int cells[32];
	// Press on a lit cell: it goes dark, and so does every lit cell crossed.
	uint32_t mask = 0x0f5;   // cells 0, 2, 4, 5, 6, 7 lit
	CHECK(p.begin(mask, 2) && !p.on, "pressing a lit cell clears it");
	CHECK(p.enter(mask, 2, cells) == 0, "staying on it does nothing more");
	int n = p.enter(mask, 5, cells);
	CHECK(n == 2 && cells[0] == 4 && cells[1] == 5, "2 -> 5 clears 4 and 5 (3 is dark already): %d cells", n);
	// The model has not caught up: nothing is toggled twice.
	CHECK(p.enter(mask, 4, cells) == 0 && p.enter(mask, 5, cells) == 0, "cells already painted are left alone");
	n = p.enter(mask, 0, cells);
	CHECK(n == 1 && cells[0] == 0, "back to 0 clears 0 (1 is dark, 2 was the press): %d cells", n);
	// Press on a dark cell: it lights, and so does every dark cell crossed.
	mask = 0x0f5;
	CHECK(p.begin(mask, 3) && p.on, "pressing a dark cell lights it");
	n = p.enter(mask, 0, cells);
	CHECK(n == 1 && cells[0] == 1, "3 -> 0 lights 1 only: %d cells", n);
	n = p.enter(mask, 11, cells);
	CHECK(n == 4 && cells[0] == 8 && cells[3] == 11, "0 -> 11 lights 8..11: %d cells", n);
	// Outside the mask: nothing.
	CHECK(!p.begin(mask, -1) && p.enter(mask, 3, cells) == 0, "no press, no painting");
	CHECK(!p.begin(mask, 32), "cell 32 does not exist");
}

void testValueText() {
	static const char* const kNames[] = { "one", "two", nullptr };
	char out[24];
	Row r = makeRow(RowKind::Number, 0, -7, -10, 10);
	valueText(r, r.value, out, sizeof(out));
	CHECK(std::strcmp(out, "-7") == 0, "a number: '%s'", out);
	std::strcpy(r.text, "1/5");
	valueText(r, r.value, out, sizeof(out));
	CHECK(std::strcmp(out, "1/5") == 0, "the firmware's text: '%s'", out);
	valueText(r, 3, out, sizeof(out));
	CHECK(std::strcmp(out, "3") == 0, "another value is not the firmware's text: '%s'", out);

	Row c = makeRow(RowKind::Choice, 1, 6, 5, 9);
	c.names = kNames;
	c.nameCount = 3;
	valueText(c, 6, out, sizeof(out));
	CHECK(std::strcmp(out, "two") == 0, "a name, counted from min: '%s'", out);
	valueText(c, 7, out, sizeof(out));
	CHECK(std::strcmp(out, "7") == 0, "a null name is the number: '%s'", out);
	valueText(c, 9, out, sizeof(out));
	CHECK(std::strcmp(out, "9") == 0, "past the names is the number: '%s'", out);
	valueText(c, 4, out, sizeof(out));
	CHECK(std::strcmp(out, "4") == 0, "below min is the number: '%s'", out);
	CHECK(choiceName(c, 5) == kNames[0] && choiceName(c, 8) == nullptr && choiceName(c, 4) == nullptr, "choiceName stays inside the names");
	c.value = 9;
	std::strcpy(c.text, "nine");
	valueText(c, 9, out, sizeof(out));
	CHECK(std::strcmp(out, "nine") == 0, "the firmware's text for a value with no name: '%s'", out);

	Row t = makeRow(RowKind::Toggle, 2, 1, 0, 1);
	valueText(t, 1, out, sizeof(out));
	CHECK(std::strcmp(out, "on") == 0, "toggle on: '%s'", out);
	valueText(t, 0, out, sizeof(out));
	CHECK(std::strcmp(out, "off") == 0, "toggle off: '%s'", out);

	// A text that fills the row's buffer with no terminator is still bounded.
	Row full = makeRow(RowKind::Number, 3, 1, 0, 9);
	std::memset(full.text, 'x', sizeof(full.text));
	valueText(full, 1, out, sizeof(out));
	CHECK(std::strlen(out) == sizeof(full.text), "unterminated text: %zu characters", std::strlen(out));
	char tiny[4];
	valueText(full, 1, tiny, sizeof(tiny));
	CHECK(std::strcmp(tiny, "xxx") == 0, "a small buffer is filled and terminated: '%s'", tiny);
}

// The row a model's edit names, and the edit built for it.
void testFindAndEdit() {
	FriendlyModel m = makeModel(4, 6);
	m.app = 3;
	m.tab = 2;
	m.row[4].id = 300;
	m.row[2].flags = shoal::kRowCursor;
	CHECK(findRow(m, 300) == 4 && findRow(m, 1) == 1 && findRow(m, 77) == -1, "findRow by id");
	CHECK(cursorRow(m) == 2, "the cursor's row");
	m.row[2].flags = 0;
	CHECK(cursorRow(m) == -1, "no cursor row");
	const FriendlyEdit e = editFor(m, FriendlyEdit::SetValue, 300, 9);
	CHECK(e.type == FriendlyEdit::SetValue && e.app == 3 && e.tab == 2 && e.id == 300 && e.value == 9, "an edit carries the model's app and tab");
}

}

int main() {
	testTiling();
	testRowParts();
	testHit();
	testMaskCells();
	testSlider();
	testScroll();
	testOutbox();
	testMaskPaint();
	testValueText();
	testFindAndEdit();
	return rkatest::finish("test_list");
}
