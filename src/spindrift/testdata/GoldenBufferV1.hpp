#pragma once
// Spindrift's saved buffer, format version 1 (BufferFile.hpp): one golden
// file, as a patch saved by this version stores it in its patch storage as
// buffer.bin. NEVER REGENERATE THIS TO MAKE A TEST PASS (TESTING.md rule 5):
// a version 1 file must load, to the bit, for as long as Spindrift exists.
//
// The header's 20 bytes are written out by hand below. The payload, 768,016
// bytes, is not stored: byte i is goldenPayloadByte(i), a fixed formula, so
// the file can be rebuilt exactly without committing it.
//
// What it holds: Vintage Digital (Beads' Media 2), mono, the write pointer
// at 123,457 (of the mono 12-bit ring's 512,000 frames), 768,016 bytes.
// Every field is away from its default, so a loader that ignored one fails.
#include <cstddef>
#include <cstdint>

namespace spindrift {
namespace golden {

const uint8_t kBufferHeaderV1[20] = {
    'S', 'P', 'D', 'R',      // magic
    0x01, 0x00, 0x00, 0x00,  // version 1, little-endian
    0x02,                    // quality: 2, Vintage Digital (Beads' Processor::Media order)
    0x01,                    // mono
    0x00, 0x00,              // reserved, zero
    0x41, 0xE2, 0x01, 0x00,  // write pointer 123,457 (0x0001E241), little-endian
    0x10, 0xB8, 0x0B, 0x00,  // payload bytes 768,016 (0x000BB810), little-endian
};

const unsigned kQuality = 2;
const bool kMono = true;
const uint32_t kWritePos = 123457;
const uint32_t kPayloadBytes = 768016;

// Byte i of the payload: the top byte of i times 2654435761 (Knuth's
// multiplicative hash, in 32-bit unsigned arithmetic) exclusive-or the low
// byte of i.
inline uint8_t goldenPayloadByte(uint32_t i) {
	return uint8_t(((i * 2654435761u) >> 24) ^ (i & 0xFFu));
}

}  // namespace golden
}  // namespace spindrift
