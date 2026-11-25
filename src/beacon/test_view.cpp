// Beacon's window geometry (src/beacon/EnvelopeView.hpp): stage widths from
// the weights, the drawn curve, and where the dot sits.
#include "beacon/Curve.hpp"
#include "beacon/EnvelopeView.hpp"
#include "beacon/Voice.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace rka::beacon;

void widths() {
	const EnvelopeLayout e = layoutEnvelope({0.001f, 0.001f, 0.5f, 0.001f, 0.75f});
	const float a = e.x[1] - e.x[0], d = e.x[2] - e.x[1], s = e.x[3] - e.x[2], r = e.x[4] - e.x[3];
	CHECK(e.x[0] == 0.f && e.x[4] == 1.f, "ends at %g and %g", e.x[0], e.x[4]);
	CHECK(std::fabs(a - d) < 1e-5f && std::fabs(a - r) < 1e-5f, "A %g D %g R %g are equal", a, d, r);
	CHECK(std::fabs(s / a - 2.5f) < 1e-4f, "Sustain is %g times A (want 2.5)", s / a);

	const EnvelopeLayout l = layoutEnvelope({60.f, 0.001f, 0.5f, 0.001f, 0.75f});
	const float la = l.x[1] - l.x[0], ld = l.x[2] - l.x[1];
	CHECK(std::fabs(la / ld - 5.78f) < 0.01f, "60 s beside 1 ms is %g times (want 5.78)", la / ld);

	const float ts[2] = {0.001f, 60.f};
	const float nan = std::nanf("");
	for (int i = 0; i < 16; i++) {
		const float a1 = ts[i & 1], d1 = ts[(i >> 1) & 1], r1 = ts[(i >> 2) & 1];
		const float sus = (i >> 3) & 1 ? 1.f : 0.f;
		const EnvelopeLayout c = layoutEnvelope({a1, d1, sus, r1, 0.75f});
		for (int k = 0; k < 4; k++)
			CHECK(c.x[k] < c.x[k + 1] && c.x[k] >= 0.f && c.x[k + 1] <= 1.f, "corner %d edge %d: %g, %g", i, k, c.x[k], c.x[k + 1]);
	}
	const EnvelopeLayout bad = layoutEnvelope({nan, -5.f, nan, 1e9f, nan});
	for (int k = 0; k < 4; k++)
		CHECK(bad.x[k] < bad.x[k + 1] && bad.x[k] >= 0.f && bad.x[k + 1] <= 1.f, "garbage edge %d: %g, %g", k, bad.x[k], bad.x[k + 1]);
	CHECK(bad.sustain01 >= 0.f && bad.sustain01 <= 1.f && bad.shape >= 0.f && bad.shape <= 1.f, "garbage level and shape clamp");
}

void heights() {
	const float shapes[3] = {0.f, 0.5f, 0.9f};
	for (float shape : shapes) {
		const EnvelopeLayout e = layoutEnvelope({0.01f, 0.3f, 0.4f, 1.f, shape});
		const float want[5] = {0.f, 1.f, 0.4f, 0.4f, 0.f};
		for (int k = 0; k < 5; k++)
			CHECK(std::fabs(e.heightAt(e.x[k]) - want[k]) < 1e-5f, "shape %g edge %d height %g (want %g)", shape, k, e.heightAt(e.x[k]), want[k]);
		for (int i = 1; i < 20; i++) {
			const float p = i / 20.f;
			const float ua = e.x[0] + p * (e.x[1] - e.x[0]);
			const float ud = e.x[1] + p * (e.x[2] - e.x[1]);
			const float us = e.x[2] + p * (e.x[3] - e.x[2]);
			const float ur = e.x[3] + p * (e.x[4] - e.x[3]);
			CHECK(std::fabs(e.heightAt(ua) - curveRise(p, shape)) < 1e-5f, "attack at %g", p);
			CHECK(std::fabs(e.heightAt(ud) - (1.f + (0.4f - 1.f) * curveFall(p, shape))) < 1e-5f, "decay at %g", p);
			CHECK(std::fabs(e.heightAt(us) - 0.4f) < 1e-5f, "sustain at %g is %g", p, e.heightAt(us));
			CHECK(std::fabs(e.heightAt(ur) - 0.4f * (1.f - curveFall(p, shape))) < 1e-5f, "release at %g", p);
		}
		CHECK(e.heightAt(-1.f) == 0.f && e.heightAt(2.f) == 0.f, "outside the window reads the ends");
	}
}

void dots() {
	const EnvelopeLayout e = layoutEnvelope({0.01f, 0.3f, 0.4f, 1.f, 0.75f});
	float u = -1.f, h = -1.f;
	e.dot(DECAY, 0.5f, 0.7f, 1.f, 0.4f, u, h);
	CHECK(std::fabs(u - 0.5f * (e.x[1] + e.x[2])) < 1e-6f, "decay dot at %g, %g", u, h);
	e.dot(ATTACK, 0.25f, 0.1f, 0.f, 1.f, u, h);
	CHECK(std::fabs(u - (e.x[0] + 0.25f * (e.x[1] - e.x[0]))) < 1e-6f, "attack dot at %g, %g", u, h);
	e.dot(RELEASE, 0.5f, 0.2f, 0.4f, 0.f, u, h);
	CHECK(std::fabs(u - 0.5f * (e.x[3] + e.x[4])) < 1e-6f, "release dot at %g, %g", u, h);
	e.dot(SUSTAIN, 0.f, 0.4f, 1.f, 0.4f, u, h);
	CHECK(std::fabs(u - 0.5f * (e.x[2] + e.x[3])) < 1e-6f && h == 0.4f, "sustain dot at %g, %g", u, h);
	e.dot(IDLE, 0.7f, 0.9f, 0.3f, 0.6f, u, h);
	CHECK(u == 0.f && h == 0.f, "idle dot at %g, %g", u, h);
	e.dot(DECAY, std::nanf(""), 0.7f, 1.f, 0.4f, u, h);
	CHECK(u >= e.x[1] && u <= e.x[2] && h >= 0.f && h <= 1.f, "NaN phase stays in the stage: %g, %g", u, h);
	e.dot(DECAY, 3.f, 7.f, 1.f, 0.4f, u, h);
	CHECK(u == e.x[2] && h == 0.4f, "phase clamps to the stage's end: %g, %g", u, h);
	e.dot(RELEASE, 0.5f, 0.5f, std::nanf(""), 9.f, u, h);
	CHECK(h >= 0.f && h <= 1.f, "garbage start and goal clamp: %g", h);
}

// The running stage drawn from its true start: start01 at p = 0, goal01 at
// p = 1, the voice's curve between (Curve.hpp, the independent path).
void live_stage_curve() {
	const float shapes[3] = {0.f, 0.5f, 1.f};
	const float ends[2][2] = {{0.4f, 1.f}, {0.97f, 0.f}};   // a rise from 4 V, a fall from 9.7 V
	for (float shape : shapes) {
		for (int k = 0; k < 2; k++) {
			const float s0 = ends[k][0], g0 = ends[k][1];
			const float h0 = liveStageHeight(0.f, s0, g0, shape), h1 = liveStageHeight(1.f, s0, g0, shape);
			CHECK(h0 == s0 && h1 == g0, "shape %g %s: ends %g, %g (want %g, %g)", shape, k ? "fall" : "rise", h0, h1, s0, g0);
			long bad = 0;
			for (int i = 1; i < 40; i++) {
				const float p = i / 40.f;
				const float c = g0 > s0 ? curveRise(p, shape) : curveFall(p, shape);
				const float want = s0 + (g0 - s0) * c;
				bad += std::fabs(liveStageHeight(p, s0, g0, shape) - want) > 1e-6f;
			}
			CHECK(bad == 0, "shape %g %s: %ld of 39 points off the curve", shape, k ? "fall" : "rise", bad);
		}
	}
	// Garbage clamps to the window.
	const float nan = std::nanf("");
	const float g = liveStageHeight(nan, 2.f, -1.f, nan);
	CHECK(g >= 0.f && g <= 1.f, "garbage reads inside the window: %g", g);
}

// The dot for a running stage lies on that curve exactly, whatever level
// was published beside it (a torn read costs the dot nothing).
void live_dot_on_line() {
	const EnvelopeLayout e = layoutEnvelope({0.01f, 0.3f, 0.4f, 1.f, 0.75f});
	const Stage stages[3] = {ATTACK, DECAY, RELEASE};
	const float ends[3][2] = {{0.4f, 1.f}, {1.f, 0.4f}, {0.97f, 0.f}};
	for (int k = 0; k < 3; k++) {
		const float a = e.x[k == 2 ? 3 : k], b = e.x[k == 2 ? 4 : k + 1];
		for (int i = 0; i <= 10; i++) {
			const float p = i / 10.f;
			float u = -1.f, h = -1.f;
			e.dot(stages[k], p, 0.123f, ends[k][0], ends[k][1], u, h);
			const float want = liveStageHeight(p, ends[k][0], ends[k][1], e.shape);
			CHECK(h == want, "stage %d p %g: dot height %g, line %g", int(stages[k]), p, h, want);
			CHECK(std::fabs(u - (a + p * (b - a))) < 1e-6f, "stage %d p %g: dot at u %g", int(stages[k]), p, u);
		}
	}
}

// Each running stage is drawn over its own span, whatever ran before it
// (revised 2026-11-25: a Release that cut Attack short no longer sweeps
// back across the A, D and S spans; it runs over the R span from the
// level it started at).
void live_span_own() {
	const EnvelopeLayout e = layoutEnvelope({0.01f, 0.3f, 0.4f, 1.f, 0.75f});
	for (int s = ATTACK; s <= RELEASE; s++) {
		float u0 = -1.f, u1 = -1.f;
		e.liveSpan(Stage(s), u0, u1);
		CHECK(u0 == e.x[s - 1] && u1 == e.x[s], "stage %d: span %g..%g (want %g..%g)", s, u0, u1, e.x[s - 1], e.x[s]);
	}
	// A Release from 9.7 V (Attack cut short): from the R span's start at
	// 9.7 V to its end at 0 V.
	float u, h;
	e.livePoint(RELEASE, 0.f, 0.97f, 0.f, u, h);
	CHECK(u == e.x[3] && h == 0.97f, "Release from 9.7 V starts at the R span at that level: %g, %g", u, h);
	e.livePoint(RELEASE, 1.f, 0.97f, 0.f, u, h);
	CHECK(u == 1.f && h == 0.f, "and ends at the right edge at 0: %g, %g", u, h);
}

// AR mode draws only Attack and Release, as the voice runs there: no D or
// S span, Release from the top; A and R keep their ADSR weights' ratio.
void ar_layout() {
	const ViewParams p{0.05f, 0.3f, 0.4f, 1.f, 0.75f};
	const EnvelopeLayout adsr = layoutEnvelope(p), ar = layoutEnvelope(p, true);
	CHECK(ar.ar && !adsr.ar, "the AR flag");
	CHECK(ar.x[0] == 0.f && ar.x[1] == ar.x[2] && ar.x[2] == ar.x[3] && ar.x[4] == 1.f, "AR: no D or S span (%g %g %g %g %g)", ar.x[0],
	      ar.x[1], ar.x[2], ar.x[3], ar.x[4]);
	const float rAdsr = (adsr.x[1] - adsr.x[0]) / (adsr.x[4] - adsr.x[3]), rAr = (ar.x[1] - ar.x[0]) / (ar.x[4] - ar.x[3]);
	CHECK(std::fabs(rAdsr - rAr) < 1e-5f, "A to R width ratio kept: %g (ADSR %g)", rAr, rAdsr);
	CHECK(ar.heightAt(ar.x[1] - 1e-6f) > 0.999f && std::fabs(ar.heightAt(ar.x[3] + 1e-6f) - 1.f) < 1e-3f && ar.heightAt(1.f) == 0.f,
	      "AR: up to the top, Release from the top to 0 (%g, %g, %g)", ar.heightAt(ar.x[1] - 1e-6f), ar.heightAt(ar.x[3] + 1e-6f),
	      ar.heightAt(1.f));
	// An AR envelope's dot rides that outline.
	long off = 0;
	for (int i = 0; i <= 20; i++) {
		const float ph = i / 20.f;
		float u, h;
		ar.dot(ATTACK, ph, 0.f, 0.f, 1.f, u, h);
		off += std::fabs(ar.heightAt(u < ar.x[1] ? u : ar.x[1] - 1e-6f) - h) > 1e-4f;
		ar.dot(RELEASE, ph, 0.f, 1.f, 0.f, u, h);
		off += std::fabs(ar.heightAt(u > ar.x[3] ? u : ar.x[3] + 1e-6f) - h) > 1e-3f;
	}
	CHECK(off == 0, "AR's dot leaves its outline at %ld points", off);
}

// After a Shape change mid-stage (#83) the bright line runs from the
// voice's breakpoint: a real Voice, 10 s Attack at Shape 0, Shape to 1 at
// phase 0.3. The dot, from the voice's phase and breakpoint, is at the
// voice's level and still moves across the span with the phase; the line
// starts at the breakpoint and ends at the span's end at 10 V. Drawn from
// the stage's start instead (the old way), it would be volts off.
void live_breakpoint() {
	Voice v;
	VoiceIn in = {10.f, 0.3f, 1.f, 10.f, 0.f, true, false, GATE_MODE};
	VoiceOut o;
	while (v.phase() < 0.3)
		v.process(in, o);
	in.shape = 1.f;
	v.process(in, o);
	const float b = float(v.bpPhase()), bp01 = float(v.bpLevel()) * 0.1f;
	CHECK(v.stage() == ATTACK && b > 0.29f && b < 0.31f && bp01 > 0.f && bp01 < 0.01f,
	      "a breakpoint at phase %g, %g of the height (%d)", b, bp01, int(v.stage()));
	const EnvelopeLayout e = layoutEnvelope({10.f, 0.3f, 1.f, 1.f, 1.f});
	float u, h;
	e.livePoint(ATTACK, 0.f, bp01, 1.f, u, h, b);
	CHECK(std::fabs(u - b * e.x[1]) < 1e-6f && h == bp01, "the line starts at the breakpoint: %g, %g (want %g, %g)", u,
	      h, b * e.x[1], bp01);
	e.livePoint(ATTACK, 1.f, bp01, 1.f, u, h, b);
	CHECK(u == e.x[1] && h == 1.f, "and ends at the span's end at 10 V: %g, %g", u, h);
	float worstH = 0.f, worstU = 0.f, oldOff = 0.f;
	long n = 0;
	while (v.stage() == ATTACK) {
		if (++n % 4800 == 0) {
			const float p = float(v.phase()), level01 = v.level() * 0.1f;
			e.dot(ATTACK, p, 0.5f, bp01, 1.f, u, h, b);
			worstH = std::fmax(worstH, std::fabs(h - level01));
			worstU = std::fmax(worstU, std::fabs(u - p * e.x[1]));
			e.dot(ATTACK, p, 0.5f, float(v.start()) * 0.1f, 1.f, u, h);
			oldOff = std::fmax(oldOff, std::fabs(h - level01));
		}
		v.process(in, o);
	}
	CHECK(n > 300000 && worstH < 1e-5f, "the dot is at the voice's level within %g of the height (%ld samples)", worstH, n);
	CHECK(worstU < 1e-6f, "and across the span by phase within %g", worstU);
	CHECK(oldOff > 0.3f, "drawn from the stage's start it would be %g of the height off", oldOff);
	// A NaN breakpoint phase reads 0: the whole span, as before.
	float u2, h2;
	e.livePoint(ATTACK, 0.5f, 0.f, 1.f, u, h);
	e.livePoint(ATTACK, 0.5f, 0.f, 1.f, u2, h2, std::nanf(""));
	CHECK(u == u2 && h == h2, "NaN bpPhase is 0: %g, %g (want %g, %g)", u2, h2, u, h);
}

// Sustain at 0: the dot at the end of the line; above 0, mid-span at the level.
void sustain_zero() {
	const EnvelopeLayout z = layoutEnvelope({0.01f, 0.3f, 0.f, 1.f, 0.75f});
	float u = -1.f, h = -1.f;
	z.dot(SUSTAIN, 0.f, 0.f, 1.f, 0.f, u, h);
	CHECK(u == 1.f && h == 0.f, "sustain 0: dot at %g, %g (want 1, 0)", u, h);
	const EnvelopeLayout e = layoutEnvelope({0.01f, 0.3f, 0.3f, 1.f, 0.75f});
	e.dot(SUSTAIN, 0.f, 0.3f, 1.f, 0.3f, u, h);
	CHECK(std::fabs(u - 0.5f * (e.x[2] + e.x[3])) < 1e-6f && h == 0.3f, "sustain 0.3: dot at %g, %g (mid-span)", u, h);
}

// The Hold line scrolls by time, not frames: 3 s at 60 Hz and at 144 Hz
// (and at a jittery 30-50 Hz) write the same number of points, and the
// lines match within a frame's change of the level (a 1 V/s ramp).
void hold_ring_time() {
	const double T = 3.0;
	auto run = [&](HoldRing& r, double fps, bool jitter) {
		double t = 0.0;
		int k = 0;
		while (t <= T + 1e-9) {
			r.sample(float(t), t);
			const double dt = jitter ? 1.0 / (30.0 + 20.0 * ((k * 7919) % 13) / 12.0) : 1.0 / fps;
			t += dt;
			k++;
		}
	};
	HoldRing a, b, c;
	run(a, 60.0, false);
	run(b, 144.0, false);
	run(c, 0.0, true);
	const long want = long(T * HoldRing::POINTS_PER_SECOND);
	CHECK(std::labs(a.pushed - want) <= 1 && std::labs(b.pushed - want) <= 1 && std::labs(c.pushed - want) <= 1,
	      "3 s writes %ld points at 60 Hz, %ld at 144 Hz, %ld jittery (want %ld)", a.pushed, b.pushed, c.pushed, want);
	float worst = 0.f;
	// The same point at both rates holds levels under a 60 Hz frame apart
	// (the newest point follows the level to the last frame of its slot).
	for (int i = 0; i < HoldRing::RING; i++)
		worst = std::fmax(worst, std::fabs(a.at(i) - b.at(i)));
	CHECK(a.pushed == b.pushed && worst <= 1.f / 60.f + 1e-4f, "60 Hz and 144 Hz lines differ by %g V at most (a 1 V/s ramp; %ld and %ld points)", worst, a.pushed, b.pushed);
	CHECK(std::fabs(b.at(HoldRing::RING - 1) - 3.f) < 0.01f, "the newest point follows the level (%g)", b.at(HoldRing::RING - 1));
	CHECK(std::fabs(a.at(0) - b.at(0)) < 0.05f && a.at(0) > 0.2f, "the same span of time is shown (oldest %g and %g V)", a.at(0), b.at(0));

	// A start fills the line flat; a stall writes one ring at most; NaN and
	// a backward clock add nothing; NaN volts read 0, the rest clamp.
	HoldRing r;
	r.sample(4.f, 10.0);
	bool flat = true;
	for (int i = 0; i < HoldRing::RING; i++)
		flat &= r.at(i) == 4.f;
	CHECK(flat && r.pushed == 0, "a start fills the line at the level");
	r.sample(5.f, 1000.0);
	CHECK(r.pushed == HoldRing::RING, "a 990 s stall writes one ring (%ld)", r.pushed);
	r.sample(6.f, std::nan(""));
	r.sample(6.f, 900.0);
	CHECK(r.pushed == HoldRing::RING, "a NaN or backward clock writes nothing (%ld)", r.pushed);
	{
		// Between points (a 144 Hz frame is under a point) the newest point
		// still shows the level now.
		const long before = r.pushed;
		r.sample(7.5f, 900.001);   // the clock is still at 1000 s: nothing
		r.sample(-7.5f, 1000.005);
		CHECK(r.pushed == before && r.at(HoldRing::RING - 1) == -7.5f && r.at(HoldRing::RING - 2) == 5.f,
		      "between points the newest follows the level (%g, %ld points)", r.at(HoldRing::RING - 1), r.pushed - before);
	}
	r.sample(std::nanf(""), 1000.1);
	CHECK(r.at(HoldRing::RING - 1) == 0.f, "NaN volts read 0");
	r.sample(50.f, 1000.2);
	CHECK(r.at(HoldRing::RING - 1) == 10.f, "volts clamp to 10");
	r.stop();
	r.sample(-2.f, 1001.0);
	CHECK(r.at(0) == -2.f && r.at(HoldRing::RING - 1) == -2.f && r.pushed == 0, "a restart after stop() is flat again");
}

// The window lines up with the output. Beacon's own voice runs through
// the patches people make: both modes; a one-sample trigger, a gate
// shorter than Attack, one ending in Decay, one held, a square faster
// than Attack, triggers faster than Attack; a RETRIG in Sustain; at four
// Shapes. Every sample the window's dot is taken from what the voice
// publishes, as the module does. It must sit at the output's level, and
// inside its own stage's span (a Release that cut Attack short runs over
// the R span). And the bright line drawn at any moment must be what the
// output then does: every 97 samples the line is kept, and while that
// stage runs on with Shape unchanged, the output must stay on it.
void window_lines_up() {
	const float fs = 48000.f, A = 0.04f, D = 0.06f, R = 0.08f;
	const long nA = 1920;
	struct Gate { const char* name; long period, width; } gates[] = {
		{"a 1-sample trigger", 0, 1},       {"a gate shorter than Attack", 0, nA / 2},
		{"a gate ending in Decay", 0, nA + 1400}, {"a held gate", 0, 20000},
		{"a square faster than Attack", 700, 350}, {"triggers faster than Attack", 480, 24}};
	long offLevel = 0, offSpan = 0, offLine = 0, checked = 0;
	for (Mode mode : {GATE_MODE, AR_MODE})
		for (float shape : {0.f, 0.5f, 0.75f, 1.f})
			for (float sus : {5.f, 0.f})
				for (const Gate& g : gates) {
					Voice v;
					v.setSampleRate(fs);
					v.reset();
					const EnvelopeLayout e = layoutEnvelope({A, D, sus * 0.1f, R, shape}, mode == AR_MODE);
					// The kept line: its stage instance and what it was drawn from.
					long instance = 0, keptInstance = -1;
					Stage lastStage = IDLE;
					double lastPhase = 0.0;
					float kStart = 0.f, kGoal = 0.f, kBp = 0.f;
					Stage kStage = IDLE;
					for (long i = 0; i < 30000; i++) {
						VoiceIn in{A, D, R, sus, shape, false, false, mode};
						in.gate = g.period ? (i % g.period) < g.width : i < g.width;
						in.retrig = g.width == 20000 && i == 12000;   // a RETRIG in Sustain
						VoiceOut o;
						v.process(in, o);
						const Stage st = v.stage();
						if (st != lastStage || v.phase() < lastPhase)
							instance++;
						lastStage = st;
						lastPhase = v.phase();
						const float level01 = o.level * 0.1f, start01 = float(v.bpLevel()) * 0.1f, goal01 = float(v.goal()) * 0.1f;
						float u, h;
						e.dot(st, float(v.phase()), level01, start01, goal01, u, h, float(v.bpPhase()));
						if (st == IDLE)
							continue;
						checked++;
						if (std::fabs(h - level01) > 2e-4f) {
							if (offLevel++ < 3)
								std::printf("  mode %d shape %g S %g, %s, sample %ld (%d): dot at %g, OUT %g\n", int(mode), shape, sus, g.name, i, int(st), h * 10.f, o.level);
						}
						const float lo = st == SUSTAIN ? e.x[2] : e.x[st - 1], hi = st == SUSTAIN ? e.x[3] : e.x[st];
						if (st != SUSTAIN && (u < lo - 1e-5f || u > hi + 1e-5f)) {
							if (offSpan++ < 3)
								std::printf("  mode %d shape %g S %g, %s, sample %ld: stage %d's dot at u %g, outside its span %g..%g\n", int(mode), shape, sus, g.name, i, int(st), u, lo, hi);
						}
						if (st == ATTACK || st == DECAY || st == RELEASE) {
							if (instance == keptInstance && kStage == st && kBp == float(v.bpPhase())) {
								const float f = kBp < 1.f ? float((v.phase() - kBp) / (1.0 - kBp)) : 1.f;
								const float want = liveStageHeight(f, kStart, kGoal, shape);
								if (std::fabs(want - level01) > 2e-4f && offLine++ < 3)
									std::printf("  mode %d shape %g S %g, %s, sample %ld: the line drawn earlier says %g, OUT %g\n", int(mode), shape, sus, g.name, i, want * 10.f, o.level);
							}
							if (i % 97 == 0) {
								keptInstance = instance;
								kStage = st;
								kStart = start01;
								kGoal = goal01;
								kBp = float(v.bpPhase());
							}
						}
					}
				}
	CHECK(checked > 100000, "the window was checked on %ld samples", checked);
	CHECK(offLevel == 0, "the dot left the output's level on %ld samples", offLevel);
	CHECK(offSpan == 0, "the dot left its stage's span on %ld samples", offSpan);
	CHECK(offLine == 0, "the output left the line drawn for it on %ld samples", offLine);
}

} // namespace

int main() {
	window_lines_up();
	widths();
	heights();
	dots();
	live_stage_curve();
	live_dot_on_line();
	live_span_own();
	ar_layout();
	live_breakpoint();
	sustain_zero();
	hold_ring_time();
	return rkatest::finish("beacon/test_view");
}
