#pragma once
// Shoal's upper screen in friendly mode: what the current app's idle view
// shows, as plain data the panel can draw with its own lines
// (docs/specs/2026-11-19-shoal-scope-design.md). shoal::Core fills a
// ScopeView on request (Core::scopeView). Rack-free, C++11-compatible, no
// firmware header; no pointers, so a view may be copied to another thread.
#include <cstdint>

namespace shoal {

enum class ViewKind : uint8_t {
	Pixels,      // the firmware's own frame, with lane dividers and letters
	Traces,      // the four outputs over time, one lane each
	XY,          // output A against B, and C against D
	GridCircle,  // Automatonnetz: its path over the grid, and the note circle
	CircleText   // Harrington 1200: the note circle, the transforms and the chord
};

// How the view's area divides between the outputs.
enum class Lanes : uint8_t {
	None,         // one picture
	Columns4,     // A B C D, left to right
	Columns4Top,  // the same, over rows 0 to 43 of 64 only (text below)
	Halves,       // left and right
	Rows4         // A B C D, top to bottom
};

constexpr int kViewPath = 4, kViewText = 6, kGridSide = 5;

struct ScopeView {
	uint8_t app;                    // index into Core::appName
	ViewKind kind;
	Lanes lanes;
	// Traces: the voltages at the bottom and the top of every lane. Fixed
	// for the app, so a lane is filled by what the app puts out: 0 to +5 V
	// for Piqued's envelopes, not half of a +-5 V lane.
	float laneLo, laneHi;
	// GridCircle, CircleText: the chord's pitch classes, 0 (C) to 11.
	uint8_t notes[3];
	// GridCircle: the last cells visited, newest first; column and row, 0 to 4.
	uint8_t pathX[kViewPath], pathY[kViewPath];
	// CircleText, as the firmware prints them: the last transforms, newest
	// first ("+P", "-L"), and the chord's notes with octaves ("C4", "D#3").
	char transforms[4][kViewText];
	char chord[3][kViewText];
};

// The four outputs' recent past, recorded by shoal::Host on the engine
// thread and published with the frames. Oldest first; `columns` and
// `points` entries are valid (fewer than the arrays hold until they fill).
constexpr int kScopeChannels = 4;
constexpr int kTraceColumns = 256, kTraceColumnTicks = 260;   // 4 s of traces
constexpr int kXyPoints = 512, kXyPointTicks = 130;           // 4 s of X/Y
struct ScopeHistory {
	// Traces: each column is the lowest and highest output over its ticks,
	// so an event shorter than a column still shows.
	float lo[kScopeChannels][kTraceColumns], hi[kScopeChannels][kTraceColumns];
	// X/Y: plain samples.
	float xy[kScopeChannels][kXyPoints];
	int16_t columns, points;
};

}
