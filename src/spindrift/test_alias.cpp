// Spindrift's aliasing (docs/DSP-ALIASING.md; TESTING.md rule 16; the spec's
// "Aliasing"), measured with rkatest::AliasMeter (src/testing_alias.hpp) at
// Rack 44.1 and 48 kHz.
//
// Beads' own aliasing is kept: its grains read faster than 1 through a
// Hermite interpolator, its envelopes are audio-rate AM, its qualities
// decimate and quantise on purpose. So the bound for a sound Beads makes is
// what Beads' code (rkaref::BeadsRef, untouched upstream) measures run at its
// own rate for that quality (48, 32 or 24 kHz), on the same input with the
// same seed and the same controls, plus kMarginDb: the test fails if
// Spindrift aliases more than Beads, not if Beads aliases. What Spindrift
// adds, the edge converters (EdgeRate.hpp), is measured alone against an
// absolute bound, and must pass the band at every Beads rate with its level
// and the delay latency() reports.
//
// `test_alias --table` prints every measurement.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "testing.hpp"
#include "testing_alias.hpp"
#include "testdata/RefBeads.hpp"
#include "SpindriftEngine.hpp"

using rkatest::AliasMeter;
using spindrift::EdgeRate;
using spindrift::Jacks;
using spindrift::Knobs;
using spindrift::Out;
using spindrift::Resampler;
using spindrift::SpindriftEngine;

namespace {

bool table = false;
const double kPi = 3.14159265358979323846;
const uint32_t kSeed = 0x5eed;
const int kRackRates[2] = {44100, 48000};
const char* const kQualityName[4] = {"Dirty Tape", "Clean Tape", "Vintage", "Digital"};
const size_t kN = 65536;  // AliasMeter's largest transform

// Over Beads' own figure, the most Spindrift may measure (2026-11-22: the
// largest excess measured is in the report, test_alias --table).
const double kMarginDb = 3.0;
// The converters alone: what is left of a tone that is not the tone.
const double kEdgeBoundDb = -90.0;

int beadsRateOf(int quality) {
	return quality == spindrift::DIRTY_TAPE ? 24000 : (quality == spindrift::VINTAGE ? 32000 : 48000);
}

Knobs defaultKnobs() {
	Knobs k;
	k.time = k.size = k.shape = k.pitch = k.density = 0.5f;
	k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
	k.feedback = k.dryWet = k.reverb = 0.f;
	return k;
}

// The engine's control path for the reference (the knobs and the Pitch CV;
// nothing else patched), as SpindriftEngine::readControls and
// buildParameters build them from beads.cc, once a block.
struct RefGlue {
	spindrift::PotFilter time, size, shape, pitch, density, timeAtt, sizeAtt, shapeAtt, pitchAtt, feedback, dryWet, reverb;
	spindrift::CvChannel pitchCv;
	RefGlue() {
		spindrift::PotFilter* all[] = {&time, &size, &shape, &pitch, &density, &timeAtt, &sizeAtt, &shapeAtt, &pitchAtt, &feedback, &dryWet, &reverb};
		for (spindrift::PotFilter* p : all)
			p->init(0.05f, p == &time ? 0.001f : 0.f);
	}
	rkaref::RefParams block(const Knobs& k, const Jacks& j) {
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
		p.pitchCv = pitchCv.process(j.pitch, 12.f, 5.f, 1.f, -60.f, 60.f);
		p.pitchHasCv = j.pitchOn;
		p.feedback = spindrift::mixParameter(feedback.process(k.feedback), 0.f, false);
		p.dryWet = spindrift::mixParameter(dryWet.process(k.dryWet), 0.f, false);
		p.reverb = spindrift::mixParameter(reverb.process(k.reverb), 0.f, false);
		p.seedState = 2;  // latched, as the engine starts
		return p;
	}
};

// One sound: Beads' controls and input, the frequencies that belong in it.
struct Sound {
	bool synth;
	int quality;           // Quality; the synth's resolution
	Knobs k;
	Jacks j;
	double inHz, inVolts;  // a sine into both inputs (grains)
	double toneHz;         // the fundamental that comes out
	double guardHz;        // what each harmonic owns either side (AliasMeter's guardHz)
};

// The frequencies that belong in a signal at fs, sampled from Beads at
// beadsRate: the tone's harmonics below both Nyquists (Beads' harmonics
// above its own Nyquist are its aliases, wherever they land).
std::vector<double> expected(const Sound& s, double fs, double beadsRate) {
	return AliasMeter::harmonics(s.toneHz, std::min(fs, beadsRate));
}

const double kSettle = 1.5;  // seconds before the measurement: the buffer filled, the controls settled

int rateOf(const Sound& s) {
	return s.synth ? 48000 : beadsRateOf(s.quality);
}

// Beads' code at its own rate, `frames` long: the reference, fed the sine
// sampled at that rate through the engine's input gain (GainControl,
// Beads' glue) and its control path (RefGlue), clipped as beads.cc
// converts its output. Beads' units (+-1 full scale).
std::vector<float> renderRef(const Sound& s, long frames) {
	const int rate = rateOf(s);
	const int block = rate / 4000;
	rkaref::BeadsRef ref;
	ref.seed(kSeed);
	if (!s.synth)
		ref.configure(s.quality, false);
	spindrift::GainControl gain;
	gain.Init(float(rate),
	          s.quality == spindrift::DIRTY_TAPE ? spindrift::GainControl::SOFT_CLIPPING_MODE_ON
	          : s.quality == spindrift::CLEAN_TAPE ? spindrift::GainControl::SOFT_CLIPPING_MODE_DYNAMIC
	                                                : spindrift::GainControl::SOFT_CLIPPING_MODE_OFF,
	          true);
	RefGlue g;
	std::vector<float> out;
	out.reserve(size_t(frames + block));
	beads::FloatFrame in[spindrift::kMaxBlock], gained[spindrift::kMaxBlock];
	float inLR[2 * spindrift::kMaxBlock], outLR[2 * spindrift::kMaxBlock];
	bool freeze[spindrift::kMaxBlock] = {};
	for (long f0 = 0; f0 < frames; f0 += block) {
		for (int i = 0; i < block; i++) {
			const double t = double(f0 + i) / rate;
			const float v = s.synth ? 0.f : float(s.inVolts * std::sin(2 * kPi * s.inHz * t));
			in[i].l = in[i].r = std::min(std::max(v * 0.1f, -1.f), 1.f);
		}
		gain.Process(true, false, 1.f, s.synth, s.synth, in, gained, size_t(block));
		for (int i = 0; i < block; i++) {
			inLR[2 * i] = gained[i].l;
			inLR[2 * i + 1] = gained[i].r;
		}
		const rkaref::RefParams p = g.block(s.k, s.j);
		if (s.synth)
			ref.synthBlock(p, nullptr, freeze, s.quality, outLR, size_t(block));
		else
			ref.processBlock(p, nullptr, freeze, inLR, outLR, size_t(block));
		for (int i = 0; i < block; i++)
			out.push_back(std::min(std::max(outLR[2 * i], -1.f), 0.999999f));
	}
	out.resize(size_t(frames));
	return out;
}

// Spindrift at Rack's rate, `frames` long, the same sine sampled at that
// rate; Beads' units (volts / 10).
std::vector<float> renderEngine(const Sound& s, int rackRate, long frames, double* latency = nullptr) {
	SpindriftEngine e(kSeed);
	CHECK(e.allocate(), "engine allocates");
	e.setSampleRate(float(rackRate));
	e.buttons().quality = e.buttons().pending = spindrift::Quality(s.quality);
	e.settings.synth = s.synth ? spindrift::SYNTH_ALWAYS : spindrift::SYNTH_NEVER;
	Jacks j = s.j;
	j.inLOn = j.inROn = !s.synth;
	std::vector<float> out;
	out.reserve(size_t(frames));
	for (long n = 0; n < frames; n++) {
		const double t = double(n) / rackRate;
		j.inL = j.inR = s.synth ? 0.f : float(s.inVolts * std::sin(2 * kPi * s.inHz * t));
		Out o;
		e.process(s.k, j, o);
		out.push_back(o.l / 10.f);
	}
	CHECK(e.quality() == spindrift::Quality(s.quality) && e.mode() == (s.synth ? spindrift::MODE_SYNTH : spindrift::MODE_GRAIN),
	      "the engine runs the sound asked for");
	CHECK(e.beadsRate() == rateOf(s), "Beads runs at %d Hz (%d)", rateOf(s), e.beadsRate());
	CHECK(e.underruns() == 0, "no FIFO underrun");
	if (latency)
		*latency = e.latency();
	return out;
}

// n frames from `from`.
std::vector<float> slice(const std::vector<float>& x, long from, size_t n = kN) {
	return std::vector<float>(x.begin() + from, x.begin() + from + long(n));
}

// The reference's window: the power of two nearest below what kN frames last
// at 48 kHz (1.37 s), so Beads at 24 kHz is measured over the same time as
// Spindrift at 48 kHz, and at 32 kHz over 1.02 s.
size_t refWindow(int beadsRate) {
	size_t n = kN;
	while (double(n) / beadsRate > double(kN) / 48000.0 + 1e-9)
		n /= 2;
	return n;
}

double rms(const std::vector<float>& x) {
	double s = 0.0;
	for (float v : x)
		s += double(v) * v;
	return std::sqrt(s / double(std::max<size_t>(1, x.size())));
}

// Spindrift against Beads at its own rate, for one sound: the strongest
// component that does not belong, dB re Beads' full scale (10 V), after
// kSettle.
void compare(const char* what, const Sound& s) {
	const int beadsRate = rateOf(s);
	const size_t n = refWindow(beadsRate);
	const std::vector<float> ref = slice(renderRef(s, long(kSettle * beadsRate) + long(n)), long(kSettle * beadsRate), n);
	const AliasMeter mr(ref.data(), ref.size(), beadsRate);
	const std::vector<double> refWant = expected(s, beadsRate, beadsRate);
	const double refDb = mr.otherDb(refWant, 1.0, s.guardHz);
	const double refTone = AliasMeter::db(mr.amplitude(s.toneHz));
	CHECK(rms(ref) > 0.01, "%s %s: Beads sounds (rms %.4f)", what, kQualityName[s.quality], rms(ref));
	for (int rack : kRackRates) {
		const long skip = long(kSettle * rack);
		const std::vector<float> x = slice(renderEngine(s, rack, skip + long(kN)), skip);
		const AliasMeter m(x.data(), x.size(), rack);
		const std::vector<double> want = expected(s, rack, beadsRate);
		const double db = m.otherDb(want, 1.0, s.guardHz);
		const AliasMeter::Other o = m.strongestOther(want, s.guardHz);
		if (table)
			std::printf("%-26s %-10s rack %5d beads %5d  tone %7.1f Hz (%6.1f dB)  Beads %7.2f dB  Spindrift %7.2f dB (at %7.1f Hz)  excess %+6.2f\n",
			            what, kQualityName[s.quality], rack, beadsRate, s.toneHz, refTone, refDb, db, o.hz, db - refDb);
		CHECK(rms(x) > 0.01, "%s %s, %d Hz: Spindrift sounds (rms %.4f)", what, kQualityName[s.quality], rack, rms(x));
		CHECK(table || db <= refDb + kMarginDb,
		      "%s %s at %d Hz: aliasing %.2f dB re full scale (at %.1f Hz) is over Beads' own at %d Hz, %.2f dB, + %.1f dB", what,
		      kQualityName[s.quality], rack, db, o.hz, beadsRate, refDb, kMarginDb);
	}
}

// Grains: Time near the head (the tape qualities' long buffers are mostly
// still silent after kSettle); Density 0.15, regular grains (left of noon),
// so the sound is periodic and its measure does not depend on which grains
// a window catches. Each harmonic owns the skirt round it: 50 Hz either side
// for the grains' envelopes, and 1 % of the tone for Dirty Tape's wow and
// flutter (a random pitch modulation, so its sidebands spread with the
// tone, and which of them a window holds depends on its length: 1.49 s at
// 44.1 kHz, 1.37 s at 48 kHz and in the reference at 24 kHz).
const float kGrainTime = 0.05f;
const double kGrainGuardHz = 50.0, kGrainGuardShare = 0.01;
// The synth: 20 Hz, so where a harmonic's guard ends does not fall between
// bins differently at 44.1 and 48 kHz (an alias of Beads' within a few
// hertz of a harmonic is owned by it in both measurements).
const double kSynthGuardHz = 20.0;

// A sine through grains at Pitch 0, +12 and +24 (the knob on the detune
// law's plateaus), every quality: long regular grains, wet only.
// 2.3 kHz in: at +24 the grains sound at 9.2 kHz, inside the band of the
// lowest rate (Dirty Tape's 24 kHz).
void grains() {
	const float pitchKnob[3] = {0.5f, 0.925f, 1.f};
	const int semitones[3] = {0, 12, 24};
	for (int q = 0; q < 4; q++)
		for (int p = 0; p < 3; p++) {
			Sound s = {};
			s.synth = false;
			s.quality = q;
			s.k = defaultKnobs();
			s.k.pitch = pitchKnob[p];
			s.k.time = kGrainTime;
			s.k.size = 0.7f;
			s.k.density = 0.15f;
			s.k.dryWet = 1.f;
			s.inHz = 2300.0;
			s.inVolts = 5.0;
			s.toneHz = s.inHz * std::pow(2.0, semitones[p] / 12.0);
			s.guardHz = kGrainGuardHz + kGrainGuardShare * s.toneHz;
			char what[64];
			std::snprintf(what, sizeof what, "grains, Pitch %+d", semitones[p]);
			compare(what, s);
		}
}

// The wavetable voice across its range (Pitch -24 .. +72 semitones from
// 130.81 Hz, by the knob and the Pitch CV), at each resolution: the dry
// oscillator alone (Dry/Wet 0), whose tone is known.
void synth() {
	struct P {
		float knob, cv;  // Pitch knob (detune law) and CV volts (12 semitones a volt)
		int semitones;
	};
	const P ps[5] = {{0.5f, 0.f, 0}, {1.f, 0.f, 24}, {1.f, 2.f, 48}, {1.f, 3.f, 60}, {1.f, 4.f, 72}};
	for (int q = 0; q < 4; q++)
		for (const P& p : ps) {
			Sound s = {};
			s.synth = true;
			s.quality = q;
			s.k = defaultKnobs();
			s.k.pitch = p.knob;
			s.k.dryWet = 0.f;
			if (p.cv != 0.f) {
				s.j.pitchOn = true;
				s.j.pitch = p.cv;
			}
			s.toneHz = 130.81 * std::pow(2.0, p.semitones / 12.0);
			s.guardHz = kSynthGuardHz;
			char what[64];
			std::snprintf(what, sizeof what, "synth, Pitch %+d (%.0f Hz)", p.semitones, s.toneHz);
			compare(what, s);
		}
}

// --- The edge converters alone ------------------------------------------

// A sine of `volts` at hz, `frames` long at fs, through one Resampler from
// fs to outRate: the output after the kernel has filled.
std::vector<float> throughResampler(int fs, int outRate, double hz, size_t frames) {
	Resampler r;
	r.setup(fs, outRate);
	std::vector<float> out;
	out.reserve(frames);
	long n = 0;
	while (out.size() < frames + 4096) {
		const float v = float(std::sin(2 * kPi * hz * double(n++) / fs));
		r.push(v, -v);
		while (r.ready()) {
			float l, rr, nearest;
			r.pop(l, rr, nearest);
			out.push_back(l);
		}
	}
	return std::vector<float>(out.begin() + 4096, out.begin() + 4096 + long(frames));
}

// What one direction leaves of a tone that is not the tone, dB re the tone's
// amplitude (1): tones in the band must come through alone; tones above the
// lower Nyquist must not come through at all (or as an alias).
double edgeOther(int from, int to, double hz) {
	const std::vector<float> x = throughResampler(from, to, hz, kN);
	const AliasMeter m(x.data(), x.size(), to);
	std::vector<double> want;
	if (hz < 0.5 * std::min(from, to))
		want.push_back(hz);
	return m.otherDb(want, 1.0);
}

// Every pair of Rack rate (44.1, 48 kHz) and Beads rate (24, 32, 48 kHz),
// both ways: a tone high in the band (0.38 of the lower rate) passes alone;
// tones between the two Nyquists (and up to the higher one) are stopped.
// Then the round trip through EdgeRate as the engine uses it.
void edgeRateAlone() {
	for (int rack : kRackRates)
		for (int b = 0; b < 3; b++) {
			const int beads = EdgeRate::beadsRate(b);
			const int lo = std::min(rack, beads), hi = std::max(rack, beads);
			std::vector<double> tones = {0.38 * lo, 0.1 * lo};
			if (hi != lo) {
				tones.push_back(0.5 * lo + 0.3 * (0.5 * hi - 0.5 * lo));
				tones.push_back(0.5 * lo + 0.9 * (0.5 * hi - 0.5 * lo));
			}
			// Each direction sees the tones its source rate can hold.
			for (double hz : tones) {
				const bool in = hz < 0.5 * rack, out = hz < 0.5 * beads;
				const double inDb = in ? edgeOther(rack, beads, hz) : -999.0;
				const double outDb = out ? edgeOther(beads, rack, hz) : -999.0;
				if (table)
					std::printf("EdgeRate %5d -> %5d and back, %8.1f Hz: in %7.2f dB, out %7.2f dB\n", rack, beads, hz, inDb, outDb);
				CHECK(table || inDb < kEdgeBoundDb, "EdgeRate in, %d -> %d Hz, a tone at %.1f Hz leaves %.2f dB of other", rack, beads, hz, inDb);
				CHECK(table || outDb < kEdgeBoundDb, "EdgeRate out, %d -> %d Hz, a tone at %.1f Hz leaves %.2f dB of other", beads, rack, hz, outDb);
			}

			// The round trip, as the engine runs it: Rack -> Beads -> FIFO -> Rack.
			EdgeRate e;
			e.setRackRate(float(rack));
			e.select(beads);
			const double hz = 0.38 * lo;
			std::vector<float> x;
			x.reserve(kN + 8192);
			for (long n = 0; x.size() < kN + 8192; n++) {
				e.in().push(float(std::sin(2 * kPi * hz * double(n) / rack)), 0.f);
				while (e.in().ready()) {
					float l, r, nearest;
					e.in().pop(l, r, nearest);
					e.out().push(l, r);
					while (e.out().ready()) {
						e.out().pop(l, r, nearest);
						e.pushOut(spindrift::EdgeFrame{l, r});
					}
				}
				x.push_back(e.popOut().l);
			}
			const std::vector<float> y(x.begin() + 8192, x.end());
			const AliasMeter m(y.data(), y.size(), rack);
			const double db = m.otherDb({hz}, 1.0);
			const double level = AliasMeter::db(m.amplitude(hz));
			if (table)
				std::printf("EdgeRate round trip %5d via %5d, %8.1f Hz: other %7.2f dB, level %+.4f dB\n", rack, beads, hz, db, level);
			CHECK(table || db < kEdgeBoundDb, "EdgeRate round trip %d via %d Hz at %.1f Hz: %.2f dB of other", rack, beads, hz, db);
			CHECK(table || std::fabs(level) < 0.01, "EdgeRate round trip %d via %d Hz at %.1f Hz: level %+.4f dB", rack, beads, hz, level);
			CHECK(e.underruns() == 0 && e.overruns() == 0, "EdgeRate round trip %d via %d: the FIFO kept up", rack, beads);
		}
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

// The band at Beads' 24 and 32 kHz (Dirty Tape, Vintage), through the whole
// engine (test_engine's rates covers 48 kHz): a 1 kHz sine at Dry/Wet 0
// leaves at Beads' own level within 0.1 dB, and as much later than Beads'
// own output as latency() says. Beads' level and delay come from the
// reference at its rate, the same input and seed: Dirty Tape's dry path is
// mu-law and wow and flutter (a random, modulated delay), so neither is
// Beads' 0.75 or zero there. The engine's window is the reference's,
// latency() later. 0.5 V in, so the gain's soft clipping hardly bends it.
void passband() {
	const int qualities[2] = {spindrift::DIRTY_TAPE, spindrift::VINTAGE};
	const double hz = 1000.0;
	for (int rack : kRackRates)
		for (int q : qualities) {
			Sound s = {};
			s.quality = q;
			s.k = defaultKnobs();
			s.inHz = hz;
			s.inVolts = 0.5;
			const int beads = rateOf(s);
			const std::vector<float> ref = renderRef(s, beads);
			double latency = 0.0;
			const std::vector<float> x = renderEngine(s, rack, long(1.2 * rack), &latency);
			double refAmp, refPh, amp, ph;
			fit(ref, beads, hz, size_t(beads / 2), size_t(beads), &refAmp, &refPh);
			const long lat = std::lround(latency);
			fit(x, rack, hz, size_t(rack / 2 + lat), size_t(rack + lat), &amp, &ph);
			const double db = 20.0 * std::log10(amp / refAmp);
			// Each one's lag behind its input (sin(2 pi f t), phase 0), in
			// Rack samples, modulo a period.
			const double period = rack / hz;
			const double lag = -ph / (2 * kPi) * period, refLag = -refPh / (2 * kPi) * period;
			double diff = std::fmod(lag - refLag - latency + 10 * period, period);
			if (diff > period / 2)
				diff -= period;
			if (table)
				std::printf("passband %5d Hz via %5d (%s): 1 kHz at %+.4f dB of Beads' (%.4f); %+.3f samples off latency() %.3f\n",
				            rack, beads, kQualityName[q], db, refAmp, diff, latency);
			CHECK(refAmp > 0.015, "%s: Beads passes the sine (%.4f)", kQualityName[q], refAmp);
			CHECK(table || std::fabs(db) < 0.1, "%d Hz %s: 1 kHz dry at %+.4f dB of Beads' own", rack, kQualityName[q], db);
			// Wow and flutter's delay moves within the window, and the
			// windows align to a whole sample, so Dirty Tape's lag is known
			// to 0.1 sample; Vintage's to 0.02.
			const double tolerance = q == spindrift::DIRTY_TAPE ? 0.1 : 0.02;
			CHECK(table || std::fabs(diff) < tolerance, "%d Hz %s: the sine is %+.3f samples off what latency() says (%.3f)", rack,
			      kQualityName[q], diff, latency);
		}
}

}  // namespace

int main(int argc, char** argv) {
	table = argc > 1 && std::strcmp(argv[1], "--table") == 0;
	edgeRateAlone();
	passband();
	grains();
	synth();
	return rkatest::finish("spindrift test_alias");
}
