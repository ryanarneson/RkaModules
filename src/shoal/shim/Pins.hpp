#pragma once
// Synthesises the encoder's two quadrature pins for the firmware's
// Encoder::Poll/Read (UI/ui_encoder.h). Header-only, Rack-free.
#include <cstddef>
#include <cstdint>

namespace shoal {

// Idle level is (A,B) = (high,high). Read() counts +1 when A falls (a&3 == 2)
// while B is low (b&3 == 0), -1 mirrored. One detent is two polls:
//   +1: (1,0) then (0,0)      -1: (0,1) then (0,0)
// then the pins return to idle. Detents queue, one sample per poll(), up to
// kMaxQueued samples (256 detents); turns past that are dropped. The queue is
// a fixed ring: nothing allocates.
struct EncoderPins {
	void turn(int detents) {
		const bool fwd = detents > 0;
		// Magnitude in unsigned arithmetic: -INT_MIN does not fit an int.
		const unsigned n = fwd ? (unsigned) detents : 0u - (unsigned) detents;
		for (unsigned i = 0; i < n; i++) {
			if (size() + 2 > kMaxQueued)
				return; // hostile caller: drop the rest
			push(fwd ? kFwd1 : kRev1);
			push(kBoth0);
		}
	}
	void clear() { head = tail = 0; }
	bool idle() const { return head == tail; }  // no sample left to play
	void poll(bool& a, bool& b) {
		uint8_t s = kIdle;
		if (head != tail) {
			s = ring[head];
			head = (head + 1) % kRingSize;
		}
		a = s & 1;
		b = s & 2;
	}

private:
	// bit 0 = A, bit 1 = B
	static constexpr uint8_t kIdle = 3, kFwd1 = 1, kRev1 = 2, kBoth0 = 0;
	static constexpr size_t kMaxQueued = 512;
	static constexpr size_t kRingSize = 1024;  // > kMaxQueued: never full
	static_assert(kRingSize > kMaxQueued, "ring holds the whole queue");
	uint8_t ring[kRingSize] = {};
	size_t head = 0, tail = 0;  // next sample to play, next free slot

	size_t size() const { return (tail + kRingSize - head) % kRingSize; }
	void push(uint8_t s) {
		ring[tail] = s;
		tail = (tail + 1) % kRingSize;
	}
};

} // namespace shoal
