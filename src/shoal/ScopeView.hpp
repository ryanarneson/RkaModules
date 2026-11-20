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
	// GridCircle, CircleText: the chord's pitch classes, 0 (C) to 11.
	uint8_t notes[3];
	// GridCircle: the last cells visited, newest first; column and row, 0 to 4.
	uint8_t pathX[kViewPath], pathY[kViewPath];
	// CircleText, as the firmware prints them: the last transforms, newest
	// first ("+P", "-L"), and the chord's notes with octaves ("C4", "D#3").
	char transforms[4][kViewText];
	char chord[3][kViewText];
};

}
