// The vendored Beads (src/spindrift/beads/, stmlib/) against the untouched
// upstream copy (testdata/ref/, through rkaref::BeadsRef). The vendored copy
// carries Spindrift's marked changes (CHANGES.md): Random's state per
// instance (BeadsContext), fixed-size blocks, buffers from BeadsMemory and
// the window's accessors. None of them may change a sample: driven the same
// way, with the same seed, the vendored Processor and Synth give the
// reference's output bit for bit, in every mode.
//
// The reference uses upstream's one global Random, so every BeadsRef shares
// one stream: each comparison seeds one reference and runs it alone (in
// lockstep with the vendored side, whose stream is its own context's).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "testing.hpp"
#include "testdata/RefBeads.hpp"
#include "BeadsContext.hpp"

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "stmlib/utils/random.h"

namespace {

const float kPi = 3.14159265358979f;
const int kBlockRate = 4000;  // beads.cc: blocks of sample_rate / 4000 frames
const uint32_t kSeed = 0x5eed;

// Beads' own rate per media (Processor::sample_rate(), shipping firmware):
// Dirty Tape, Clean Tape, Vintage Digital, Digital.
const int kMediaRate[4] = {48000, 48000, 32000, 48000};
const char* const kMediaName[4] = {"dirty tape", "clean tape", "vintage digital", "digital"};

void toBeads(const rkaref::RefParams& p, beads::Parameters* bp, beads::ModulatedParameter* d,
             beads::Processor::MixParameters* m) {
	bp->time.base = p.time; bp->time.attenuverter = p.timeAtt; bp->time.cv = p.timeCv; bp->time.has_cv = p.timeHasCv;
	bp->size.base = p.size; bp->size.attenuverter = p.sizeAtt; bp->size.cv = p.sizeCv; bp->size.has_cv = p.sizeHasCv;
	bp->shape.base = p.shape; bp->shape.attenuverter = p.shapeAtt; bp->shape.cv = p.shapeCv; bp->shape.has_cv = p.shapeHasCv;
	bp->pitch.base = p.pitch; bp->pitch.attenuverter = p.pitchAtt; bp->pitch.cv = p.pitchCv; bp->pitch.has_cv = p.pitchHasCv;
	bp->monophonic_mode = false;
	d->base = p.density;
	d->cv = p.densityCv;
	m->feedback = p.feedback;
	m->dry_wet = p.dryWet;
	m->reverb_amount = p.reverb;
	m->output_gate = p.gateOutput;
}

// The vendored Processor and Synth with their own context and memory,
// driven as RefBeads.cpp drives the reference.
struct Vendored {
	spindrift::BeadsContext ctx;
	spindrift::BeadsMemory mem;
	std::unique_ptr<beads::Processor> proc{new beads::Processor()};
	std::unique_ptr<beads::Synth> synth{new beads::Synth()};
	beads::FloatFrame in[spindrift::kMaxBlock], out[spindrift::kMaxBlock];

	explicit Vendored(uint32_t seed) {
		bool ok = mem.allocate();
		CHECK(ok, "BeadsMemory allocates");
		uint8_t* banks[3];
		mem.banks(banks);
		spindrift::BeadsScope scope(ctx);
		proc->Init(banks, mem.reverb);
		synth->Init(mem.reverb);
		stmlib::Random::Seed(seed);  // after Init, as the reference is seeded after construction
	}

	void configure(int media, bool mono) {
		spindrift::BeadsScope scope(ctx);
		proc->Configure(beads::Processor::Media(media), mono);
	}

	void processBlock(const rkaref::RefParams& p, const uint8_t* seedFlags, const bool* freeze,
	                  const float* inLR, float* outLR, size_t frames) {
		beads::Parameters bp;
		beads::ModulatedParameter d;
		beads::Processor::MixParameters m;
		toBeads(p, &bp, &d, &m);
		for (size_t i = 0; i < frames; i++) {
			in[i].l = inLR[2 * i];
			in[i].r = inLR[2 * i + 1];
		}
		spindrift::BeadsScope scope(ctx);
		proc->Process(beads::SeedButtonState(p.seedState), seedFlags, freeze, d, bp, m, in, out, frames);
		for (size_t i = 0; i < frames; i++) {
			outLR[2 * i] = out[i].l;
			outLR[2 * i + 1] = out[i].r;
		}
	}

	void synthBlock(const rkaref::RefParams& p, const uint8_t* seedFlags, const bool* freeze,
	                int resolution, float* outLR, size_t frames) {
		beads::Parameters bp;
		beads::ModulatedParameter d;
		beads::Processor::MixParameters m;
		toBeads(p, &bp, &d, &m);
		spindrift::BeadsScope scope(ctx);
		synth->Render(beads::SeedButtonState(p.seedState), seedFlags, freeze,
		              beads::Synth::Resolution(resolution), d, bp, m, out, frames);
		for (size_t i = 0; i < frames; i++) {
			outLR[2 * i] = out[i].l;
			outLR[2 * i + 1] = out[i].r;
		}
	}
};

// A deterministic input: two sines and a little noise (the test's own
// generator, not stmlib's), different on each side.
struct Input {
	uint32_t lcg = 12345;
	long n = 0;
	int rate;
	explicit Input(int r) : rate(r) {}
	void next(float* lr) {
		lcg = lcg * 1664525u + 1013904223u;
		float noise = float(int32_t(lcg)) / 2147483648.f;
		float t = float(n++) / float(rate);
		lr[0] = 0.4f * std::sin(2.f * kPi * 220.f * t) + 0.1f * noise;
		lr[1] = 0.4f * std::sin(2.f * kPi * 331.f * t) - 0.1f * noise;
	}
};

// SEED as Beads hands it to the DSP: stmlib::GateFlags per frame, from a
// clock high for the first 5 ms of each period.
struct SeedClock {
	float periodS;
	uint8_t previous = 0;
	uint8_t next(double t) {
		double phase = std::fmod(t, double(periodS));
		bool high = phase < 0.005;
		uint8_t f = high ? (previous & 1 ? 1 : 3) : (previous & 1 ? 4 : 0);
		previous = f;
		return f;
	}
};

// One stretch of the processor comparison.
struct Segment {
	const char* name;
	float density, size;
	int seedState;      // 0 off, 1 on, 2 locked
	float clockS;       // SEED clock period; 0: SEED unpatched
	bool cv;            // CVs patched (time, size, shape, pitch; density CV)
	bool freezeHalf;    // freeze from halfway through
	bool gate;          // gate output mode
};

const float kSegmentS = 3.f;
// Seed is latched (as Beads starts) unless the segment says otherwise; with
// it off and SEED quiet Beads makes no grains.
const Segment kSegments[] = {
	{"grain, density 0.2", 0.2f, 0.5f, 2, 0.f, false, false, false},
	{"grain, density 0.5, Seed pulses every 50 ms", 0.5f, 0.5f, 0, 0.05f, false, false, false},
	{"grain, density 0.8", 0.8f, 0.5f, 2, 0.f, false, false, false},
	{"grain, density 0.2, CVs", 0.2f, 0.5f, 2, 0.f, true, false, false},
	{"grain, density 0.5, Seed pulses every 50 ms, CVs", 0.5f, 0.5f, 0, 0.05f, true, false, true},
	{"grain, density 0.8, CVs", 0.8f, 0.5f, 2, 0.f, true, false, true},
	{"grain, Seed held, density 0.3", 0.3f, 0.5f, 1, 0.f, false, false, false},
	{"grain, Seed locked, clock on SEED", 0.6f, 0.5f, 2, 0.125f, false, false, true},
	{"grain, frozen halfway", 0.7f, 0.5f, 2, 0.f, false, true, false},
	{"delay (Size 1.0)", 0.3f, 1.f, 2, 0.f, false, false, false},
	{"delay, Seed locked, clock on SEED", 0.6f, 1.f, 2, 0.2f, true, false, true},
	{"delay, frozen halfway", 0.6f, 1.f, 2, 0.f, false, true, false},
};

rkaref::RefParams segmentParams(const Segment& s, float t) {
	rkaref::RefParams p = {};
	p.time = 0.3f;
	p.size = s.size;
	p.shape = 0.4f;
	p.pitch = 0.f;
	// Every attenuverter at +-0.6: without CVs it picks the random
	// modulation's kind, with CVs the CV's polarity; the signs flip between
	// the two so each parameter sees both.
	float sign = s.cv ? -1.f : 1.f;
	p.timeAtt = 0.6f * sign;
	p.sizeAtt = -0.6f * sign;
	p.shapeAtt = 0.6f * sign;
	p.pitchAtt = -0.6f * sign;
	p.timeHasCv = p.sizeHasCv = p.shapeHasCv = p.pitchHasCv = s.cv;
	if (s.cv) {
		p.timeCv = 0.4f * std::sin(2.f * kPi * 0.7f * t);
		p.sizeCv = 0.4f * std::sin(2.f * kPi * 0.31f * t + 1.f);
		p.shapeCv = 0.4f * std::sin(2.f * kPi * 0.53f * t + 2.f);
		p.pitchCv = 7.f * std::sin(2.f * kPi * 0.23f * t);  // semitones
		p.densityCv = 0.2f * std::sin(2.f * kPi * 0.9f * t);
	}
	p.density = s.density;
	p.feedback = 0.4f;
	p.dryWet = 0.8f;
	p.reverb = 0.4f;
	p.seedState = s.seedState;
	p.gateOutput = s.gate;
	return p;
}

bool firstDifference(const float* ref, const float* ven, size_t n, size_t* at) {
	for (size_t i = 0; i < n; i++) {
		if (!(ven[i] == ref[i])) {
			*at = i;
			return true;
		}
	}
	return false;
}

// One media, mono or stereo: every segment in turn on one running pair.
// Returns the output's RMS (to show it is not silence), or -1 after a
// difference.
double compareProcessor(int media, bool mono) {
	const int rate = kMediaRate[media];
	const size_t block = size_t(rate / kBlockRate);
	rkaref::BeadsRef ref;
	ref.seed(kSeed);
	ref.configure(media, mono);
	Vendored ven(kSeed);
	ven.configure(media, mono);

	Input input(rate);
	float inLR[2 * spindrift::kMaxBlock], refOut[2 * spindrift::kMaxBlock], venOut[2 * spindrift::kMaxBlock];
	bool freeze[spindrift::kMaxBlock];
	uint8_t flags[spindrift::kMaxBlock];
	double sum = 0.0;
	long count = 0;
	long frame = 0;
	spindrift::GrainSnap grains[32];
	spindrift::TapSnap taps[32];
	for (const Segment& s : kSegments) {
		const long frames = long(kSegmentS * float(rate));
		SeedClock clock{s.clockS > 0.f ? s.clockS : 1.f};
		int maxGrains = 0, maxTaps = 0;  // what the segment exercised
		for (long f = 0; f < frames; f += long(block)) {
			const float t = float(frame) / float(rate);
			rkaref::RefParams p = segmentParams(s, t);
			for (size_t i = 0; i < block; i++) {
				input.next(&inLR[2 * i]);
				freeze[i] = s.freezeHalf && f + long(i) >= frames / 2;
				flags[i] = clock.next(double(f + long(i)) / double(rate));
			}
			const uint8_t* seed = s.clockS > 0.f ? flags : nullptr;
			ref.processBlock(p, seed, freeze, inLR, refOut, block);
			ven.processBlock(p, seed, freeze, inLR, venOut, block);
			size_t at;
			if (firstDifference(refOut, venOut, 2 * block, &at)) {
				CHECK(false, "%s, %s, %s: first difference at %.4f s into the segment (%s): ref %.9g, vendored %.9g",
					kMediaName[media], mono ? "mono" : "stereo", s.name,
					double(f + long(at / 2)) / double(rate), at % 2 ? "R" : "L",
					double(refOut[at]), double(venOut[at]));
				return -1.0;
			}
			for (size_t i = 0; i < 2 * block; i++) sum += double(venOut[i]) * double(venOut[i]);
			count += long(2 * block);
			frame += long(block);
			maxGrains = std::max(maxGrains, ven.proc->grains(grains, 32));
			maxTaps = std::max(maxTaps, ven.proc->taps(taps, 32));
		}
		// The comparison means something only if the segment made grains
		// (or, in delay mode, taps).
		const bool delay = s.size >= 1.f;
		CHECK(delay ? maxTaps > 0 : maxGrains > 0, "%s, %s, %s: exercised (%d grains, %d taps at most)",
			kMediaName[media], mono ? "mono" : "stereo", s.name, maxGrains, maxTaps);
	}
	return std::sqrt(sum / double(count));
}

// Synth mode at one resolution: 3 s with the bank swept (Feedback), Seed
// locked with a clock then free, a freeze, pitch CV, and gate output.
double compareSynth(int resolution) {
	const int rate = 48000;  // synth mode always runs at 48 kHz (beads.cc)
	const size_t block = size_t(rate / kBlockRate);
	rkaref::BeadsRef ref;
	ref.seed(kSeed);
	Vendored ven(kSeed);
	float refOut[2 * spindrift::kMaxBlock], venOut[2 * spindrift::kMaxBlock];
	bool freeze[spindrift::kMaxBlock];
	uint8_t flags[spindrift::kMaxBlock];
	SeedClock clock{0.1f};
	double sum = 0.0;
	long count = 0;
	const long frames = 3 * rate;
	for (long f = 0; f < frames; f += long(block)) {
		const float t = float(f) / float(rate);
		const bool firstHalf = t < 1.5f;
		rkaref::RefParams p = {};
		p.time = 0.5f; p.timeAtt = 0.6f;
		p.size = 0.6f; p.sizeAtt = -0.6f;
		p.shape = 0.3f; p.shapeAtt = 0.6f;
		p.pitch = -5.f; p.pitchAtt = -0.6f; p.pitchCv = 5.f * std::sin(2.f * kPi * 0.4f * t); p.pitchHasCv = true;
		p.density = firstHalf ? 0.65f : 0.85f;
		p.densityCv = 0.f;
		p.feedback = t / 3.f;  // the bank, swept across all twelve
		p.dryWet = 0.6f;
		p.reverb = 0.3f;
		p.seedState = firstHalf ? 2 : 0;
		p.gateOutput = t >= 2.f;
		for (size_t i = 0; i < block; i++) {
			const float ti = float(f + long(i)) / float(rate);
			freeze[i] = ti >= 1.f && ti < 1.5f;
			flags[i] = clock.next(double(ti));
		}
		const uint8_t* seed = firstHalf ? flags : nullptr;
		ref.synthBlock(p, seed, freeze, resolution, refOut, block);
		ven.synthBlock(p, seed, freeze, resolution, venOut, block);
		size_t at;
		if (firstDifference(refOut, venOut, 2 * block, &at)) {
			CHECK(false, "synth, resolution %d: first difference at %.4f s (%s): ref %.9g, vendored %.9g",
				resolution, double(f + long(at / 2)) / double(rate), at % 2 ? "R" : "L",
				double(refOut[at]), double(venOut[at]));
			return -1.0;
		}
		for (size_t i = 0; i < 2 * block; i++) sum += double(venOut[i]) * double(venOut[i]);
		count += long(2 * block);
	}
	return std::sqrt(sum / double(count));
}

void matchesRefEveryMode() {
	for (int media = 0; media < 4; media++) {
		for (int mono = 0; mono < 2; mono++) {
			double r = compareProcessor(media, mono != 0);
			CHECK(r < 0.0 || r > 0.01, "%s, %s: output RMS %g", kMediaName[media], mono ? "mono" : "stereo", r);
		}
	}
	for (int resolution = 0; resolution < 4; resolution++) {
		double r = compareSynth(resolution);
		CHECK(r < 0.0 || r > 0.01, "synth, resolution %d: output RMS %g", resolution, r);
	}
}

// Two seconds of Digital stereo grains at Density 0.8 with random modulation
// on every parameter, on instances that run interleaved block by block. Time
// stays near the head: the line counts as full from the start, so grains
// reaching further back than what has been written are silent.
rkaref::RefParams cloud() {
	rkaref::RefParams p = {};
	p.time = 0.05f; p.timeAtt = 0.1f;
	p.size = 0.5f; p.sizeAtt = 0.6f;
	p.shape = 0.4f; p.shapeAtt = 0.6f;
	p.pitch = 0.f; p.pitchAtt = 0.6f;
	p.density = 0.8f;
	p.dryWet = 1.f;
	p.reverb = 0.2f;
	p.seedState = 2;  // latched
	return p;
}

void runInterleaved(Vendored& a, Vendored& b, std::vector<float>& outA, std::vector<float>& outB) {
	const size_t block = 12;
	const long frames = 2 * 48000;
	Input input(48000);
	float inLR[2 * spindrift::kMaxBlock];
	const bool freeze[spindrift::kMaxBlock] = {};
	outA.assign(2 * size_t(frames), 0.f);
	outB.assign(2 * size_t(frames), 0.f);
	for (long f = 0; f < frames; f += long(block)) {
		for (size_t i = 0; i < block; i++) input.next(&inLR[2 * i]);
		a.processBlock(cloud(), nullptr, freeze, inLR, &outA[2 * size_t(f)], block);
		b.processBlock(cloud(), nullptr, freeze, inLR, &outB[2 * size_t(f)], block);
	}
}

double rms(const std::vector<float>& v) {
	double sum = 0.0;
	for (float x : v) sum += double(x) * double(x);
	return std::sqrt(sum / double(v.size()));
}

void twoContextsDiffer() {
	Vendored a(1), b(2);
	std::vector<float> outA, outB;
	runInterleaved(a, b, outA, outB);
	size_t differing = 0, sounding = 0;
	for (size_t i = 0; i < outA.size(); i++) {
		differing += outA[i] != outB[i];
		sounding += outA[i] != 0.f || outB[i] != 0.f;
	}
	CHECK(sounding > outA.size() / 2 && differing > sounding * 9 / 10,
		"seeds 1 and 2 give different output: %zu of %zu sounding samples differ (of %zu)",
		differing, sounding, outA.size());
	CHECK(rms(outA) > 0.01 && rms(outB) > 0.01, "both make sound: RMS %g, %g", rms(outA), rms(outB));
}

void sameSeedSameOutput() {
	// Interleaved: if the two shared one random stream, each would take
	// every other draw and they would differ.
	Vendored a(7), b(7);
	std::vector<float> outA, outB;
	runInterleaved(a, b, outA, outB);
	size_t at = 0;
	bool differ = firstDifference(outA.data(), outB.data(), outA.size(), &at);
	CHECK(!differ, "same seed, same output; first difference at frame %zu", at / 2);
	CHECK(rms(outA) > 0.01, "makes sound: RMS %g", rms(outA));
}

bool unit(float x) {
	return x >= 0.f && x <= 1.f;
}

// The window's accessors: grains at Density 0.8, the write head moving,
// taps in delay mode, and synth mode's wave and grains.
void snapReportsGrains() {
	Vendored v(3);
	const size_t block = 12;
	Input input(48000);
	float inLR[2 * spindrift::kMaxBlock], outLR[2 * spindrift::kMaxBlock];
	const bool freeze[spindrift::kMaxBlock] = {};
	spindrift::GrainSnap grains[40];
	spindrift::TapSnap taps[40];

	int maxGrains = 0, badGrains = 0, maxTaps = 0, badTaps = 0, headMoves = 0, badHead = 0;
	float lastHead = v.proc->writeHead();
	rkaref::RefParams p = cloud();
	for (long f = 0; f < 4 * 48000; f += long(block)) {
		const bool delayMode = f >= 2 * 48000;
		p.size = delayMode ? 1.f : 0.5f;
		for (size_t i = 0; i < block; i++) input.next(&inLR[2 * i]);
		v.processBlock(p, nullptr, freeze, inLR, outLR, block);

		const int n = v.proc->grains(grains, 32);
		if (n < 0 || n > 32) badGrains++;
		if (!delayMode && n > maxGrains) maxGrains = n;
		for (int i = 0; i < n && i < 32; i++) {
			const spindrift::GrainSnap& g = grains[i];
			// A sounding grain has rendered at least once: its envelope is
			// above 0 (a finished one reads 0).
			if (!unit(g.position) || !unit(g.span) || !unit(g.pan) || !(g.env > 0.f && g.env <= 1.f)) badGrains++;
		}
		const int nt = v.proc->taps(taps, 32);
		if (nt < 0 || nt > 32) badTaps++;
		if (!delayMode && nt != 0) badTaps++;
		if (nt > maxTaps) maxTaps = nt;
		for (int i = 0; i < nt && i < 32; i++) {
			if (!unit(taps[i].position) || !(taps[i].gain >= 0.f)) badTaps++;
		}
		const float head = v.proc->writeHead();
		if (!unit(head)) badHead++;
		headMoves += head != lastHead;
		lastHead = head;
	}
	CHECK(maxGrains > 0, "grains() reports sounding grains at Density 0.8: at most %d", maxGrains);
	CHECK(badGrains == 0, "every grain sounding and within 0..1, at most 32: %d bad", badGrains);
	CHECK(v.proc->grains(grains, 2) <= 2, "grains() honours max");
	CHECK(maxTaps > 0, "taps() reports taps in delay mode: at most %d", maxTaps);
	CHECK(badTaps == 0, "taps only in delay mode, within 0..1: %d bad", badTaps);
	CHECK(badHead == 0 && headMoves > 1000, "the write head stays in 0..1 and moves: %d out, %d moves",
		badHead, headMoves);

	// Synth mode: the wave follows Time, and the grains report a wave
	// position in 0..1 with no span.
	int synthGrains = 0, badSynth = 0;
	rkaref::RefParams s = cloud();
	s.time = 0.75f; s.timeAtt = 0.f;
	for (long f = 0; f < 48000; f += long(block)) {
		v.synthBlock(s, nullptr, freeze, 3, outLR, block);
		const int n = v.synth->grains(grains, 32);
		if (n > synthGrains) synthGrains = n;
		for (int i = 0; i < n; i++) {
			if (!unit(grains[i].position) || grains[i].span != 0.f || !unit(grains[i].pan) || !unit(grains[i].env))
				badSynth++;
		}
	}
	const float wave = v.synth->wave();
	CHECK(std::fabs(wave - 0.75f) < 0.01f, "Synth::wave() follows Time 0.75: %g", double(wave));
	CHECK(synthGrains > 0 && badSynth == 0, "synth grains reported (at most %d), in range: %d bad",
		synthGrains, badSynth);
}

}  // namespace

int main() {
	matchesRefEveryMode();
	twoContextsDiffer();
	sameSeedSameOutput();
	snapReportsGrains();
	return rkatest::finish("test_vendor");
}
