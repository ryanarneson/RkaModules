#pragma once
// Portable C versions of the Teensy DSP intrinsics the firmware calls
// (upstream extern/dspinst.h wraps ARM assembly). Meanings are taken from
// that file's comments; intermediates are 64-bit.
#include <stdint.h>

// Claim the upstream header's include guard so firmware/extern/dspinst.h
// (ARM assembly) compiles to nothing once this file is included first.
#define dspinst_h_

// computes limit((val >> rshift), 2**bits): signed saturation to `bits` bits.
// Defined for bits 1..32 and rshift 0..63 (rshift >= 32 shifts out everything
// but the sign, like an arithmetic shift). Out-of-range arguments are clamped.
static inline int32_t signed_saturate_rshift(int32_t val, int bits, int rshift) {
	if (bits < 1) bits = 1;
	if (bits > 32) bits = 32;
	if (rshift < 0) rshift = 0;
	if (rshift > 63) rshift = 63;
	int64_t out = rshift >= 32 ? (val < 0 ? -1 : 0) : (int64_t) (val >> rshift);
	int64_t max = ((int64_t) 1 << (bits - 1)) - 1;
	int64_t min = -((int64_t) 1 << (bits - 1));
	if (out > max) out = max;
	if (out < min) out = min;
	return (int32_t) out;
}

// computes ((a[31:0] * b[15:0]) >> 16); b[15:0] is signed.
static inline int32_t signed_multiply_32x16b(int32_t a, uint32_t b) {
	return (int32_t) (((int64_t) a * (int16_t) (b & 0xffff)) >> 16);
}

// computes (((uint64_t)a[31:0] * (uint64_t)b[31:0]) >> 32)
static inline uint32_t multiply_u32xu32_rshift32(uint32_t a, uint32_t b) {
	return (uint32_t) (((uint64_t) a * b) >> 32);
}
