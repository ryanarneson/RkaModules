// Spindrift's engine under hostile input (the spec's Review Focus 2), built
// with the address and undefined-behaviour sanitizers and a counting
// operator new (tests.mk): a +-12 V sine, +12 V and -12 V held, NaN, +inf
// and -inf on each input and CV jack in turn, 1 s each, in every mode
// (grains, delay, synth) and quality, at Rack 44.1 kHz (so every quality
// goes through the edge converters). Throughout, the output is finite and
// within +-10.5 V; 1 s after the input is sane again (6 s in delay mode,
// whose limiter Beads releases slowly: settleSeconds), the output's RMS is
// within 1 dB of a fresh engine's on the same sane input; nothing allocates.
//
// The guards are the engine's, before Beads (the vendored code is never
// given a NaN): audio in and the CVs are held to their range and a NaN is
// read as 0 V; a gate keeps its state through a NaN.
//
// `test_hostile --table` prints every segment's figures.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "testing.hpp"
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

bool table = false;
const double kPi = 3.14159265358979323846;
const uint32_t kSeed = 0x5eed;
const int kRate = 44100;
const float kBound = 10.5f;     // volts: the output's limit, whatever comes in
const double kRecoverDb = 1.0;  // the RMS after recovery, against a fresh engine's
const char* const kQualityName[4] = {"Dirty Tape", "Clean Tape", "Vintage", "Digital"};

struct Jack {
	const char* name;
	float Jacks::*v;
	bool Jacks::*on;
};
const Jack kJacks[] = {
    {"In L", &Jacks::inL, &Jacks::inLOn},       {"In R", &Jacks::inR, &Jacks::inROn},
    {"Time", &Jacks::time, &Jacks::timeOn},     {"Size", &Jacks::size, &Jacks::sizeOn},
    {"Shape", &Jacks::shape, &Jacks::shapeOn},  {"Pitch", &Jacks::pitch, &Jacks::pitchOn},
    {"Density", &Jacks::density, &Jacks::densityOn}, {"Feedback", &Jacks::feedback, &Jacks::feedbackOn},
    {"Dry/Wet", &Jacks::dryWet, &Jacks::dryWetOn}, {"Reverb", &Jacks::reverb, &Jacks::reverbOn},
    {"Freeze", &Jacks::freeze, &Jacks::freezeOn}, {"Seed", &Jacks::seed, &Jacks::seedOn},
};
const int kSeedJack = 11;

enum Kind { SINE12, DC_PLUS12, DC_MINUS12, NOT_A_NUMBER, PLUS_INF, MINUS_INF, NUM_KINDS };
const char* const kKindName[NUM_KINDS] = {"+-12 V sine", "+12 V", "-12 V", "NaN", "+inf", "-inf"};

float hostile(Kind kind, long n) {
	switch (kind) {
		case SINE12:
			return float(12.0 * std::sin(2 * kPi * 220.0 * double(n) / kRate));
		case DC_PLUS12:
			return 12.f;
		case DC_MINUS12:
			return -12.f;
		case NOT_A_NUMBER:
			return std::numeric_limits<float>::quiet_NaN();
		case PLUS_INF:
			return std::numeric_limits<float>::infinity();
		default:
			return -std::numeric_limits<float>::infinity();
	}
}

enum Mode { GRAINS, DELAY, SYNTH, NUM_MODES };
const char* const kModeName[NUM_MODES] = {"grains", "delay", "synth"};

// The patch: every jack patched, so a hostile value on any of them is seen
// and a sane one is the same patch (a CV jack at 0 V, the gates low, the
// audio two sines and a little noise). Grains: regular (Density 0.3), read
// near the head (Time 0.02) so 1 s after the input is sane again they read
// sane audio; Feedback and Reverb up, so a value that got into Beads would
// stay there.
struct Patch {
	Knobs k;
	Jacks j;
	uint32_t lcg = 12345;
	explicit Patch(Mode mode) {
		k.time = 0.02f;
		k.size = mode == DELAY ? 1.f : 0.5f;
		k.shape = 0.5f;
		k.pitch = 0.5f;
		k.density = 0.3f;
		k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
		k.feedback = 0.3f;
		k.dryWet = 0.6f;
		k.reverb = 0.3f;
		j = Jacks{};
		for (const Jack& jack : kJacks)
			j.*jack.on = true;
	}
	void sane(long n) {
		for (const Jack& jack : kJacks)
			j.*jack.v = 0.f;
		lcg = lcg * 1664525u + 1013904223u;
		const float noise = float(int32_t(lcg)) / 2147483648.f;
		const double t = double(n) / kRate;
		j.inL = float(4.0 * std::sin(2 * kPi * 220.0 * t)) + 0.3f * noise;
		j.inR = float(4.0 * std::sin(2 * kPi * 331.0 * t)) - 0.3f * noise;
	}
};

struct Run {
	SpindriftEngine e;
	Patch p;
	long n = 0;
	Run(Mode mode, int quality) : e(kSeed), p(mode) {
		if (!e.allocate()) {  // not CHECK: this runs on the test's threads
			std::printf("FAIL: the engine could not allocate\n");
			std::abort();
		}
		e.setSampleRate(float(kRate));
		e.buttons().quality = e.buttons().pending = spindrift::Quality(quality);
		e.settings.synth = mode == SYNTH ? spindrift::SYNTH_ALWAYS : spindrift::SYNTH_NEVER;
	}
	// `seconds`, sane unless `jack` is given; the output's peak, how many
	// samples were not finite, and the RMS.
	struct Stats {
		float peak = 0.f;
		long notFinite = 0;
		double rms = 0.0;
	};
	Stats run(double seconds, const Jack* jack = nullptr, Kind kind = SINE12) {
		Stats s;
		double sum = 0.0;
		const long frames = long(seconds * kRate);
		for (long i = 0; i < frames; i++, n++) {
			p.sane(n);
			if (jack)
				p.j.*jack->v = hostile(kind, n);
			Out o;
			e.process(p.k, p.j, o);
			if (!std::isfinite(o.l) || !std::isfinite(o.r)) {
				s.notFinite++;
				continue;
			}
			s.peak = std::max(s.peak, std::max(std::fabs(o.l), std::fabs(o.r)));
			sum += double(o.l) * o.l + double(o.r) * o.r;
		}
		s.rms = std::sqrt(sum / (2.0 * double(frames)));
		return s;
	}
};

// What one mode and quality found: its table lines and its failures, kept
// until the threads are done (CHECK is not for several threads at once).
struct Report {
	std::vector<std::string> lines, failures;
	// Both allocate, so the count pauses for them.
	template <typename... A>
	void line(const char* fmt, A... a) {
		const bool counting = tCounting;
		tCounting = false;
		char b[512];
		std::snprintf(b, sizeof b, fmt, a...);
		lines.push_back(b);
		tCounting = counting;
	}
	template <typename... A>
	void check(bool ok, const char* fmt, A... a) {
		if (ok)
			return;
		const bool counting = tCounting;
		tCounting = false;
		char b[512];
		std::snprintf(b, sizeof b, fmt, a...);
		failures.push_back(b);
		tCounting = counting;
	}
};

// Beads' output limiter releases by 1/6 of full scale a second
// (processor.cc: output_limiter_.Init(1 / (sr * 6), 0.9)), so in delay mode,
// where a full-scale input comes back through the taps and Feedback, Beads
// itself is several decibels down for seconds after any loud input, a legal
// +-10 V as much as a hostile one; there the recovery is measured 6 s after.
double settleSeconds(Mode mode) {
	return mode == DELAY ? 6.0 : 1.0;
}

// One mode and quality: a fresh engine's RMS on the sane patch, then one
// engine through every jack in turn: each hostile kind for 1 s, one after
// another (each checked for a finite output within the bound), then the
// sane patch, and the RMS measured over the second after the settle.
void modeQuality(Mode mode, int quality, Report& rep) {
	const char* m = kModeName[mode];
	const char* q = kQualityName[quality];
	const double settle = settleSeconds(mode);
	double fresh;
	{
		Run r(mode, quality);
		r.run(2.0);
		fresh = r.run(1.0).rms;
		rep.check(fresh > 0.1, "%s %s: the patch sounds (%.3f V)", m, q, fresh);
	}
	// In delay mode SEED is Beads' clock, and Beads keeps the period it was
	// last clocked at: the hostile run's edges (the 220 Hz sine, then one
	// rising edge in each of +12 V and +inf) leave the delay at another
	// time. There SEED's recovery is measured against a fresh engine given
	// the same run in legal values (each held to +-10 V; NaN, which a gate
	// reads as no change, as 0 V since the gate is low before it).
	double freshClocked = fresh;
	if (mode == DELAY) {
		Run r(mode, quality);
		r.run(2.0);
		const Jack& seed = kJacks[kSeedJack];
		for (int k = 0; k < NUM_KINDS; k++)
			for (long i = 0; i < long(kRate); i++, r.n++) {
				r.p.sane(r.n);
				const float v = hostile(Kind(k), r.n);
				r.p.j.*seed.v = v == v ? std::min(std::max(v, -10.f), 10.f) : 0.f;
				Out o;
				r.e.process(r.p.k, r.p.j, o);
			}
		r.run(settle);
		freshClocked = r.run(1.0).rms;
	}
	Run r(mode, quality);
	r.run(2.0);
	tAllocs = 0;
	tCounting = true;
	for (const Jack& jack : kJacks) {
		for (int k = 0; k < NUM_KINDS; k++) {
			const Kind kind = Kind(k);
			const Run::Stats h = r.run(1.0, &jack, kind);
			if (table)
				rep.line("%-6s %-10s %-8s %-11s  peak %6.3f V  not finite %ld", m, q, jack.name, kKindName[kind], double(h.peak),
				         h.notFinite);
			rep.check(h.notFinite == 0, "%s %s, %s at %s: %ld output samples not finite", m, q, jack.name, kKindName[kind],
			          h.notFinite);
			rep.check(h.peak <= kBound, "%s %s, %s at %s: the output reached %.3f V (bound %.1f V)", m, q, jack.name,
			          kKindName[kind], double(h.peak), double(kBound));
		}
		const Run::Stats s = r.run(settle);
		const Run::Stats after = r.run(1.0);
		const double want = &jack == &kJacks[kSeedJack] ? freshClocked : fresh;
		const double db = 20.0 * std::log10(std::max(after.rms, 1e-9) / want);
		if (table)
			rep.line("%-6s %-10s %-8s %.0f s after: %6.3f V (fresh %6.3f, %+6.2f dB)", m, q, jack.name, settle, after.rms, want, db);
		rep.check(s.notFinite + after.notFinite == 0 && std::max(s.peak, after.peak) <= kBound,
		          "%s %s, %s: after, %ld samples not finite, peak %.3f V", m, q, jack.name, s.notFinite + after.notFinite,
		          double(std::max(s.peak, after.peak)));
		rep.check(std::fabs(db) <= kRecoverDb,
		          "%s %s, %s: %.0f s after the hostile input, the output's RMS is %.3f V, %+.2f dB from a fresh engine's %.3f V",
		          m, q, jack.name, settle, after.rms, db, want);
	}
	tCounting = false;
	rep.check(tAllocs == 0, "%s %s: no allocation through every hostile input (%ld)", m, q, tAllocs);
	rep.check(r.e.underruns() == 0, "%s %s: the output FIFO never ran dry or over (%ld)", m, q, r.e.underruns());
	const spindrift::Mode want = mode == SYNTH ? spindrift::MODE_SYNTH : (mode == DELAY ? spindrift::MODE_DELAY : spindrift::MODE_GRAIN);
	rep.check(r.e.mode() == want && r.e.quality() == spindrift::Quality(quality), "%s %s: still in its mode and quality", m, q);
}

}  // namespace

int main(int argc, char** argv) {
	table = argc > 1 && std::strcmp(argv[1], "--table") == 0;
	// Each mode and quality on its own thread (each engine on one thread
	// only, as in Rack), the reports printed in order after.
	Report reports[NUM_MODES * 4];
	std::vector<std::thread> threads;
	for (int mode = 0; mode < NUM_MODES; mode++)
		for (int q = 0; q < 4; q++)
			threads.emplace_back([mode, q, &reports] { modeQuality(Mode(mode), q, reports[mode * 4 + q]); });
	for (std::thread& t : threads)
		t.join();
	for (const Report& r : reports) {
		for (const std::string& l : r.lines)
			std::printf("%s\n", l.c_str());
		for (const std::string& f : r.failures)
			CHECK(false, "%s", f.c_str());
	}
	return rkatest::finish("spindrift test_hostile");
}
