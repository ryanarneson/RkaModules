// Beacon's window geometry (src/beacon/EnvelopeView.hpp): stage widths from
// the weights, the drawn curve, and where the dot sits.
#include "beacon/Curve.hpp"
#include "beacon/EnvelopeView.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>

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
	e.dot(DECAY, 0.5f, 0.7f, u, h);
	CHECK(std::fabs(u - 0.5f * (e.x[1] + e.x[2])) < 1e-6f && h == 0.7f, "decay dot at %g, %g", u, h);
	e.dot(ATTACK, 0.25f, 0.1f, u, h);
	CHECK(std::fabs(u - (e.x[0] + 0.25f * (e.x[1] - e.x[0]))) < 1e-6f && h == 0.1f, "attack dot at %g, %g", u, h);
	e.dot(RELEASE, 0.5f, 0.2f, u, h);
	CHECK(std::fabs(u - 0.5f * (e.x[3] + e.x[4])) < 1e-6f && h == 0.2f, "release dot at %g, %g", u, h);
	e.dot(SUSTAIN, 0.f, 0.4f, u, h);
	CHECK(std::fabs(u - 0.5f * (e.x[2] + e.x[3])) < 1e-6f && h == 0.4f, "sustain dot at %g, %g", u, h);
	e.dot(IDLE, 0.7f, 0.9f, u, h);
	CHECK(u == 0.f && h == 0.f, "idle dot at %g, %g", u, h);
	e.dot(DECAY, std::nanf(""), 0.7f, u, h);
	CHECK(u >= e.x[1] && u <= e.x[2], "NaN phase stays in the stage: %g", u);
	e.dot(DECAY, 3.f, 7.f, u, h);
	CHECK(u == e.x[2] && h == 1.f, "phase and level clamp: %g, %g", u, h);
}

} // namespace

int main() {
	widths();
	heights();
	dots();
	return rkatest::finish("beacon/test_view");
}
