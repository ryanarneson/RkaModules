// Spindrift's engine (SpindriftEngine.hpp): Beads' hardware glue re-written
// Rack-free, tested against the untouched upstream Beads (rkaref::BeadsRef)
// driven the way beads.cc drives it, and on its own for the glue beads.cc
// is (the SEED delay, the mutes, the synth switch, mono, quality, levels,
// rates). Built with the sanitizers and a counting operator new.
//
// The reference is driven by this file's own copy of beads.cc's control
// path (Glue, below: CvReader's pot laws and filters, the calibrated CV
// channels, the SEED delay line), written from the firmware side by side
// (TESTING.md rule 2), at Rack 48 kHz in Digital, where the edge converters
// pass through and the engine must give Beads' output to float tolerance,
// one block (its reported latency) late. The reference keeps upstream's one
// global Random: each comparison seeds one reference and runs it alone.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <vector>

#include "testing.hpp"
#include "testdata/RefBeads.hpp"
#include "SpindriftEngine.hpp"

// Heap allocations made by operator new on this thread, while counting.
static thread_local long tAllocs = 0;
static thread_local bool tCounting = false;

void* operator new(std::size_t n) {
	if (tCounting)
		tAllocs++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
	if (tCounting)
		tAllocs++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
	if (tCounting)
		tAllocs++;
	return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
	if (tCounting)
		tAllocs++;
	return std::malloc(n ? n : 1);
}
void operator delete(void* p) noexcept {
	std::free(p);
}
void operator delete[](void* p) noexcept {
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
	std::free(p);
}

using spindrift::Jacks;
using spindrift::Knobs;
using spindrift::Out;
using spindrift::SpindriftEngine;

namespace {

const double kPi = 3.14159265358979323846;
const uint32_t kSeed = 0x5eed;
const int kRate = 48000;
const int kBlock = 12;  // Beads' block at 48 kHz

// stmlib::DelayLine<.., 256>::Read(d) returns the sample written d - 1
// writes before the newest (Read(1) is the one just written), so beads.cc's
// event_delay.Read(size * 10) puts SEED 119 frames late at 48 kHz: 2.5 ms
// less one frame. The engine keeps upstream's line and its Read.
const int kSeedDelayFrames = kBlock * 10 - 1;

Knobs defaultKnobs() {
	Knobs k;
	k.time = k.size = k.shape = k.pitch = k.density = 0.5f;
	k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
	k.feedback = k.dryWet = k.reverb = 0.f;
	return k;
}

Jacks noJacks() {
	Jacks j = {};
	return j;
}

// The test's own input: two sines and a little noise, volts.
struct Signal {
	uint32_t lcg = 12345;
	void at(long n, float* l, float* r) {
		lcg = lcg * 1664525u + 1013904223u;
		const float noise = float(int32_t(lcg)) / 2147483648.f;
		const double t = double(n) / kRate;
		*l = float(4.0 * std::sin(2 * kPi * 220.0 * t)) + 0.5f * noise;
		*r = float(4.0 * std::sin(2 * kPi * 331.0 * t)) - 0.5f * noise;
	}
};

// A Schmitt trigger at 0.1 V / 1 V, as the house standard (CLAUDE.md).
struct Schmitt {
	bool high = false;
	bool process(float v) {
		if (high && v <= 0.1f)
			high = false;
		else if (!high && v >= 1.f)
			high = true;
		return high;
	}
};

// beads.cc's control path, written from the firmware for the reference:
// CvReader (cv_reader.cc: pot laws, lp 0.05, Time's hysteresis 0.001; CV
// channels' lp 0.5 Time/Size/Shape, 1 Pitch/Density, 0.1 assign), the
// calibration targets (settings.cc), the mix parameters (beads.cc:213-222)
// and the SEED delay (beads.cc:145-156).
struct Glue {
	spindrift::PotFilter time, size, shape, pitch, density, timeAtt, sizeAtt, shapeAtt, pitchAtt, feedback, dryWet, reverb;
	spindrift::CvChannel cvs[8];
	int seedDelay = kSeedDelayFrames;
	std::vector<uint8_t> seedHistory;  // SEED high per frame, from frame 0
	uint8_t previous = 0;

	Glue() {
		spindrift::PotFilter* all[] = {&time, &size, &shape, &pitch, &density, &timeAtt, &sizeAtt, &shapeAtt, &pitchAtt, &feedback, &dryWet, &reverb};
		for (spindrift::PotFilter* p : all)
			p->init(0.05f, p == &time ? 0.001f : 0.f);
	}

	// The CVs as volts and Beads' unit per volt (calibration: +-5 V is +-1,
	// Pitch 12 semitones a volt). cv_scaling replaces this with the values
	// Beads should see, at one unit per "volt".
	static void beadsCvs(const Jacks& j, float* v, float* scale) {
		const float volts[8] = {j.time, j.size, j.shape, j.pitch, j.density, j.feedback, j.dryWet, j.reverb};
		for (int i = 0; i < 8; i++) {
			v[i] = volts[i];
			scale[i] = i == 3 ? 12.f : 0.2f;
		}
	}

	// CvReader::Init's channels (cv_reader.cc:56-87): lp and limits; the ADC
	// reads +-8 V on Time, Size and Shape, +-5 V on the others.
	rkaref::RefParams block(const Knobs& k, const Jacks& j, const float* cvIn, const float* scale, int seedState, bool gate) {
		const float coeff[8] = {0.5f, 0.5f, 0.5f, 1.f, 1.f, 0.1f, 0.1f, 0.1f};
		const float lo[8] = {-1.6f, -1.6f, -1.6f, -60.f, -1.f, -1.f, -1.f, -1.f};
		const float adc[8] = {8.f, 8.f, 8.f, 5.f, 5.f, 5.f, 5.f, 5.f};
		float cv[8];
		for (int i = 0; i < 8; i++)
			cv[i] = cvs[i].process(cvIn[i], scale[i], scale[i] == 1.f ? 1e9f : adc[i], coeff[i], lo[i], -lo[i]);
		rkaref::RefParams p = {};
		p.time = time.process(k.time);
		p.size = size.process(k.size);
		p.shape = shape.process(k.shape);
		p.pitch = pitch.process(spindrift::detuneLaw(k.pitch));
		p.density = density.process(k.density);
		p.timeAtt = timeAtt.process(spindrift::attenuverterLaw(k.timeAtt));
		p.sizeAtt = sizeAtt.process(spindrift::attenuverterLaw(k.sizeAtt));
		p.shapeAtt = shapeAtt.process(spindrift::attenuverterLaw(k.shapeAtt));
		p.pitchAtt = pitchAtt.process(spindrift::attenuverterLaw(k.pitchAtt));
		const float fb = feedback.process(k.feedback), dw = dryWet.process(k.dryWet), rv = reverb.process(k.reverb);
		p.timeCv = cv[0]; p.timeHasCv = j.timeOn;
		p.sizeCv = cv[1]; p.sizeHasCv = j.sizeOn;
		p.shapeCv = cv[2]; p.shapeHasCv = j.shapeOn;
		p.pitchCv = cv[3]; p.pitchHasCv = j.pitchOn;
		p.densityCv = j.densityOn ? cv[4] : 0.f;
		p.feedback = spindrift::mixParameter(fb, cv[5], j.feedbackOn);
		p.dryWet = spindrift::mixParameter(dw, cv[6], j.dryWetOn);
		p.reverb = spindrift::mixParameter(rv, cv[7], j.reverbOn);
		p.seedState = seedState;
		p.gateOutput = gate;
		return p;
	}

	// GateFlags for frames [f0, f0 + n), SEED read seedDelay frames late.
	void seedFlags(long f0, int n, bool patched, uint8_t* flags) {
		for (int i = 0; i < n; i++) {
			const long src = f0 + i - seedDelay;
			const bool high = src >= 0 && seedHistory[size_t(src)];
			if (patched)
				previous = flags[i] = stmlib::ExtractGateFlags(previous, high);
			else
				previous = 0;
		}
	}
};

typedef std::function<void(long, Knobs&, Jacks&)> Drive;

struct RefOptions {
	int seedState = 2;          // 0 off, 1 on, 2 locked: what the scenario's presses leave
	bool freezeButton = false;  // the scenario pressed Freeze before starting
	bool synth = false;         // SYNTH_ALWAYS: the reference renders the Synth
	bool gate = false;          // gate output mode
	int seedDelay = kSeedDelayFrames;
	// The CVs and their scales; default Glue::beadsCvs.
	std::function<void(const Jacks&, float*, float*)> cvs;
};

struct Compare {
	bool matched = false;
	double firstDiffS = -1.0, worst = 0.0;
	double rms = 0.0;   // of the engine's output, volts
	int maxGrains = 0, maxTaps = 0;
	long gateHigh = 0, gateLow = 0;
};

// Runs a fresh engine and a fresh reference side by side for `seconds` at
// 48 kHz Digital. The engine's output at sample n + latency is compared with
// the reference's frame n, in Beads' units (volts / 10), within 1e-6.
Compare compareNative(double seconds, const std::function<void(SpindriftEngine&)>& setup, const Drive& drive,
                      const RefOptions& o) {
	Compare c;
	SpindriftEngine e(kSeed);
	CHECK(e.allocate(), "engine allocates");
	e.setSampleRate(float(kRate));
	setup(e);

	Knobs k0 = defaultKnobs();
	Jacks j0 = noJacks();
	drive(0, k0, j0);
	const bool mono = !j0.inLOn || !j0.inROn;

	rkaref::BeadsRef ref;
	ref.seed(kSeed);
	ref.configure(3, mono);
	Glue g;
	g.seedDelay = o.seedDelay;

	const long frames = long(seconds * kRate) / kBlock * kBlock;
	g.seedHistory.assign(size_t(frames), 0);
	std::vector<float> engL((size_t) frames), engR((size_t) frames), refLR(2 * (size_t) frames);
	float inLR[2 * kBlock];
	bool freeze[kBlock];
	uint8_t flags[kBlock];
	Schmitt seedG, freezeG;
	spindrift::GrainSnap gs[32];
	spindrift::TapSnap ts[32];
	double sum = 0.0;
	const int latency = int(e.latency());
	CHECK(e.latency() == double(kBlock), "latency at 48 kHz Digital is one block: %g", e.latency());

	for (long n = 0; n < frames; n++) {
		Knobs k = defaultKnobs();
		Jacks j = noJacks();
		drive(n, k, j);
		Out out;
		e.process(k, j, out);
		engL[size_t(n)] = out.l;
		engR[size_t(n)] = out.r;
		sum += double(out.l) * out.l + double(out.r) * out.r;

		const int i = int(n % kBlock);
		// The input as the engine hands it to Beads at Digital and gain 1:
		// volts / 10, an unpatched side copied from the other (GainControl).
		float l = j.inLOn ? j.inL * 0.1f : 0.f, r = j.inROn ? j.inR * 0.1f : 0.f;
		l = std::min(std::max(l, -1.f), 1.f);
		r = std::min(std::max(r, -1.f), 1.f);
		if (!j.inROn) r = l;
		if (!j.inLOn) l = r;
		inLR[2 * i] = l;
		inLR[2 * i + 1] = r;
		g.seedHistory[size_t(n)] = j.seedOn && seedG.process(j.seed);
		freeze[i] = o.freezeButton || (j.freezeOn && freezeG.process(j.freeze));
		if (i == kBlock - 1) {
			float bcv[8], scale[8];
			if (o.cvs)
				o.cvs(j, bcv, scale);
			else
				Glue::beadsCvs(j, bcv, scale);
			rkaref::RefParams p = g.block(k, j, bcv, scale, o.seedState, o.gate);
			const long f0 = n - (kBlock - 1);
			g.seedFlags(f0, kBlock, j.seedOn, flags);
			const uint8_t* seed = j.seedOn ? flags : nullptr;
			if (o.synth)
				ref.synthBlock(p, seed, freeze, 3, &refLR[2 * size_t(f0)], kBlock);
			else
				ref.processBlock(p, seed, freeze, inLR, &refLR[2 * size_t(f0)], kBlock);
			for (int q = 0; q < 2 * kBlock; q++) {
				float& v = refLR[2 * size_t(f0) + size_t(q)];
				v = std::min(std::max(v, -1.f), 0.999999f);  // beads.cc's output conversion
			}
			const spindrift::SpindriftEngine& ce = e;
			c.maxGrains = std::max(c.maxGrains, o.synth ? ce.synthVoice().grains(gs, 32) : ce.processor().grains(gs, 32));
			c.maxTaps = std::max(c.maxTaps, ce.processor().taps(ts, 32));
		}
	}
	c.rms = std::sqrt(sum / (2.0 * double(frames)));
	c.matched = true;
	for (long n = latency; n < frames; n++) {
		const float rl = refLR[2 * size_t(n - latency)], rr = refLR[2 * size_t(n - latency) + 1];
		const float el = engL[size_t(n)] / 10.f;
		float er = engR[size_t(n)] / 10.f;
		float want = rr;
		if (o.gate) {
			// The gate leaves as 0/10 V: Beads' -0.8 becomes 10 V.
			CHECK(engR[size_t(n)] == 0.f || engR[size_t(n)] == 10.f, "gate output is 0 or 10 V: %g", double(engR[size_t(n)]));
			(engR[size_t(n)] > 0.f ? c.gateHigh : c.gateLow)++;
			er = engR[size_t(n)] > 0.f ? 1.f : 0.f;
			want = rr < -0.4f ? 1.f : 0.f;
		}
		const double d = std::max(std::fabs(double(el) - rl), std::fabs(double(er) - want));
		c.worst = std::max(c.worst, d);
		if (!(d <= 1e-6)) {
			if (c.matched)
				c.firstDiffS = double(n) / kRate;
			c.matched = false;
		}
	}
	return c;
}

// Scenario A: grains in stereo, every CV moving, a clock in SEED, a freeze
// by the gate, the knobs moving.
void driveGrain(long n, Knobs& k, Jacks& j) {
	static thread_local Signal sig;
	if (n == 0)
		sig = Signal();
	const double t = double(n) / kRate;
	k.time = float(0.3 + 0.2 * std::sin(2 * kPi * 0.15 * t));
	k.size = 0.45f;
	k.shape = 0.4f;
	k.pitch = 0.55f;
	k.density = 0.72f;
	k.timeAtt = 0.85f;
	k.sizeAtt = 0.2f;
	k.shapeAtt = 0.7f;
	k.pitchAtt = 0.3f;
	k.feedback = 0.4f;
	k.dryWet = 0.7f;
	k.reverb = 0.35f;
	j.inLOn = j.inROn = true;
	sig.at(n, &j.inL, &j.inR);
	j.timeOn = j.sizeOn = j.pitchOn = j.densityOn = j.dryWetOn = j.reverbOn = true;
	j.time = float(2.0 * std::sin(2 * kPi * 0.7 * t));
	j.size = float(1.5 * std::sin(2 * kPi * 0.31 * t + 1.0));
	j.pitch = float(0.5 * std::sin(2 * kPi * 0.23 * t));
	j.density = float(1.0 * std::sin(2 * kPi * 0.9 * t));
	j.dryWet = float(-1.0 + 0.5 * std::sin(2 * kPi * 0.4 * t));
	j.reverb = 1.f;
	j.seedOn = true;
	j.seed = std::fmod(t, 0.125) < 0.005 ? 5.f : 0.f;
	j.freezeOn = true;
	j.freeze = (t >= 2.5 && t < 3.0) ? 10.f : 0.f;
}

void matchesRefAtNativeRate() {
	const auto none = [](SpindriftEngine&) {};
	{
		Compare c = compareNative(4.0, none, driveGrain, RefOptions());
		CHECK(c.matched, "grain, stereo, CVs, SEED clock, freeze gate: first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
		CHECK(c.rms > 0.3 && c.maxGrains > 0, "grain: exercised (rms %.3f V, %d grains)", c.rms, c.maxGrains);
	}
	{
		Drive d = [](long n, Knobs& k, Jacks& j) {
			driveGrain(n, k, j);
			k.size = 1.f;  // delay mode after 1.5 s
			k.density = 0.3f;
			j.seedOn = false;
			j.freezeOn = false;
		};
		Compare c = compareNative(4.0, [](SpindriftEngine&) {}, d, RefOptions());
		CHECK(c.matched, "delay mode: first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
		CHECK(c.rms > 0.3 && c.maxTaps > 0, "delay: exercised (rms %.3f V, %d taps)", c.rms, c.maxTaps);
	}
	{
		Drive d = [](long n, Knobs& k, Jacks& j) {
			driveGrain(n, k, j);
			j.inROn = false;  // mono: L copied to R
			j.freezeOn = false;
		};
		Compare c = compareNative(3.0, none, d, RefOptions());
		CHECK(c.matched, "mono (R unpatched): first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
		CHECK(c.rms > 0.3 && c.maxGrains > 0, "mono: exercised (rms %.3f V)", c.rms);
	}
	{
		RefOptions o;
		o.gate = true;
		Compare c = compareNative(2.0, [](SpindriftEngine& e) { e.settings.gateOutput = true; }, driveGrain, o);
		CHECK(c.matched, "gate output: first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
		CHECK(c.gateHigh > 0 && c.gateLow > 0, "gate output: both levels seen (%ld high, %ld low)", c.gateHigh, c.gateLow);
	}
	{
		// Seed unlatched (one press and release): Beads' OFF state, SEED unpatched.
		RefOptions o;
		o.seedState = 0;
		Compare c = compareNative(2.0, [](SpindriftEngine& e) {
			e.buttons().seedDown();
			e.buttons().seedUp(1);
		}, [](long n, Knobs& k, Jacks& j) {
			driveGrain(n, k, j);
			j.seedOn = false;
		}, o);
		CHECK(c.matched, "Seed unlatched: first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
	}
	{
		// The Freeze button (the whole run frozen, so the buffer stays empty
		// of input but the grains and the dry path run).
		RefOptions o;
		o.freezeButton = true;
		Compare c = compareNative(1.5, [](SpindriftEngine& e) { e.buttons().freezePress(); }, driveGrain, o);
		CHECK(c.matched, "Freeze button: first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
	}
	{
		// The synth (Always): the reference renders Synth at 48 kHz, resolution 3.
		RefOptions o;
		o.synth = true;
		Drive d = [](long n, Knobs& k, Jacks& j) {
			driveGrain(n, k, j);
			k.feedback = float(n) / (3.f * kRate);  // the bank, swept
			k.density = 0.65f;
		};
		Compare c = compareNative(3.0, [](SpindriftEngine& e) { e.settings.synth = spindrift::SYNTH_ALWAYS; }, d, o);
		CHECK(c.matched, "synth: first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
		CHECK(c.rms > 0.3 && c.maxGrains > 0, "synth: exercised (rms %.3f V, %d grains)", c.rms, c.maxGrains);
	}
}

// SEED pulses every 50 ms into Density's dead zone with Seed unlatched: each
// rising edge makes one grain, so the engine matches the reference only with
// the SEED line delayed exactly as beads.cc delays it.
void seedDelay() {
	Drive d = [](long n, Knobs& k, Jacks& j) {
		driveGrain(n, k, j);
		k.density = 0.5f;
		k.time = 0.1f;  // grains from the recent buffer, so they sound
		k.size = 0.3f;
		j.timeOn = false;
		j.densityOn = false;
		j.freezeOn = false;
		const double t = double(n) / kRate;
		j.seed = std::fmod(t, 0.05) < 0.005 ? 5.f : 0.f;
	};
	auto setup = [](SpindriftEngine& e) {
		e.buttons().seedDown();
		e.buttons().seedUp(1);
	};
	RefOptions o;
	o.seedState = 0;
	Compare c = compareNative(1.5, setup, d, o);
	CHECK(c.matched, "SEED read %d frames late (event_delay.Read(12 * 10), 2.5 ms at 48 kHz): first difference at %.4f s",
	      kSeedDelayFrames, c.firstDiffS);
	CHECK(c.maxGrains > 1, "SEED made grains (%d at once)", c.maxGrains);
	const int wrong[] = {0, kSeedDelayFrames - 1, kSeedDelayFrames + 1};
	for (int w : wrong) {
		o.seedDelay = w;
		Compare x = compareNative(1.5, setup, d, o);
		CHECK(!x.matched, "the reference with SEED %d frames late differs from the engine (worst %g, grains %d)", w, x.worst, x.maxGrains);
	}
}

// With fixed CVs, the engine against a reference given the values Beads'
// calibration targets mean (calibrator.h, settings.cc), not computed from
// the volts: Pitch 1 V = 12 semitones (attenuverter full clockwise), every
// other CV +-5 V = +-1.
void cvScaling() {
	Drive d = [](long n, Knobs& k, Jacks& j) {
		driveGrain(n, k, j);
		k.pitchAtt = 1.f;
		k.timeAtt = 1.f;
		j.freezeOn = false;
		j.seedOn = false;
		j.timeOn = j.sizeOn = j.shapeOn = j.pitchOn = j.densityOn = j.feedbackOn = j.dryWetOn = j.reverbOn = true;
		k.time = 0.35f;
		j.pitch = 1.f;
		j.time = -1.25f;
		j.size = -5.f;
		j.shape = 2.5f;
		j.density = -2.5f;
		j.feedback = 2.5f;
		j.dryWet = -1.25f;
		j.reverb = 5.f;
	};
	RefOptions o;
	o.cvs = [](const Jacks&, float* v, float* scale) {
		v[0] = -0.25f; // Time -1.25 V
		v[1] = -1.f;   // Size -5 V
		v[2] = 0.5f;   // Shape 2.5 V
		v[3] = 12.f;   // Pitch 1 V: 12 semitones
		v[4] = -0.5f;  // Density -2.5 V
		v[5] = 0.5f;   // Feedback 2.5 V
		v[6] = -0.25f; // Dry/Wet -1.25 V
		v[7] = 1.f;    // Reverb 5 V
		for (int i = 0; i < 8; i++)
			scale[i] = 1.f;
	};
	Compare c = compareNative(1.5, [](SpindriftEngine&) {}, d, o);
	CHECK(c.matched, "CV scaling: first difference at %.4f s (worst %g)", c.firstDiffS, c.worst);
	// And the comparison can tell: 11 semitones a volt would not match.
	RefOptions bad = o;
	bad.cvs = [](const Jacks&, float* v, float* scale) {
		v[0] = -0.25f; v[1] = -1.f; v[2] = 0.5f; v[3] = 11.f; v[4] = -0.5f; v[5] = 0.5f; v[6] = -0.25f; v[7] = 1.f;
		for (int i = 0; i < 8; i++)
			scale[i] = 1.f;
	};
	Compare x = compareNative(1.5, [](SpindriftEngine&) {}, d, bad);
	CHECK(!x.matched, "a reference at 11 semitones a volt differs (worst %g, rms %g)", x.worst, x.rms);
}

// --- The glue on its own --------------------------------------------------

struct Rig {
	SpindriftEngine e;
	Knobs k = defaultKnobs();
	Jacks j = noJacks();
	long n = 0;
	int rate;
	int mutes = 0;           // rising edges of muted()
	bool wasMuted = false;
	bool sawSynthPatched = false;
	explicit Rig(int r = kRate, uint32_t seed = kSeed) : e(seed), rate(r) {
		CHECK(e.allocate(), "engine allocates");
		e.setSampleRate(float(r));
	}
	Out step() {
		Out o;
		e.process(k, j, o);
		n++;
		if (e.muted() && !wasMuted)
			mutes++;
		wasMuted = e.muted();
		if (e.mode() == spindrift::MODE_SYNTH && (j.inLOn || j.inROn) && e.settings.synth == spindrift::SYNTH_AUTO)
			sawSynthPatched = true;
		return o;
	}
	void run(double seconds, std::vector<Out>* rec = nullptr) {
		const long end = n + long(seconds * rate);
		while (n < end) {
			Out o = step();
			if (rec)
				rec->push_back(o);
		}
	}
	double t() const {
		return double(n) / rate;
	}
	// The engine acts on a block boundary: step until it has (at most two
	// of Beads' blocks at this rate).
	template <typename F>
	bool stepUntil(F done) {
		for (int i = 0; i < 2 * kBlock * rate / 24000 + 2; i++) {
			if (j.inLOn || j.inROn)
				sine(440.0, 5.f);
			step();
			if (done())
				return true;
		}
		return false;
	}
	void sine(double hz, float amp) {
		j.inL = j.inR = float(amp * std::sin(2 * kPi * hz * double(n) / rate));
	}
	void runSine(double seconds, double hz, float amp, std::vector<Out>* rec = nullptr) {
		const long end = n + long(seconds * rate);
		while (n < end) {
			sine(hz, amp);
			Out o = step();
			if (rec)
				rec->push_back(o);
		}
	}
};

double rms(const std::vector<Out>& v, size_t from, size_t to) {
	double s = 0.0;
	for (size_t i = from; i < to && i < v.size(); i++)
		s += double(v[i].l) * v[i].l + double(v[i].r) * v[i].r;
	return std::sqrt(s / (2.0 * double(std::max<size_t>(1, std::min(to, v.size()) - from))));
}

// Amplitude and phase (radians, of a sine) at hz, least squares over [from, to).
void fit(const std::vector<float>& x, int rate, double hz, size_t from, size_t to, double* amp, double* phase) {
	double ss = 0, sc = 0;
	for (size_t i = from; i < to; i++) {
		const double w = 2 * kPi * hz * double(i) / rate;
		ss += x[i] * std::sin(w);
		sc += x[i] * std::cos(w);
	}
	const double m = double(to - from);
	*amp = 2.0 * std::sqrt(ss * ss + sc * sc) / m;
	*phase = std::atan2(sc, ss);
}

void levels() {
	// DC: +10 V and -5 V in, Dry/Wet 0: Beads' 0.75 of +-1 and -0.5.
	{
		Rig r;
		r.j.inLOn = r.j.inROn = true;
		r.j.inL = 10.f;
		r.j.inR = -5.f;
		std::vector<Out> v;
		r.run(0.5, &v);
		const Out o = v.back();
		CHECK(std::fabs(o.l - 7.5f) < 1e-4f && std::fabs(o.r + 3.75f) < 1e-4f,
		      "+10 V and -5 V in give 0.75 of full scale and half of it: %g %g", double(o.l), double(o.r));
		// Past full scale Beads clips: +15 V is +10 V.
		r.j.inL = 15.f;
		r.j.inR = -20.f;
		v.clear();
		r.run(0.5, &v);
		CHECK(std::fabs(v.back().l - 7.5f) < 1e-4f && std::fabs(v.back().r + 7.5f) < 1e-4f,
		      "+15 V and -20 V clip at +-10 V in: %g %g", double(v.back().l), double(v.back().r));
	}
	// A 5 V sine, Dry/Wet 0: 3.75 V.
	{
		Rig r;
		r.j.inLOn = r.j.inROn = true;
		std::vector<Out> v;
		r.runSine(1.0, 1000.0, 5.f, &v);
		float peak = 0.f;
		for (size_t i = v.size() / 2; i < v.size(); i++)
			peak = std::max(peak, std::fabs(v[i].l));
		CHECK(std::fabs(peak - 3.75f) < 0.01f, "a 5 V sine leaves dry at 3.75 V: %g", double(peak));
	}
	// Gate output: R is 0 or 10 V, both seen; L a mono mix.
	{
		Rig r;
		r.e.settings.gateOutput = true;
		r.k.density = 0.85f;
		r.k.dryWet = 0.6f;
		r.j.inLOn = r.j.inROn = true;
		std::vector<Out> v;
		r.runSine(2.0, 220.0, 4.f, &v);
		long hi = 0, lo = 0, other = 0;
		for (const Out& o : v)
			(o.r == 10.f ? hi : (o.r == 0.f ? lo : other))++;
		CHECK(other == 0 && hi > 0 && lo > 0, "gate output: %ld at 10 V, %ld at 0 V, %ld other", hi, lo, other);
	}
}

void synthAfter10s() {
	Rig r;
	CHECK(r.e.mode() == spindrift::MODE_GRAIN, "not synth before the first sample");
	r.step();
	CHECK(r.e.mode() == spindrift::MODE_GRAIN && !r.e.muted(), "never synth at add, and no mute");
	r.run(9.9 - r.t());
	CHECK(r.e.mode() == spindrift::MODE_GRAIN, "still grain at 9.9 s");
	long synthAt = -1, unmuteAt = -1;
	bool zerosWhileMuted = true;
	std::vector<Out> v;
	while (r.t() < 10.5) {
		Out o = r.step();
		v.push_back(o);
		if (synthAt < 0 && r.e.mode() == spindrift::MODE_SYNTH)
			synthAt = r.n;
		if (synthAt >= 0 && r.e.muted() && (o.l != 0.f || o.r != 0.f))
			zerosWhileMuted = false;
		if (synthAt >= 0 && unmuteAt < 0 && !r.e.muted())
			unmuteAt = r.n;
	}
	const double ts = double(synthAt) / kRate;
	CHECK(synthAt > 0 && ts >= 10.0 && ts < 10.001, "synth at 10 s: %.5f s", ts);
	const double muteS = double(unmuteAt - synthAt) / kRate;
	CHECK(std::fabs(muteS - 0.25) < 0.001, "entering mutes 250 ms: %.4f s", muteS);
	CHECK(zerosWhileMuted, "zero output while muted, entering");
	CHECK(rms(v, v.size() - 4800, v.size()) > 0.5, "the synth sounds after the mute: rms %g V", rms(v, v.size() - 4800, v.size()));

	// Patching L leaves the synth at once (the next block), with a mute.
	r.j.inLOn = true;
	const bool left = r.stepUntil([&] { return r.e.mode() != spindrift::MODE_SYNTH; });
	CHECK(left, "leaves the synth when L is patched");
	CHECK(r.e.muted(), "leaving mutes");
	long start = r.n;
	bool zeros = true;
	while (r.e.muted()) {
		r.sine(440.0, 5.f);
		Out o = r.step();
		if (o.l != 0.f || o.r != 0.f)
			zeros = false;
	}
	CHECK(zeros, "zero output while muted, leaving");
	CHECK(std::fabs(double(r.n - start) / kRate - 0.25) < 0.001, "leaving mutes 250 ms: %.4f s", double(r.n - start) / kRate);
	v.clear();
	r.runSine(0.2, 440.0, 5.f, &v);
	CHECK(rms(v, 0, v.size()) > 1.0, "the input comes through after: rms %g V", rms(v, 0, v.size()));

	// A sounding output holds the timer back (beads.cc: |out| over 1 % of
	// full scale resets the start time): no input, but the buffer keeps
	// playing through full feedback.
	Rig q;
	q.j.inLOn = true;
	q.runSine(1.0, 220.0, 8.f);
	q.k.dryWet = 1.f;
	q.k.feedback = 1.f;  // the buffer keeps sounding after the input goes
	q.k.density = 0.8f;
	q.j.inLOn = false;
	q.run(10.5);
	CHECK(q.e.mode() != spindrift::MODE_SYNTH, "a sounding output holds the synth off");
}

void synthPolicyAlwaysNever() {
	{
		Rig r;
		r.e.settings.synth = spindrift::SYNTH_ALWAYS;
		r.j.inLOn = r.j.inROn = true;
		r.step();
		CHECK(r.e.mode() == spindrift::MODE_SYNTH && !r.e.muted(), "Always: synth from the first sample, patched, no mute");
		r.runSine(1.0, 220.0, 5.f);
		CHECK(r.e.mode() == spindrift::MODE_SYNTH, "Always: stays with inputs patched");
		r.e.settings.synth = spindrift::SYNTH_NEVER;
		r.stepUntil([&] { return r.e.mode() != spindrift::MODE_SYNTH; });
		CHECK(r.e.mode() != spindrift::MODE_SYNTH && r.e.muted(), "Never: leaves the synth, with a mute");
	}
	{
		Rig r;
		r.e.settings.synth = spindrift::SYNTH_NEVER;
		r.run(12.0);
		CHECK(r.e.mode() == spindrift::MODE_GRAIN, "Never: no synth after 12 s without input");
		CHECK(r.mutes == 0, "Never: no mute (%d)", r.mutes);
		r.e.settings.synth = spindrift::SYNTH_ALWAYS;
		r.stepUntil([&] { return r.e.mode() == spindrift::MODE_SYNTH; });
		CHECK(r.e.mode() == spindrift::MODE_SYNTH && r.e.muted(), "Always from the menu: enters with a mute");
		r.e.settings.synth = spindrift::SYNTH_AUTO;
		r.run(0.5);
		CHECK(r.e.mode() == spindrift::MODE_SYNTH, "Automatic with nothing patched: stays in the synth");
	}
}

void monoWhenEitherUnpatched() {
	// R unpatched: L copied to R (Dry/Wet 0, so the output is the input).
	{
		Rig r;
		r.j.inLOn = true;
		std::vector<Out> v;
		r.runSine(0.5, 300.0, 5.f, &v);
		bool same = true;
		for (size_t i = v.size() / 2; i < v.size(); i++)
			same = same && v[i].l == v[i].r;
		CHECK(r.e.mono() && same && rms(v, v.size() / 2, v.size()) > 1.0, "R unpatched: mono, L copied to R");
	}
	{
		Rig r;
		r.j.inROn = true;
		std::vector<Out> v;
		r.runSine(0.5, 300.0, 5.f, &v);
		bool same = true;
		for (size_t i = v.size() / 2; i < v.size(); i++)
			same = same && v[i].l == v[i].r;
		CHECK(r.e.mono() && same, "L unpatched: mono, R copied to L");
	}
	// The buffer's length per quality, stereo and mono, from the record
	// head's speed: Digital, Vintage, Clean Tape, Dirty Tape.
	const spindrift::Quality order[4] = {spindrift::DIGITAL, spindrift::VINTAGE, spindrift::CLEAN_TAPE, spindrift::DIRTY_TAPE};
	const double stereoS[4] = {4.0, 8.0, 32.0 / 3.0, 16.0};
	for (int mono = 0; mono < 2; mono++) {
		Rig r;
		r.j.inLOn = true;
		r.j.inROn = mono == 0;
		r.runSine(0.1, 300.0, 1.f);
		for (int q = 0; q < 4; q++) {
			if (q > 0) {
				r.e.buttons().qualityDown();
				r.e.buttons().qualityUp(10);
				r.runSine(0.3, 300.0, 1.f);  // past the mute
			}
			const double want = stereoS[q] * (mono ? 2 : 1);
			CHECK(r.e.quality() == order[q] && r.e.mono() == (mono != 0), "quality %d, %s", int(order[q]), mono ? "mono" : "stereo");
			const float h0 = r.e.writeHead();
			r.runSine(1.0, 300.0, 1.f);
			float dh = r.e.writeHead() - h0;
			if (dh < 0.f)
				dh += 1.f;
			const double length = 1.0 / double(dh);
			CHECK(std::fabs(length / want - 1.0) < 0.002, "%s quality %d: the head goes round in %.3f s, want %.3f",
			      mono ? "mono" : "stereo", int(order[q]), length, want);
			CHECK(std::fabs(r.e.bufferSeconds() / want - 1.0) < 1e-4, "bufferSeconds %.3f, want %.3f", double(r.e.bufferSeconds()), want);
		}
	}
}

// A grain-filled buffer, then the input silenced: grains keep playing it
// back unless it was cleared.
struct Filled {
	Rig r;
	std::vector<Out> after;
	long mutedSamples = 0;
	bool zerosWhileMuted = true;
	explicit Filled(bool press) {
		r.k.dryWet = 1.f;
		r.k.density = 0.8f;
		r.k.time = 0.25f;  // a second back: inside what was recorded
		r.j.inLOn = r.j.inROn = true;
		r.runSine(2.0, 330.0, 6.f);
		r.j.inL = r.j.inR = 0.f;
		if (press) {
			r.e.buttons().qualityDown();
			r.e.buttons().qualityUp(10);
		}
		const long end = r.n + long(1.5 * r.rate);
		while (r.n < end) {
			const Out o = r.step();
			after.push_back(o);
			if (r.e.muted()) {
				mutedSamples++;
				zerosWhileMuted = zerosWhileMuted && o.l == 0.f && o.r == 0.f;
			}
		}
	}
};

void qualityChangeClearsAndMutes() {
	Filled control(false), pressed(true);
	CHECK(control.r.mutes == 0, "no press, no mute");
	CHECK(pressed.r.mutes == 1, "one press, one mute (%d)", pressed.r.mutes);
	CHECK(pressed.r.e.quality() == spindrift::VINTAGE && pressed.r.e.beadsRate() == 32000,
	      "Digital steps to Vintage Digital, at 32 kHz");
	CHECK(std::fabs(double(pressed.mutedSamples) / kRate - 0.25) < 0.001 && pressed.zerosWhileMuted,
	      "250 ms muted, silent, after the press: %.4f s", double(pressed.mutedSamples) / kRate);
	const double c = rms(control.after, 24000, control.after.size());
	const double p = rms(pressed.after, 24000, pressed.after.size());
	CHECK(c > 0.2 && p < 1e-3, "the buffer is cleared: grains from the old buffer %g V without the press, %g V with it", c, p);
}

void deferredUntilUnfreeze() {
	// Frozen by the button: three presses choose Dirty Tape but change nothing.
	{
		Rig r;
		r.k.dryWet = 1.f;
		r.k.density = 0.8f;
		r.k.time = 0.1f;
		r.j.inLOn = r.j.inROn = true;
		r.runSine(1.0, 330.0, 6.f);
		r.e.buttons().freezePress();
		r.runSine(0.2, 330.0, 6.f);
		for (int i = 0; i < 3; i++) {
			r.e.buttons().qualityDown();
			r.e.buttons().qualityUp(10);
			r.runSine(0.4, 330.0, 6.f);
		}
		r.j.inROn = false;  // and a mono change, also held
		std::vector<Out> v;
		r.runSine(0.5, 330.0, 6.f, &v);
		CHECK(r.mutes == 0, "no mute while frozen (%d)", r.mutes);
		CHECK(r.e.quality() == spindrift::DIGITAL && !r.e.mono(), "frozen: still Digital, stereo");
		CHECK(r.e.buttons().pending == spindrift::DIRTY_TAPE, "the choice waits: pending %d", int(r.e.buttons().pending));
		CHECK(rms(v, 0, v.size()) > 0.2, "the frozen buffer still plays");
		r.e.buttons().freezePress();
		r.runSine(0.5, 330.0, 6.f);
		CHECK(r.mutes == 1, "unfreezing: one change, one mute (%d)", r.mutes);
		CHECK(r.e.quality() == spindrift::DIRTY_TAPE && r.e.mono() && r.e.beadsRate() == 24000,
		      "unfreezing applies Dirty Tape and mono");
	}
	// Frozen by the gate: the same.
	{
		Rig r;
		r.j.inLOn = r.j.inROn = true;
		r.j.freezeOn = true;
		r.j.freeze = 10.f;
		r.runSine(0.5, 330.0, 6.f);
		for (int i = 0; i < 3; i++) {
			r.e.buttons().qualityDown();
			r.e.buttons().qualityUp(10);
			r.runSine(0.3, 330.0, 6.f);
		}
		CHECK(r.mutes == 0 && r.e.quality() == spindrift::DIGITAL, "gate-frozen: no change, no mute (%d)", r.mutes);
		r.j.freeze = 0.f;
		r.runSine(0.5, 330.0, 6.f);
		CHECK(r.mutes == 1 && r.e.quality() == spindrift::DIRTY_TAPE, "gate released: one change, one mute (%d)", r.mutes);
	}
}

// Inputs toggled every 3 s for 60 s, frozen from 30 to 45 s. The model,
// from MonitorStateChange: unfrozen and unmuted, with an input patched, a
// change of mono mutes once; with none patched nothing changes (and 3 s is
// too short for the synth).
void plugUnplugStorm() {
	Rig r;
	uint32_t lcg = 99;
	int expected = 0;
	bool modelMono = false;
	bool first = true;
	int frozenMutes = 0;
	for (int step = 0; step < 20; step++) {
		lcg = lcg * 1664525u + 1013904223u;
		const int state = int(lcg >> 30);  // 0 none, 1 L, 2 R, 3 both
		const bool frozen = step >= 10 && step < 15;
		if (step == 10)
			r.e.buttons().freezePress();
		if (step == 15)
			r.e.buttons().freezePress();
		r.j.inLOn = (state & 1) != 0;
		r.j.inROn = (state & 2) != 0;
		const bool mono = state != 3;
		if (first) {
			modelMono = mono;  // configured at add, no mute
			first = false;
		}
		else if (!frozen && state != 0 && mono != modelMono) {
			expected++;
			modelMono = mono;
		}
		const int before = r.mutes;
		r.runSine(3.0, 250.0, 5.f);
		if (frozen)
			frozenMutes += r.mutes - before;
		CHECK(r.e.mode() != spindrift::MODE_SYNTH, "step %d: no synth", step);
	}
	CHECK(!r.sawSynthPatched, "never synth while an input is patched");
	CHECK(frozenMutes == 0, "no mute while frozen (%d)", frozenMutes);
	CHECK(r.mutes == expected, "one mute per mono/stereo change: %d, expected %d", r.mutes, expected);
	CHECK(expected >= 4, "the storm changed mono/stereo (%d times)", expected);
}

void rates() {
	const int rs[3] = {44100, 48000, 96000};
	for (int rate : rs) {
		for (int clean = 0; clean < 2; clean++) {
			Rig r(rate);
			r.j.inLOn = r.j.inROn = true;
			if (clean) {
				// Digital -> Vintage -> Clean Tape (48 kHz, decimated inside).
				for (int i = 0; i < 2; i++) {
					r.e.buttons().qualityDown();
					r.e.buttons().qualityUp(10);
					r.runSine(0.3, 1000.0, 5.f);
				}
			}
			std::vector<Out> v;
			r.runSine(1.0, 1000.0, 5.f, &v);
			std::vector<float> l(v.size());
			for (size_t i = 0; i < v.size(); i++)
				l[i] = v[i].l;
			double amp, ph;
			const size_t from = v.size() / 2;
			fit(l, rate, 1000.0, from, v.size(), &amp, &ph);
			const double db = 20.0 * std::log10(amp / 3.75);
			CHECK(std::fabs(db) < 0.1, "%d Hz %s: 1 kHz dry at %.4f dB of 0.75", rate, clean ? "Clean Tape" : "Digital", db);
			// The delay the engine reports is the delay a sine sees.
			double inAmp, inPh;
			std::vector<float> in(v.size());
			const long n0 = r.n - long(v.size());
			for (size_t i = 0; i < v.size(); i++)
				in[i] = float(5.0 * std::sin(2 * kPi * 1000.0 * double(n0 + long(i)) / rate));
			fit(in, rate, 1000.0, from, v.size(), &inAmp, &inPh);
			double lag = (inPh - ph) / (2 * kPi) * rate / 1000.0;  // samples, modulo a period
			const double period = rate / 1000.0;
			double want = std::fmod(r.e.latency(), period);
			double diff = std::fmod(lag - want + 10 * period, period);
			if (diff > period / 2)
				diff -= period;
			CHECK(std::fabs(diff) < 0.02, "%d Hz %s: the sine is %.3f samples late modulo a period; latency() says %.3f",
			      rate, clean ? "Clean Tape" : "Digital", lag, r.e.latency());
			CHECK(r.e.underruns() == 0, "%d Hz: the FIFO never ran dry", rate);
		}
	}

	// A rate change while frozen keeps the buffer. Three engines fill a buffer
	// at 48 kHz, freeze (two of them) and lose their input; then
	// - A changes to 44.1 kHz frozen: its grains go on as B's, which stays;
	// - C changes unfrozen: its buffer is cleared;
	// - D goes to 44.1 kHz and back at a block boundary, frozen: from one
	//   block later it is B, sample for sample.
	struct Run {
		Rig r;
		std::vector<Out> v;
		Run() : r(kRate) {}
		void fill(bool freeze) {
			r.k.dryWet = 1.f;
			r.k.density = 0.8f;
			r.k.time = 0.15f;
			r.j.inLOn = r.j.inROn = true;
			r.runSine(1.5, 330.0, 6.f);
			if (freeze)
				r.e.buttons().freezePress();
			r.runSine(0.1, 330.0, 6.f);
			while (r.n % kBlock != 0)
				r.runSine(1.0 / kRate, 330.0, 6.f);
			r.j.inL = r.j.inR = 0.f;
		}
	};
	Run a, b, c, d;
	a.fill(true);
	b.fill(true);
	c.fill(false);
	d.fill(true);
	a.r.e.setSampleRate(44100.f);
	a.r.rate = 44100;
	c.r.e.setSampleRate(44100.f);
	c.r.rate = 44100;
	d.r.e.setSampleRate(44100.f);
	d.r.e.setSampleRate(48000.f);
	a.r.run(2.0, &a.v);
	b.r.run(2.0, &b.v);
	c.r.run(2.0, &c.v);
	d.r.run(2.0, &d.v);
	const double ra = rms(a.v, a.v.size() / 4, a.v.size());
	const double rb = rms(b.v, b.v.size() / 4, b.v.size());
	const double rc = rms(c.v, c.v.size() / 4, c.v.size());
	CHECK(rb > 0.2 && std::fabs(20.0 * std::log10(ra / rb)) < 0.5,
	      "frozen through 48 -> 44.1 kHz: the grains go on (%.3f V; staying at 48 kHz %.3f V)", ra, rb);
	CHECK(rc < 1e-3 && c.r.mutes == 1, "unfrozen, the rate change clears the buffer (%.4f V) and mutes once (%d)", rc, c.r.mutes);
	CHECK(a.r.mutes == 0 && d.r.mutes == 0, "frozen, no mute");
	bool same = true;
	for (size_t i = kBlock; i < d.v.size(); i++)
		same = same && d.v[i].l == b.v[i].l && d.v[i].r == b.v[i].r;
	CHECK(same, "frozen through 48 -> 44.1 -> 48 kHz: Beads is untouched (sample for sample after a block)");
}

void noAllocation() {
	const int rs[2] = {44100, 48000};
	for (int rate : rs) {
		Rig r(rate);
		r.j.inLOn = r.j.inROn = true;
		r.j.seedOn = r.j.freezeOn = r.j.timeOn = r.j.pitchOn = true;
		r.k.density = 0.85f;
		r.k.dryWet = 0.6f;
		r.k.reverb = 0.5f;
		r.k.feedback = 0.5f;
		r.runSine(0.05, 220.0, 5.f);  // the first sample boots
		tAllocs = 0;
		tCounting = true;
		r.runSine(1.5, 220.0, 5.f);                       // grains
		r.k.size = 1.f;
		r.runSine(2.0, 220.0, 5.f);                       // delay
		r.k.size = 0.5f;
		for (int q = 0; q < 4; q++) {                     // every quality
			r.e.buttons().qualityDown();
			r.e.buttons().qualityUp(10);
			r.runSine(0.6, 220.0, 5.f);
		}
		r.j.inROn = false;                                // mono
		r.runSine(0.6, 220.0, 5.f);
		r.e.buttons().freezePress();                      // frozen
		r.runSine(0.5, 220.0, 5.f);
		r.e.buttons().freezePress();
		r.e.settings.gateOutput = true;                   // gate output
		r.e.settings.gain = spindrift::GAIN_AUTO;         // automatic gain
		r.runSine(0.8, 220.0, 5.f);
		r.e.settings.synth = spindrift::SYNTH_ALWAYS;     // synth
		r.runSine(2.0, 220.0, 5.f);
		tCounting = false;
		CHECK(r.e.mode() == spindrift::MODE_SYNTH, "ended in the synth");
		CHECK(tAllocs == 0, "%d Hz: no allocation across 10 s of every mode (%ld)", rate, tAllocs);
		CHECK(r.e.underruns() == 0, "%d Hz: the output FIFO never ran dry or over in any quality (%ld)", rate, r.e.underruns());
	}
}

}  // namespace

int main() {
	matchesRefAtNativeRate();
	seedDelay();
	cvScaling();
	levels();
	synthAfter10s();
	synthPolicyAlwaysNever();
	monoWhenEitherUnpatched();
	qualityChangeClearsAndMutes();
	deferredUntilUnfreeze();
	plugUnplugStorm();
	rates();
	noAllocation();
	return rkatest::finish("spindrift test_engine");
}
