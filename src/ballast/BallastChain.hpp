#pragma once
// Walking Ballast's expander chain, Rack-free so the rules can be tested:
// start at Ballast's right-hand neighbour and go right; a module that is not
// a BallastExpander ends the chain; a bypassed expander is skipped but does
// not end it (nor claim its kind); only the first expander of each nonzero
// kind (1 to 31) is kept, later ones are skipped; at most `max` are
// collected. Ballast.cpp passes Rack's
// accessors (rightExpander.module, isBypassed(), the cast).
#include "BallastExpander.hpp"

#include <cstdint>

namespace rka {

template <class M, class Next, class Bypassed, class As>
int collectBallastExpanders(M* first, BallastExpander** out, int max, Next next, Bypassed bypassed, As as) {
	int n = 0;
	uint32_t seen = 0;
	for (M* m = first; m && n < max; m = next(m)) {
		BallastExpander* x = as(m);
		if (!x)
			break;
		if (bypassed(m))
			continue;
		const int k = x->kind();
		if (k > 0 && k < 32) {
			if (seen & (1u << k))
				continue;
			seen |= 1u << k;
		}
		out[n++] = x;
	}
	return n;
}

// Ballast's per-sample link step, on its own thread: attached() on each
// expander that was not in the previous sample's chain, then linked() on
// every one. The previous chain is kept as addresses for comparison only;
// they are never dereferenced.
class BallastLinker {
public:
	static const int MAX = 16;

	void link(BallastExpander* const* chain, int n) {
		n = n < MAX ? n : MAX;
		for (int k = 0; k < n; k++) {
			bool was = false;
			for (int j = 0; j < lastN_ && !was; j++)
				was = last_[j] == chain[k];
			if (!was)
				chain[k]->attached();
			chain[k]->linked();
		}
		for (int k = 0; k < n; k++)
			last_[k] = chain[k];
		lastN_ = n;
	}

private:
	const BallastExpander* last_[MAX] = {};
	int lastN_ = 0;
};

} // namespace rka
