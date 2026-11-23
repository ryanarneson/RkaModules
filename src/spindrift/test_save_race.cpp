// Spindrift's save against the running engine (TESTING.md rule 7): the
// module's onSave calls SpindriftEngine::bufferImage() on the UI thread while
// the audio thread runs process(). The image's bytes are plain memory, so a
// copy taken as the freeze ends reads bytes being written; the ring's
// sequence count (a seqlock) must catch that and discard the copy, so that
// every image bufferImage() returns is the ring exactly as it stood at the
// sequence number it reports.
//
// Not a ThreadSanitizer test: the copy is a deliberate (detected) race that
// TSan would report. Built with ASan/UBSan and -pthread (tests.mk).
//
// The engine's thread plays a signal in and freezes and unfreezes every few
// milliseconds, by the button and by the gate in turn. Each time the ring
// becomes stable (ringSeq() turns to a new even number) it records, between
// two process() calls, so with nothing else writing, a hash of the ring and
// its header for that number. Another thread calls bufferImage() in a loop
// and hashes each image it is given. Afterwards every returned image must
// match the record for its number; some copies must have been discarded
// (the race happened); and many images must have been returned.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "testing.hpp"
#include "BufferFile.hpp"
#include "SpindriftEngine.hpp"

namespace {

using spindrift::SpindriftEngine;

uint64_t fnv(const uint8_t* p, size_t n, uint64_t h = 1469598103934665603ull) {
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

uint64_t headerHash(unsigned quality, unsigned mono, uint32_t writePos, uint64_t h) {
	const uint8_t b[6] = {uint8_t(quality), uint8_t(mono), uint8_t(writePos), uint8_t(writePos >> 8),
	                      uint8_t(writePos >> 16), uint8_t(writePos >> 24)};
	return fnv(b, sizeof b, h);
}

void save_race() {
	std::unique_ptr<SpindriftEngine> e(new SpindriftEngine(0x5eed));
	CHECK(e->allocate(), "allocated");
	e->setSampleRate(48000.f);

	std::mutex lock;
	std::map<uint32_t, uint64_t> stable;  // ringSeq -> hash of the ring and header then
	std::atomic<bool> stop{false};
	std::atomic<long> samples{0};

	std::thread engine([&]() {
		spindrift::Knobs k = {};
		k.time = k.size = k.shape = k.pitch = 0.5f;
		k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
		k.density = 0.8f;
		k.dryWet = 1.f;
		spindrift::Jacks j = {};
		j.inLOn = j.inROn = true;
		j.freezeOn = true;
		uint32_t lcg = 1;
		long n = 0, next = 0;
		int phase = 0;  // 0 unfrozen, 1 frozen by the button, 2 unfrozen, 3 frozen by the gate
		uint32_t seen = e->ringSeq();
		std::vector<uint8_t> ring(spindrift::kBufferPayloadBytes);
		while (!stop.load(std::memory_order_relaxed)) {
			if (n >= next) {
				phase = (phase + 1) & 3;
				if (phase == 1 || phase == 2)
					e->buttons().freezePress();  // on at 1, off at 2
				lcg = lcg * 1664525u + 1013904223u;
				next = n + 24 + long(lcg >> 24);  // 0.5 to 5.8 ms
			}
			j.freeze = phase == 3 ? 5.f : 0.f;
			const float t = float(n) / 48000.f;
			j.inL = 5.f * std::sin(2.f * 3.14159265f * 220.f * t);
			j.inR = 5.f * std::sin(2.f * 3.14159265f * 331.f * t + 1.f);
			spindrift::Out o;
			e->process(k, j, o);
			n++;
			// Stable anew: nothing writes the ring until our next process().
			const uint32_t s = e->ringSeq();
			if (s != seen && !(s & 1u)) {
				for (size_t i = 0; i < ring.size(); i++)
					ring[i] = *e->processor().delay_buffer_raw_bytes(i);
				uint64_t h = fnv(ring.data(), ring.size());
				h = headerHash(unsigned(e->processor().media()), e->processor().mono() ? 1u : 0u,
				               uint32_t(*e->processor().mutable_delay_write_ptr()), h);
				std::lock_guard<std::mutex> g(lock);
				stable[s] = h;
			}
			seen = s;
			if ((n & 1023) == 0)
				samples.store(n, std::memory_order_relaxed);
		}
		samples.store(n, std::memory_order_relaxed);
	});

	struct Got {
		uint32_t seq;
		uint64_t hash;
	};
	std::vector<Got> got;
	long discarded = 0, notFrozen = 0;
	std::thread saver([&]() {
		std::vector<uint8_t> image(spindrift::kBufferPayloadBytes);
		const auto start = std::chrono::steady_clock::now();
		for (;;) {
			const auto now = std::chrono::steady_clock::now();
			if (now - start > std::chrono::seconds(60) ||
			    (now - start > std::chrono::seconds(3) && got.size() >= 200 && discarded >= 20))
				break;
			const uint32_t before = e->ringSeq();
			spindrift::BufferHeader h;
			uint32_t seq = 1;
			if (e->bufferImage(h, image.data(), &seq)) {
				uint64_t x = fnv(image.data(), image.size());
				x = headerHash(h.quality, h.mono, h.writePos, x);
				got.push_back({seq, x});
			}
			else if (!(before & 1u))
				discarded++;  // stable when asked, written meanwhile
			else
				notFrozen++;
		}
	});
	saver.join();
	stop.store(true);
	engine.join();

	long wrong = 0, unknown = 0;
	for (const Got& g : got) {
		auto it = stable.find(g.seq);
		if (it == stable.end())
			unknown++;
		else if (it->second != g.hash)
			wrong++;
	}
	std::printf("save race: engine ran %ld samples (%.1f s of audio), %zu stable rings; %zu images returned, %ld "
	            "discarded as written meanwhile, %ld refused unfrozen\n",
	            samples.load(), samples.load() / 48000.0, stable.size(), got.size(), discarded, notFrozen);
	CHECK(wrong == 0, "every image returned is the ring as it stood at its number (%ld torn of %zu)", wrong, got.size());
	CHECK(unknown == 0, "every image's number is one the ring stood still at (%ld unknown)", unknown);
	CHECK(got.size() >= 200, "images returned (%zu)", got.size());
	CHECK(discarded >= 20, "the race happened: copies discarded (%ld)", discarded);
	CHECK(stable.size() >= 50, "the ring froze and thawed (%zu stable rings)", stable.size());
}

}  // namespace

int main() {
	save_race();
	return rkatest::finish("spindrift test_save_race");
}
