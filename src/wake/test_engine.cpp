// Wake's engine (src/wake/WakeEngine.hpp) against the spec's Testing
// section: echo time, Ratio, the clock, Feedback, the styles' time changes,
// Drift, Freeze, Ping Pong, clicks, stereo, Mix and the limit.
#include "hadal/Random.hpp"
#include "wake/WakeEngine.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

using F = rka::WakeFrame;
using namespace rka::wake;
const double TWO_PI = 6.283185307179586;

double db(double x) {
	return 20.0 * std::log10(x > 1e-30 ? x : 1e-30);
}

// Wet only, Digital, no Feedback, Diffuse or Drift, one cable in, both out.
struct Rig {
	rka::WakeEngine e;
	F f;
	rka::WakeResult r;
	float fs;
	explicit Rig(float rate = 48000.f, uint32_t seed = 1) : e(seed), fs(rate) {
		e.setSampleRate(rate);
		f.inLPatched = true;
		f.outRPatched = true;
		f.params[F::MIX] = 1.f;
		f.params[F::DRIFT] = 0.f;
		f.params[F::FEEDBACK] = 0.f;
		f.style = 2;
	}
	void time(float seconds) {
		f.params[F::TIME] = timeKnob(seconds);
	}
	void feedback(float gain) {
		f.params[F::FEEDBACK] = gain / FEEDBACK_MAX;
	}
	void step(float l, float rr = 0.f) {
		f.inL = l;
		f.inR = rr;
		e.process(f, r);
	}
};

// The sample index of the largest |x| in [from, to).
int peakAt(const std::vector<float>& x, int from, int to) {
	int at = from;
	for (int i = from; i < to && i < (int) x.size(); i++)
		if (std::fabs(x[i]) > std::fabs(x[at]))
			at = i;
	return at;
}

// The frequency from the first to the last rising zero crossing, each
// placed between its two samples.
double crossingsHz(const std::vector<float>& x, float fs) {
	int c = 0;
	double first = 0, last = 0;
	for (size_t i = 1; i < x.size(); i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f) {
			const double at = (double) i - x[i] / ((double) x[i] - x[i - 1]);
			if (c == 0)
				first = at;
			last = at;
			c++;
		}
	return c > 1 ? (c - 1) * (double) fs / (last - first) : 0.0;
}

void echo_time() {
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (int style = 0; style < 3; style++)
			for (float t : {0.01f, 0.3f, 1.f, 10.f}) {
				Rig g(fs);
				g.f.style = style;
				g.time(t);
				const int n0 = 64, want = (int) std::lround(t * fs);
				std::vector<float> l((size_t) (want + want / 2 + 2000));
				for (size_t i = 0; i < l.size(); i++) {
					g.step((int) i == n0 ? 5.f : 0.f);
					l[i] = g.r.l;
				}
				CHECK(std::fabs(g.e.seconds(0) / t - 1.f) < 0.002f, "Time set to %g s is %g s", t, g.e.seconds(0));
				const int at = peakAt(l, n0 + 1, (int) l.size()) - n0;
				const double exact = (double) g.e.seconds(0) * fs;
				// Tape's and Analog's filters round the echo: its peak is
				// a few samples late.
				const double tol = std::max(style == 2 ? 1.0 : 4.0 * fs / 48000.0, 0.001 * exact);
				CHECK(std::fabs(at - exact) <= tol && std::fabs(l[(size_t) (at + n0)]) > 0.2f,
				      "style %d at %.0f Hz, %g s: the echo arrives after %d samples (want %.1f), %.2f V", style, fs, t, at, exact,
				      l[(size_t) (at + n0)]);
			}
}

// With Diffuse in, the echo is spread but still centred on Time.
void echo_time_diffused() {
	for (float amount : {0.3f, 1.f}) {
		Rig g;
		g.time(0.2f);
		g.f.params[F::DIFFUSE] = amount;
		const int n0 = 4800;   // after the smear's fade-in
		double energy = 0, moment = 0;
		for (int i = 0; i < 24000; i++) {
			g.step(i == n0 ? 5.f : 0.f);
			if (i > n0) {
				energy += (double) g.r.l * g.r.l;
				moment += (double) g.r.l * g.r.l * (i - n0);
			}
		}
		const double centroid = moment / energy;
		CHECK(std::fabs(centroid - 9600.0) < 48.0, "Diffuse %.1f: the echo's energy is centred %.1f samples after the hit (want 9600)",
		      amount, centroid);
		CHECK(std::fabs(db(std::sqrt(energy) / 5.0)) < 2.0, "Diffuse %.1f: the echo's level is %+.2f dB (want within 2)", amount,
		      db(std::sqrt(energy) / 5.0));
	}
}

void ratio_times() {
	const double want[7][2] = {{0.5, 1}, {2.0 / 3, 1}, {0.75, 1}, {1, 1}, {1, 0.75}, {1, 2.0 / 3}, {1, 0.5}};
	for (int ratio = 0; ratio < 7; ratio++) {
		Rig g;
		g.time(0.2f);
		g.f.params[F::RATIO] = (float) ratio;
		std::vector<float> l(12000), r(12000);
		for (int i = 0; i < 12000; i++) {
			g.step(i == 10 ? 5.f : 0.f);
			l[i] = g.r.l;
			r[i] = g.r.r;
		}
		const int al = peakAt(l, 11, 12000) - 10, ar = peakAt(r, 11, 12000) - 10;
		const double t = g.e.seconds(want[ratio][0] == 1 ? 0 : 1) * 48000.0;
		CHECK(std::abs(al - (int) std::lround(t * want[ratio][0])) <= 1 && std::abs(ar - (int) std::lround(t * want[ratio][1])) <= 1,
		      "ratio %d: echoes after %d and %d samples (want %.0f and %.0f)", ratio, al, ar, t * want[ratio][0],
		      t * want[ratio][1]);
	}
}

void clocked() {
	const double mult[10] = {1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1, 3.0 / 2, 2, 3, 4};
	// A 0.5 s clock; every knob step.
	for (int step = 0; step < 10; step++) {
		Rig g;
		g.f.clockPatched = true;
		g.f.params[F::TIME] = step / 9.f;
		for (int i = 0; i < 24000 * 3; i++) {
			g.f.clock = i % 24000 < 480 ? 10.f : 0.f;
			g.step(0.f);
			if (i == 23000)
				CHECK(g.e.clockStep() == -1, "before two edges Time is its own (step %d)", g.e.clockStep());
		}
		CHECK(g.e.clockStep() == step && std::fabs(g.e.seconds(0) / (0.5 * mult[step]) - 1.0) < 1e-3,
		      "knob step %d on a 0.5 s clock gives step %d, %g s (want %g)", step, g.e.clockStep(), g.e.seconds(0), 0.5 * mult[step]);
	}
	// A CV sweeps the steps and never lands between them.
	Rig g;
	g.f.clockPatched = true;
	g.f.params[F::TIME] = 0.5f;
	g.f.params[F::TIME_ATT] = 1.f;
	bool onGrid = true;
	int seen = 0;
	for (int i = 0; i < 48000 * 8; i++) {
		g.f.clock = i % 24000 < 480 ? 10.f : 0.f;
		g.f.timeCv = -6.f + 12.f * i / (48000.f * 8);
		g.step(0.f);
		if (i > 48000) {
			bool ok = false;
			for (int k = 0; k < 10; k++)
				ok = ok || std::fabs(g.e.seconds(0) / (0.5 * mult[k]) - 1.0) < 1e-3;
			onGrid = onGrid && ok;
			seen |= 1 << g.e.clockStep();
		}
	}
	CHECK(onGrid, "a clocked Time under a CV must stay on the clock's steps");
	CHECK(seen == 0x3ff, "a +-6 V CV at full attenuverter reaches every step (seen %#x)", seen);
	// A 4 s clock at x4 is 16 s: halved to 8.
	Rig h;
	h.f.clockPatched = true;
	h.f.params[F::TIME] = 1.f;
	for (int i = 0; i < 192000 * 2 + 100; i++) {
		h.f.clock = i % 192000 < 480 ? 10.f : 0.f;
		h.step(0.f);
	}
	CHECK(std::fabs(h.e.seconds(0) - 8.f) < 0.01f, "a 4 s clock at x4 is halved to %g s (want 8)", h.e.seconds(0));
	// The clock stops: the tempo holds. Unpatched: the knob's own time.
	for (int i = 0; i < 480000; i++) {
		h.f.clock = 0.f;
		h.step(0.f);
	}
	CHECK(std::fabs(h.e.seconds(0) - 8.f) < 0.01f, "a stopped clock holds (%g s)", h.e.seconds(0));
	h.f.clockPatched = false;
	for (int i = 0; i < 64; i++)
		h.step(0.f);
	CHECK(h.e.clockStep() == -1 && std::fabs(h.e.seconds(0) - 10.f) < 0.01f, "unpatched, Time is the knob's (%g s)", h.e.seconds(0));
}

void feedback_6db() {
	Rig g;
	g.time(0.1f);
	g.feedback(0.5f);
	std::vector<float> l(30000);
	for (int i = 0; i < 30000; i++) {
		g.step(i == 10 ? 5.f : 0.f);
		l[i] = g.r.l;
	}
	const double a = std::fabs(l[peakAt(l, 4000, 6000)]), b = std::fabs(l[peakAt(l, 9000, 11000)]),
	             c = std::fabs(l[peakAt(l, 14000, 16000)]);
	std::printf("  Feedback 50 %%: echoes of %.3f, %.3f, %.3f V\n", a, b, c);
	CHECK(std::fabs(db(b / a) + 6.02) < 0.5 && std::fabs(db(c / b) + 6.02) < 0.5, "successive echoes fall %.2f and %.2f dB (want 6)",
	      -db(b / a), -db(c / b));
	CHECK(std::fabs(db(a / 5.0)) < 0.2, "the first echo is %.2f dB re the input (want 0)", db(a / 5.0));
	// Feedback's CV: 10 V is the knob's travel.
	Rig h;
	h.time(0.1f);
	h.f.feedbackCv = 5.f;
	std::vector<float> m(12000);
	for (int i = 0; i < 12000; i++) {
		h.step(i == 10 ? 5.f : 0.f);
		m[i] = h.r.l;
	}
	const double a2 = std::fabs(m[peakAt(m, 4000, 6000)]), b2 = std::fabs(m[peakAt(m, 9000, 11000)]);
	CHECK(std::fabs(b2 / a2 - 0.55) < 0.02, "5 V on Feedback's CV gives a gain of %.3f (want 0.55)", b2 / a2);
}

// A sine in the delay, then Time doubles.
void time_step_per_style() {
	for (int style = 0; style < 3; style++) {
		Rig g;
		g.f.style = style;
		g.time(0.3f);
		long n = 0;
		auto block = [&](int len) {
			std::vector<float> x((size_t) len);
			for (int i = 0; i < len; i++) {
				g.step(2.f * (float) std::sin(TWO_PI * 440.0 * n++ / 48000.0));
				x[(size_t) i] = g.r.l;
			}
			return crossingsHz(x, 48000.f);
		};
		block(48000);
		g.time(0.6f);
		double lowest = 1e9, highest = 0;
		for (int b = 0; b < 100; b++) {   // 5 s in 50 ms blocks
			const double hz = block(2400);
			lowest = std::min(lowest, hz);
			highest = std::max(highest, hz);
		}
		const double settled = block(4800);
		std::printf("  style %d, Time 300 -> 600 ms: pitch %.0f .. %.0f Hz, then %.0f Hz\n", style, lowest, highest, settled);
		if (style == 2)
			CHECK(lowest > 440.0 * 0.99 && highest < 440.0 * 1.01, "Digital must not bend pitch (%.1f .. %.1f Hz)", lowest, highest);
		else
			CHECK(lowest < 400.0 && lowest > 109.0 && highest < 445.0, "style %d must bend down, within two octaves (%.1f .. %.1f Hz)",
			      style, lowest, highest);
		CHECK(std::fabs(settled - 440.0) < 2.0 && !g.e.tap(0).moving(), "style %d settles at 440 Hz (%.1f)", style, settled);
		CHECK(std::fabs(g.e.tap(0).delay() - (0.6f * 48000.f - 1.f)) < 30.f, "and at the new time (%.1f samples)", g.e.tap(0).delay());
	}
}

void drift() {
	// At 0: nothing moves.
	Rig g;
	g.time(0.25f);
	bool still = true;
	for (int i = 0; i < 48000 * 20; i++) {
		g.step(0.f);
		still = still && g.e.driftSamples(0) == 0.f && g.e.driftSamples(1) == 0.f;
	}
	CHECK(still && g.e.tap(0).delay() == g.e.tap(1).delay(), "Drift 0 must not move the echo time");
	// At full: each side's pitch wanders within +-35 cents, by more than 5
	// at times, and the two sides go their own ways.
	// (A short echo wavers less: what comes out differs from what went in
	// by how far the speed moved in between.)
	for (float t : {0.05f, 0.3f, 3.f}) {
		Rig h;
		h.time(t);
		h.f.params[F::DRIFT] = 1.f;
		double worst[2] = {0, 0}, sxy = 0, sxx = 0, syy = 0;
		float prev[2] = {0, 0};
		for (int i = 0; i < 48000 * 120; i++) {
			h.step(0.f);
			if (i % 16 == 15) {
				double c[2];
				for (int s = 0; s < 2; s++) {
					const double slope = (h.e.driftSamples(s) - prev[s]) / 16.0;
					prev[s] = h.e.driftSamples(s);
					c[s] = 1200.0 * std::log2(1.0 - slope);
					if (i > 48000)
						worst[s] = std::max(worst[s], std::fabs(c[s]));
				}
				sxy += c[0] * c[1];
				sxx += c[0] * c[0];
				syy += c[1] * c[1];
			}
		}
		const double corr = sxy / std::sqrt(sxx * syy + 1e-30);
		std::printf("  Drift full at %g s: pitch to +-%.1f and +-%.1f cents, sides correlate %.2f\n", t, worst[0], worst[1], corr);
		for (int s = 0; s < 2; s++)
			CHECK(worst[s] <= 35.0 && worst[s] > (t < 0.1f ? 1.0 : 10.0), "side %d's pitch wanders to %.1f cents at %g s (want over %d, within 35)", s,
			      worst[s], t, t < 0.1f ? 1 : 10);
		CHECK(std::fabs(corr) < 0.3, "the two sides' wanders correlate at %.2f (want under 0.3)", corr);
		// A long echo's pitch is the difference of two speeds, so it goes
		// beyond one speed's 15 cents; a short echo's stays well inside.
		if (t >= 3.f)
			CHECK(worst[0] > 18.0 && worst[1] > 18.0, "at %g s the wander should pass 18 cents (%.1f, %.1f)", t, worst[0], worst[1]);
		if (t < 0.1f)
			CHECK(worst[0] < 8.0 && worst[1] < 8.0, "at %g s the wander should stay under 8 cents (%.1f, %.1f)", t, worst[0], worst[1]);
	}
	// It is heard: an echo's arrival moves by a few samples.
	Rig a, b;
	b.f.params[F::DRIFT] = 1.f;
	for (Rig* x : {&a, &b})
		x->time(0.3f);
	int at[2] = {0, 0};
	Rig* rigs[2] = {&a, &b};
	for (int k = 0; k < 2; k++) {
		std::vector<float> l(48000 * 6);
		for (size_t i = 0; i < l.size(); i++) {
			rigs[k]->step(i == 48000 * 5 ? 5.f : 0.f);
			l[i] = rigs[k]->r.l;
		}
		at[k] = peakAt(l, 48000 * 5 + 1, (int) l.size()) - 48000 * 5;
	}
	CHECK(at[0] == 14400 && at[1] != at[0] && std::abs(at[1] - at[0]) < 300, "with Drift the echo arrives at %d (without: %d)", at[1],
	      at[0]);
}

void freeze() {
	Rig g;
	rka::hadal::Random rng;
	rng.seed(2);
	g.time(0.3f);
	g.feedback(0.6f);
	for (int i = 0; i < 96000; i++)
		g.step(3.f * rng.uniform());
	g.f.freeze = true;
	// The switch fades; then take one loop.
	for (int i = 0; i < 4800; i++)
		g.step(3.f * rng.uniform());
	CHECK(g.r.frozen, "the latch freezes");
	const int L = 14400;
	std::vector<float> first((size_t) L), firstR((size_t) L);
	for (int i = 0; i < L; i++) {
		g.step(3.f * rng.uniform());
		first[(size_t) i] = g.r.l;
		firstR[(size_t) i] = g.r.r;
	}
	double rms = 0;
	for (float v : first)
		rms += (double) v * v;
	CHECK(std::sqrt(rms / L) > 0.5, "the frozen loop is sounding (%.3f V RMS)", std::sqrt(rms / L));
	// Five minutes, with input and every held control moving; Feedback to 0
	// and back; Tone and Diffuse turned.
	double worst = 0;
	const long loops = 1000;
	for (long k = 0; k < loops; k++) {
		if (k == 100) {
			g.time(1.7f);
			g.f.params[F::RATIO] = 0.f;
		}
		if (k == 150)
			CHECK(std::fabs(g.e.seconds(0) - 0.3f) < 1e-3f && std::fabs(g.e.seconds(1) - 0.3f) < 1e-3f,
			      "Time and Ratio are held while frozen (%g / %g s)", g.e.seconds(0), g.e.seconds(1));
		if (k == 200)
			g.f.pingPong = true;
		if (k == 300)
			g.feedback(0.f);
		if (k == 400) {
			g.f.params[F::TONE] = 0.f;
			g.f.params[F::DIFFUSE] = 1.f;
			g.f.style = 1;
		}
		if (k == 500) {
			g.time(0.3f);
			g.f.params[F::RATIO] = 3.f;
			g.f.pingPong = false;
			g.feedback(0.6f);
			g.f.params[F::TONE] = 0.5f;
			g.f.params[F::DIFFUSE] = 0.f;
			g.f.style = 2;
		}
		for (int i = 0; i < L; i++) {
			g.step(8.f * rng.uniform());
			worst = std::max(worst, (double) std::fabs(g.r.l - first[(size_t) i]));
			worst = std::max(worst, (double) std::fabs(g.r.r - firstR[(size_t) i]));
		}
	}
	CHECK(worst < 1e-5, "a frozen loop must repeat unchanged for 5 minutes, whatever moves (off by %g)", worst);
	// Its period is the echo time, not a part of it.
	double diff = 0;
	for (int i = 0; i + L / 2 < L; i++)
		diff = std::max(diff, (double) std::fabs(first[(size_t) i] - first[(size_t) (i + L / 2)]));
	CHECK(diff > 0.5, "the loop is one echo time long, not half");

	// The gate freezes too, and holds the left and right loops' own lengths.
	Rig h;
	h.time(0.2f);
	h.f.params[F::RATIO] = 6.f;   // right at half
	h.feedback(0.5f);
	long n = 0;
	auto sine = [&]() { return 3.f * (float) std::sin(TWO_PI * 71.3 * n++ / 48000.0); };
	for (int i = 0; i < 48000; i++)
		h.step(sine());
	h.f.freezeGate = 5.f;
	float prevL = h.r.l, prevR = h.r.r, jump = 0.f, peak = 0.f;
	std::vector<float> r;
	for (int i = 0; i < 48000; i++) {
		h.step(sine());
		jump = std::max(jump, std::max(std::fabs(h.r.l - prevL), std::fabs(h.r.r - prevR)));
		peak = std::max(peak, std::fabs(h.r.l));
		prevL = h.r.l;
		prevR = h.r.r;
		if (i >= 9600)
			r.push_back(h.r.r);
	}
	CHECK(h.r.frozen, "the gate freezes");
	double worstR = 0;
	for (size_t i = 0; i + 4800 < r.size(); i++)
		worstR = std::max(worstR, (double) std::fabs(r[i] - r[i + 4800]));
	CHECK(worstR < 1e-5, "the right side loops its own 100 ms (off by %g)", worstR);
	h.f.freezeGate = 0.f;
	for (int i = 0; i < 48000; i++) {
		h.step(sine());
		jump = std::max(jump, std::max(std::fabs(h.r.l - prevL), std::fabs(h.r.r - prevR)));
		prevL = h.r.l;
		prevR = h.r.r;
	}
	CHECK(!h.r.frozen, "the gate releases");
	const float allowed = 4.f * peak * (float) (TWO_PI * 71.3 / 48000.0);
	CHECK(jump <= allowed, "freezing, the joins and releasing step %.4f V a sample (allowed %.4f)", jump, allowed);
}

void ping_pong() {
	// One cable, Ping Pong on, no Feedback: one echo a side, left first.
	Rig g;
	g.time(0.1f);
	g.f.pingPong = true;
	std::vector<float> l(30000), r(30000);
	for (int i = 0; i < 30000; i++) {
		g.step(i == 10 ? 5.f : 0.f);
		l[i] = g.r.l;
		r[i] = g.r.r;
	}
	const int al = peakAt(l, 11, 30000) - 10, ar = peakAt(r, 11, 30000) - 10;
	CHECK(std::abs(al - 4800) <= 1 && std::abs(ar - 9600) <= 1, "the bounce: left after %d, right after %d (want 4800, 9600)", al, ar);
	CHECK(std::fabs(db(std::fabs(r[(size_t) (ar + 10)]) / std::fabs(l[(size_t) (al + 10)]))) < 0.1 &&
	          std::fabs(db(std::fabs(l[(size_t) (al + 10)]) / 5.0)) < 0.1,
	      "at Feedback 0 the two echoes are %.3f and %.3f V (want 5 and 5)", l[(size_t) (al + 10)], r[(size_t) (ar + 10)]);
	float later = 0.f, rEarly = 0.f;
	for (int i = 9700; i < 30000; i++)
		later = std::max(later, std::max(std::fabs(l[(size_t) i]), i > 9800 ? std::fabs(r[(size_t) i]) : 0.f));
	for (int i = 0; i < 9000; i++)
		rEarly = std::max(rEarly, std::fabs(r[(size_t) i]));
	CHECK(later < 0.01f && rEarly < 0.01f, "at Feedback 0 nothing else sounds (%.4f V later, %.4f V early on the right)", later,
	      rEarly);
	// With Feedback the pairs fall by it: left at 3 x the time.
	Rig h;
	h.time(0.1f);
	h.f.pingPong = true;
	h.feedback(0.5f);
	std::vector<float> l2(30000), r2(30000);
	for (int i = 0; i < 30000; i++) {
		h.step(i == 10 ? 5.f : 0.f);
		l2[i] = h.r.l;
		r2[i] = h.r.r;
	}
	const float third = std::fabs(l2[(size_t) peakAt(l2, 14000, 15000)]), fourth = std::fabs(r2[(size_t) peakAt(r2, 18800, 19800)]);
	CHECK(std::fabs(third / 5.f - 0.5f) < 0.03f && std::fabs(fourth / 5.f - 0.5f) < 0.03f,
	      "the second pair is %.3f and %.3f V (want 2.5 and 2.5)", third, fourth);
	// Off, a stereo source's left never reaches the right.
	Rig s;
	s.time(0.05f);
	s.feedback(0.7f);
	s.f.inRPatched = true;
	float right = 0.f, left = 0.f;
	for (int i = 0; i < 48000; i++) {
		s.step(i < 2000 ? 5.f * (float) std::sin(TWO_PI * 300.0 * i / 48000.0) : 0.f, 0.f);
		right = std::max(right, std::fabs(s.r.r));
		left = std::max(left, std::fabs(s.r.l));
	}
	CHECK(right == 0.f && left > 1.f, "with Ping Pong off the left stays left (right %.6f V, left %.3f V)", right, left);
}

// Every switch and move on a sine: no step beyond the signal's own.
void no_clicks() {
	for (int style = 0; style < 3; style++) {
		Rig g;
		g.f.style = style;
		g.f.params[F::MIX] = 0.5f;
		g.time(0.25f);
		g.feedback(0.6f);
		g.f.params[F::TIME_ATT] = 1.f;
		long n = 0;
		float prevL = 0.f, prevR = 0.f, jump = 0.f, peak = 0.f;
		int worstAt = -1;
		const int events = 16;
		for (int ev = 0; ev <= events; ev++) {
			switch (ev) {
				case 1: g.time(0.6f); break;
				case 2: g.time(0.05f); break;
				case 3: g.f.timeCv = 2.f; break;
				case 4: g.f.timeCv = 0.f; break;
				case 5: g.f.params[F::RATIO] = 5.f; break;
				case 6: g.f.params[F::RATIO] = 1.f; break;
				case 7: g.f.style = (style + 1) % 3; break;
				case 8: g.f.style = style; break;
				case 9: g.f.freeze = true; break;
				case 10: g.f.freeze = false; break;
				case 11: g.f.pingPong = true; break;
				case 12: g.f.pingPong = false; break;
				case 13: g.f.clockPatched = true; break;
				case 14: g.f.clockPatched = false; break;
				case 15: g.f.params[F::DIFFUSE] = 0.7f; break;
				case 16: g.f.params[F::DIFFUSE] = 0.f; break;
				default: break;
			}
			for (int i = 0; i < 48000; i++) {
				g.f.clock = (n % 9000) < 200 ? 10.f : 0.f;
				g.step(2.f * (float) std::sin(TWO_PI * 50.0 * n / 48000.0));
				n++;
				const float j = std::max(std::fabs(g.r.l - prevL), std::fabs(g.r.r - prevR));
				if (j > jump) {
					jump = j;
					worstAt = ev;
				}
				peak = std::max(peak, std::max(std::fabs(g.r.l), std::fabs(g.r.r)));
				prevL = g.r.l;
				prevR = g.r.r;
			}
		}
		// A pitch bend of two octaves raises the slope fourfold.
		const float allowed = 6.f * peak * (float) (TWO_PI * 50.0 / 48000.0);
		std::printf("  style %d: largest step %.4f V (event %d), peak %.2f V, allowed %.4f\n", style, jump, worstAt, peak, allowed);
		CHECK(jump <= allowed, "style %d steps %.4f V a sample at event %d (allowed %.4f)", style, jump, worstAt, allowed);
	}
}

// A loud signal and a change of style: Analog saturates it and Digital does
// not, so an instant switch would step.
void style_change_ramps() {
	Rig g;
	g.f.style = 1;
	g.time(0.05f);
	long n = 0;
	float prev = 0.f, jump = 0.f, peak = 0.f;
	for (int i = 0; i < 48000 * 4; i++) {
		if (i == 48000 + 123)
			g.f.style = 2;
		if (i == 96000 + 77)
			g.f.style = 0;
		if (i == 144000 + 31)
			g.f.style = 1;
		g.step(9.f * (float) std::sin(TWO_PI * 50.0 * n++ / 48000.0));
		if (i > 4800)
			jump = std::max(jump, std::fabs(g.r.l - prev));
		peak = std::max(peak, std::fabs(g.r.l));
		prev = g.r.l;
	}
	const float allowed = 3.f * peak * (float) (TWO_PI * 50.0 / 48000.0);
	CHECK(jump <= allowed, "changing style on a loud signal steps %.4f V a sample (allowed %.4f)", jump, allowed);
}

void stereo() {
	// L alone and R alone are the same source.
	Rig a, b;
	b.f.inLPatched = false;
	b.f.inRPatched = true;
	for (Rig* g : {&a, &b}) {
		g->feedback(0.5f);
		g->f.params[F::MIX] = 0.5f;
		g->f.params[F::DRIFT] = 0.5f;
	}
	rka::hadal::Random rng;
	rng.seed(4);
	bool same = true;
	double sumL = 0, sumR = 0, sumM = 0;
	for (int i = 0; i < 96000; i++) {
		const float x = 3.f * rng.uniform();
		a.step(x, 0.f);
		b.step(0.f, x);
		same = same && a.r.l == b.r.l && a.r.r == b.r.r;
		sumL += (double) a.r.l * a.r.l;
		sumR += (double) a.r.r * a.r.r;
		sumM += 0.25 * ((double) a.r.l + a.r.r) * ((double) a.r.l + a.r.r);
	}
	CHECK(same, "one cable in L and one cable in R must sound the same");
	CHECK(sumM > 0.6 * 0.5 * (sumL + sumR), "the two sides must not cancel in mono (%.2f of a side's power)", sumM / (0.5 * (sumL + sumR)));
	// With R unpatched, L is the average of the two sides.
	Rig c, d;
	d.f.outRPatched = false;
	for (Rig* g : {&c, &d}) {
		g->feedback(0.5f);
		g->f.params[F::RATIO] = 5.f;
		g->f.params[F::MIX] = 0.5f;
	}
	double worst = 0;
	for (int i = 0; i < 48000; i++) {
		const float x = 2.f * rng.uniform();
		c.step(x);
		d.step(x);
		worst = std::max(worst, (double) std::fabs(d.r.l - 0.5f * (c.r.l + c.r.r)));
	}
	CHECK(worst < 1e-5, "with R unpatched L is the average of the sides (off by %g)", worst);
}

void mix_law() {
	Rig g;
	g.f.params[F::MIX] = 0.f;
	g.feedback(0.8f);
	rka::hadal::Random rng;
	rng.seed(6);
	bool dry = true;
	for (int i = 0; i < 48000; i++) {
		const float x = 9.f * rng.uniform();
		g.step(x);
		dry = dry && g.r.l == (std::fabs(x) <= 8.f ? x : g.r.l) && g.r.r == g.r.l;
	}
	CHECK(dry, "Mix 0 is the input exactly");
	Rig h;
	h.time(0.1f);
	h.f.params[F::MIX] = 0.5f;
	std::vector<float> l(6000);
	for (int i = 0; i < 6000; i++) {
		h.step(i == 10 ? 4.f : 0.f);
		l[i] = h.r.l;
	}
	CHECK(std::fabs(l[10] / 4.f - 0.70711f) < 1e-3f && std::fabs(l[4810] / 4.f - 0.70711f) < 2e-3f,
	      "Mix at half: dry %.4f, wet %.4f of the input (want 0.7071 each)", l[10] / 4.f, l[4810] / 4.f);
	// Mix's CV.
	Rig k;
	k.f.params[F::MIX] = 0.f;
	k.f.mixCv = 10.f;
	float direct = 0.f;
	for (int i = 0; i < 200; i++) {
		k.step(i == 100 ? 4.f : 0.f);
		direct = std::max(direct, std::fabs(k.r.l));
	}
	CHECK(direct < 1e-4f, "10 V on Mix's CV is fully wet (dry leaks %.5f V)", direct);
}

void limit() {
	for (int style = 0; style < 3; style++) {
		Rig g;
		g.f.style = style;
		g.f.params[F::MIX] = 0.5f;
		g.time(0.02f);
		g.f.params[F::FEEDBACK] = 1.f;
		rka::hadal::Random rng;
		rng.seed(8);
		float peak = 0.f;
		bool finite = true;
		for (int i = 0; i < 48000 * 60; i++) {
			g.step(10.f * rng.uniform());
			finite = finite && g.r.l == g.r.l && g.r.r == g.r.r;
			peak = std::max(peak, std::max(std::fabs(g.r.l), std::fabs(g.r.r)));
		}
		CHECK(finite && peak <= 10.f && peak > 8.f, "style %d at 110 %% with +-10 V noise peaks at %.3f V (want over 8, at most 10)",
		      style, peak);
	}
}

} // namespace

int main() {
	echo_time();
	echo_time_diffused();
	ratio_times();
	clocked();
	feedback_6db();
	time_step_per_style();
	drift();
	freeze();
	ping_pong();
	no_clicks();
	style_change_ramps();
	stereo();
	mix_law();
	limit();
	return rkatest::finish("wake test_engine");
}
