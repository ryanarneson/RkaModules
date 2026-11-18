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
//   postLevel(ch, l, r, src)  each channel's pair after Level and CV (Pan,
//                             Mute, Solo, Fade, send taps). src is MONO when
//                             exactly one of IN L and IN R is patched (l and
//                             r then both hold that jack's signal), STEREO
//                             otherwise;
//   intoMix(l, r)             the summed mix, CHAIN included, before Master
//                             (a send's return).
//
// Ballast also calls linked() on each expander once per sample, before the
// other calls and whether or not an output is patched: an expander may use
// it to read its controls and advance its smoothing, and to know it is
// linked. Just before that, on the first sample an expander is in the chain
// after not being in it (placed, unbypassed, or a gap closed), Ballast calls
// attached(). Both come from Ballast's thread: an expander must not work out
// "newly attached" from a counter its own process() also writes, because
// with several engine threads the two race. kind() names a kind of expander; Ballast keeps only the first
// unbypassed expander of each nonzero kind (Pan is 1), so two of a kind never
// stack. Kind 0 (the default) may repeat.
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

enum BallastSource { STEREO, MONO };

struct BallastExpander {
	virtual ~BallastExpander() {}
	virtual int kind() const {
		return 0;
	}
	virtual void linked() {}
	virtual void attached() {}
	virtual void preLevel(int ch, float& l, float& r) {
		(void) ch;
		(void) l;
		(void) r;
	}
	virtual void postLevel(int ch, float& l, float& r, BallastSource src) {
		(void) ch;
		(void) l;
		(void) r;
		(void) src;
	}
	virtual void intoMix(float& l, float& r) {
		(void) l;
		(void) r;
	}
};

} // namespace rka
