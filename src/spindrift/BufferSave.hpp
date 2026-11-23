#pragma once
// The module's onSave for the frozen buffer, and the patch's "frozen" field,
// Rack-free so test_buffer can run the save path (Spindrift.cpp gives it
// Rack's filesystem calls).
//
// saveBuffer(): buffer.bin is written, synchronously (Rack archives the
// folder as soon as onSave returns), when the ring holds still (frozen, by
// the button or the gate, outside the synth: SpindriftEngine::bufferImage)
// and "Keep the frozen buffer" is on; otherwise it is removed. A buffer
// already stored and frozen since is not written again.
//
// Fs, the filesystem (each of exists, makeDir and remove may throw
// std::exception, as Rack's system:: calls do; that never fails the save):
//   bool exists(const std::string& path)
//   void makeDir()                      the patch storage directory
//   void remove(const std::string& path)
//   void warn(const std::string& message)
//
// Threads: the UI thread (the save), with the engine running.
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include "BufferFile.hpp"
#include "SpindriftEngine.hpp"

namespace spindrift {

// What buffer.bin holds: the image the engine's ring had at `seq`, if stored.
struct BufferRecord {
	bool stored = false;
	uint32_t seq = 1;
};

// The patch's "frozen": buffer.bin holds the ring as it is now.
inline bool savedFrozen(const SpindriftEngine& engine, const BufferRecord& rec) {
	return rec.stored && engine.ringSeq() == rec.seq;
}

template <class Fs>
inline void saveBuffer(const SpindriftEngine& engine, const std::string& path, BufferRecord& rec, Fs& fs) {
	if (engine.settings.keepFrozen.load(std::memory_order_relaxed)) {
		const uint32_t seq = engine.ringSeq();
		if (!(seq & 1u) && rec.stored && seq == rec.seq) {
			bool there = false;
			try {
				there = fs.exists(path);
			}
			catch (std::exception& ex) {
				fs.warn("Spindrift: cannot look for " + path + " (" + ex.what() + "); writing it again");
			}
			if (there)
				return;
		}
		BufferHeader h;
		std::vector<uint8_t> payload(kBufferPayloadBytes);
		uint32_t imageSeq = 1;
		// Frozen when asked but written during the copy (the freeze
		// ended meanwhile, or ended and began again): the copy is
		// discarded (SpindriftEngine::bufferImage). Try again while it is
		// frozen anew; say so if it never holds still.
		bool wasFrozen = false, got = false;
		for (int attempt = 0; attempt < 3 && !got; attempt++) {
			wasFrozen |= !(engine.ringSeq() & 1u);
			got = engine.bufferImage(h, payload.data(), &imageSeq);
		}
		if (got) {
			bool dir = true;
			try {
				fs.makeDir();
			}
			catch (std::exception& ex) {
				dir = false;
				fs.warn("Spindrift: cannot create the patch storage folder for " + path + ": " + ex.what() +
				        "; the frozen buffer is not stored");
			}
			std::string why;
			if (dir && writeBufferFile(path, h, payload.data(), why)) {
				rec.stored = true;
				rec.seq = imageSeq;
				return;
			}
			if (dir)
				fs.warn("Spindrift: cannot store the frozen buffer in " + path + ": " + why);
		}
		else if (wasFrozen)
			fs.warn("Spindrift: the frozen buffer changed while saving (the freeze ended); not stored");
	}
	rec.stored = false;
	// Rack's filesystem calls throw on failure (a file held open
	// elsewhere, on Windows): that must not fail the patch's save.
	try {
		if (fs.exists(path))
			fs.remove(path);
	}
	catch (std::exception& ex) {
		fs.warn("Spindrift: cannot remove " + path + ": " + ex.what());
	}
}

}  // namespace spindrift
