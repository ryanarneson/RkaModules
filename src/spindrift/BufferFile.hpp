#pragma once
// Spindrift's frozen buffer as the patch keeps it (the spec, "Saved with the
// patch"): buffer.bin in the module's patch storage, a 20-byte header and
// the first 768,016 bytes of Beads' delay memory, in Beads' own format for
// the quality it was recorded in: the 768,000 bytes Beads' own backup saves
// (background_buffer_backup.cc) and 16 more. Beads' ring does not end at
// byte 768,000 in every format: its 12-bit items are 3 bytes and leave a
// byte spare at the end of each 262,144-byte bank, so the 12-bit ring ends
// at byte 768,002 (Beads' backup loses half its last frame), and every
// DelayLine keeps a copy of its first 4 frames just past its end (its
// interpolation tail, which Hermite reads use at the wrap): up to byte
// 768,016 (16-bit stereo). 768,016 bytes hold all of it in every format, so a
// reload is the ring as it was, to the bit. Rack-free; the format and its checks, and reading and writing
// the file. Tested in test_buffer.cpp, with a golden version 1 file
// (testdata/GoldenBufferV1.hpp: never regenerate it).
//
// The header, little-endian whatever the machine (encodeHeader):
//   0  magic     "SPDR"
//   4  version   1
//   8  quality   Beads' Processor::Media (0 Dirty Tape .. 3 Digital)
//   9  mono      0 or 1
//   10 reserved  0, 0
//   12 writePos  Beads' write pointer in the ring, < ringFrames()
//   16 bytes     the payload's length: 768,016
//
// Threads: no state; the file functions are for the UI thread (they do I/O).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "BeadsContext.hpp"

namespace spindrift {

constexpr uint32_t kBufferMagic = 0x52445053u;  // "SPDR" read little-endian
constexpr uint32_t kBufferVersion = 1;
constexpr size_t kBufferHeaderBytes = 20;
// Past Beads' 768,000 bytes: the rest of the 12-bit ring and the
// interpolation tail (above).
constexpr size_t kBufferTailBytes = 16;
constexpr size_t kBufferPayloadBytes = kSavedBytes + kBufferTailBytes;
static_assert(kBufferPayloadBytes <= kDelayBytes - 256 * 8,
              "the payload stays clear of DelayLine's freeze tail (256 FloatFrames at the end of the memory)");

struct BufferHeader {
	uint32_t magic = kBufferMagic, version = kBufferVersion;
	uint8_t quality = 3, mono = 0;
	uint32_t writePos = 0, bytes = uint32_t(kBufferPayloadBytes);
};

// Frames in Beads' ring for a quality (Processor::Media order) and mono: the
// 768,000 bytes at 8, 12, 12 or 16 bits a sample, two channels (mono: one,
// so twice the frames). processor.h's GranularProcessor sizes.
inline uint32_t ringFrames(unsigned quality, bool mono) {
	static const uint32_t kBits[4] = {8, 12, 12, 16};
	if (quality > 3)
		return 0;
	return uint32_t(kSavedBytes * 8 / (kBits[quality] * 2)) * (mono ? 2u : 1u);
}

// A header that fits Beads' memory, for a file of fileBytes bytes: ours,
// version 1 (a newer version is refused, not guessed at), a quality Beads
// has, mono 0 or 1, the whole 768,016-byte payload and nothing more, and a
// write pointer inside the ring that quality and mono make.
inline bool validBuffer(const BufferHeader& h, size_t fileBytes) {
	return h.magic == kBufferMagic && h.version == kBufferVersion && h.quality <= 3 && h.mono <= 1 &&
	       h.bytes == kBufferPayloadBytes && fileBytes == kBufferHeaderBytes + size_t(h.bytes) &&
	       h.writePos < ringFrames(h.quality, h.mono != 0);
}

// The reason validBuffer() refuses, for the log.
inline const char* bufferProblem(const BufferHeader& h, size_t fileBytes) {
	if (h.magic != kBufferMagic)
		return "not a Spindrift buffer";
	if (h.version != kBufferVersion)
		return h.version > kBufferVersion ? "from a newer Spindrift" : "an unknown version";
	if (h.quality > 3)
		return "an unknown quality";
	if (h.mono > 1)
		return "neither mono nor stereo";
	if (h.bytes != kBufferPayloadBytes || fileBytes != kBufferHeaderBytes + size_t(h.bytes))
		return "the wrong length (truncated?)";
	if (h.writePos >= ringFrames(h.quality, h.mono != 0))
		return "a write position outside its ring (mono and stereo mixed up?)";
	return "";
}

namespace detail {
inline void put32(uint8_t* p, uint32_t v) {
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
	p[2] = uint8_t(v >> 16);
	p[3] = uint8_t(v >> 24);
}
inline uint32_t get32(const uint8_t* p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}  // namespace detail

inline void encodeHeader(const BufferHeader& h, uint8_t* out /* kBufferHeaderBytes */) {
	detail::put32(out, h.magic);
	detail::put32(out + 4, h.version);
	out[8] = h.quality;
	out[9] = h.mono;
	out[10] = out[11] = 0;
	detail::put32(out + 12, h.writePos);
	detail::put32(out + 16, h.bytes);
}

inline BufferHeader decodeHeader(const uint8_t* in /* kBufferHeaderBytes */) {
	BufferHeader h;
	h.magic = detail::get32(in);
	h.version = detail::get32(in + 4);
	h.quality = in[8];
	h.mono = in[9];
	h.writePos = detail::get32(in + 12);
	h.bytes = detail::get32(in + 16);
	return h;
}

// Writes the header and kBufferPayloadBytes of payload. False, with why, on failure.
inline bool writeBufferFile(const std::string& path, const BufferHeader& h, const uint8_t* payload, std::string& why) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) {
		why = "cannot create the file";
		return false;
	}
	uint8_t head[kBufferHeaderBytes];
	encodeHeader(h, head);
	bool ok = std::fwrite(head, 1, sizeof head, f) == sizeof head &&
	          std::fwrite(payload, 1, kBufferPayloadBytes, f) == kBufferPayloadBytes;
	ok = (std::fclose(f) == 0) && ok;
	if (!ok)
		why = "cannot write the file";
	return ok;
}

// Reads and checks a file: true only for a valid one (validBuffer), with
// its payload. False, with why, for a missing, short, long or foreign file;
// nothing past the file is read and nothing larger than the payload is
// allocated.
inline bool readBufferFile(const std::string& path, BufferHeader& h, std::vector<uint8_t>& payload, std::string& why) {
	payload.clear();
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) {
		why = "no such file";
		return false;
	}
	uint8_t head[kBufferHeaderBytes];
	if (std::fread(head, 1, sizeof head, f) != sizeof head) {
		std::fclose(f);
		why = "shorter than its header (truncated)";
		return false;
	}
	h = decodeHeader(head);
	// The payload, and one byte more to tell a long file.
	if (h.bytes == kBufferPayloadBytes) {
		payload.resize(kBufferPayloadBytes + 1);
		const size_t got = std::fread(payload.data(), 1, payload.size(), f);
		payload.resize(got);
	}
	std::fclose(f);
	if (!validBuffer(h, kBufferHeaderBytes + payload.size())) {
		why = bufferProblem(h, kBufferHeaderBytes + payload.size());
		payload.clear();
		return false;
	}
	return true;
}

}  // namespace spindrift
