// Beacon's window geometry (src/beacon/EnvelopeView.hpp): stage widths from
// the weights, the drawn curve, and where the dot sits.
#include "beacon/Curve.hpp"
#include "beacon/EnvelopeView.hpp"
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

} // namespace

int main() {
	widths();
	heights();
	dots();
	live_stage_curve();
	live_dot_on_line();
	sustain_zero();
	hold_ring_time();
	return rkatest::finish("beacon/test_view");
}
