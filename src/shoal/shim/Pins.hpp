#pragma once
// Synthesises the encoder's two quadrature pins for the firmware's
// Encoder::Poll/Read (UI/ui_encoder.h). Header-only, Rack-free.
#include <cstddef>
#include <cstdint>
#include <deque>

namespace shoal {

// Idle level is (A,B) = (high,high). Read() counts +1 when A falls (a&3 == 2)
// while B is low (b&3 == 0), -1 mirrored. One detent is two polls:
//   +1: (1,0) then (0,0)      -1: (0,1) then (0,0)
// then the pins return to idle. Detents queue, one sample per poll().
struct EncoderPins {
	void turn(int detents) {
		const bool fwd = detents > 0;
		// Magnitude in unsigned arithmetic: -INT_MIN does not fit an int.
		const unsigned n = fwd ? (unsigned) detents : 0u - (unsigned) detents;
		for (unsigned i = 0; i < n; i++) {
			if (queue.size() > kMaxQueued)
				return; // hostile caller: drop the rest
			queue.push_back(fwd ? kFwd1 : kRev1);
			queue.push_back(kBoth0);
		}
	}
	void clear() { queue.clear(); }
	void poll(bool& a, bool& b) {
		uint8_t s = kIdle;
		if (!queue.empty()) {
			s = queue.front();
			queue.pop_front();
		}
		a = s & 1;
		b = s & 2;
	}

private:
	// bit 0 = A, bit 1 = B
	static constexpr uint8_t kIdle = 3, kFwd1 = 1, kRev1 = 2, kBoth0 = 0;
	static constexpr size_t kMaxQueued = 512;
	std::deque<uint8_t> queue;
};

} // namespace shoal
