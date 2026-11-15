#pragma once
// Shoal friendly list: where everything on the lower screen goes, what a
// point on it hits, and the arithmetic and bookkeeping of its gestures
// (slider drags, scrolling, mask painting, edits the host's queue refused).
// The Rack widget (FriendlyList.hpp) only draws these boxes and feeds them
// mouse events. Rack-free, C++11, tested offline (test_list.cpp).
//
// The screen, top to bottom: a header; while the model is busy, a banner;
// with more than one channel, a tab bar; then the rows, with a scrollbar on
// the right when they do not all fit. Every size is a share of the screen's,
// so the list fills it exactly at any size.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "Friendly.hpp"

namespace shoal {
namespace list {

struct Box {
	float x, y, w, h;
	float right() const { return x + w; }
	float bottom() const { return y + h; }
};

// Lines of equal height under the header: the banner, the tab bar and the rows.
constexpr int kLines = 8;
constexpr float kHeaderShare = 0.14f;      // of the height
constexpr float kControlShare = 0.56f;     // of a row's width: slider, dropdown, mask, sub-editor
constexpr float kToggleShare = 0.2f;       // of a row's width: an on/off switch
constexpr float kScrollbarShare = 0.03f;   // of the width

// The furthest a list of `rows` can scroll with `visible` shown, in rows.
inline int maxScroll(int rows, int visible) {
	return rows > visible && visible > 0 ? rows - visible : 0;
}

inline int clampScroll(int scroll, int rows, int visible) {
	const int most = maxScroll(rows, visible);
	return scroll < 0 ? 0 : scroll > most ? most : scroll;
}

// The scroll nearest `scroll` that shows `row`; unchanged for a row the list
// does not have.
inline int scrollToShow(int scroll, int row, int rows, int visible) {
	if (row < 0 || row >= rows)
		return scroll;
	if (row < scroll)
		scroll = row;
	else if (row >= scroll + visible)
		scroll = row - visible + 1;
	return clampScroll(scroll, rows, visible);
}

enum class Part : uint8_t {
	None,       // nothing to click (a heading, the space under the last row, outside)
	Header,
	Banner,     // the busy line
	Tab,        // index: the tab
	Label,      // index: the row
	Control,    // index: the row
	Cell,       // index: the row; cell: the mask cell
	Scrollbar
};

struct Hit {
	Part part;
	int index;
	int cell;
};

struct Layout {
	float w, h;
	int tabs;       // tabs in the tab bar; 0: no tab bar
	int rows;
	int visible;    // rows that fit
	int scroll;     // the first row shown; clamped
	bool busy;

	Layout(float width, float height, const FriendlyModel& m, int wantedScroll)
		: w(width), h(height),
		  tabs(m.tabs > 1 ? (m.tabs < kMaxTabs ? m.tabs : kMaxTabs) : 0),
		  rows(m.rows < 0 ? 0 : m.rows > kMaxRows ? kMaxRows : m.rows),
		  visible(kLines - (m.tabs > 1 ? 1 : 0) - ((m.flags & kModelBusy) ? 1 : 0)),
		  scroll(clampScroll(wantedScroll, rows, visible)),
		  busy((m.flags & kModelBusy) != 0) {}

	float headerH() const { return h * kHeaderShare; }
	float lineH() const { return (h - headerH()) / kLines; }
	// Line n of kLines under the header, full width.
	Box line(int n) const { return Box{ 0.f, headerH() + lineH() * n, w, lineH() }; }

	Box header() const { return Box{ 0.f, 0.f, w, headerH() }; }
	Box banner() const { return busy ? line(0) : Box{ 0.f, 0.f, 0.f, 0.f }; }
	Box tabBar() const { return tabs ? line(busy ? 1 : 0) : Box{ 0.f, 0.f, 0.f, 0.f }; }
	Box tab(int i) const {
		if (i < 0 || i >= tabs)
			return Box{ 0.f, 0.f, 0.f, 0.f };
		const Box bar = tabBar();
		return Box{ bar.w * i / tabs, bar.y, bar.w / tabs, bar.h };
	}
	Box rowsArea() const {
		const Box first = line(kLines - visible);
		return Box{ 0.f, first.y, w, lineH() * visible };
	}

	bool scrollable() const { return rows > visible; }
	Box scrollbar() const {
		if (!scrollable())
			return Box{ w, 0.f, 0.f, 0.f };
		const Box area = rowsArea();
		const float sw = w * kScrollbarShare;
		return Box{ w - sw, area.y, sw, area.h };
	}
	// The thumb: the share of the rows shown, never less than a row high.
	Box thumb() const {
		if (!scrollable())
			return Box{ w, 0.f, 0.f, 0.f };
		const Box bar = scrollbar();
		const float th = thumbH(bar);
		return Box{ bar.x, bar.y + (bar.h - th) * scroll / maxScroll(rows, visible), bar.w, th };
	}
	// The scroll that puts the thumb's top nearest y.
	int scrollForThumbTop(float y) const {
		if (!scrollable())
			return 0;
		const Box bar = scrollbar();
		const float travel = bar.h - thumbH(bar);
		if (travel <= 0.f)
			return 0;
		const float s = (y - bar.y) / travel * maxScroll(rows, visible);
		return clampScroll(s < 0.f ? 0 : (int) (s + 0.5f), rows, visible);
	}

	float rowWidth() const { return w - scrollbar().w; }
	// Row i's box; empty while it is scrolled out.
	Box row(int i) const {
		if (i < scroll || i >= scroll + visible || i >= rows)
			return Box{ 0.f, 0.f, 0.f, 0.f };
		const Box area = rowsArea();
		return Box{ 0.f, area.y + lineH() * (i - scroll), rowWidth(), lineH() };
	}
	// A row is its label and then its control. A heading is all label, an
	// action all control (a button carrying the label).
	Box control(const Row& r, int i) const {
		const Box b = row(i);
		const float cw = b.w * controlShare(r.kind);
		return Box{ b.x + b.w - cw, b.y, cw, b.h };
	}
	Box label(const Row& r, int i) const {
		const Box b = row(i);
		return Box{ b.x, b.y, b.w - b.w * controlShare(r.kind), b.h };
	}
	// Cell n of a mask row's control; the cells tile it, cell 0 on the left.
	Box cell(const Row& r, int i, int n) const {
		const Box c = control(r, i);
		const int cells = cellCount(r);
		if (n < 0 || n >= cells)
			return Box{ 0.f, 0.f, 0.f, 0.f };
		return Box{ c.x + c.w * n / cells, c.y, c.w / cells, c.h };
	}
	// The cell of mask row i nearest x (a drag may leave the control); -1
	// for a row with no cells.
	int cellAt(const Row& r, int i, float x) const {
		const int cells = cellCount(r);
		const Box c = control(r, i);
		if (cells <= 0 || c.w <= 0.f)
			return -1;
		const int n = x <= c.x ? 0 : (int) ((x - c.x) / c.w * cells);
		return n >= cells ? cells - 1 : n;
	}

	Hit hit(const FriendlyModel& m, float x, float y) const {
		const Hit none = { Part::None, -1, -1 };
		if (!(x >= 0.f && x < w && y >= 0.f && y < h))
			return none;
		if (y < headerH())
			return Hit{ Part::Header, -1, -1 };
		int n = (int) ((y - headerH()) / lineH());
		if (n >= kLines)
			n = kLines - 1;
		if (busy && n == 0)
			return Hit{ Part::Banner, -1, -1 };
		if (tabs && n == (busy ? 1 : 0)) {
			const int t = (int) (x / w * tabs);
			return Hit{ Part::Tab, t >= tabs ? tabs - 1 : t, -1 };
		}
		if (scrollable() && x >= scrollbar().x)
			return Hit{ Part::Scrollbar, -1, -1 };
		const int i = scroll + n - (kLines - visible);
		if (i < 0 || i >= rows)
			return none;
		const Row& r = m.row[i];
		if (r.kind == RowKind::Heading)
			return none;
		if (x < control(r, i).x)
			return Hit{ Part::Label, i, -1 };
		if (r.kind == RowKind::Mask && cellCount(r) > 0)
			return Hit{ Part::Cell, i, cellAt(r, i, x) };
		return Hit{ Part::Control, i, -1 };
	}

	// A mask row's cells: Row::max, at most the 32 bits of its value.
	static int cellCount(const Row& r) {
		return r.kind != RowKind::Mask || r.max <= 0 ? 0 : r.max > 32 ? 32 : (int) r.max;
	}

private:
	static float controlShare(RowKind kind) {
		return kind == RowKind::Heading ? 0.f : kind == RowKind::Action ? 1.f : kind == RowKind::Toggle ? kToggleShare : kControlShare;
	}
	float thumbH(const Box& bar) const {
		const float share = bar.h * visible / rows;
		return share < lineH() ? lineH() : share;
	}
};

// --- Sliders. A bar of width w shows min at its left edge and max at its right. ---

inline int32_t sliderValue(float x, float w, int32_t min, int32_t max) {
	if (max <= min || w <= 0.f || x <= 0.f)
		return min;
	if (x >= w)
		return max;
	const double span = (double) max - (double) min;
	const double v = (double) min + (double) (int64_t) ((double) x / (double) w * span + 0.5);
	return v >= (double) max ? max : (int32_t) v;
}

inline float sliderX(int32_t value, float w, int32_t min, int32_t max) {
	if (max <= min || w <= 0.f || value <= min)
		return 0.f;
	if (value >= max)
		return w;
	return (float) (((double) value - (double) min) / ((double) max - (double) min) * (double) w);
}

// Drag speeds: plain, fine (Ctrl/Cmd) and finer (Ctrl/Cmd + Shift), as
// Rack's knobs.
constexpr float kFine = 0.25f, kFiner = 0.0625f;

// A slider drag: starts where the value is and follows the pointer's travel
// times the speed. The ends hold it, so travel past one is not owed back.
struct SliderDrag {
	float x = 0.f;

	void begin(int32_t value, float w, int32_t min, int32_t max) {
		x = sliderX(value, w, min, max);
	}
	// The pointer moved dx: the value now.
	int32_t move(float dx, float speed, float w, int32_t min, int32_t max) {
		x += dx * speed;
		if (x < 0.f)
			x = 0.f;
		if (x > w)
			x = w;
		return sliderValue(x, w, min, max);
	}
};

// --- Mask painting: a press toggles one cell, and dragging on gives every
// cell crossed the state that one was given. Each cell is decided once per
// drag, so a model that has not caught up is not toggled twice. ---

struct MaskPaint {
	bool active = false;
	bool on = false;       // the state being painted
	uint32_t done = 0;     // cells decided in this drag
	int last = -1;

	// A press on `cell` of a row whose value is `mask`. True: toggle it.
	bool begin(uint32_t mask, int cell) {
		active = cell >= 0 && cell < 32;
		if (!active)
			return false;
		on = !((mask >> cell) & 1u);
		done = 1u << cell;
		last = cell;
		return true;
	}
	// The pointer is now over `cell`: fills `toggle` (room for 32) with the
	// cells between the last one and this one that need toggling; the count.
	int enter(uint32_t mask, int cell, int* toggle) {
		if (!active || cell < 0 || cell >= 32)
			return 0;
		int n = 0;
		const int step = cell > last ? 1 : -1;
		for (int c = last; c != cell;) {
			c += step;
			if (done & (1u << c))
				continue;
			done |= 1u << c;
			if ((((mask >> c) & 1u) != 0) != on)
				toggle[n++] = c;
		}
		last = cell;
		return n;
	}
};

// --- Edits waiting for the host. Host::postEdit and Host::post return false
// while the host's queue is full; what they refuse waits here and is offered
// again every frame, so the last value of a drag, the last tab and the last
// app chosen are never lost. ---

struct Outbox {
	enum { kSlots = 16 };
	FriendlyEdit edits[kSlots];
	int count = 0;
	int app = -1;   // an app selection waiting; goes before the edits

	bool empty() const { return count == 0 && app < 0; }

	// Queue an edit. A SetValue for a row that already has one waiting, and
	// a SelectTab when one is waiting, replace it in place: only the latest
	// target matters. False: no room, the edit is dropped.
	bool add(const FriendlyEdit& e) {
		if (e.type == FriendlyEdit::SetValue || e.type == FriendlyEdit::SelectTab) {
			for (int i = 0; i < count; i++) {
				const FriendlyEdit& o = edits[i];
				if (o.type != e.type || o.app != e.app)
					continue;
				if (e.type == FriendlyEdit::SelectTab || (o.tab == e.tab && o.id == e.id)) {
					edits[i] = e;
					return true;
				}
			}
		}
		if (count >= kSlots)
			return false;
		edits[count++] = e;
		return true;
	}

	// Choose an app. Edits still waiting were drawn for the app being left,
	// and the core would drop them: they go.
	void selectApp(int index) {
		app = index;
		count = 0;
	}

	// Offer everything, in order, to postApp(int) and postEdit(const
	// FriendlyEdit&), each returning whether it took it; stops at the first
	// refusal and keeps the rest.
	template <typename PostApp, typename PostEdit>
	void flush(PostApp postApp, PostEdit postEdit) {
		if (app >= 0) {
			if (!postApp(app))
				return;
			app = -1;
		}
		int sent = 0;
		while (sent < count && postEdit(edits[sent]))
			sent++;
		for (int i = sent; i < count; i++)
			edits[i - sent] = edits[i];
		count -= sent;
	}
};

// --- Reading a model. ---

// The index of the row with this id; -1 if the list does not have it.
inline int findRow(const FriendlyModel& m, int id) {
	for (int i = 0; i < m.rows && i < kMaxRows; i++)
		if (m.row[i].id == id)
			return i;
	return -1;
}

// The row the firmware's menu cursor is on; -1 if none is.
inline int cursorRow(const FriendlyModel& m) {
	for (int i = 0; i < m.rows && i < kMaxRows; i++)
		if (m.row[i].flags & kRowCursor)
			return i;
	return -1;
}

// An edit for the app and tab model `m` shows, so that the core drops it if
// something else is there by the time it arrives.
inline FriendlyEdit editFor(const FriendlyModel& m, FriendlyEdit::Type type, int id, int32_t value) {
	FriendlyEdit e = {};
	e.type = type;
	e.app = m.app;
	e.tab = m.tab;
	e.id = (int16_t) id;
	e.value = value;
	return e;
}

// A Choice row's name for `value`; null where the firmware has none.
inline const char* choiceName(const Row& r, int32_t value) {
	const int64_t i = (int64_t) value - r.min;
	return r.names && i >= 0 && i < r.nameCount ? r.names[i] : nullptr;
}

// What a row shows for `value`: the firmware's own text when it has one for
// the row's current value, else the name (Choice), on or off (Toggle), or
// the number.
inline void valueText(const Row& r, int32_t value, char* out, size_t size) {
	if (size == 0)
		return;
	if (r.text[0] && value == r.value) {
		// Row::text may fill its buffer with no terminator.
		std::snprintf(out, size, "%.*s", kRowText, r.text);
		return;
	}
	const char* name = r.kind == RowKind::Choice ? choiceName(r, value)
		: r.kind == RowKind::Toggle ? (value ? "on" : "off") : nullptr;
	if (name)
		std::snprintf(out, size, "%s", name);
	else
		std::snprintf(out, size, "%ld", (long) value);
}

}
}
