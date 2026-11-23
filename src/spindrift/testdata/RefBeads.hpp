#pragma once
// The reference: Mutable Instruments Beads (eurorack f4cfa76, stmlib
// e3bd7c9) untouched, in testdata/ref/, behind an interface that names no
// Beads type. RefBeads.cpp is compiled with -Dbeads=beadsref
// -Dstmlib=stmlibref, so it links beside the vendored copy
// (src/spindrift/beads/, stmlib/) in one test binary. Tests only.
#include <cstddef>
#include <cstdint>

namespace rkaref {

// One block's controls, as beads.cc hands them to Processor::Process and
// Synth::Render (CvReader's output: pots 0..1, attenuverters -1..1).
struct RefParams {
	float time, timeAtt, timeCv; bool timeHasCv;
	float size, sizeAtt, sizeCv; bool sizeHasCv;
	float shape, shapeAtt, shapeCv; bool shapeHasCv;
	float pitch, pitchAtt, pitchCv; bool pitchHasCv;  // pitch in semitones, as CvReader gives it
	float density, densityCv;
	float feedback, dryWet, reverb;
	int seedState;   // 0 off, 1 on, 2 locked (SeedButtonState)
	bool gateOutput;
};

class BeadsRef {
public:
	BeadsRef();
	~BeadsRef();
	BeadsRef(const BeadsRef&) = delete;
	BeadsRef& operator=(const BeadsRef&) = delete;

	// stmlib::Random::Seed. The reference keeps upstream's one global Random,
	// so every BeadsRef shares one stream: a comparison seeds one reference
	// and runs it alone (with no other BeadsRef running in between).
	void seed(uint32_t s);
	void configure(int media, bool mono);   // Processor::Configure; media 0 Dirty Tape .. 3 Digital
	// Processor::Process on one block. seedGateFlags: stmlib::GateFlags per
	// frame, nullptr when SEED is unpatched. inLR, outLR: interleaved, +-1.
	void processBlock(const RefParams& p, const uint8_t* seedGateFlags,
	                  const bool* freeze, const float* inLR, float* outLR, size_t frames);
	// Synth::Render on one block; resolution is Synth::Resolution (0..3).
	void synthBlock(const RefParams& p, const uint8_t* seedGateFlags, const bool* freeze,
	                int resolution, float* outLR, size_t frames);

private:
	struct Impl;
	Impl* impl;
};

}  // namespace rkaref
