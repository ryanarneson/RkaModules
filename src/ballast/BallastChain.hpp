#pragma once
// Walking Ballast's expander chain, Rack-free so the rules can be tested:
// start at Ballast's right-hand neighbour and go right; a module that is not
// a BallastExpander ends the chain; a bypassed expander is skipped but does
// not end it; at most `max` are collected. Ballast.cpp passes Rack's
// accessors (rightExpander.module, isBypassed(), the cast).
#include "BallastExpander.hpp"

namespace rka {

template <class M, class Next, class Bypassed, class As>
int collectBallastExpanders(M* first, BallastExpander** out, int max, Next next, Bypassed bypassed, As as) {
	int n = 0;
	for (M* m = first; m && n < max; m = next(m)) {
		BallastExpander* x = as(m);
		if (!x)
			break;
		if (!bypassed(m))
			out[n++] = x;
	}
	return n;
}

} // namespace rka
