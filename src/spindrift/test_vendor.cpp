// The vendored Beads (src/spindrift/beads/, stmlib/) against the untouched
// upstream copy (testdata/ref/, through rkaref::BeadsRef): the reference
// runs, and the vendored Processor, driven the same way, gives the same
// samples. Every change Spindrift makes to the vendored copy must keep this
// equal or say, in CHANGES.md, why it cannot.
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "testing.hpp"
#include "testdata/RefBeads.hpp"

#include "beads/dsp/processor.h"
#include "stmlib/utils/random.h"

namespace {

const float kPi = 3.14159265358979f;
const int kSampleRate = 48000;
const size_t kBlock = 12;  // Beads' block at 48 kHz: 48000 / 4000 (beads.cc)
const uint32_t kSeed = 0x5eed;

rkaref::RefParams cloudParams() {
	rkaref::RefParams p = {};
	p.time = 0.1f;  // grains start within the 1 s written (0.5 reaches 2 s back: silence)
	p.size = 0.5f;
	p.shape = 0.5f;
	p.pitch = 0.f;
	p.density = 0.7f;
	p.feedback = 0.f;
	p.dryWet = 1.f;
	p.reverb = 0.3f;
	p.seedState = 2;  // locked
	p.gateOutput = false;
	return p;
}

// One second of a 220 Hz sine at +-0.5 on both channels, interleaved.
std::vector<float> sineIn() {
	std::vector<float> in(2 * kSampleRate);
	for (int i = 0; i < kSampleRate; i++) {
		float s = 0.5f * std::sin(2.f * kPi * 220.f * float(i) / float(kSampleRate));
		in[2 * i] = s;
		in[2 * i + 1] = s;
	}
	return in;
}

std::vector<float> runRef(const rkaref::RefParams& p, const std::vector<float>& in) {
	rkaref::BeadsRef ref;
	ref.seed(kSeed);
	ref.configure(3, false);  // Digital, stereo
	std::vector<float> out(in.size());
	const bool freeze[kBlock] = {};
	for (size_t f = 0; f < in.size() / 2; f += kBlock) {
		ref.processBlock(p, nullptr, freeze, &in[2 * f], &out[2 * f], kBlock);
	}
	return out;
}

// The vendored Processor, driven as RefBeads.cpp drives the reference.
std::vector<float> runVendored(const rkaref::RefParams& p, const std::vector<float>& in) {
	const size_t bank = 256 * 1024;
	std::unique_ptr<uint8_t[]> delay(new uint8_t[3 * bank]());
	std::unique_ptr<uint16_t[]> reverb(new uint16_t[32768]());
	std::unique_ptr<beads::Processor> proc(new beads::Processor());
	uint8_t* banks[3] = {delay.get(), delay.get() + bank, delay.get() + 2 * bank};
	proc->Init(banks, reverb.get());
	stmlib::Random::Seed(kSeed);  // after Init, as runRef seeds after constructing
	proc->Configure(beads::Processor::MEDIA_DIGITAL, false);

	beads::Parameters bp;
	bp.time.base = p.time; bp.time.attenuverter = p.timeAtt; bp.time.cv = p.timeCv; bp.time.has_cv = p.timeHasCv;
	bp.size.base = p.size; bp.size.attenuverter = p.sizeAtt; bp.size.cv = p.sizeCv; bp.size.has_cv = p.sizeHasCv;
	bp.shape.base = p.shape; bp.shape.attenuverter = p.shapeAtt; bp.shape.cv = p.shapeCv; bp.shape.has_cv = p.shapeHasCv;
	bp.pitch.base = p.pitch; bp.pitch.attenuverter = p.pitchAtt; bp.pitch.cv = p.pitchCv; bp.pitch.has_cv = p.pitchHasCv;
	bp.monophonic_mode = false;
	beads::ModulatedParameter d;
	d.base = p.density;
	d.cv = p.densityCv;
	beads::Processor::MixParameters m;
	m.feedback = p.feedback;
	m.dry_wet = p.dryWet;
	m.reverb_amount = p.reverb;
	m.output_gate = p.gateOutput;

	std::vector<float> out(in.size());
	const bool freeze[kBlock] = {};
	beads::FloatFrame fin[kBlock], fout[kBlock];
	for (size_t f = 0; f < in.size() / 2; f += kBlock) {
		for (size_t i = 0; i < kBlock; i++) {
			fin[i].l = in[2 * (f + i)];
			fin[i].r = in[2 * (f + i) + 1];
		}
		proc->Process(beads::SeedButtonState(p.seedState), nullptr, freeze, d, bp, m, fin, fout, kBlock);
		for (size_t i = 0; i < kBlock; i++) {
			out[2 * (f + i)] = fout[i].l;
			out[2 * (f + i) + 1] = fout[i].r;
		}
	}
	return out;
}

double rms(const std::vector<float>& v) {
	double sum = 0.0;
	for (float x : v) sum += double(x) * double(x);
	return std::sqrt(sum / double(v.size()));
}

void refRuns() {
	std::vector<float> out = runRef(cloudParams(), sineIn());
	bool finite = true;
	for (float x : out) finite = finite && std::isfinite(x);
	CHECK(finite, "reference output is finite");
	double r = rms(out);
	CHECK(r > 0.01, "reference makes sound: RMS %g", r);
}

void vendoredMatchesRefUnchanged() {
	std::vector<float> in = sineIn();
	std::vector<float> ref = runRef(cloudParams(), in);
	std::vector<float> ven = runVendored(cloudParams(), in);
	size_t firstDiff = ref.size();
	for (size_t i = 0; i < ref.size(); i++) {
		if (!(ven[i] == ref[i])) {
			firstDiff = i;
			break;
		}
	}
	CHECK(firstDiff == ref.size(), "every sample equal; first difference at frame %zu (%s): ref %.9g, vendored %.9g",
		firstDiff / 2, firstDiff % 2 ? "R" : "L",
		firstDiff < ref.size() ? double(ref[firstDiff]) : 0.0,
		firstDiff < ref.size() ? double(ven[firstDiff]) : 0.0);
	CHECK(rms(ven) > 0.01, "vendored makes sound: RMS %g", rms(ven));
}

}  // namespace

int main() {
	refRuns();
	vendoredMatchesRefUnchanged();
	return rkatest::finish("test_vendor");
}
