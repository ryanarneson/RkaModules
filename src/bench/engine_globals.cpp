// Not run: compiled to an object for `make engine-globals`, which fails if
// it holds writable global state (tools/check_shoal_globals.sh). For every
// module with a declaration file (src/<module>/bench.inc, found by
// tests.mk) it calls the engine's entry points through the declaration's
// Instance, so each inline function they reach, and any static it holds, is
// emitted here. Instances must share nothing mutable: Rack may run two on
// different engine threads, where shared state would race, and a shared
// written cache line would cost both.
#include "modules.inc"

#define X(m)                                                                    \
	void engineGlobalsProbe_##m(rkabench::m::Instance& i, float sampleRate) { \
		i.process(sampleRate);                                                  \
		i.reset();                                                              \
	}
RKA_BENCH_MODULES(X)
#undef X
