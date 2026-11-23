#pragma once
// Spindrift's side of the vendored Beads (src/spindrift/beads/, stmlib/):
// the per-instance state Beads kept in globals, the memory it kept in
// static buffers, the largest block it is handed, and the window's view of
// its grains. Included by the vendored files where CHANGES.md says; Rack-free.
//
// Threads: a BeadsContext, a BeadsMemory and the Beads objects using them
// belong to one engine. BeadsMemory::allocate()/release() run off the audio
// thread; everything else runs on the engine's thread inside a BeadsScope.
#include <cstddef>
#include <cstdint>
#include <new>

namespace spindrift {

// All of Beads' mutable global state: stmlib::Random's seed (upstream a
// static, initialised to 0x21).
struct BeadsContext {
	uint32_t rng = 0x21;
};

// The thread's current context, read by stmlib::Random (random.cc). One
// thread-local pointer, defined in BeadsFirmware.cpp; null outside a scope.
BeadsContext*& current();

// Makes `c` the thread's current context for its lifetime; restores the
// previous one on exit. Every call into Beads runs inside one.
struct BeadsScope {
	explicit BeadsScope(BeadsContext& c) : previous(current()) { current() = &c; }
	~BeadsScope() { current() = previous; }
	BeadsScope(const BeadsScope&) = delete;
	BeadsScope& operator=(const BeadsScope&) = delete;

private:
	BeadsContext* previous;
};

// The largest block handed to Beads: 48 kHz / 4 kHz block rate (beads.cc).
// Processor::Process and Synth::Render assert it.
constexpr size_t kMaxBlock = 12;

// Beads' delay memory: three 262,144-byte banks (beads.cc: delay_buffer_1,
// two banks, and delay_buffer_2), of which the first 768,000 bytes are the
// buffer Beads backs up; DelayLine keeps its freeze tail at the end of the
// last bank. The reverb's: 32,768 16-bit words (beads.cc: reverb_buffer),
// shared by the Processor and the Synth.
constexpr size_t kBankBytes = 262144;
constexpr size_t kDelayBytes = 786432, kSavedBytes = 768000;
constexpr size_t kReverbWords = 32768;
static_assert(kDelayBytes == 3 * kBankBytes, "three banks");

struct BeadsMemory {
	uint8_t* delay = nullptr;
	uint16_t* reverb = nullptr;

	BeadsMemory() = default;
	~BeadsMemory() { release(); }
	BeadsMemory(const BeadsMemory&) = delete;
	BeadsMemory& operator=(const BeadsMemory&) = delete;

	// Off the audio thread. Zeroed; false (and nothing held) if out of memory.
	bool allocate() {
		if (delay && reverb) return true;
		release();
		delay = new (std::nothrow) uint8_t[kDelayBytes]();
		reverb = new (std::nothrow) uint16_t[kReverbWords]();
		if (!delay || !reverb) {
			release();
			return false;
		}
		return true;
	}

	// Off the audio thread, with nothing using the memory.
	void release() {
		delete[] delay;
		delete[] reverb;
		delay = nullptr;
		reverb = nullptr;
	}

	// The three banks, as Processor::Init takes them.
	void banks(uint8_t* out[3]) const {
		for (size_t i = 0; i < 3; i++) out[i] = delay ? delay + i * kBankBytes : nullptr;
	}
};

// The window's view, filled by the vendored accessors (const, no
// allocation, cheap enough to call every block). Positions are distances
// behind the write head as a fraction of the buffer (0 at the head, 1 a whole
// buffer back), so a point's place in the ring is writeHead() - position,
// wrapped; Processor::writeHead() is the head's place in the ring, 0..1,
// increasing with time.
struct GrainSnap {
	float position;  // delay grains: read point; synth grains: wave, 0..1 across the bank
	float span;      // buffer still to be read, |speed| x remaining samples, 0..1 (synth: 0)
	float pan;       // 0 left .. 1 right
	float env;       // the envelope now, 0..1
};

struct TapSnap {
	float position;  // the tap's delay, 0..1 of the buffer
	float gain;
};

}  // namespace spindrift
