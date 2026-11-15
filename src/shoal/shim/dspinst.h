#pragma once
// Portable C versions of the Teensy DSP intrinsics the firmware calls
// (upstream extern/dspinst.h wraps ARM assembly). Meanings are taken from
// that file's comments; intermediates are 64-bit.
#include <stdint.h>

// computes limit((val >> rshift), 2**bits): signed saturation to `bits` bits.
static inline int32_t signed_saturate_rshift(int32_t val, int bits, int rshift) {
	int32_t out = val >> rshift;
	int32_t max = (int32_t) (1u << (bits - 1));
	if (out >= 0) {
		if (out > max - 1) out = max - 1;
	}
	else {
		if (out < -max) out = -max;
	}
	return out;
}

// computes ((a[31:0] * b[15:0]) >> 16); b[15:0] is signed.
static inline int32_t signed_multiply_32x16b(int32_t a, uint32_t b) {
	return (int32_t) (((int64_t) a * (int16_t) (b & 0xffff)) >> 16);
}

// computes (((uint64_t)a[31:0] * (uint64_t)b[31:0]) >> 32)
static inline uint32_t multiply_u32xu32_rshift32(uint32_t a, uint32_t b) {
	return (uint32_t) (((uint64_t) a * b) >> 32);
}
