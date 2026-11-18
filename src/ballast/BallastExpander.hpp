#pragma once
// The interface a Ballast expander implements (spec:
// docs/specs/2026-11-17-ballast-design.md, "The expander connection").
//
// An expander is a module that sits to Ballast's right, in an unbroken chain
// of expanders. On every sample Ballast walks that chain (rightExpander,
// stopping at the first module that is not a BallastExpander) and calls each
// expander, in chain order, at three points in its signal path, all on the
// same sample:
//
//   preLevel(ch, l, r)        each channel's input pair, before its Level and
//                             CV (Offset);
//   postLevel(ch, l, r, mono) each channel's pair after Level and CV (Pan,
//                             Mute, Solo, Fade, send taps). mono is true when
//                             the channel's IN L is patched and IN R is not
//                             (r then holds IN L's copy);
//   intoMix(l, r)             the summed mix, CHAIN included, before Master
//                             (a send's return).
//
// ch runs 0 to 3. Every method defaults to doing nothing. Calls come from
// Ballast's process(), on the audio thread; an expander must not allocate
// or block in them.
namespace rka {

struct BallastExpander {
	virtual ~BallastExpander() {}
	virtual void preLevel(int ch, float& l, float& r) {
		(void) ch;
		(void) l;
		(void) r;
	}
	virtual void postLevel(int ch, float& l, float& r, bool mono) {
		(void) ch;
		(void) l;
		(void) r;
		(void) mono;
	}
	virtual void intoMix(float& l, float& r) {
		(void) l;
		(void) r;
	}
};

} // namespace rka
