// Unit tests for src/narrows/NarrowsEngine.hpp. Run with `make test`.
#include "../fathom/Ladder.hpp"
#include "NarrowsEngine.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

using namespace rka;
using namespace rka::narrows;
typedef NarrowsFrame F;

static const float FS = 48000.f;

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float next() {
		s = s * 1664525u + 1013904223u;
		return float(int32_t(s)) * (1.f / 2147483648.f);
	}
};

// An engine with the panel's defaults, one voice, both outputs patched.
struct Rig {
	NarrowsEngine e;
	NarrowsFrame f;
	NarrowsResult r;
	float fs;
	explicit Rig(float rate = FS) : fs(rate) {
		e.setSampleRate(fs);
		f.params[F::CUTOFF] = CUTOFF_DEFAULT;
		f.params[F::CORE] = 1.f;   // Ladder
		f.outRConnected = true;
		e.reset();
	}
	void mono(float volts) {
		f.inL.channels = 1;
		f.inL.v[0] = volts;
	}
	void step() {
		e.process(f, r);
	}
	void run(int n) {
		for (int i = 0; i < n; i++)
			step();
	}
	// One 1 ms knock on PING.
	void knock() {
		f.ping.channels = 1;
		f.ping.v[0] = 10.f;
		run(int(0.002f * fs));
		f.ping.v[0] = 0.f;
	}
};

static double gainDb(Rig& g, float sineHz, float volts, int n = 16384) {
	double in = 0.0, out = 0.0;
	for (int i = 0; i < n; i++) {
		const float x = volts * std::sin(2.f * float(M_PI) * sineHz * i / g.fs);
		g.mono(x);
		g.step();
		if (i >= n / 2) {
			in += double(x) * x;
			out += double(g.r.l[0]) * g.r.l[0];
		}
	}
	return 10.0 * std::log10(out / in);
}

struct Tone {
	double peak, hz;
};

// Peak and pitch (rising zero crossings) of a function of the rig's result.
static Tone listen(Rig& g, double seconds, const std::function<float(const NarrowsResult&)>& pick) {
	const int n = int(seconds * g.fs);
	float prev = 0.f;
	double first = -1.0, last = -1.0;
	int crossings = 0;
	Tone t = {0.0, 0.0};
	for (int i = 0; i < n; i++) {
		g.step();
		const float y = pick(g.r);
		t.peak = std::fmax(t.peak, std::fabs(double(y)));
		if (prev < 0.f && y >= 0.f) {
			const double at = i - 1 + double(-prev) / (double(y) - prev);
			if (first < 0.0)
				first = at;
			last = at;
			crossings++;
		}
		prev = y;
	}
	if (crossings >= 2)
		t.hz = double(crossings - 1) * g.fs / (last - first);
	return t;
}

static float leftOf(const NarrowsResult& r) {
	return r.l[0];
}
static float rightOf(const NarrowsResult& r) {
	return r.r[0];
}

static double cents(double hz, double want) {
	return 1200.0 * std::log2(hz / want);
}

// --- responses ---------------------------------------------------------------

static void lowpass_24_and_12() {
	for (int core = 0; core < 2; core++) {
		Rig g;
		g.f.params[F::CORE] = float(core);
		g.e.reset();
		const double pass = gainDb(g, 100.f, 1.f), at4 = gainDb(g, 4096.f, 1.f), at8 = gainDb(g, 8192.f, 1.f);
		const double slope = at4 - at8, want = core ? 24.0 : 12.0;
		CHECK(std::fabs(pass) < 0.5, "%s passband %f dB", core ? "ladder" : "state", pass);
		CHECK(std::fabs(slope - want) < 3.0, "%s slope %f dB per octave", core ? "ladder" : "state", slope);
	}
}

// In low-pass the passband keeps its level as Res rises, in both cores.
static void passband_holds_with_res() {
	for (int core = 0; core < 2; core++)
		for (float res : {0.4f, 0.7f, 0.9f}) {
			Rig g;
			g.f.params[F::CORE] = float(core);
			g.f.params[F::RES] = res;
			g.e.reset();
			const double pass = gainDb(g, 60.f, 0.5f);
			CHECK(std::fabs(pass) < 1.0, "%s passband at Res %g: %f dB", core ? "ladder" : "state", res, pass);
		}
}

// The ladder core's three pure points are Fathom's own LP24, BP and HP.
static void ladder_pure_points_are_fathoms() {
	const LadderType types[3] = {LadderType::LP24, LadderType::BP, LadderType::HP};
	const float modes[3] = {0.f, 0.5f, 1.f};
	const float volts = 1.f;
	for (int t = 0; t < 3; t++)
		for (float hz : {150.f, 1024.f, 6000.f}) {
			Rig g;
			g.f.params[F::MODE] = modes[t];
			g.e.reset();
			const double mine = gainDb(g, hz, volts);
			Ladder ref;
			ref.setCoefficients(cutoffHz(CUTOFF_DEFAULT), 0.f, types[t], FS);
			double in = 0.0, out = 0.0;
			for (int i = 0; i < 16384; i++) {
				const float x = volts / VOLTS * std::sin(2.f * float(M_PI) * hz * i / FS);
				const float y = ref.process(x);
				if (i >= 8192) {
					in += double(x) * x;
					out += double(y) * y;
				}
			}
			const double theirs = 10.0 * std::log10(out / in);
			CHECK(std::fabs(mine - theirs) < 0.3, "mode %g at %g Hz: %f dB, Fathom's ladder %f", modes[t], hz, mine, theirs);
		}
}

static void state_pure_points() {
	Rig bp;
	bp.f.params[F::CORE] = 0.f;
	bp.f.params[F::MODE] = 0.5f;
	bp.e.reset();
	const double at = gainDb(bp, 1024.f, 1.f), lo = gainDb(bp, 128.f, 1.f), hi = gainDb(bp, 8192.f, 1.f);
	CHECK(at - lo > 15.0 && at - hi > 15.0, "band-pass: %f at the cutoff, %f and %f three octaves away", at, lo, hi);
	Rig hp;
	hp.f.params[F::CORE] = 0.f;
	hp.f.params[F::MODE] = 1.f;
	hp.e.reset();
	const double pass = gainDb(hp, 12000.f, 1.f), stop = gainDb(hp, 128.f, 1.f);
	CHECK(std::fabs(pass) < 0.7 && stop < -30.0, "high-pass: %f in the passband, %f three octaves down", pass, stop);
}

static void mode_continuous() {
	for (int core = 0; core < 2; core++) {
		Rig g;
		g.f.params[F::CORE] = float(core);
		g.f.mode.channels = 1;
		g.e.reset();
		double step = 0.0, changed = 0.0;
		float prev = 0.f;
		for (int i = 0; i < 48000; i++) {
			g.mono(5.f * std::sin(2.f * float(M_PI) * 100.f * i / FS));
			g.f.mode.v[0] = i < 24000 ? 0.f : 5.f;   // LP, then straight to BP
			g.step();
			if (i > 100)
				step = std::fmax(step, std::fabs(double(g.r.l[0]) - prev));
			if (i > 40000)
				changed = std::fmax(changed, std::fabs(double(g.r.l[0])));
			prev = g.r.l[0];
		}
		CHECK(step < 0.15, "%s: largest step when Mode's CV jumps: %g V", core ? "ladder" : "state", step);
		CHECK(changed < 2.5, "%s: and it did become a band-pass (100 Hz now at %g V)", core ? "ladder" : "state", changed);
	}
}

// --- self-oscillation -------------------------------------------------------------

static void self_oscillation_in_tune() {
	double worstOnset = 0.0, worstTop = 0.0;
	double level[2] = {0.0, 0.0};
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (int core = 0; core < 2; core++)
			for (float voct : {-2.f, 0.f, 2.f, 4.f}) {
				const double want = 220.0 * std::exp2(double(voct));
				for (int top = 0; top < 2; top++) {
					Rig g(fs);
					g.f.params[F::CORE] = float(core);
					g.f.params[F::CUTOFF] = cutoffKnob(220.f);
					g.f.params[F::RES] = top ? 1.f : RES_ONSET + 0.006f;
					g.f.voct.channels = 1;
					g.f.voct.v[0] = voct;
					g.e.reset();
					g.knock();
					g.run(int(4 * fs));
					const Tone t = listen(g, 1.5, leftOf);
					const double c = t.hz > 0.0 ? cents(t.hz, want) : 1e9;
					CHECK(std::fabs(c) < (top ? 15.0 : 5.0), "%s %s at %g Hz, %g: %f cents", core ? "ladder" : "state",
					      top ? "Res 1" : "onset", want, fs, c);
					(top ? worstTop : worstOnset) = std::fmax(top ? worstTop : worstOnset, std::fabs(c));
					if (top) {
						CHECK(t.peak > 4.0 && t.peak < 9.0, "%s at %g Hz, %g: %f V", core ? "ladder" : "state", want, fs, t.peak);
						if (fs == 48000.f && voct == 0.f)
							level[core] = t.peak;
					}
				}
			}
	std::printf("  self-oscillation: worst %.2f cents at the onset, %.2f at Res 1; %.2f V state, %.2f V ladder\n", worstOnset,
	            worstTop, level[0], level[1]);
	CHECK(std::fabs(20.0 * std::log10(level[0] / level[1])) < 3.0, "the cores' levels: %f and %f V", level[0], level[1]);
}

static void voct_at_audio_rate() {
	// A self-oscillating filter at 1 kHz, a 200 Hz sine of 1 V on V/OCT:
	// the period swings between about half and twice.
	Rig g;
	g.f.params[F::CUTOFF] = cutoffKnob(1000.f);
	g.f.params[F::RES] = 1.f;
	g.f.voct.channels = 1;
	g.e.reset();
	g.knock();
	g.run(48000);
	double shortest = 1e9, longest = 0.0, lastAt = -1.0;
	float prev = 0.f;
	for (int i = 0; i < 48000; i++) {
		g.f.voct.v[0] = std::sin(2.f * float(M_PI) * 200.f * i / FS);
		g.step();
		const float y = g.r.l[0];
		if (prev < 0.f && y >= 0.f) {
			const double at = i - 1 + double(-prev) / (double(y) - prev);
			if (lastAt >= 0.0 && i > 4800) {
				shortest = std::fmin(shortest, at - lastAt);
				longest = std::fmax(longest, at - lastAt);
			}
			lastAt = at;
		}
		prev = y;
	}
	CHECK(longest / shortest > 1.6, "periods from %g to %g samples", shortest, longest);
}

static void spread() {
	// Spread 0: left is right to the bit, with everything else engaged.
	Rig z;
	z.f.params[F::RES] = 1.f;
	z.f.params[F::COMB] = 0.8f;
	z.f.params[F::DRIVE] = 0.5f;
	z.f.params[F::MODE] = 0.3f;
	z.e.reset();
	Lcg rng(21);
	long different = 0;
	double energy = 0.0;
	for (int i = 0; i < 48000; i++) {
		z.mono(3.f * rng.next());
		z.step();
		if (!(z.r.l[0] == z.r.r[0]))
			different++;
		energy += double(z.r.l[0]) * z.r.l[0];
	}
	CHECK(different == 0 && energy > 1.0, "Spread 0: %ld samples differ between the sides", different);
	// Spread 1: half an octave down on the left, up on the right.
	for (int core = 0; core < 2; core++) {
		Rig g;
		g.f.params[F::CORE] = float(core);
		g.f.params[F::CUTOFF] = cutoffKnob(440.f);
		g.f.params[F::RES] = RES_ONSET + 0.006f;
		g.f.params[F::SPREAD] = 1.f;
		g.e.reset();
		g.knock();
		g.run(3 * 48000);
		Rig h = g;
		const Tone l = listen(g, 1.0, leftOf), r = listen(h, 1.0, rightOf);
		CHECK(std::fabs(cents(l.hz, 440.0 / std::sqrt(2.0))) < 6.0 && std::fabs(cents(r.hz, 440.0 * std::sqrt(2.0))) < 6.0,
		      "%s: left %f Hz, right %f Hz", core ? "ladder" : "state", l.hz, r.hz);
	}
}

// --- the core switch --------------------------------------------------------------

static void core_switch_no_click_no_gap() {
	for (int from = 0; from < 2; from++) {
		// Audio passing through.
		Rig g;
		g.f.params[F::CORE] = float(from);
		g.e.reset();
		double step = 0.0;
		float prev = 0.f;
		for (int i = 0; i < 24000; i++) {
			g.mono(5.f * std::sin(2.f * float(M_PI) * 100.f * i / FS));
			if (i == 12000)
				g.f.params[F::CORE] = float(1 - from);
			g.step();
			if (i > 100)
				step = std::fmax(step, std::fabs(double(g.r.l[0]) - prev));
			prev = g.r.l[0];
		}
		CHECK(step < 0.5, "from core %d, audio through: largest step %g V", from, step);
		// A self-oscillating voice, no input.
		Rig s;
		s.f.params[F::CORE] = float(from);
		s.f.params[F::CUTOFF] = cutoffKnob(220.f);
		s.f.params[F::RES] = 1.f;
		s.e.reset();
		s.knock();
		s.run(3 * 48000);
		const Tone before = listen(s, 0.2, leftOf);
		s.f.params[F::CORE] = float(1 - from);
		double jump = 0.0;
		prev = s.r.l[0];
		for (int i = 0; i < 4800; i++) {   // 100 ms
			s.step();
			jump = std::fmax(jump, std::fabs(double(s.r.l[0]) - prev));
			prev = s.r.l[0];
		}
		const Tone after = listen(s, 0.05, leftOf);
		// A 220 Hz sine of 9 V steps by 0.26 V between samples at most.
		CHECK(jump < 0.5, "from core %d, self-oscillating: largest step %g V", from, jump);
		CHECK(after.peak > before.peak * 0.5 && after.peak < 9.0, "from core %d: %f V before, %f V 100 ms after the switch",
		      from, before.peak, after.peak);
	}
	// Flipped every few samples: bounded and finite.
	Rig g;
	g.f.params[F::RES] = 1.f;
	g.e.reset();
	Lcg rng(2);
	double peak = 0.0;
	for (int i = 0; i < 96000; i++) {
		g.mono(5.f * rng.next());
		g.f.params[F::CORE] = float((i / 3) % 2);
		g.step();
		peak = std::fmax(peak, std::fabs(double(g.r.l[0])));
		if (!(g.r.l[0] == g.r.l[0]))
			peak = 1e9;
	}
	CHECK(peak <= 10.0, "flipped every 3 samples: peak %g V", peak);
}

// --- drive --------------------------------------------------------------------------

static void drive() {
	for (int core = 0; core < 2; core++) {
		double base = 0.0, lastThd = 0.0;
		for (int d = 0; d <= 10; d++) {
			Rig g;
			g.f.params[F::CORE] = float(core);
			g.f.params[F::DRIVE] = 0.1f * d;
			g.e.reset();
			const float hz = 100.f;
			double s1 = 0.0, s2 = 0.0, total = 0.0, in2 = 0.0;
			const int n0 = 9600, n1 = n0 + 48000;
			for (int i = 0; i < n1; i++) {
				const double ph = 2.0 * M_PI * hz * i / FS;
				const float x = 5.f * float(std::sin(ph));
				g.mono(x);
				g.step();
				const float y = g.r.l[0];
				if (i >= n0) {
					s1 += y * std::sin(ph);
					s2 += y * std::cos(ph);
					total += double(y) * y;
					in2 += double(x) * x;
				}
			}
			const double db = 10.0 * std::log10(total / in2);
			const double thd = std::sqrt(std::fmax(0.0, 1.0 - 2.0 * (s1 * s1 + s2 * s2) / (n1 - n0) / total));
			if (d == 0) {
				base = db;
				Rig small;
				small.f.params[F::CORE] = float(core);
				small.e.reset();
				const double smallDb = gainDb(small, hz, 0.05f);
				CHECK(std::fabs(db - smallDb) < 1.0 && thd < 0.03, "%s clean at Drive 0: %f dB (small signal %f), distortion %f",
				      core ? "ladder" : "state", db, smallDb, thd);
			}
			CHECK(std::fabs(db - base) < 6.0, "%s at Drive %g: %f dB against %f at 0", core ? "ladder" : "state", 0.1 * d, db, base);
			lastThd = thd;
		}
		CHECK(lastThd > 0.15, "%s at full Drive is dirty: distortion %f", core ? "ladder" : "state", lastThd);
	}
}

// --- ping and comb ------------------------------------------------------------------

static void ping_rings() {
	for (int core = 0; core < 2; core++) {
		Rig g;
		g.f.params[F::CORE] = float(core);
		g.f.params[F::CUTOFF] = cutoffKnob(440.f);
		g.f.params[F::RES] = 0.9f;
		g.e.reset();
		g.run(4800);
		const Tone quiet = listen(g, 0.1, leftOf);
		g.knock();
		const Tone ring = listen(g, 0.05, leftOf);
		g.run(3 * 48000);
		const Tone later = listen(g, 0.1, leftOf);
		CHECK(quiet.peak < 0.01, "%s: silent before the ping (%g V)", core ? "ladder" : "state", quiet.peak);
		CHECK(ring.peak > 0.3 && std::fabs(cents(ring.hz, 440.0)) < 60.0, "%s: rings at %f Hz, %f V", core ? "ladder" : "state",
		      ring.hz, ring.peak);
		CHECK(later.peak < 0.01, "%s: and dies away (%g V)", core ? "ladder" : "state", later.peak);
	}
	// A polyphonic PING strikes each voice from its own channel; a mono
	// one strikes them all.
	Rig p;
	p.f.params[F::RES] = 0.9f;
	p.f.voct.channels = 3;
	p.f.ping.channels = 3;
	p.e.reset();
	p.run(100);
	p.f.ping.v[1] = 10.f;
	double peak[3] = {0, 0, 0};
	for (int i = 0; i < 4800; i++) {
		p.step();
		for (int v = 0; v < 3; v++)
			peak[v] = std::fmax(peak[v], std::fabs(double(p.r.l[v])));
	}
	CHECK(p.r.channels == 3 && peak[1] > 0.3 && peak[0] < 0.01 && peak[2] < 0.01, "poly ping: %g %g %g V", peak[0], peak[1],
	      peak[2]);
	Rig m;
	m.f.params[F::RES] = 0.9f;
	m.f.voct.channels = 3;
	m.e.reset();
	m.run(100);
	m.f.ping.channels = 1;
	m.f.ping.v[0] = 10.f;
	double all[3] = {0, 0, 0};
	for (int i = 0; i < 4800; i++) {
		m.step();
		for (int v = 0; v < 3; v++)
			all[v] = std::fmax(all[v], std::fabs(double(m.r.l[v])));
	}
	CHECK(all[0] > 0.3 && all[1] > 0.3 && all[2] > 0.3, "mono ping reaches every voice: %g %g %g V", all[0], all[1], all[2]);
}

static void comb_in_the_engine() {
	// Turned up, a ping rings at the cutoff's pitch from the comb alone
	// (Res 0). Band-pass, so the pulses' DC is gone and the ring crosses
	// zero once a period.
	Rig g;
	g.f.params[F::CUTOFF] = cutoffKnob(330.f);
	g.f.params[F::MODE] = 0.5f;
	g.f.params[F::COMB] = 0.9f;
	g.e.reset();
	g.knock();
	const Tone t = listen(g, 0.3, leftOf);
	CHECK(t.peak > 0.05 && std::fabs(cents(t.hz, 330.0)) < 25.0, "plucked comb: %f Hz, %f V", t.hz, t.peak);
	// Without the comb the same ping leaves next to nothing after 50 ms.
	Rig dry;
	dry.f.params[F::CUTOFF] = cutoffKnob(330.f);
	dry.f.params[F::MODE] = 0.5f;
	dry.e.reset();
	dry.knock();
	dry.run(2400);
	g.e.reset();
	g.knock();
	g.run(2400);
	const Tone d = listen(dry, 0.1, leftOf), w = listen(g, 0.1, leftOf);
	CHECK(w.peak > 20.0 * d.peak, "50 ms on: %g V with the comb, %g V without", w.peak, d.peak);
	// Negative: the peaks are the odd multiples of half the cutoff (165,
	// 495 Hz), not the cutoff itself; through the band-pass at 330 Hz the
	// ring is on one of those.
	Rig n;
	n.f.params[F::CUTOFF] = cutoffKnob(330.f);
	n.f.params[F::MODE] = 0.5f;
	n.f.params[F::COMB] = -0.9f;
	n.e.reset();
	n.knock();
	const Tone u = listen(n, 0.3, leftOf);
	CHECK(std::fabs(cents(u.hz, 165.0)) < 25.0 || std::fabs(cents(u.hz, 495.0)) < 25.0, "negative comb: %f Hz", u.hz);
	// Comb's CV: +5 V is the knob's full travel.
	Rig a, b;
	a.f.params[F::COMB] = 1.f;
	b.f.comb.channels = 1;
	b.f.comb.v[0] = 5.f;
	a.e.reset();
	b.e.reset();
	Lcg rng(31);
	long different = 0;
	for (int i = 0; i < 9600; i++) {
		const float x = rng.next();
		a.mono(x);
		b.mono(x);
		a.step();
		b.step();
		if (!(a.r.l[0] == b.r.l[0]))
			different++;
	}
	CHECK(different == 0, "Comb knob at full against 5 V on its CV: %ld samples differ", different);
}

// --- voices -------------------------------------------------------------------------

// n voices, each with its own pitch and Res CV.
static void fillVoices(Rig& g, int n) {
	g.f.inL.channels = n;
	g.f.voct.channels = n;
	g.f.res.channels = n;
	for (int v = 0; v < n; v++) {
		g.f.voct.v[v] = -2.f + 0.3f * v;
		g.f.res.v[v] = 0.6f * v;
	}
}

static void voices_independent() {
	Rig a, b;
	fillVoices(a, 16);
	fillVoices(b, 16);
	a.f.params[F::COMB] = 0.5f;
	b.f.params[F::COMB] = 0.5f;
	a.e.reset();
	b.e.reset();
	Lcg same(77), ra(1), rb(2);
	long different = 0, othersDiffer = 0;
	for (int i = 0; i < 48000; i++) {
		const float x = 4.f * same.next();
		for (int v = 0; v < 16; v++) {
			a.f.inL.v[v] = v == 5 ? x : 4.f * ra.next();
			b.f.inL.v[v] = v == 5 ? x : 4.f * rb.next();
		}
		a.step();
		b.step();
		if (!(a.r.l[5] == b.r.l[5] && a.r.r[5] == b.r.r[5]))
			different++;
		if (a.r.l[4] != b.r.l[4])
			othersDiffer++;
	}
	CHECK(a.r.channels == 16, "16 voices out, got %d", a.r.channels);
	CHECK(different == 0 && othersDiffer > 40000, "voice 5 among different neighbours: %ld samples differ", different);
}

static void cables_and_voices() {
	// One audio channel, four V/OCT channels: four voices, all sounding,
	// each at its own cutoff.
	Rig g;
	g.f.voct.channels = 4;
	for (int v = 0; v < 4; v++)
		g.f.voct.v[v] = float(v) - 2.f;
	g.e.reset();
	Lcg rng(3);
	double e[4] = {0, 0, 0, 0};
	for (int i = 0; i < 24000; i++) {
		g.mono(3.f * rng.next());
		g.step();
		for (int v = 0; v < 4; v++)
			e[v] += double(g.r.l[v]) * g.r.l[v];
	}
	CHECK(g.r.channels == 4, "four voices, got %d", g.r.channels);
	CHECK(e[0] > 1.0 && e[1] > 1.3 * e[0] && e[2] > 1.3 * e[1] && e[3] > 1.3 * e[2], "a mono cable feeds every voice: %g %g %g %g",
	      e[0], e[1], e[2], e[3]);
	// A two-channel audio cable with four voices: voices 2 and 3 get silence.
	Rig h;
	h.f.voct.channels = 4;
	h.f.inL.channels = 2;
	h.e.reset();
	double q[4] = {0, 0, 0, 0};
	for (int i = 0; i < 24000; i++) {
		h.f.inL.v[0] = h.f.inL.v[1] = 3.f * rng.next();
		h.step();
		for (int v = 0; v < 4; v++)
			q[v] += double(h.r.l[v]) * h.r.l[v];
	}
	CHECK(q[0] > 1.0 && q[1] > 1.0 && q[2] < 1e-3 && q[3] < 1e-3, "past a polyphonic cable's last channel: %g %g %g %g", q[0], q[1],
	      q[2], q[3]);
	// A mono CV reaches every voice; a polyphonic one sets each.
	Rig m;
	m.f.voct.channels = 2;
	m.f.params[F::CUTOFF] = cutoffKnob(440.f);
	m.f.res.channels = 1;
	m.f.res.v[0] = 10.f;
	m.e.reset();
	m.knock();
	m.run(2 * 48000);
	CHECK(std::fabs(m.r.l[0]) + std::fabs(m.r.l[1]) > 0.f, "sounding");
	double p0 = 0.0, p1 = 0.0;
	for (int i = 0; i < 4800; i++) {
		m.step();
		p0 = std::fmax(p0, std::fabs(double(m.r.l[0])));
		p1 = std::fmax(p1, std::fabs(double(m.r.l[1])));
	}
	CHECK(p0 > 3.0 && p1 > 3.0, "a mono Res CV makes both voices self-oscillate: %g, %g V", p0, p1);
	m.f.res.channels = 2;
	m.f.res.v[1] = 0.f;
	m.run(2 * 48000);
	p0 = p1 = 0.0;
	for (int i = 0; i < 4800; i++) {
		m.step();
		p0 = std::fmax(p0, std::fabs(double(m.r.l[0])));
		p1 = std::fmax(p1, std::fabs(double(m.r.l[1])));
	}
	CHECK(p0 > 3.0 && p1 < 0.01, "a polyphonic Res CV sets each voice: %g, %g V", p0, p1);
}

static void voice_count_change_unbroken() {
	Rig a, b;
	for (Rig* g : {&a, &b}) {
		g->f.inL.channels = 4;
		g->f.params[F::RES] = 0.7f;
		g->f.params[F::COMB] = 0.6f;
		g->e.reset();
	}
	Lcg rng(9);
	long different = 0;
	double rejoin = 0.0;
	for (int i = 0; i < 30000; i++) {
		// a stays at 4 voices; b drops to 2 for a while and comes back.
		b.f.inL.channels = (i >= 10000 && i < 20000) ? 2 : 4;
		for (int v = 0; v < 4; v++)
			a.f.inL.v[v] = b.f.inL.v[v] = (i < 20000) ? 3.f * rng.next() : 0.f;
		a.step();
		b.step();
		if (!(a.r.l[0] == b.r.l[0] && a.r.r[1] == b.r.r[1]))
			different++;
		if (i >= 20000 && i < 20010)
			rejoin = std::fmax(rejoin, std::fabs(double(b.r.l[3])));
	}
	CHECK(different == 0, "voices 0 and 1 while the count changes: %ld samples differ", different);
	CHECK(b.r.channels == 4 && rejoin < 1e-3, "a returning voice starts from rest: %g V with silence in", rejoin);
}

static void normalling() {
	// IN R alone feeds both sides.
	Rig g;
	g.f.inR.channels = 1;
	g.e.reset();
	Lcg rng(4);
	double e = 0.0;
	long different = 0;
	for (int i = 0; i < 9600; i++) {
		g.f.inR.v[0] = 3.f * rng.next();
		g.step();
		e += double(g.r.l[0]) * g.r.l[0];
		if (g.r.l[0] != g.r.r[0])
			different++;
	}
	CHECK(e > 1.0 && different == 0, "IN R alone: energy %g, %ld samples differ between the sides", e, different);
	// With R out unpatched, L carries the average of the two sides.
	Rig both, left;
	for (Rig* r : {&both, &left}) {
		r->f.inL.channels = 1;
		r->f.inR.channels = 1;
		r->f.params[F::SPREAD] = 1.f;
	}
	left.f.outRConnected = false;
	both.e.reset();
	left.e.reset();
	double worst = 0.0;
	for (int i = 0; i < 9600; i++) {
		const float l = 2.f * rng.next(), r = 2.f * rng.next();
		both.f.inL.v[0] = left.f.inL.v[0] = l;
		both.f.inR.v[0] = left.f.inR.v[0] = r;
		both.step();
		left.step();
		worst = std::fmax(worst, std::fabs(double(left.r.l[0]) - 0.5 * (double(both.r.l[0]) + both.r.r[0])));
	}
	CHECK(worst < 1e-5, "L alone is the average: off by %g V", worst);
}

// --- bounds -------------------------------------------------------------------------

static void bounded_everything_up() {
	for (int core = 0; core < 2; core++)
		for (float comb : {1.f, -1.f}) {
			Rig g;
			g.f.params[F::CORE] = float(core);
			g.f.params[F::RES] = 1.f;
			g.f.params[F::DRIVE] = 1.f;
			g.f.params[F::COMB] = comb;
			g.f.params[F::CUTOFF] = cutoffKnob(300.f);
			g.e.reset();
			Lcg rng(5);
			double peak = 0.0, dc = 0.0;
			const int n = 20 * 48000;
			for (int i = 0; i < 3 * n; i++) {
				g.mono(i < n ? 10.f * rng.next() : i < 2 * n ? 10.f : 0.f);
				g.step();
				peak = std::fmax(peak, std::fabs(double(g.r.l[0])));
				if (!(g.r.l[0] == g.r.l[0]))
					peak = 1e9;
				if (i >= 3 * n - 48000)
					dc += g.r.l[0];
			}
			CHECK(peak <= 10.0, "core %d, comb %g: peak %g V", core, comb, peak);
			CHECK(std::fabs(dc / 48000.0) < 0.05, "core %d, comb %g: DC %g V in silence", core, comb, dc / 48000.0);
			g.f.params[F::RES] = 0.f;
			g.f.params[F::COMB] = 0.f;
			g.run(5 * 48000);
			const Tone t = listen(g, 0.2, leftOf);
			CHECK(t.peak < 0.01, "core %d, comb %g: silent 5 s after Res comes down (%g V)", core, comb, t.peak);
		}
}

// The outputs' soft limit: straight below 8 V, never past 10 V, on both
// sides, where the filter alone would go well beyond.
static void output_limit() {
	for (int core = 0; core < 2; core++) {
		Rig g;
		g.f.params[F::CORE] = float(core);
		g.f.params[F::RES] = 0.9f;
		g.f.params[F::DRIVE] = 0.25f;
		g.f.params[F::CUTOFF] = cutoffKnob(500.f);
		g.e.reset();
		double peakL = 0.0, peakR = 0.0;
		for (int i = 0; i < 96000; i++) {
			g.mono(10.f * std::sin(2.f * float(M_PI) * 500.f * i / FS));
			g.step();
			peakL = std::fmax(peakL, std::fabs(double(g.r.l[0])));
			peakR = std::fmax(peakR, std::fabs(double(g.r.r[0])));
		}
		CHECK(peakL <= 10.0 && peakR <= 10.0, "core %d: peaks %g and %g V", core, peakL, peakR);
		// The state core is the one that reaches the limit here (14.8 V
		// without it); the ladder holds itself to about 7.5 V.
		if (core == 0)
			CHECK(peakL > 9.0 && peakR > 9.0, "the limit is at work: peaks %g and %g V", peakL, peakR);
	}
	// Below the knee nothing is touched: a 1 V and a 2 V sine come out in
	// proportion.
	Rig a, b;
	a.e.reset();
	b.e.reset();
	const double ga = gainDb(a, 100.f, 1.f), gb = gainDb(b, 100.f, 2.f);
	CHECK(std::fabs(ga - gb) < 0.05, "1 V and 2 V: %f and %f dB", ga, gb);
}

static void reset_and_rate_change_rest() {
	Rig g;
	g.f.params[F::RES] = 1.f;
	g.f.params[F::COMB] = 0.9f;
	g.e.reset();
	Lcg rng(6);
	for (int i = 0; i < 48000; i++) {
		g.mono(5.f * rng.next());
		g.step();
	}
	g.mono(0.f);
	g.f.params[F::RES] = 0.f;
	g.e.reset();
	Tone t = listen(g, 0.1, leftOf);
	CHECK(t.peak < 1e-3, "after reset: %g V", t.peak);
	g.f.params[F::RES] = 1.f;
	for (int i = 0; i < 48000; i++) {
		g.mono(5.f * rng.next());
		g.step();
	}
	g.mono(0.f);
	g.f.params[F::RES] = 0.f;
	g.fs = 96000.f;
	g.e.setSampleRate(96000.f);
	t = listen(g, 0.1, leftOf);
	CHECK(t.peak < 1e-3, "after a rate change: %g V", t.peak);
}

int main() {
	lowpass_24_and_12();
	passband_holds_with_res();
	ladder_pure_points_are_fathoms();
	state_pure_points();
	mode_continuous();
	self_oscillation_in_tune();
	voct_at_audio_rate();
	spread();
	core_switch_no_click_no_gap();
	drive();
	ping_rings();
	comb_in_the_engine();
	voices_independent();
	cables_and_voices();
	voice_count_change_unbroken();
	normalling();
	bounded_everything_up();
	output_limit();
	reset_and_rate_change_rest();
	return rkatest::finish("narrows/test_engine");
}
