#pragma once
// The interface Neap calls on the module at its right (Spring implements it).
//
// Neap's process() finds rightExpander.module, dynamic_casts it to
// NeapExpander, skips it if bypassed, and calls fromNeap once per sample with
// the shaped cycle values in +-5 V (after Wave and crossfade, before Level).
//
// Threads and state: the call comes from Neap's thread, which Rack may run
// concurrently with the expander's own process(). All per-sample work happens
// inside fromNeap; the expander's process() only writes outputs and lights
// from what the call left. Must not allocate or block.
namespace rka {

struct NeapExpander {
	virtual ~NeapExpander() {}
	virtual void fromNeap(const float x[8], float sampleTime) = 0;
};

} // namespace rka
