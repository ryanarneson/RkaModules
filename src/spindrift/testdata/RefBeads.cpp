// The reference Beads (testdata/ref/, untouched upstream) and its driver,
// as beads.cc drives Processor and Synth. Built only into the tests, with
// -Dbeads=beadsref -Dstmlib=stmlibref -Isrc/spindrift/testdata/ref
// (tests.mk), so its names never meet the vendored copy's.
#include "RefBeads.hpp"

#include <cstdio>  // printf under TEST (wavetable_granular_player.h); see BeadsFirmware.cpp
#include <vector>

// The reference's .cc files (as BeadsFirmware.cpp includes the vendored ones).
#include "beads/dsp/delay_clock.cc"
#include "beads/dsp/delay_controller.cc"
#include "beads/dsp/mu_law.cc"
#include "beads/dsp/processor.cc"
#include "beads/dsp/synth.cc"
#include "beads/dsp/trigger_generator.cc"
#include "beads/resources.cc"
#include "stmlib/dsp/units.cc"
#include "stmlib/utils/random.cc"

namespace rkaref {

namespace {

const size_t kBankSize = 256 * 1024;  // beads.cc: delay_buffer_1 (two banks), delay_buffer_2
const size_t kReverbSize = 32768;     // beads.cc: reverb_buffer

void toParameters(const RefParams& p, beads::Parameters* bp, beads::ModulatedParameter* d,
                  beads::Processor::MixParameters* m) {
	bp->time.base = p.time; bp->time.attenuverter = p.timeAtt; bp->time.cv = p.timeCv; bp->time.has_cv = p.timeHasCv;
	bp->size.base = p.size; bp->size.attenuverter = p.sizeAtt; bp->size.cv = p.sizeCv; bp->size.has_cv = p.sizeHasCv;
	bp->shape.base = p.shape; bp->shape.attenuverter = p.shapeAtt; bp->shape.cv = p.shapeCv; bp->shape.has_cv = p.shapeHasCv;
	bp->pitch.base = p.pitch; bp->pitch.attenuverter = p.pitchAtt; bp->pitch.cv = p.pitchCv; bp->pitch.has_cv = p.pitchHasCv;
	bp->monophonic_mode = false;  // beads.cc without ENABLE_ADVANCED_FEATURES
	d->base = p.density;
	d->cv = p.densityCv;
	m->feedback = p.feedback;
	m->dry_wet = p.dryWet;
	m->reverb_amount = p.reverb;
	m->output_gate = p.gateOutput;
}

}  // namespace

struct BeadsRef::Impl {
	std::vector<uint8_t> delay = std::vector<uint8_t>(3 * kBankSize);
	std::vector<uint16_t> reverb = std::vector<uint16_t>(kReverbSize);
	beads::Processor processor;
	beads::Synth synth;  // shares the reverb buffer with the processor, as on Beads
	std::vector<beads::FloatFrame> in, out;

	Impl() {
		uint8_t* banks[3] = {&delay[0], &delay[kBankSize], &delay[2 * kBankSize]};
		processor.Init(banks, &reverb[0]);
		synth.Init(&reverb[0]);
	}

	void reserve(size_t frames) {
		if (in.size() < frames) {
			in.resize(frames);
			out.resize(frames);
		}
	}
};

BeadsRef::BeadsRef() : impl(new Impl()) {}

BeadsRef::~BeadsRef() {
	delete impl;
}

void BeadsRef::seed(uint32_t s) {
	stmlib::Random::Seed(s);
}

void BeadsRef::configure(int media, bool mono) {
	impl->processor.Configure(beads::Processor::Media(media), mono);
}

void BeadsRef::processBlock(const RefParams& p, const uint8_t* seedGateFlags, const bool* freeze,
                            const float* inLR, float* outLR, size_t frames) {
	beads::Parameters bp;
	beads::ModulatedParameter d;
	beads::Processor::MixParameters m;
	toParameters(p, &bp, &d, &m);
	impl->reserve(frames);
	for (size_t i = 0; i < frames; i++) {
		impl->in[i].l = inLR[2 * i];
		impl->in[i].r = inLR[2 * i + 1];
	}
	impl->processor.Process(beads::SeedButtonState(p.seedState),
	                        seedGateFlags  /* stmlib::GateFlags is uint8_t */, freeze,
	                        d, bp, m, &impl->in[0], &impl->out[0], frames);
	for (size_t i = 0; i < frames; i++) {
		outLR[2 * i] = impl->out[i].l;
		outLR[2 * i + 1] = impl->out[i].r;
	}
}

void BeadsRef::synthBlock(const RefParams& p, const uint8_t* seedGateFlags, const bool* freeze,
                          int resolution, float* outLR, size_t frames) {
	beads::Parameters bp;
	beads::ModulatedParameter d;
	beads::Processor::MixParameters m;
	toParameters(p, &bp, &d, &m);
	impl->reserve(frames);
	impl->synth.Render(beads::SeedButtonState(p.seedState),
	                   seedGateFlags  /* stmlib::GateFlags is uint8_t */, freeze,
	                   beads::Synth::Resolution(resolution), d, bp, m, &impl->out[0], frames);
	for (size_t i = 0; i < frames; i++) {
		outLR[2 * i] = impl->out[i].l;
		outLR[2 * i + 1] = impl->out[i].r;
	}
}

}  // namespace rkaref
