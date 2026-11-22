// The window's mouse (src/eddy/Select.hpp, src/TapeGesture.hpp): what a drag
// in Eddy's window makes of the START and LENGTH knobs. Where the window
// lands is read back through the engine's own law (Laws.hpp, window()), in
// frames at 48 kHz, not through Select.hpp. Built with the plugin's
// floating-point flags.
#include "eddy/Select.hpp"
#include "testing.hpp"

#include <cmath>

namespace {

using namespace rka;
using namespace rka::eddy;

const float NAN_F = std::nanf("");
const float INF_F = INFINITY;
const float FS = 48000.f;

// The window the engine makes of the knobs, as shares of the tape: start,
// length and whether it wraps.
struct Heard {
	double start, length;
	bool wraps;
};
Heard heard(WindowKnobs k, float cvShare, double seconds) {
	const double frames = std::floor(seconds * FS + 0.5);
	const Window w = window(k.start, cvShare * START_CV_VOLTS, k.length, 0.f, frames, 0.0, FS);
	Heard h = {w.start / frames, w.length / frames, w.start + w.length > frames};
	return h;
}

const double TAPES[] = {0.05, 0.1, 0.5, 1.0, 7.3, 60.0, 300.0};
const double LONG_TAPES[] = {1.0, 7.3, 60.0, 300.0};   // the column test's range
const double COLUMN = 1.0 / 512.0;

// The inverse LENGTH law against the forward one: the engine's window() and
// lengthShare, relative to the length; the ends exactly.
void inverse_length() {
	double worst = 0.0, worstLaw = 0.0, worstKnob = 0.0;
	for (double T : TAPES) {
		const double m = LENGTH_MIN_S / T;
		for (int i = 0; i <= 400; i++) {
			const double share = m * std::pow(1.0 / m, i / 400.0);   // 10 ms to the tape, evenly in log
			const float k = lengthKnobFor(share, T);
			const double back = lengthShare(k, T);
			worst = std::max(worst, std::fabs(back / share - 1.0));
			// The engine's law, in frames (an independent path).
			WindowKnobs kn;
			kn.start = 0.f;
			kn.length = k;
			worstLaw = std::max(worstLaw, std::fabs(heard(kn, 0.f, T).length / share - 1.0));
		}
		CHECK(lengthKnobFor(m, T) == 0.f && lengthKnobFor(m * 0.5, T) == 0.f, "%g s: 10 ms and less is knob 0", T);
		CHECK(lengthKnobFor(1.0, T) == 1.f && lengthKnobFor(1.5, T) == 1.f, "%g s: the whole tape is knob 1", T);
		CHECK(lengthShare(0.f, T) == m && lengthShare(1.f, T) == 1.0, "%g s: the forward law's ends (%g, %g)", T, lengthShare(0.f, T),
		      lengthShare(1.f, T));
		// Knob to share and back.
		for (int i = 0; i <= 100; i++) {
			const float k = (float) i / 100.f;
			worstKnob = std::max(worstKnob, (double) std::fabs(lengthKnobFor(lengthShare(k, T), T) - k));
		}
	}
	CHECK(worst <= 2e-6, "inverse then forward (lengthShare): worst %.3g of the length (want <= 2e-6)", worst);
	CHECK(worstLaw <= 2e-6, "inverse then the engine's law: worst %.3g of the length (want <= 2e-6)", worstLaw);
	CHECK(worstKnob <= 2e-6, "knob, forward, inverse: worst %.3g (want <= 2e-6)", worstKnob);
	// A tape of 10 ms or less: any knob is the whole tape, so 1.
	CHECK(lengthKnobFor(0.5, 0.01) == 1.f && lengthKnobFor(0.5, 0.004) == 1.f && lengthShare(0.3f, 0.004) == 1.0, "a 10 ms tape");
	std::printf("  inverse LENGTH law: worst %.2g (Select), %.2g (engine's law), knob %.2g\n", worst, worstLaw, worstKnob);
}

// A new selection: either way round the same, to the bit; start and end
// where the mouse was, within a column, as the engine reads the knobs.
void new_selection() {
	const float pairs[][2] = {{0.1f, 0.6f}, {0.37f, 0.3701f}, {0.f, 1.f}, {0.5f, 0.98f}, {0.02f, 0.021f}, {0.25f, 0.999f}, {0.4f, 1.f}};
	bool same = true, onMouse = true;
	double worst = 0.0;
	for (double T : LONG_TAPES)
		for (const auto& pr : pairs) {
			WindowKnobs before;
			before.start = 0.44f;
			before.length = 0.3f;
			const WindowKnobs a = dragWindow(GRAB_NEW, pr[0], pr[1], before, 0.f, T);
			const WindowKnobs b = dragWindow(GRAB_NEW, pr[1], pr[0], before, 0.f, T);
			// Under 10 ms it is 10 ms from the press, so that way round counts.
			if ((double) pr[1] - pr[0] >= LENGTH_MIN_S / T)
				same = same && a.start == b.start && a.length == b.length;
			const Heard h = heard(a, 0.f, T);
			const double want = std::max((double) pr[1] - pr[0], LENGTH_MIN_S / T);
			const double es = std::fabs(h.start - pr[0]), ee = std::fabs(h.start + h.length - (pr[0] + want));
			worst = std::max(worst, std::max(es, ee));
			onMouse = onMouse && es <= COLUMN && ee <= COLUMN && !h.wraps;
		}
	CHECK(same, "a new selection is the same either way round");
	CHECK(onMouse, "start and end on the mouse within a column (1/512), never wrapping: worst %.3g", worst);
	std::printf("  new selection: worst %.2g of the tape from the mouse (a column is %.2g)\n", worst, COLUMN);
}

// A selection under 10 ms: 10 ms, from the press toward the mouse; at the
// tape's end, back inside it.
void shortest() {
	const double T = 60.0, m = LENGTH_MIN_S / T;
	WindowKnobs before;
	const WindowKnobs r = dragWindow(GRAB_NEW, 0.5f, 0.5f + (float) (m / 4), before, 0.f, T);
	Heard h = heard(r, 0.f, T);
	CHECK(r.length == 0.f && std::fabs(h.start - 0.5) <= 1e-7, "rightward: 10 ms from the press (knob %g, start %g)", r.length, h.start);
	const WindowKnobs l = dragWindow(GRAB_NEW, 0.5f, 0.5f - (float) (m / 4), before, 0.f, T);
	h = heard(l, 0.f, T);
	CHECK(l.length == 0.f && std::fabs(h.start + h.length - 0.5) <= 1e-6, "leftward: 10 ms ending at the press (end %g)", h.start + h.length);
	const WindowKnobs e = dragWindow(GRAB_NEW, 1.f, 1.f, before, 0.f, T);
	h = heard(e, 0.f, T);
	CHECK(!h.wraps && std::fabs(h.length - m) <= 2e-6 && h.start + h.length <= 1.0 && h.start + h.length > 1.0 - COLUMN,
	      "at the tape's end: 10 ms inside it (start %g, length %g)", h.start, h.length);
}

// The start line taken: the end stays (within 2e-6 of the tape); the end
// line taken: START untouched to the bit.
void move_ends() {
	double worst = 0.0;
	bool startKept = true, moved = true;
	for (double T : LONG_TAPES) {
		WindowKnobs before;
		before.start = 0.3f;
		before.length = lengthKnobFor(0.4, T);   // 0.3 .. 0.7
		const Heard h0 = heard(before, 0.f, T);
		const double end0 = h0.start + h0.length;
		for (float d : {-0.2f, -0.05f, 0.f, 0.01f, 0.2f, 0.35f}) {
			// Pressed off the line (4 px is about 0.05 of the glass): the
			// line moves by the mouse's travel, not to the mouse.
			const WindowKnobs s = dragWindow(GRAB_START, 0.33f, 0.33f + d, before, 0.f, T);
			const Heard hs = heard(s, 0.f, T);
			worst = std::max(worst, std::fabs(hs.start + hs.length - end0));
			moved = moved && std::fabs(hs.start - (0.3 + d)) <= COLUMN;
			const WindowKnobs e = dragWindow(GRAB_END, 0.67f, 0.67f + d * 0.5f, before, 0.f, T);
			startKept = startKept && e.start == before.start;
			const Heard he = heard(e, 0.f, T);
			moved = moved && std::fabs(he.start + he.length - (end0 + d * 0.5)) <= COLUMN;
		}
	}
	// START CV holding the window at the tape's start: the end line taken
	// still leaves the knob alone (it is not set to what the CV makes 0).
	{
		WindowKnobs b;
		b.start = 0.1f;
		b.length = lengthKnobFor(0.2, 10.0);
		const WindowKnobs e = dragWindow(GRAB_END, 0.2f, 0.3f, b, -0.3f, 10.0);
		startKept = startKept && e.start == 0.1f;
	}
	CHECK(worst <= 2e-6, "the start line taken: the end stays (worst %.3g of the tape)", worst);
	CHECK(startKept, "the end line taken: START untouched, to the bit");
	CHECK(moved, "the taken line follows the mouse's travel within a column");
	std::printf("  start line taken: the end moves at most %.2g of the tape\n", worst);
}

// An end dragged past the other swaps: never negative, never NaN.
void cross_over() {
	const double T = 10.0;
	WindowKnobs before;
	before.start = 0.4f;
	before.length = lengthKnobFor(0.2, T);   // 0.4 .. 0.6
	// The end line dragged to 0.1: the window is 0.1 .. 0.4.
	WindowKnobs k = dragWindow(GRAB_END, 0.6f, 0.1f, before, 0.f, T);
	Heard h = heard(k, 0.f, T);
	CHECK(std::fabs(h.start - 0.1) <= COLUMN && std::fabs(h.start + h.length - 0.4) <= COLUMN, "end past start: %g .. %g (want 0.1 .. 0.4)",
	      h.start, h.start + h.length);
	// The start line dragged to 0.9: the window is 0.6 .. 0.9.
	k = dragWindow(GRAB_START, 0.4f, 0.9f, before, 0.f, T);
	h = heard(k, 0.f, T);
	CHECK(std::fabs(h.start - 0.6) <= COLUMN && std::fabs(h.start + h.length - 0.9) <= COLUMN, "start past end: %g .. %g (want 0.6 .. 0.9)",
	      h.start, h.start + h.length);
	// Swept across the other end, every step a positive length.
	bool ok = true;
	for (int i = 0; i <= 1000; i++) {
		const float x = (float) i / 1000.f;
		for (TapeGrab g : {GRAB_START, GRAB_END}) {
			const WindowKnobs r = dragWindow(g, g == GRAB_START ? 0.4f : 0.6f, x, before, 0.f, T);
			const Heard q = heard(r, 0.f, T);
			ok = ok && std::isfinite(r.start) && std::isfinite(r.length) && q.length > 0.0 && !q.wraps;
		}
	}
	CHECK(ok, "swept across: finite, a positive length, never wrapping");
}

// A window that wraps (made by the knobs): either end taken, the other stays
// where it is drawn and the window is the stretch between it and the mouse.
void wrapped() {
	const double T = 10.0;
	WindowKnobs before;
	before.start = 0.8f;
	before.length = lengthKnobFor(0.3, T);   // 0.8 .. 1.1, the end drawn at 0.1
	WindowKnobs k = dragWindow(GRAB_END, 0.1f, 0.15f, before, 0.f, T);
	Heard h = heard(k, 0.f, T);
	CHECK(!h.wraps && std::fabs(h.start - 0.15) <= COLUMN && std::fabs(h.start + h.length - 0.8) <= COLUMN, "end taken: %g .. %g (want 0.15 .. 0.8)",
	      h.start, h.start + h.length);
	k = dragWindow(GRAB_START, 0.8f, 0.7f, before, 0.f, T);
	h = heard(k, 0.f, T);
	CHECK(!h.wraps && std::fabs(h.start - 0.1) <= COLUMN && std::fabs(h.start + h.length - 0.7) <= COLUMN, "start taken: %g .. %g (want 0.1 .. 0.7)",
	      h.start, h.start + h.length);
}

// START CV: knob + CV lands on the mouse, held to the knob's range.
void with_cv() {
	const double T = 30.0;
	WindowKnobs before;
	const float cv = 0.3f;   // 1.5 V
	WindowKnobs k = dragWindow(GRAB_NEW, 0.5f, 0.7f, before, cv, T);
	Heard h = heard(k, cv, T);
	CHECK(std::fabs(k.start - 0.2f) <= 1e-6f && std::fabs(h.start - 0.5) <= COLUMN && std::fabs(h.start + h.length - 0.7) <= COLUMN,
	      "CV 0.3: knob %g, window %g .. %g (want 0.5 .. 0.7)", k.start, h.start, h.start + h.length);
	k = dragWindow(GRAB_NEW, 0.1f, 0.3f, before, cv, T);
	CHECK(k.start == 0.f, "CV past the mouse: the knob is held at 0 (%g)", k.start);
	k = dragWindow(GRAB_NEW, 0.5f, 0.7f, before, -0.8f, T);
	CHECK(k.start == 1.f, "a CV of -0.8: the knob is held at 1 (%g)", k.start);
	// A NaN CV reads 0, as the engine reads it.
	k = dragWindow(GRAB_NEW, 0.5f, 0.7f, before, NAN_F, T);
	CHECK(k.start == 0.5f, "a NaN CV is 0: the knob at the mouse (%g)", k.start);
	// The start line taken under CV: the end stays.
	before.start = 0.1f;
	before.length = lengthKnobFor(0.3, T);   // with the CV: 0.4 .. 0.7
	const Heard h0 = heard(before, cv, T);
	k = dragWindow(GRAB_START, 0.4f, 0.5f, before, cv, T);
	h = heard(k, cv, T);
	CHECK(std::fabs(h.start - 0.5) <= COLUMN && std::fabs(h.start + h.length - (h0.start + h0.length)) <= 2e-6,
	      "CV, start taken: %g .. %g (want 0.5 .. %g)", h.start, h.start + h.length, h0.start + h0.length);
}

// Any input NaN, infinite or out of range: finite knobs inside 0..1.
void hostile() {
	const float junk[] = {NAN_F, INF_F, -INF_F, -0.5f, 1.5f, 0.f, 1.f, 0.3f, 1e30f};
	const double tapes[] = {0.0, -1.0, NAN, INFINITY, 1e-9, 0.004, 0.01, 10.0, 1e12};
	bool ok = true;
	long n = 0;
	for (int g = 0; g < 3; g++)
		for (float p : junk)
			for (float x : junk)
				for (float ks : junk)
					for (float kl : {NAN_F, 0.f, 1.f, 2.f})
						for (float cv : {NAN_F, INF_F, -INF_F, 0.f, 0.3f})
							for (double T : tapes) {
								WindowKnobs b;
								b.start = ks;
								b.length = kl;
								const WindowKnobs r = dragWindow((TapeGrab) g, p, x, b, cv, T);
								ok = ok && r.start >= 0.f && r.start <= 1.f && r.length >= 0.f && r.length <= 1.f;
								n++;
							}
	CHECK(ok, "%ld hostile drags: finite knobs inside 0..1", n);
	WindowKnobs b;
	b.start = 0.25f;
	b.length = 0.6f;
	const WindowKnobs r = dragWindow(GRAB_NEW, 0.1f, 0.9f, b, 0.f, 0.0);
	CHECK(r.start == 0.25f && r.length == 0.6f, "no tape: the knobs as they were");
	for (double T : {(double) NAN, -1.0, 0.0, (double) INFINITY})
		for (double s : {0.5, (double) NAN, -1.0, 2.0}) {
			const float k = lengthKnobFor(s, T);
			ok = ok && k >= 0.f && k <= 1.f;
			const double sh = lengthShare(NAN_F, T);
			ok = ok && sh > 0.0 && sh <= 1.0;
		}
	CHECK(ok, "lengthKnobFor and lengthShare on hostile tapes and shares");
}

// Which end a press takes, and when it is a drag.
void grabs() {
	CHECK(tapeGrabAt(50.f, 52.f, 120.f) == GRAB_START && tapeGrabAt(56.f, 52.f, 120.f) == GRAB_START, "within 4 px of the start");
	CHECK(tapeGrabAt(57.f, 52.f, 120.f) == GRAB_NEW && tapeGrabAt(80.f, 52.f, 120.f) == GRAB_NEW, "past 4 px: a new selection");
	CHECK(tapeGrabAt(117.f, 52.f, 120.f) == GRAB_END && tapeGrabAt(124.f, 52.f, 120.f) == GRAB_END, "within 4 px of the end");
	CHECK(tapeGrabAt(60.f, 57.f, 62.f) == GRAB_END && tapeGrabAt(58.f, 57.f, 62.f) == GRAB_START, "two lines near: the nearer");
	CHECK(tapeGrabAt(61.f, 60.f, 60.f) == GRAB_END && tapeGrabAt(59.f, 60.f, 60.f) == GRAB_START && tapeGrabAt(60.f, 60.f, 60.f) == GRAB_START,
	      "lines that meet: by the side of the press");
	CHECK(tapeGrabAt(20.f, 100.f, 21.f) == GRAB_END, "a wrapped window: its end line left of its start");
	CHECK(tapeGrabAt(NAN_F, 10.f, 20.f) == GRAB_NEW && tapeGrabAt(10.f, NAN_F, NAN_F) == GRAB_NEW, "NaN: a new selection");
	CHECK(!tapeDragged(40.f, 41.9f) && !tapeDragged(40.f, 38.1f) && tapeDragged(40.f, 42.f) && tapeDragged(40.f, 37.f), "a drag from 2 px");
	CHECK(!tapeDragged(40.f, NAN_F), "NaN is no drag");
}

// A gesture followed (TapeDrag): a click, a drag, the window stopping
// being usable mid-drag (recording begins, the tape cleared or replaced),
// and a release that never came.
void gestures() {
	int a = 0, b = 0;
	const void* A = &a;
	const void* B = &b;
	{
		TapeDrag d;
		CHECK(!d.press(40.f, true, A, 10.f, 90.f) && d.grab == GRAB_NEW, "a press in the middle: a new selection");
		CHECK(d.move(41.f, true, A) == TapeDrag::NOTHING && d.release() == false, "a click (1 px): nothing, and no end");
	}
	{
		TapeDrag d;
		d.press(12.f, true, A, 10.f, 90.f);
		CHECK(d.grab == GRAB_START, "a press by the start line takes it");
		CHECK(d.move(13.f, true, A) == TapeDrag::NOTHING && d.move(15.f, true, A) == TapeDrag::BEGIN && d.move(30.f, true, A) == TapeDrag::SELECT &&
		          d.move(30.f, true, A) == TapeDrag::SELECT,
		      "a drag: nothing under 2 px, then begin once, then select");
		CHECK(d.release() && !d.release(), "its release ends it, once");
	}
	// Recording begins (or the tape is emptied) mid-drag: no more selects,
	// not even when the window is usable again; the release still ends it.
	{
		TapeDrag d;
		d.press(40.f, true, A, 10.f, 90.f);
		d.move(50.f, true, A);
		CHECK(d.move(60.f, false, A) == TapeDrag::NOTHING && d.stopped, "recording begins: the drag stops");
		CHECK(d.move(70.f, true, A) == TapeDrag::NOTHING, "and does not resume when the window is usable again");
		CHECK(d.release(), "its release still ends it (its undo step)");
	}
	// The tape replaced mid-drag: stops; the old tape back: still stopped.
	{
		TapeDrag d;
		d.press(40.f, true, A, 10.f, 90.f);
		d.move(50.f, true, A);
		CHECK(d.move(60.f, true, B) == TapeDrag::NOTHING && d.move(60.f, true, A) == TapeDrag::NOTHING, "a new tape: the drag stops for good");
		CHECK(d.release(), "and its release ends it");
	}
	// Unusable before it became a drag: nothing to end.
	{
		TapeDrag d;
		d.press(40.f, true, A, 10.f, 90.f);
		CHECK(d.move(41.f, false, A) == TapeDrag::NOTHING && d.move(60.f, true, A) == TapeDrag::NOTHING && !d.release(),
		      "stopped before a drag: never begins, nothing to end");
	}
	// Pressed on an empty tape or while recording: nothing at all.
	{
		TapeDrag d;
		CHECK(!d.press(40.f, false, A, 10.f, 90.f) && d.move(80.f, true, A) == TapeDrag::NOTHING && !d.release(), "an unusable press: nothing");
	}
	// The release lost: the next press reports the drag still open, once.
	{
		TapeDrag d;
		d.press(40.f, true, A, 10.f, 90.f);
		d.move(60.f, true, A);
		CHECK(d.press(20.f, true, A, 10.f, 90.f), "a press after a lost release: end the open drag first");
		CHECK(!d.dragging && d.move(30.f, true, A) == TapeDrag::BEGIN, "then the new press is followed as its own");
		CHECK(d.release() && !d.press(50.f, true, A, 10.f, 90.f), "a press after a release: nothing open");
		d.move(50.f, false, A);   // stopped mid-press, no drag
		CHECK(!d.press(50.f, true, A, 10.f, 90.f), "a stopped click left nothing open");
		d.move(60.f, true, A);
		d.move(70.f, false, A);   // a drag, stopped
		CHECK(d.press(50.f, true, A, 10.f, 90.f), "a stopped drag whose release was lost is ended at the next press");
	}
}

} // namespace

int main() {
	inverse_length();
	new_selection();
	shortest();
	move_ends();
	cross_over();
	wrapped();
	with_cv();
	hostile();
	grabs();
	gestures();
	return rkatest::finish("eddy test_select");
}
