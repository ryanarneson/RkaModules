// Not run: compiled to an object for `make engine-globals`, which fails if
// it holds writable global state (tools/check_shoal_globals.sh). It calls
// every entry point of Sway's and Fathom's engines, so each inline function
// they reach, and any static it holds, is emitted here. Instances must share
// nothing mutable: Rack may run two on different engine threads, where
// shared state would race, and a shared written cache line would cost both.
#include "fathom/FathomEngine.hpp"
#include "sway/SwayEngine.hpp"

void engineGlobalsProbe(rka::SwayEngine& sway, const rka::SwayFrame& sf, rka::SwayResult& sr, rka::FathomEngine& fathom,
                        const rka::FathomFrame& ff, float* out, float sampleRate) {
	sway.process(sf, 1.f / sampleRate, sampleRate, sr);
	sway.reset();
	fathom.process(ff, sampleRate, out);
	fathom.reset();
}
