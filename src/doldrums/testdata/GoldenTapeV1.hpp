#pragma once
// Doldrums' frozen tape as a patch saved by this version keeps it (the
// design's "With the patch"; TapeStore.hpp): tape.wav in the module's patch
// storage folder. NEVER REGENERATE THIS TO MAKE A TEST PASS (TESTING.md rule
// 5): such a file must read back, to the bit, for as long as Doldrums exists.
//
// The header's 44 bytes are written out by hand below, as WavFile.hpp's
// writer makes them: RIFF (its size 8 short of the usual 36 + data: dr_wav's
// count, which readers ignore)/WAVE, a 16-byte
// "fmt " chunk (format 3, IEEE float; 2 channels; 48,000 Hz; 384,000 bytes a
// second; 8 bytes a frame; 32 bits), then "data". The samples, a whole tape
// at 48 kHz (1,440,000 frames, oldest first, left then right, in volts), are
// not stored: frame i is goldenLeft(i), goldenRight(i), fixed formulas of
// exact floats (24-bit integers over 2^20: +-8 V, every bit of the mantissa
// in use), so the file can be rebuilt exactly without committing 11.5 MB.
//
// The patch data saved with it (Doldrums.cpp's dataToJson, SavedState.hpp)
// is kGoldenData below: every menu setting away from its default, so a loader
// that ignored one fails.
#include <cstdint>

namespace rka { namespace doldrums { namespace golden {

const uint32_t kTapeRate = 48000;
const uint32_t kTapeFrames = 1440000;   // 30 s at 48 kHz

const uint8_t kTapeHeaderV1[44] = {
    'R', 'I', 'F', 'F',
    0x1C, 0xC8, 0xAF, 0x00,   // 11,520,028: 28 + the data's bytes, as the writer (dr_wav) puts it
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    0x10, 0x00, 0x00, 0x00,   // the chunk's 16 bytes
    0x03, 0x00,               // format 3: IEEE float
    0x02, 0x00,               // 2 channels
    0x80, 0xBB, 0x00, 0x00,   // 48,000 Hz
    0x00, 0xDC, 0x05, 0x00,   // 384,000 bytes a second
    0x08, 0x00,               // 8 bytes a frame
    0x20, 0x00,               // 32 bits
    'd', 'a', 't', 'a',
    0x00, 0xC8, 0xAF, 0x00,   // 11,520,000 bytes: 1,440,000 frames of 8
};

inline uint32_t goldenHash(uint32_t i, uint32_t salt) {
	uint32_t h = (i + salt) * 2654435761u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return h;
}
// A 24-bit integer over 2^20: an exact float in [-8, 8).
inline float goldenSample(uint32_t h) {
	return float(int32_t(h >> 8) - 0x800000) / 1048576.f;
}
inline float goldenLeft(uint32_t i) { return goldenSample(goldenHash(i, 0x9E3779B9u)); }
inline float goldenRight(uint32_t i) { return goldenSample(goldenHash(i, 0x7F4A7C15u)); }

// The patch data, as Rack's JSON decodes it: {"grain": 2, "wide": true,
// "atEnd": 1, "keepTape": false}.
struct GoldenField {
	const char* key;
	int kind;   // SavedValue::Kind: 0 BOOL, 1 INT
	double value;
};
const GoldenField kGoldenData[4] = {
    {"grain", 1, 2.0},
    {"wide", 0, 1.0},
    {"atEnd", 1, 1.0},
    {"keepTape", 0, 0.0},
};

}}} // namespace rka::doldrums::golden
