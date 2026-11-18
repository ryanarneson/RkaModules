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
// ch runs 0 to 3. Every method defaults to doing nothing.
//
// Threads and state: the calls come from Ballast's process(), which Rack may
// run on a different engine thread from the expander's own process(), at
// the same time. So everything an expander does per sample (reading its
// params and inputs, smoothing, its own state) happens inside these calls,
// never in its process(); its process() only sets its lights and outputs
// from values the calls left. The calls must not allocate or block. A
// bypassed expander is skipped (it is not called) without ending the chain.
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
