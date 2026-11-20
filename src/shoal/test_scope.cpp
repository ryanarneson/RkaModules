// shoal::scope (ScopeLayout.hpp): where the upper screen's lanes, dividers,
// letters, traces, plots, circle and grid go.
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "ScopeLayout.hpp"
#include "testing.hpp"

using shoal::Lanes;
using shoal::ScopeView;
using shoal::ViewKind;
using namespace shoal::scope;

namespace {

// The upper screen in firmware pixels, and at Rack's 100 % zoom.
const float kSizes[][2] = { { 128.f, 64.f }, { 245.67f, 122.83f } };
const float kEps = 1e-3f;

bool near(float a, float b) {
	return std::fabs(a - b) <= kEps;
}

bool overlap(const Box& a, const Box& b) {
	return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps && b.y < a.bottom() - kEps;
}

bool inside(const Box& b, const Box& outer) {
	return b.x >= outer.x - kEps && b.y >= outer.y - kEps && b.right() <= outer.right() + kEps && b.bottom() <= outer.bottom() + kEps;
}

bool within(const Point& p, const Box& b) {
	return p.x >= b.x - kEps && p.x <= b.right() + kEps && p.y >= b.y - kEps && p.y <= b.bottom() + kEps;
}

ScopeView make(ViewKind kind, Lanes lanes, float lo = 0.f, float hi = 0.f) {
	ScopeView v = ScopeView();
	v.kind = kind;
	v.lanes = lanes;
	v.laneLo = lo;
	v.laneHi = hi;
	return v;
}

// The design's table: every arrangement, with the letters it shows.
void lanes_tile_the_screen() {
	const struct {
		const char* name;
		ScopeView v;
		int count;
		const char* labels[4];
		float coverH;   // share of the height the lanes cover
		bool rows;
	} cases[] = {
		{"traces", make(ViewKind::Traces, Lanes::Rows4, 0.f, 5.f), 4, {"A", "B", "C", "D"}, 1.f, true},
		{"X/Y", make(ViewKind::XY, Lanes::Halves), 2, {"A \xC3\x97 B", "C \xC3\x97 D"}, 1.f, false},
		{"grid and circle", make(ViewKind::GridCircle, Lanes::None), 0, {}, 0.f, false},
		{"circle and text", make(ViewKind::CircleText, Lanes::None), 0, {}, 0.f, false},
		{"pixel columns", make(ViewKind::Pixels, Lanes::Columns4), 4, {"A", "B", "C", "D"}, 1.f, false},
		{"pixel columns over text", make(ViewKind::Pixels, Lanes::Columns4Top), 4, {"A", "B", "C", "D"}, 44.f / 64.f, false},
		{"pixel halves", make(ViewKind::Pixels, Lanes::Halves), 2, {"A", "B"}, 1.f, false},
		{"pixel bands", make(ViewKind::Pixels, Lanes::Rows4), 4, {"A", "B", "C", "D"}, 1.f, true},
		{"one picture", make(ViewKind::Pixels, Lanes::None), 0, {}, 0.f, false},
	};
	for (const auto& size : kSizes) {
		const float w = size[0], h = size[1];
		for (const auto& k : cases) {
			const Lanes4 l = lanes(k.v, w, h);
			CHECK(l.count == k.count, "%s: %d lanes, want %d", k.name, l.count, k.count);
			CHECK(l.dividers == (k.count ? k.count - 1 : 0), "%s: %d dividers", k.name, l.dividers);
			float area = 0.f;
			for (int i = 0; i < l.count; i++) {
				CHECK(inside(l.lane[i], Box{0.f, 0.f, w, h}), "%s: lane %d leaves the screen", k.name, i);
				for (int j = i + 1; j < l.count; j++)
					CHECK(!overlap(l.lane[i], l.lane[j]), "%s: lanes %d and %d overlap", k.name, i, j);
				area += l.lane[i].w * l.lane[i].h;
				CHECK(std::strcmp(l.label[i].text, k.labels[i]) == 0, "%s: lane %d is labelled \"%s\"", k.name, i, l.label[i].text);
				CHECK(within(l.label[i].at, l.lane[i]) && l.label[i].at.x + labelSize(h) <= l.lane[i].right() &&
				      l.label[i].at.y + labelSize(h) <= l.lane[i].bottom(), "%s: label %d is outside its lane", k.name, i);
				// References prints its values along the top of its columns:
				// its letters go at the foot of each, clear of them.
				CHECK((l.label[i].at.y > l.lane[i].y + l.lane[i].h * 0.6f) == (k.v.lanes == Lanes::Columns4Top),
				      "%s: label %d is at the %s of its lane", k.name, i, l.label[i].at.y > l.lane[i].y + l.lane[i].h * 0.6f ? "foot" : "top");
				// In order: left to right, or top to bottom.
				if (i > 0)
					CHECK(k.rows ? near(l.lane[i].y, l.lane[i - 1].bottom()) && near(l.lane[i].x, l.lane[i - 1].x)
					             : near(l.lane[i].x, l.lane[i - 1].right()) && near(l.lane[i].y, l.lane[i - 1].y),
					      "%s: lane %d does not follow lane %d", k.name, i, i - 1);
			}
			if (l.count)
				CHECK(std::fabs(area - w * h * k.coverH) < w * h * 1e-4f, "%s: the lanes cover %g of %g", k.name, area, w * h * k.coverH);
			for (int d = 0; d < l.dividers; d++) {
				const Line& ln = l.divider[d];
				const Box& a = l.lane[d];
				if (k.rows) {
					CHECK(near(ln.a.y, a.bottom()) && near(ln.b.y, a.bottom()), "%s: divider %d is not on the lanes' edge", k.name, d);
					CHECK(ln.a.x >= h / 64.f - kEps && ln.b.x <= w - h / 64.f + kEps && ln.b.x - ln.a.x > w * 0.9f,
					      "%s: divider %d runs %g to %g", k.name, d, ln.a.x, ln.b.x);
				}
				else {
					CHECK(near(ln.a.x, a.right()) && near(ln.b.x, a.right()), "%s: divider %d is not on the lanes' edge", k.name, d);
					CHECK(ln.a.y >= h / 64.f - kEps && ln.b.y <= a.bottom() - h / 64.f + kEps && ln.b.y - ln.a.y > a.h * 0.9f,
					      "%s: divider %d runs %g to %g", k.name, d, ln.a.y, ln.b.y);
				}
			}
		}
	}
}

// A trace stays in its own lane whatever the voltage, newest at the right.
void trace_clips_to_lane() {
	for (const auto& size : kSizes) {
		const float w = size[0], h = size[1];
		const struct { float lo, hi; } ranges[] = {{0.f, 5.f}, {0.f, 6.25f}, {-3.75f, 6.25f}};
		for (const auto& r : ranges) {
			const ScopeView v = make(ViewKind::Traces, Lanes::Rows4, r.lo, r.hi);
			const Lanes4 l = lanes(v, w, h);
			for (int i = 0; i < 4; i++) {
				const Box& lane = l.lane[i];
				const int n = shoal::kTraceColumns;
				CHECK(near(tracePoint(lane, v, n - 1, n, r.lo).y, lane.bottom() - traceMargin(lane)), "lane %d: %g V is not at the bottom", i, r.lo);
				CHECK(near(tracePoint(lane, v, n - 1, n, r.hi).y, lane.y + traceMargin(lane)), "lane %d: %g V is not at the top", i, r.hi);
				const float mid = (r.lo + r.hi) / 2.f;
				CHECK(near(tracePoint(lane, v, n - 1, n, mid).y, lane.y + lane.h / 2.f), "lane %d: the middle voltage is off centre", i);
				CHECK(near(traceZeroY(lane, v), tracePoint(lane, v, 0, n, 0.f).y), "lane %d: the 0 V line is not where 0 V plots", i);
				for (float volts : {-10.f, -3.75f, 6.25f, 10.f, 100.f}) {
					const Point p = tracePoint(lane, v, 5, n, volts);
					CHECK(p.y >= lane.y + traceMargin(lane) - kEps && p.y <= lane.bottom() - traceMargin(lane) + kEps,
					      "lane %d: %g V plots at %g, outside %g to %g", i, volts, p.y, lane.y, lane.bottom());
				}
				CHECK(near(tracePoint(lane, v, n - 1, n, 1.f).x, lane.right()), "lane %d: the newest column is not at the right edge", i);
				CHECK(tracePoint(lane, v, 0, n, 1.f).x >= l.label[i].at.x + labelSize(h) - kEps, "lane %d: the oldest column runs under the letter", i);
				// A history still filling ends at the right edge too, at the same pitch.
				CHECK(near(tracePoint(lane, v, 9, 10, 1.f).x, lane.right()), "lane %d: 10 columns do not end at the right", i);
				CHECK(near(tracePoint(lane, v, 9, 10, 1.f).x - tracePoint(lane, v, 8, 10, 1.f).x,
				           tracePoint(lane, v, n - 1, n, 1.f).x - tracePoint(lane, v, n - 2, n, 1.f).x), "lane %d: column pitch changes with the count", i);
			}
		}
	}
}

// Higher voltage is to the right and up; the whole output range fits.
void xy_orientation() {
	for (const auto& size : kSizes) {
		const float w = size[0], h = size[1];
		const Lanes4 l = lanes(make(ViewKind::XY, Lanes::Halves), w, h);
		for (int i = 0; i < 2; i++) {
			const Box& half = l.lane[i];
			const Point c = xyPoint(half, 1.25f, 1.25f);
			CHECK(near(c.x, half.x + half.w / 2.f) && near(c.y, half.y + half.h / 2.f), "half %d: mid-range is off centre", i);
			CHECK(xyPoint(half, 2.f, 0.f).x > xyPoint(half, 1.f, 0.f).x, "half %d: higher x volts is not to the right", i);
			CHECK(xyPoint(half, 0.f, 2.f).y < xyPoint(half, 0.f, 1.f).y, "half %d: higher y volts is not up", i);
			CHECK(within(xyPoint(half, -3.75f, -3.75f), half) && within(xyPoint(half, 6.25f, 6.25f), half), "half %d: the output range does not fit", i);
			// The same scale both ways: a circle of voltages is a circle.
			CHECK(near(xyPoint(half, 2.25f, 1.25f).x - c.x, c.y - xyPoint(half, 1.25f, 2.25f).y), "half %d: the axes differ in scale", i);
		}
	}
	const int n = shoal::kXyPoints;
	CHECK(near(xyAlpha(0, n), 0.15f) && near(xyAlpha(n - 1, n), 1.f), "alpha runs %g to %g", xyAlpha(0, n), xyAlpha(n - 1, n));
	bool rising = true;
	for (int i = 1; i < n; i++)
		rising = rising && xyAlpha(i, n) >= xyAlpha(i - 1, n);
	CHECK(rising, "alpha is not rising with newness");
	CHECK(near(xyAlpha(0, 1), 1.f), "a single point is not at full brightness");
}

void circle_and_grid() {
	const Point c = {50.f, 40.f};
	const float r = 20.f;
	const struct { int note; float dx, dy; } at[] = {{0, 0.f, -1.f}, {3, 1.f, 0.f}, {6, 0.f, 1.f}, {9, -1.f, 0.f}};
	for (const auto& k : at) {
		const Point p = circleNote(c, r, k.note);
		CHECK(std::fabs(p.x - (c.x + r * k.dx)) < 1e-3f && std::fabs(p.y - (c.y + r * k.dy)) < 1e-3f, "note %d is at %g, %g", k.note, p.x, p.y);
	}
	for (int note = 0; note < 12; note++) {
		const Point p = circleNote(c, r, note);
		CHECK(std::fabs(std::hypot(p.x - c.x, p.y - c.y) - r) < 1e-3f, "note %d is off the circle", note);
	}
	for (const auto& size : kSizes) {
		const float w = size[0], h = size[1];
		const Box screen = {0.f, 0.f, w, h};
		const Box grid = gridArea(w, h), circ = circleArea(w, h, true), circL = circleArea(w, h, false), text = textArea(w, h);
		CHECK(inside(grid, screen) && inside(circ, screen) && inside(circL, screen) && inside(text, screen), "an area leaves the screen");
		CHECK(!overlap(grid, circ) && !overlap(circL, text), "areas overlap");
		CHECK(near(grid.w, grid.h) && near(circ.w, circ.h) && near(circL.w, circL.h), "the grid and the circles are not square");
		const float pitch = gridCell(grid, 1, 0).x - gridCell(grid, 0, 0).x;
		for (int y = 0; y < 5; y++)
			for (int x = 0; x < 5; x++) {
				const Point p = gridCell(grid, x, y);
				CHECK(within(p, grid), "cell %d, %d is outside the grid's area", x, y);
				CHECK(near(p.x, gridCell(grid, 0, 0).x + pitch * x) && near(p.y, gridCell(grid, 0, 0).y + pitch * y), "cell %d, %d is off the pitch", x, y);
			}
		CHECK(pitch > 0.f && gridCell(grid, 0, 0).x - grid.x >= pitch / 2.f - kEps, "the first cell has no room for its disc");
	}
}

} // namespace

int main() {
	lanes_tile_the_screen();
	trace_clips_to_lane();
	xy_orientation();
	circle_and_grid();
	return rkatest::finish("shoal test_scope");
}
