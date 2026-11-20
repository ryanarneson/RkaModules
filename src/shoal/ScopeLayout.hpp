#pragma once
// Shoal's upper screen in friendly mode: where everything goes. Lanes,
// their dividers and letters for each arrangement; a voltage and a time to
// a point in a trace lane or an X/Y plot; the note circle and the grid.
// The Rack widget (ScopeScreen.hpp) only strokes what these return.
// Rack-free, C++11, tested offline (test_scope.cpp). Every size is a share
// of the screen's, so the view fills it at any zoom.
#include <cmath>

#include "ScopeView.hpp"

namespace shoal {
namespace scope {

struct Box {
	float x, y, w, h;
	float right() const { return x + w; }
	float bottom() const { return y + h; }
};
struct Point { float x, y; };
struct Line { Point a, b; };
struct Label {
	Point at;           // the text's top left
	const char* text;   // static storage, UTF-8
};

// One firmware pixel of a 64-row screen: the unit of the small distances.
inline float pixel(float h) { return h / 64.f; }
// The letters' height.
inline float labelSize(float h) { return h / 9.f; }
// How far a divider stops short of the screen's edge.
inline float dividerInset(float h) { return h / 48.f; }

struct Lanes4 {
	int count;          // 0: one picture
	Box lane[4];
	int dividers;       // count - 1, or 0
	Line divider[3];    // divider d lies between lanes d and d + 1
	Label label[4];
};

inline Lanes4 lanes(const ScopeView& v, float w, float h) {
	static const char* const kLetters[4] = {"A", "B", "C", "D"};
	static const char* const kPairs[2] = {"A \xC3\x97 B", "C \xC3\x97 D"};   // A x B, with a multiplication sign
	Lanes4 l = Lanes4();
	const float inset = dividerInset(h);
	switch (v.lanes) {
		case Lanes::Columns4:
		case Lanes::Columns4Top:
		case Lanes::Halves: {
			l.count = v.lanes == Lanes::Halves ? 2 : 4;
			// References prints two lines of text under its columns, from row 44.
			const float laneH = v.lanes == Lanes::Columns4Top ? h * 44.f / 64.f : h;
			for (int i = 0; i < l.count; i++) {
				l.lane[i] = Box{w * i / l.count, 0.f, w / l.count, laneH};
				l.label[i].text = v.lanes == Lanes::Halves ? (v.kind == ViewKind::XY ? kPairs[i] : kLetters[i]) : kLetters[i];
			}
			for (int d = 0; d + 1 < l.count; d++) {
				const float x = l.lane[d].right();
				l.divider[d] = Line{Point{x, inset}, Point{x, laneH - inset}};
			}
			break;
		}
		case Lanes::Rows4:
			l.count = 4;
			for (int i = 0; i < 4; i++) {
				l.lane[i] = Box{0.f, h * i / 4.f, w, h / 4.f};
				l.label[i].text = kLetters[i];
			}
			for (int d = 0; d < 3; d++) {
				const float y = l.lane[d].bottom();
				l.divider[d] = Line{Point{inset, y}, Point{w - inset, y}};
			}
			break;
		default:
			break;
	}
	l.dividers = l.count ? l.count - 1 : 0;
	for (int i = 0; i < l.count; i++)
		l.label[i].at = Point{l.lane[i].x + pixel(h) * 1.5f, l.lane[i].y + pixel(h)};
	return l;
}

// --- Traces (ViewKind::Traces, four rows). ---

// Clear space above and below a trace, so it never lies on a divider.
inline float traceMargin(const Box& lane) { return lane.h * 0.08f; }

// The left end of the trace: right of the lane's letter. (A row lane is a
// quarter of the screen's height.)
inline float traceLeft(const Box& lane) {
	const float h = lane.h * 4.f;
	return lane.x + pixel(h) * 3.f + labelSize(h);
}

// Column `column` of `columns` valid ones (oldest first) at `volts`. The
// newest column is at the lane's right edge, and columns are a fixed
// distance apart: a history still filling grows leftward from there. The
// voltage is clipped to the lane's range (v.laneLo at the bottom, v.laneHi
// at the top).
inline Point tracePoint(const Box& lane, const ScopeView& v, int column, int columns, float volts) {
	const float pitch = (lane.right() - traceLeft(lane)) / (kTraceColumns - 1);
	const float span = v.laneHi - v.laneLo;
	float t = span > 0.f ? (volts - v.laneLo) / span : 0.5f;
	t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
	const float m = traceMargin(lane);
	return Point{lane.right() - pitch * (columns - 1 - column), lane.bottom() - m - t * (lane.h - 2.f * m)};
}

// Where 0 V plots (the lane's bottom for the envelopes).
inline float traceZeroY(const Box& lane, const ScopeView& v) {
	return tracePoint(lane, v, 0, 1, 0.f).y;
}

// --- X/Y (ViewKind::XY, two halves). ---

// Higher voltage to the right and up, the same scale both ways, the output
// range (-3.75 V to +6.25 V, 10 V centred on +1.25 V) across 94 % of the
// half's smaller side.
inline Point xyPoint(const Box& half, float xVolts, float yVolts) {
	const float side = (half.w < half.h ? half.w : half.h) * 0.94f;
	const float perVolt = side / 10.f;
	return Point{half.x + half.w / 2.f + (xVolts - 1.25f) * perVolt, half.y + half.h / 2.f - (yVolts - 1.25f) * perVolt};
}

// Brightness of point `point` of `points` (oldest first): 15 % to 100 %.
inline float xyAlpha(int point, int points) {
	return points > 1 ? 0.15f + 0.85f * point / (points - 1) : 1.f;
}

// --- The note circle and the grid (GridCircle, CircleText). ---

// Pitch class 0 (C) at the top, clockwise by semitone.
inline Point circleNote(Point centre, float r, int pitchClass) {
	const float a = 6.283185307179586f * pitchClass / 12.f;
	return Point{centre.x + r * std::sin(a), centre.y - r * std::cos(a)};
}

// A square in the middle of the left or the right half of the screen.
inline Box halfSquare(float w, float h, bool right) {
	const float side = (w / 2.f < h ? w / 2.f : h) * 0.9f;
	return Box{w * (right ? 0.75f : 0.25f) - side / 2.f, (h - side) / 2.f, side, side};
}

// Automatonnetz: the grid on the left. Harrington 1200: the text on the right.
inline Box gridArea(float w, float h) { return halfSquare(w, h, false); }
// The circle is drawn inside this square, with room for its discs.
inline Box circleArea(float w, float h, bool right) { return halfSquare(w, h, right); }
inline Box textArea(float w, float h) {
	const float m = pixel(h) * 3.f;
	return Box{w / 2.f + m, m, w / 2.f - 2.f * m, h - 2.f * m};
}

// The centre of grid cell (x, y), 0 to kGridSide - 1; (0, 0) is top left.
inline Point gridCell(const Box& area, int x, int y) {
	const float pitch = area.w / kGridSide;
	return Point{area.x + pitch * (x + 0.5f), area.y + pitch * (y + 0.5f)};
}

} // namespace scope
} // namespace shoal
