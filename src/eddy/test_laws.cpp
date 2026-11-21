// Eddy's control laws (src/eddy/Laws.hpp) against the numbers in the plan's
// Global Constraints. Built with the plugin's floating-point flags.
#include "eddy/Laws.hpp"
#include "testing.hpp"

#include <cmath>

namespace {

using namespace rka::eddy;

const float NAN_F = std::nanf("");
const float INF_F = INFINITY;

void speed_flats() {
	const float flats[3] = {-1.f, 0.f, 1.f};
	for (float c : flats) {
		for (float d : {-0.039f, -0.02f, 0.f, 0.02f, 0.03f, 0.039f})
			CHECK(speedKnob(c + d) == c, "knob %g gives %g (want exactly %g)", c + d, speedKnob(c + d), c);
	}
	CHECK(speedKnob(0.97f) == 1.f && speedKnob(1.f) == 1.f && speedKnob(1.03f) == 1.f, "the +1 flat");
	CHECK(speedKnob(2.f) == 2.f && speedKnob(-2.f) == -2.f, "the ends are +-2 (%g, %g)", speedKnob(2.f), speedKnob(-2.f));
	CHECK(speedKnob(9.f) == 2.f && speedKnob(-9.f) == -2.f && speedKnob(INF_F) == 2.f && speedKnob(-INF_F) == -2.f,
	      "the knob clamps");
	CHECK(speedKnob(NAN_F) == 1.f, "a NaN knob reads the default");
	// Monotonic everywhere, and no step bigger than the steepest slope allows.
	float prev = speedKnob(-2.f);
	bool mono = true, smooth = true;
	for (int i = 1; i <= 40000; i++) {
		const float k = -2.f + 1e-4f * (float) i;
		const float v = speedKnob(k);
		mono = mono && v >= prev;
		smooth = smooth && v - prev <= 1.1f * 1e-4f + 1e-6f;
		prev = v;
	}
	CHECK(mono, "the law is monotonic");
	CHECK(smooth, "the law has no jumps");
	// Continuous to 1e-4 at the edge of each flat.
	for (float c : flats)
		for (float sgn : {-1.f, 1.f}) {
			const float edge = c + sgn * 0.04f;
			CHECK(std::fabs(speedKnob(edge + sgn * 1e-5f) - c) < 1e-4f, "edge of the flat at %g: %g", c, speedKnob(edge + sgn * 1e-5f));
		}
	// Between the flats, straight lines: halfway from 0.04 to 0.96 is 0.5.
	CHECK(std::fabs(speedKnob(0.5f) - 0.5f) < 1e-6f && std::fabs(speedKnob(-0.5f) + 0.5f) < 1e-6f, "0.5 reads %g", speedKnob(0.5f));
	CHECK(std::fabs(speedKnob(1.52f) - 1.5f) < 1e-6f, "1.52 reads %g (want 1.5)", speedKnob(1.52f));
}

void speed_cv() {
	CHECK(std::fabs(speedTarget(1.f, 5.f, 1.f) - 3.f) < 1e-6f, "+5 V adds 2 (%g)", speedTarget(1.f, 5.f, 1.f));
	CHECK(std::fabs(speedTarget(1.f, -5.f, 1.f) + 1.f) < 1e-6f, "-5 V adds -2 (%g)", speedTarget(1.f, -5.f, 1.f));
	CHECK(std::fabs(speedTarget(0.f, 5.f, -1.f) + 2.f) < 1e-6f, "the attenuverter inverts (%g)", speedTarget(0.f, 5.f, -1.f));
	CHECK(std::fabs(speedTarget(0.f, 5.f, 0.5f) - 1.f) < 1e-6f, "half attenuation (%g)", speedTarget(0.f, 5.f, 0.5f));
	CHECK(speedTarget(1.f, 5.f, 0.f) == 1.f, "attenuverter 0 is no CV");
	CHECK(speedTarget(1.f, 5.f, 7.f) == speedTarget(1.f, 5.f, 1.f), "the attenuverter clamps");
	const float junk[3] = {NAN_F, INF_F, -INF_F};
	bool ok = true;
	for (float a : junk)
		for (float b : junk)
			for (float c : junk) {
				const float v = speedTarget(a, b, c);
				ok = ok && std::isfinite(v) && std::fabs(v) < 100.f;
			}
	CHECK(ok, "NaN and infinities give finite results");
	CHECK(speedTarget(1.f, NAN_F, 1.f) == 1.f, "NaN volts read 0");
}

void voct() {
	CHECK(std::fabs(speedFinal(1.f, 1.f) - 2.f) < 1e-6f, "+1 V doubles (%g)", speedFinal(1.f, 1.f));
	CHECK(std::fabs(speedFinal(1.f, -1.f) - 0.5f) < 1e-6f, "-1 V halves (%g)", speedFinal(1.f, -1.f));
	CHECK(speedFinal(1.f, 0.f) == 1.f && speedFinal(-0.7f, 0.f) == -0.7f, "0 V leaves it");
	CHECK(speedFinal(2.f, 5.f) == 4.f && speedFinal(-2.f, 5.f) == -4.f, "held to +-4 (%g, %g)", speedFinal(2.f, 5.f), speedFinal(-2.f, 5.f));
	CHECK(speedFinal(1.f, NAN_F) == 1.f, "NaN volts read 0");
	const float junk[6] = {NAN_F, INF_F, -INF_F, 1e30f, -1e30f, 0.f};
	bool ok = true;
	for (float g : junk)
		for (float v : junk) {
			const float s = speedFinal(g, v);
			ok = ok && std::isfinite(s) && std::fabs(s) <= 4.f;
		}
	CHECK(ok, "never beyond +-4, always finite");
}

void gains() {
	CHECK(outputGain(0.f) == 0.f && outputGain(-0.f) == 0.f, "stopped is silent");
	CHECK(std::fabs(outputGain(1.f / 128.f) - 0.5f) < 1e-6f && std::fabs(outputGain(-1.f / 128.f) - 0.5f) < 1e-6f, "half way at 1/128");
	CHECK(outputGain(1.f / 64.f) == 1.f && outputGain(1.f) == 1.f && outputGain(-4.f) == 1.f, "full from 1/64");
	CHECK(outputGain(NAN_F) == 0.f && outputGain(INF_F) == 1.f, "NaN is silent, infinite is full");
	CHECK(writeGain(0.f) == 0.f && writeGain(1.f / 16.f) == 0.f && writeGain(0.05f) == 0.f, "no writing at or under 1/16");
	CHECK(std::fabs(writeGain(3.f / 32.f) - 0.5f) < 1e-6f && std::fabs(writeGain(-3.f / 32.f) - 0.5f) < 1e-6f, "half way at 3/32");
	CHECK(writeGain(1.f / 8.f) == 1.f && writeGain(1.f) == 1.f && writeGain(-4.f) == 1.f, "full from 1/8");
	CHECK(writeGain(NAN_F) == 0.f && writeGain(INF_F) == 1.f, "NaN writes nothing");
}

const double TEN_S = 480000.0;   // 10 s at 48 kHz

void window_table() {
	Window w = window(1.f, 0.f, 1.f, 0.f, TEN_S, 480.0, 48000.f);
	CHECK(w.start == TEN_S - 1.0 && w.length == TEN_S, "start knob 1: start %g length %g", w.start, w.length);
	w = window(0.f, 0.f, 0.f, 0.f, TEN_S, 0.0, 48000.f);
	CHECK(w.start == 0.0 && w.length == 480.0, "knob 0: start %g, length %g (want 0, 480)", w.start, w.length);
	w = window(0.f, 2.5f, 1.f, 0.f, TEN_S, 0.0, 48000.f);
	CHECK(w.start == TEN_S / 2, "Start CV +2.5 V gives half way (%g)", w.start);
	w = window(0.25f, 1.25f, 1.f, 0.f, TEN_S, 0.0, 48000.f);
	CHECK(std::fabs(w.start - TEN_S / 2) < 1e-6, "knob and CV add (%g)", w.start);
	w = window(0.5f, -5.f, 1.f, 0.f, TEN_S, 0.0, 48000.f);
	CHECK(w.start == 0.0, "the sum clamps at 0 (%g)", w.start);
	// The middle of Length is the geometric mean: sqrt(480 * 480000).
	w = window(0.f, 0.f, 0.5f, 0.f, TEN_S, 0.0, 48000.f);
	CHECK(std::fabs(w.length / std::sqrt(480.0 * TEN_S) - 1.0) < 1e-6, "length knob 0.5 gives %g", w.length);
	// F and the fade floor scale with the rate.
	w = window(0.f, 0.f, 0.f, 0.f, 96000.0 * 10, 0.0, 96000.f);
	CHECK(w.length == 960.0 && w.minFade == 240.0, "at 96 kHz: length %g, m %g", w.length, w.minFade);
	w = window(0.f, 0.f, 1.f, 0.f, TEN_S, 0.0, 48000.f);
	CHECK(w.minFade == 480.0, "m is F (480) on a long window (%g)", w.minFade);
}

void turn_exact() {
	const float lens[3] = {0.3f, 0.7f, 1.f};
	for (float k : lens) {
		Window w = window(0.f, 0.f, k, 0.f, TEN_S, 480.0, 48000.f);
		CHECK(w.turn == w.length, "Smear 0, tail >= m: turn %g, length %g", w.turn, w.length);
		w = window(0.f, 0.f, k, 0.f, TEN_S, 1e9, 48000.f);
		CHECK(w.turn == w.length, "a long tail is held to m (turn %g, length %g)", w.turn, w.length);
	}
}

void turn_no_tail() {
	const float lens[3] = {0.3f, 0.7f, 1.f};
	for (float k : lens) {
		const Window w = window(0.f, 0.f, k, 0.f, TEN_S, 0.0, 48000.f);
		CHECK(w.turn == w.length - w.minFade && w.minFade == 480.0, "no tail: turn %g, length %g, m %g", w.turn, w.length, w.minFade);
	}
	const Window s = window(0.f, 0.f, 0.f, 0.f, TEN_S, 0.0, 48000.f);   // L = 480, m = 120
	CHECK(s.minFade == 120.0 && s.turn == 360.0, "a short window: m %g turn %g", s.minFade, s.turn);
	const Window h = window(0.f, 0.f, 1.f, 0.f, TEN_S, 100.0, 48000.f);   // a tail of 100 of the 480
	CHECK(h.turn == h.length - 380.0, "a short tail: turn %g", h.turn);
}

void turn_smear() {
	const float lens[3] = {0.2f, 0.6f, 1.f};
	for (float k : lens) {
		const Window w = window(0.f, 0.f, k, 1.f, TEN_S, 0.0, 48000.f);
		CHECK(w.overlap == w.length / 2 && w.turn == w.length / 2, "Smear 1, no tail: overlap %g, turn %g, length %g", w.overlap, w.turn, w.length);
		const Window t = window(0.f, 0.f, k, 1.f, TEN_S, 1e9, 48000.f);
		CHECK(t.overlap == t.length / 2 && t.turn == t.length / 2 + t.minFade, "Smear 1 with tail: turn %g (want %g)", t.turn, t.length / 2 + t.minFade);
	}
	// Half Smear on a long window: o = L/4 when that is over m.
	const Window h = window(0.f, 0.f, 1.f, 0.5f, TEN_S, 0.0, 48000.f);
	CHECK(h.overlap == TEN_S / 4, "Smear 0.5: overlap %g", h.overlap);
	// A little Smear stays under m: the overlap is m.
	const Window l = window(0.f, 0.f, 1.f, 0.0005f, TEN_S, 0.0, 48000.f);
	CHECK(l.overlap == 480.0, "a little Smear leaves the overlap at m (%g)", l.overlap);
}

void window_extremes() {
	const double tape = 2400.0;   // 50 ms at 48 kHz
	const float cvs[5] = {10.f, -10.f, NAN_F, INF_F, -INF_F};
	const float knobs[4] = {0.f, 1.f, 0.4f, NAN_F};
	const double tails[4] = {0.0, 100.0, 1e9, NAN};
	bool ok = true;
	for (float cv : cvs)
		for (float sk : knobs)
			for (float lk : knobs)
				for (float sm : {0.f, 1.f, NAN_F})
					for (double tail : tails) {
						const Window w = window(sk, cv, lk, sm, tape, tail, 48000.f);
						const bool fin = std::isfinite(w.start) && std::isfinite(w.length) && std::isfinite(w.minFade) &&
						                 std::isfinite(w.overlap) && std::isfinite(w.turn);
						ok = ok && fin && w.start >= 0.0 && w.start < tape && w.length >= 480.0 && w.length <= tape && w.minFade > 0.0 &&
						     w.turn >= w.minFade && w.overlap >= w.minFade && w.overlap <= w.length / 2;
					}
	CHECK(ok, "a 50 ms tape, Length 0, Smear 1, Start CV +-10 V and NaN: inside, finite, turn >= m > 0");
	const Window w = window(0.f, 10.f, 0.f, 1.f, tape, 0.0, 48000.f);
	CHECK(w.start == tape - 1.0 && w.length == 480.0 && w.minFade == 120.0 && w.turn == 240.0,
	      "the worst case: start %g length %g m %g turn %g", w.start, w.length, w.minFade, w.turn);
	// A tape shorter than 10 ms: the window is the tape.
	const Window sh = window(0.f, 0.f, 0.f, 1.f, 100.0, 0.0, 48000.f);
	CHECK(sh.length == 100.0 && sh.minFade == 25.0 && sh.turn >= sh.minFade, "a 100-frame tape: length %g m %g turn %g", sh.length, sh.minFade, sh.turn);
	// No tape, no window.
	const Window e = window(1.f, 1.f, 1.f, 1.f, 0.0, 0.0, 48000.f);
	CHECK(e.start == 0.0 && e.length == 0.0 && e.turn == 0.0, "an empty tape gives an empty window");
	const Window nf = window(0.f, 0.f, 1.f, 0.f, TEN_S, 480.0, NAN_F);
	CHECK(std::isfinite(nf.turn) && nf.turn > 0.0, "a NaN rate still gives a window");
}

void dub_table() {
	const float d[4] = {0.f, 0.25f, 0.5f, 1.f}, add[4] = {0.f, 0.5f, 1.f, 1.f}, keep[4] = {1.f, 0.9375f, 0.75f, 0.f};
	for (int i = 0; i < 4; i++) {
		float a, k;
		dubGains(d[i], a, k);
		CHECK(a == add[i] && k == keep[i], "d %g: add %g keep %g (want %g, %g)", d[i], a, k, add[i], keep[i]);
	}
	float a, k;
	dubGains(NAN_F, a, k);
	CHECK(a == 0.f && k == 1.f, "NaN writes nothing");
	dubGains(7.f, a, k);
	CHECK(a == 1.f && k == 0.f, "d clamps at 1");
	dubGains(-1.f, a, k);
	CHECK(a == 0.f && k == 1.f, "d clamps at 0");
}

} // namespace

int main() {
	speed_flats();
	speed_cv();
	voct();
	gains();
	window_table();
	turn_exact();
	turn_no_tail();
	turn_smear();
	window_extremes();
	dub_table();
	return rkatest::finish("eddy test_laws");
}
