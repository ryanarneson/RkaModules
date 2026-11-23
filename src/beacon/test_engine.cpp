// Beacon's polyphonic engine (src/beacon/BeaconEngine.hpp): channels, ports,
// PUSH, Schmitt gates, mode switches, INV, the published channel-1 state,
// a rate change and Shape's smoothing, against the plan's Global
// Constraints. Each channel is compared with a lone Voice fed the same
// input by the test's own route (knob + CV through Timing, 0 / 5 V gates).
#include "beacon/BeaconEngine.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Beacon.cpp (C++11) can only assert int's lock-freedom; the window's
// float reads are asserted here, on the same targets.
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free,
              "the published channel-1 state is lock-free");

namespace {

using namespace rka::beacon;
typedef rka::BeaconFrame F;

const char* stageName(int s) {
	static const char* n[] = {"IDLE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
	return (s >= IDLE && s <= RELEASE) ? n[s] : "?";
}

struct Eng {
	rka::BeaconEngine e;
	F f;
	rka::BeaconResult r;
	float gate[16] = {}, retrig[16] = {}, acv[16] = {}, dcv[16] = {}, scv[16] = {}, rcv[16] = {};
	explicit Eng(float fs = 48000.f) {
		e.setSampleRate(fs);
		e.reset();
		f.gate.v = gate;
		f.retrig.v = retrig;
		f.attackCv.v = acv;
		f.decayCv.v = dcv;
		f.sustainCv.v = scv;
		f.releaseCv.v = rcv;
	}
	void step() { e.process(f, r); }
	void run(long n) {
		for (long i = 0; i < n; i++)
			step();
	}
	int stage() const { return e.published().stage.load(); }
};

// A port's channel c, as the engine is to read it: mono to all, poly per
// channel, 0 V past its count or unpatched.
float portV(const rka::BeaconPort& p, int c) {
	if (p.channels <= 0)
		return 0.f;
	if (p.channels == 1)
		return p.v[0];
	return c < p.channels ? p.v[c] : 0.f;
}

// The VoiceIn for channel c by the test's route (gates are 0 / 5 V here).
VoiceIn refIn(const Eng& g, int c, Mode mode) {
	const F& f = g.f;
	VoiceIn in{};
	in.attack = stageSecondsCv(f.params[F::ATTACK], portV(f.attackCv, c));
	in.decay = stageSecondsCv(f.params[F::DECAY], portV(f.decayCv, c));
	in.release = stageSecondsCv(f.params[F::RELEASE], portV(f.releaseCv, c));
	const float s = portV(f.sustainCv, c);
	if (mode == HOLD_MODE) {
		const float x = s + 10.f * f.params[F::SUSTAIN];
		in.sustain = x < -10.f ? -10.f : x > 10.f ? 10.f : x;
	} else {
		const float k = f.params[F::SUSTAIN] + s * 0.1f;
		in.sustain = 10.f * (k < 0.f ? 0.f : k > 1.f ? 1.f : k);
	}
	in.shape = f.params[F::SHAPE];
	in.gate = (f.gate.channels > 0 ? portV(f.gate, c) > 0.5f : false) || f.params[F::PUSH] > 0.5f;
	in.retrig = portV(f.retrig, c) > 0.5f;
	in.mode = mode;
	return in;
}

// Whether channel c of the engine's result is the lone voice's output, bit
// for bit (OUT, INV, the four gates and TRIG).
bool same(const rka::BeaconResult& r, int c, const VoiceOut& o) {
	if (std::memcmp(&r.out[c], &o.level, sizeof(float)) != 0)
		return false;
	const float inv = -o.level;
	if (std::memcmp(&r.inv[c], &inv, sizeof(float)) != 0)
		return false;
	for (int g = 0; g < 4; g++)
		if (r.stageGate[g][c] != (o.stageGate[g] ? 10.f : 0.f))
			return false;
	return r.trig[c] == (o.trig ? 10.f : 0.f);
}

// ---------------------------------------------------------------------------

void poly_independent() {
	Eng g;
	g.f.params[F::SHAPE] = 0.3f;
	g.f.params[F::ATTACK] = 0.25f;
	g.f.params[F::DECAY] = 0.3f;
	g.f.params[F::RELEASE] = 0.35f;
	g.f.gate.channels = 16;
	g.f.retrig.channels = 16;
	g.f.attackCv.channels = g.f.decayCv.channels = g.f.sustainCv.channels = g.f.releaseCv.channels = 16;
	Voice ref[16];
	VoiceOut o[16];
	long bad[16] = {};
	bool distinct = false;
	for (long i = 0; i < 24000; i++) {
		for (int c = 0; c < 16; c++) {
			g.gate[c] = ((i + 97 * c) / (900 + 60 * c)) % 2 == 0 ? 5.f : 0.f;
			g.retrig[c] = ((i + 31 * c) / (250 + 20 * c)) % 3 == 0 ? 5.f : 0.f;
			g.acv[c] = 0.2f * c - 1.f;
			g.dcv[c] = 1.f - 0.1f * c;
			g.scv[c] = 0.3f * c - 2.f;
			g.rcv[c] = 0.05f * c;
		}
		g.step();
		for (int c = 0; c < 16; c++) {
			ref[c].process(refIn(g, c, GATE_MODE), o[c]);
			if (!same(g.r, c, o[c]))
				bad[c]++;
		}
		distinct |= g.r.out[0] != g.r.out[15] && g.r.out[3] != g.r.out[9];
	}
	CHECK(g.r.channels == 16, "16 channels (%d)", g.r.channels);
	for (int c = 0; c < 16; c++)
		CHECK(bad[c] == 0, "channel %d equals a lone Voice bit for bit (%ld samples differ)", c + 1, bad[c]);
	CHECK(distinct, "the channels do differ from one another");
}

// Mono CVs and a mono RETRIG apply to every channel.
void mono_cv_to_all() {
	Eng g;
	g.f.gate.channels = 4;
	g.f.retrig.channels = 1;
	g.f.attackCv.channels = g.f.decayCv.channels = g.f.sustainCv.channels = g.f.releaseCv.channels = 1;
	g.acv[0] = 3.f;
	g.dcv[0] = -2.f;
	g.scv[0] = 2.f;
	g.rcv[0] = 1.f;
	Voice ref[4], plain[4];
	VoiceOut o, p;
	long bad[4] = {}, differs = 0;
	for (long i = 0; i < 40000; i++) {
		for (int c = 0; c < 4; c++)
			g.gate[c] = ((i + 300 * c) / 12000) % 2 == 0 ? 5.f : 0.f;
		g.retrig[0] = (i % 5000) < 10 ? 5.f : 0.f;
		g.step();
		for (int c = 0; c < 4; c++) {
			ref[c].process(refIn(g, c, GATE_MODE), o);
			if (!same(g.r, c, o))
				bad[c]++;
			VoiceIn none = refIn(g, c, GATE_MODE);
			none.attack = stageSeconds(g.f.params[F::ATTACK]);
			none.sustain = 10.f * g.f.params[F::SUSTAIN];
			none.retrig = false;
			plain[c].process(none, p);
			if (p.level != o.level)
				differs++;
		}
	}
	for (int c = 0; c < 4; c++)
		CHECK(bad[c] == 0, "mono CVs: channel %d equals a lone Voice (%ld samples differ)", c + 1, bad[c]);
	CHECK(differs > 1000, "and the CVs change the envelope (%ld samples differ from none)", differs);
}

// Polyphonic CVs give each channel its own; a cable with fewer channels
// than the voices gives the rest 0 V.
void poly_cv_per_channel() {
	Eng g;
	g.f.gate.channels = 4;
	g.f.retrig.channels = 4;
	g.f.attackCv.channels = 4;
	g.f.decayCv.channels = 4;
	g.f.sustainCv.channels = 2;
	g.f.releaseCv.channels = 4;
	const float a[4] = {-1.f, 0.5f, 2.f, 3.5f};
	for (int c = 0; c < 4; c++) {
		g.acv[c] = a[c] - 3.f;    // Attack and Decay over by sample 14000
		g.dcv[c] = -2.f - a[c];
		g.rcv[c] = 0.5f * a[c];
	}
	g.scv[0] = -3.f;
	g.scv[1] = 4.f;
	g.scv[2] = 1.f;   // past the cable's 2 channels: must read 0 V
	g.scv[3] = 2.f;
	Voice ref[4];
	VoiceOut o;
	long bad[4] = {};
	float peak[4] = {};
	for (long i = 0; i < 40000; i++) {
		for (int c = 0; c < 4; c++) {
			g.gate[c] = (i / 15000) % 2 == 0 ? 5.f : 0.f;
			g.retrig[c] = i > 30000 && ((i + 1000 * c) % 3000) < 5 ? 5.f : 0.f;
		}
		g.step();
		for (int c = 0; c < 4; c++) {
			ref[c].process(refIn(g, c, GATE_MODE), o);
			if (!same(g.r, c, o))
				bad[c]++;
			if (i == 14000)
				peak[c] = g.r.out[c];   // in Sustain
		}
	}
	for (int c = 0; c < 4; c++)
		CHECK(bad[c] == 0, "poly CVs: channel %d equals a lone Voice (%ld samples differ)", c + 1, bad[c]);
	CHECK(std::fabs(peak[0] - 2.f) < 0.01f && std::fabs(peak[1] - 9.f) < 0.01f && std::fabs(peak[2] - 5.f) < 0.01f &&
	          std::fabs(peak[3] - 5.f) < 0.01f,
	      "Sustain per channel, 0 V past the cable's count (%g %g %g %g)", peak[0], peak[1], peak[2], peak[3]);
}

// Review Focus 3: GATE 4 -> 7 -> 2 -> 4 channels mid-run.
void channel_count() {
	Eng g;
	g.f.params[F::ATTACK] = 0.4f;   // ~80 ms: still rising when the count changes
	g.f.gate.channels = 4;
	Voice ref[16];
	VoiceOut o;
	const int counts[4] = {4, 7, 2, 4};
	long bad[16] = {};
	bool countOk = true, zeroPast = true, freshIdle = true;
	for (int phase = 0; phase < 4; phase++) {
		const int n = counts[phase];
		const int was = phase > 0 ? counts[phase - 1] : 0;
		g.f.gate.channels = n;
		for (int c = was; c < n; c++)
			ref[c] = Voice();   // a new channel: idle at 0 V
		for (long i = 0; i < 3000; i++) {
			for (int c = 0; c < 16; c++)
				g.gate[c] = (i + 400 * c) % 2600 < 1800 ? 5.f : 0.f;
			// The new channels' first sample has the gate low, so they are
			// seen idle at 0 V before they start.
			if (i == 0)
				for (int c = was; c < n; c++)
					g.gate[c] = 0.f;
			g.step();
			countOk &= g.r.channels == n;
			for (int c = 0; c < n; c++) {
				ref[c].process(refIn(g, c, GATE_MODE), o);
				if (!same(g.r, c, o))
					bad[c]++;
			}
			if (i == 0)
				for (int c = was; c < n; c++)
					freshIdle &= g.r.out[c] == 0.f && g.r.stageGate[0][c] == 0.f && g.r.stageGate[2][c] == 0.f;
			for (int c = n; c < 16; c++)
				zeroPast &= g.r.out[c] == 0.f && g.r.inv[c] == 0.f && g.r.trig[c] == 0.f;
		}
	}
	CHECK(countOk, "the result's count follows GATE's");
	for (int c = 0; c < 7; c++)
		CHECK(bad[c] == 0, "channel %d keeps its state / starts fresh as a lone Voice (%ld samples differ)", c + 1, bad[c]);
	CHECK(freshIdle, "new channels start at 0 V, idle");
	CHECK(zeroPast, "channels past the count read 0 V");

	// HOLD: the Sustain CV's count, not GATE's.
	g.f.params[F::MODE] = 2.f;
	g.f.sustainCv.channels = 3;
	g.f.gate.channels = 5;
	g.step();
	CHECK(g.r.channels == 3, "HOLD follows the Sustain CV's count (%d)", g.r.channels);
	g.f.sustainCv.channels = 0;
	g.step();
	CHECK(g.r.channels == 1, "HOLD, Sustain CV unpatched: 1 (%d)", g.r.channels);
	g.f.params[F::MODE] = 1.f;
	g.f.gate.channels = 0;
	g.f.sustainCv.channels = 6;
	g.f.retrig.channels = 9;
	g.step();
	CHECK(g.r.channels == 1, "TRIG, GATE unpatched: 1 (%d)", g.r.channels);
	g.f.gate.channels = 20;
	g.step();
	CHECK(g.r.channels == 16, "capped at 16 (%d)", g.r.channels);
}

// Review minor 6, pinned: four channels of GATE held high in GATE mode,
// Sustain CV unpatched; to HOLD the count is the Sustain CV's, so it
// collapses to 1 (channels 2-4 are dropped, their outputs 0 V); back to
// GATE the count is 4 again and channels 2-4 appear as new channels: idle at
// 0 V, then, the gate being high, a fresh Attack from 0 V (each equal to a
// lone new Voice); channel 1 keeps its one level throughout and attacks
// from it.
void hold_collapses_channels() {
	Eng g;
	g.f.params[F::SUSTAIN] = 0.6f;
	g.f.gate.channels = 4;
	for (int c = 0; c < 4; c++)
		g.gate[c] = 5.f;
	g.run(48000);   // into Sustain at 6 V on every channel
	bool sustaining = g.r.channels == 4;
	for (int c = 0; c < 4; c++)
		sustaining &= std::fabs(g.r.out[c] - 6.f) < 1e-3f && g.r.stageGate[2][c] == 10.f;
	CHECK(sustaining, "GATE: four channels sustain at 6 V (%d channels, ch 2 %g V)", g.r.channels, g.r.out[1]);

	// HOLD: one channel; the slew input is 10 V x 0.6 = 6 V, so channel 1 stays.
	g.f.params[F::MODE] = 2.f;
	bool one = true, quiet = true, steady = true;
	for (int i = 0; i < 4800; i++) {
		g.step();
		one &= g.r.channels == 1;
		steady &= std::fabs(g.r.out[0] - 6.f) < 1e-3f;
		for (int c = 1; c < 4; c++)
			quiet &= g.r.out[c] == 0.f && g.r.inv[c] == 0.f && g.r.trig[c] == 0.f && g.r.stageGate[2][c] == 0.f;
	}
	CHECK(one, "HOLD with Sustain CV unpatched: the count collapses to 1 (%d)", g.r.channels);
	CHECK(quiet, "HOLD: channels 2-4 are dropped, their outputs 0 V");
	CHECK(steady, "HOLD: channel 1 keeps its level (%g V)", g.r.out[0]);

	// Back to GATE, the gates still high.
	const float before = g.r.out[0];
	g.f.params[F::MODE] = 0.f;
	Voice ref[4];
	VoiceOut o;
	long bad = 0;
	bool firstLow = true;
	for (int i = 0; i < 2400; i++) {
		g.step();
		if (i == 0) {
			CHECK(g.r.channels == 4, "back in GATE: 4 channels (%d)", g.r.channels);
			CHECK(g.stage() == ATTACK && std::fabs(g.r.out[0] - before) < 0.05f,
			      "channel 1 attacks from its level (%s, %g V from %g V)", stageName(g.stage()), g.r.out[0], before);
			for (int c = 1; c < 4; c++)
				firstLow &= g.r.out[c] < 0.2f && g.r.stageGate[0][c] == 10.f && g.r.trig[c] == 10.f;
		}
		for (int c = 1; c < 4; c++) {
			ref[c].process(refIn(g, c, GATE_MODE), o);
			bad += !same(g.r, c, o);
		}
	}
	CHECK(firstLow, "channels 2-4 restart from 0 V (Attack begun, TRIG high, under 0.2 V on the first sample)");
	CHECK(bad == 0, "channels 2-4 each equal a new lone Voice from 0 V (%ld samples differ)", bad);
}

// PUSH gates every channel, GATE patched or not.
void push() {
	{
		Eng g;
		g.f.params[F::PUSH] = 1.f;
		g.run(100);
		CHECK(g.r.channels == 1 && g.stage() == ATTACK && g.r.out[0] > 0.f, "PUSH, GATE unpatched: Attack (%s, %g)",
		      stageName(g.stage()), g.r.out[0]);
		g.run(48000);
		CHECK(g.stage() == SUSTAIN, "and held: Sustain (%s)", stageName(g.stage()));
		g.f.params[F::PUSH] = 0.f;
		g.run(10);
		CHECK(g.stage() == RELEASE, "released: Release (%s)", stageName(g.stage()));
	}
	{
		Eng g;
		g.f.gate.channels = 3;   // all low
		g.run(10);
		CHECK(g.r.out[0] == 0.f && g.r.out[2] == 0.f, "GATE patched, low: idle");
		g.f.params[F::PUSH] = 1.f;
		g.run(100);
		bool all = true;
		for (int c = 0; c < 3; c++)
			all &= g.r.stageGate[0][c] == 10.f && g.r.out[c] > 0.f;
		CHECK(all, "PUSH with GATE patched gates every channel (A gates %g %g %g)", g.r.stageGate[0][0],
		      g.r.stageGate[0][1], g.r.stageGate[0][2]);
	}
}

// GATE and RETRIG: Schmitt at 0.1 V and 1 V, one state per channel.
void schmitt() {
	Eng g;
	g.f.gate.channels = 2;
	g.f.retrig.channels = 2;
	g.gate[0] = 0.9f;
	g.run(10);
	CHECK(g.r.out[0] == 0.f, "GATE 0.9 V from low: still idle (%g)", g.r.out[0]);
	g.gate[0] = 1.f;
	g.run(48000);
	CHECK(g.stage() == SUSTAIN, "GATE 1 V: high (%s)", stageName(g.stage()));
	CHECK(g.r.out[1] == 0.f, "channel 2's gate is its own (%g)", g.r.out[1]);
	g.gate[0] = 0.11f;
	g.run(10);
	CHECK(g.stage() == SUSTAIN, "GATE 0.11 V from high: still high (%s)", stageName(g.stage()));
	// RETRIG while high: 0.9 V is no edge, 1 V is.
	g.retrig[0] = 0.9f;
	g.run(10);
	CHECK(g.stage() == SUSTAIN, "RETRIG 0.9 V: nothing (%s)", stageName(g.stage()));
	g.retrig[0] = 1.f;
	g.step();
	CHECK(g.stage() == ATTACK, "RETRIG 1 V: Attack (%s)", stageName(g.stage()));
	g.run(48000);
	g.retrig[0] = 0.11f;   // still high: no new edge
	g.run(10);
	g.retrig[0] = 1.f;
	g.step();
	CHECK(g.stage() == SUSTAIN, "RETRIG 0.11 V between: still high, so no edge (%s)", stageName(g.stage()));
	g.retrig[0] = 0.09f;
	g.run(10);
	g.retrig[0] = 1.f;
	g.step();
	CHECK(g.stage() == ATTACK, "RETRIG 0.09 V between: low, then an edge (%s)", stageName(g.stage()));
	g.run(48000);
	g.gate[0] = 0.09f;
	g.step();
	CHECK(g.stage() == RELEASE, "GATE 0.09 V: low (%s)", stageName(g.stage()));

	// Unpatched, GATE reads 0 V through its Schmitt: a cable plugged back
	// in at 0.5 V (inside the hysteresis) is low, not a new edge.
	Eng h;
	h.f.gate.channels = 1;
	h.gate[0] = 5.f;
	h.run(48000);
	CHECK(h.stage() == SUSTAIN, "GATE 5 V: high (%s)", stageName(h.stage()));
	h.f.gate.channels = 0;
	h.run(10);
	CHECK(h.stage() == RELEASE, "GATE unpatched: low (%s)", stageName(h.stage()));
	h.gate[0] = 0.5f;
	h.f.gate.channels = 1;
	h.run(10);
	CHECK(h.stage() == RELEASE, "GATE replugged at 0.5 V: still low, no edge (%s)", stageName(h.stage()));
}

// Review Focus 1: GATE -> HOLD -> TRIG -> GATE every 7 ms for 2 s, gate
// held, a 3 Hz Sustain CV. Each step is held to the active stage's (or the
// slew's) own largest step x 1.5 + 0.1 mV, the bound computed from the
// stage's start and goal: |goal - start| x (1 + 3a) x dt / T, a = |2 Shape -
// 1| (r'(p) = 1 - a + a c'(p), c' at most 4); Sustain |sustain - level| x
// its 2 ms pole; the slew 10 V x dt / T.
void mode_switch() {
	const float fs = 48000.f;
	const double dt = 1.0 / fs;
	{
		Eng g(fs);
		g.f.gate.channels = 1;
		g.f.sustainCv.channels = 1;
		g.gate[0] = 5.f;
		g.f.params[F::ATTACK] = stageKnob(0.003f);   // A and D inside a 14 ms GATE + TRIG stretch
		g.f.params[F::DECAY] = stageKnob(0.004f);
		const double ta = stageSeconds(g.f.params[F::ATTACK]), td = stageSeconds(g.f.params[F::DECAY]),
		             tr = stageSeconds(g.f.params[F::RELEASE]);
		const double a = std::fabs(2.0 * g.f.params[F::SHAPE] - 1.0);
		const double pole = 1.0 - std::exp(-1.0 / (0.002 * fs));
		const int order[3] = {0, 2, 1};   // GATE, HOLD, TRIG
		int prevStage = IDLE, prevMode = 0;
		float prevPhase = 0.f, prevOut = 0.f;
		double start = 0.0, goal = 0.0;
		long over = 0, incoherent = 0, holds = 0, stages[5] = {};
		double worst = 0.0;
		for (long i = 0; i < 96000; i++) {
			const int mode = order[(i / 336) % 3];
			g.f.params[F::MODE] = float(mode);
			g.scv[0] = 4.f * float(std::sin(2.0 * M_PI * 3.0 * i * dt));
			const float sus = 10.f * std::fmin(1.f, std::fmax(0.f, 0.5f + g.scv[0] * 0.1f));
			g.step();
			const int s = g.stage();
			const float ph = g.e.published().phase.load();
			const float out = g.r.out[0];
			double bound;
			if (mode == 2) {
				bound = 10.0 * dt / (ta < td ? ta : td);
				holds++;
			} else {
				if (s != prevStage || ph < prevPhase || prevMode == 2) {
					start = prevOut;
					goal = s == ATTACK ? 10.0 : s == DECAY ? sus : 0.0;
				}
				if (s == SUSTAIN)
					bound = std::fabs(sus - prevOut) * pole;
				else if (s == IDLE)
					bound = 0.0;
				else
					bound = std::fabs(goal - start) * (1.0 + 3.0 * a) * dt / (s == ATTACK ? ta : s == DECAY ? td : tr);
			}
			const double step = std::fabs(double(out) - prevOut);
			if (step > bound * 1.5 + 1e-4) {
				if (over < 5)
					std::printf("  sample %ld mode %d %s: step %g over %g\n", i, mode, stageName(s), step, bound);
				over++;
			}
			if (step - bound > worst)
				worst = step - bound;
			for (int k = 0; k < 4; k++)
				incoherent += g.r.stageGate[k][0] != (s == ATTACK + k ? 10.f : 0.f);
			incoherent += mode == 2 && g.r.stageGate[3][0] != 0.f;
			incoherent += g.r.inv[0] != -out;
			if (s >= IDLE && s <= RELEASE)
				stages[s]++;
			prevStage = s;
			prevPhase = ph;
			prevMode = mode;
			prevOut = out;
		}
		CHECK(over == 0, "switching every 7 ms: %ld steps over the active stage's own (largest excess %g V)", over, worst);
		CHECK(incoherent == 0, "gates follow the stage, R low in HOLD (%ld samples not)", incoherent);
		CHECK(holds > 30000 && stages[ATTACK] > 1000 && stages[DECAY] > 1000 && stages[SUSTAIN] > 1000,
		      "every mode and stage visited (hold %ld, A %ld, D %ld, S %ld)", holds, stages[ATTACK], stages[DECAY],
		      stages[SUSTAIN]);
		g.f.params[F::MODE] = 0.f;
		g.scv[0] = 0.f;
		g.run(24000);
		CHECK(g.stage() == SUSTAIN && std::fabs(g.r.out[0] - 5.f) < 1e-3f, "then GATE, held: Sustain at 5 V (%s, %g)",
		      stageName(g.stage()), g.r.out[0]);
	}
	// HOLD -> GATE with the gate low and the level at -3 V: Release rises to
	// 0 V in Release's time.
	{
		Eng g(fs);
		g.f.params[F::MODE] = 2.f;
		g.f.params[F::SUSTAIN] = 0.f;
		g.f.sustainCv.channels = 1;
		g.scv[0] = -3.f;
		g.f.gate.channels = 1;
		g.run(24000);
		CHECK(g.r.out[0] == -3.f, "HOLD settles at -3 V (%g)", g.r.out[0]);
		g.f.params[F::MODE] = 0.f;
		float prev = g.r.out[0];
		g.step();
		CHECK(g.stage() == RELEASE && g.r.out[0] > -3.f && g.r.out[0] < -2.99f, "HOLD -> GATE, low: Release from -3 V (%s, %g)",
		      stageName(g.stage()), g.r.out[0]);
		long n = 0;
		bool rising = true;
		float last = 0.f;
		while (g.stage() == RELEASE && n < 100000) {
			n++;
			last = g.r.out[0];
			rising &= last >= prev;
			prev = last;
			g.step();
		}
		const double want = stageSeconds(g.f.params[F::RELEASE]) * fs;
		CHECK(std::fabs(n - want) <= 1.0 && last == 0.f && rising,
		      "rises to 0 V in Release's time (%ld samples, want %.1f; to %g; rising %d)", n, want, last, int(rising));
		CHECK(g.stage() == IDLE, "then idle (%s)", stageName(g.stage()));
	}
}

// INV is -OUT bit for bit, in every mode, at 0 V too (-0).
void inv_exact() {
	Eng g;
	g.f.gate.channels = 3;
	g.f.sustainCv.channels = 3;
	long bad = 0, negative = 0;
	for (long i = 0; i < 60000; i++) {
		g.f.params[F::MODE] = float((i / 9000) % 3);
		for (int c = 0; c < 3; c++) {
			g.gate[c] = ((i + 700 * c) / 3000) % 2 ? 5.f : 0.f;
			g.scv[c] = -9.f + 6.f * c + float((i / 500) % 4);
		}
		g.step();
		for (int c = 0; c < g.r.channels; c++) {
			const float want = -g.r.out[c];
			bad += std::memcmp(&g.r.inv[c], &want, sizeof(float)) != 0;
			negative += g.r.out[c] < 0.f;
		}
	}
	CHECK(bad == 0, "INV == -OUT bit for bit (%ld samples not)", bad);
	CHECK(negative > 100, "including negative OUT in HOLD (%ld)", negative);
}

// published(): channel 1's stage, phase, level and the mode.
void published() {
	Eng g;
	g.f.gate.channels = 2;
	g.f.params[F::MODE] = 1.2f;   // rounds to TRIG
	g.acv[1] = 5.f;               // channel 2: a longer Attack, so it differs
	g.f.attackCv.channels = 2;
	g.gate[0] = g.gate[1] = 5.f;
	int seq[16], n = 0, last = -1;
	long levelBad = 0, phaseBad = 0, modeBad = 0;
	for (long i = 0; i < 96000; i++) {
		if (i == 24000)
			g.gate[0] = g.gate[1] = 0.f;
		g.step();
		const rka::BeaconPublished& p = g.e.published();
		const int s = p.stage.load(std::memory_order_relaxed);
		if (s != last && n < 16)
			seq[n++] = s;
		last = s;
		levelBad += p.level.load() != g.r.out[0];
		const float ph = p.phase.load();
		phaseBad += !(ph >= 0.f && ph <= 1.f);
		modeBad += p.mode.load() != TRIG_MODE;
	}
	const int want[5] = {ATTACK, DECAY, SUSTAIN, RELEASE, IDLE};
	bool ok = n == 5;
	for (int k = 0; ok && k < 5; k++)
		ok = seq[k] == want[k];
	CHECK(ok, "published stage walks A, D, S, R, IDLE (%d stages: %s %s %s %s %s)", n, stageName(seq[0]),
	      n > 1 ? stageName(seq[1]) : "-", n > 2 ? stageName(seq[2]) : "-", n > 3 ? stageName(seq[3]) : "-",
	      n > 4 ? stageName(seq[4]) : "-");
	CHECK(levelBad == 0, "published level is channel 1's OUT (%ld samples not)", levelBad);
	CHECK(phaseBad == 0, "published phase within 0..1 (%ld samples not)", phaseBad);
	CHECK(modeBad == 0, "published mode: TRIG (%ld samples not)", modeBad);
	// Phase mid-Attack is the voice's.
	Eng h;
	h.f.params[F::ATTACK] = stageKnob(0.1f);
	h.f.params[F::PUSH] = 1.f;
	h.run(2400);
	const float ph = h.e.published().phase.load();
	const double want2 = 2400.0 / (stageSeconds(h.f.params[F::ATTACK]) * 48000.0);
	CHECK(std::fabs(ph - want2) < 1e-4, "published phase mid-Attack %g (want %g)", ph, want2);
}

// Channel 1's stage start and goal (volts), latched when the stage began.
void published_start_goal() {
	Eng g;
	g.f.params[F::ATTACK] = stageKnob(0.1f);
	g.f.params[F::RELEASE] = stageKnob(1.f);
	g.f.gate.channels = 1;
	g.gate[0] = 5.f;
	g.run(2400);                      // half way up the Attack
	const rka::BeaconPublished& p = g.e.published();
	CHECK(g.stage() == ATTACK && p.start.load() == 0.f && p.goal.load() == 10.f,
	      "Attack from 0 V to 10 V (%s, %g, %g)", stageName(g.stage()), p.start.load(), p.goal.load());
	const float atFall = g.r.out[0];
	g.gate[0] = 0.f;
	g.step();
	CHECK(atFall > 1.f && atFall < 9.9f, "the fall is mid-Attack (%g V)", atFall);
	CHECK(g.stage() == RELEASE, "the fall starts Release (%s)", stageName(g.stage()));
	CHECK(p.start.load() == atFall, "Release starts at the level at the fall: %g (want %g)", p.start.load(), atFall);
	CHECK(p.goal.load() == 0.f, "Release's goal 0 V (%g)", p.goal.load());
	g.run(12000);                     // a quarter into the Release
	const float atRise = g.r.out[0];
	g.gate[0] = 5.f;
	g.step();
	CHECK(atRise > 0.1f && atRise < atFall, "the retrigger is mid-Release (%g V)", atRise);
	CHECK(g.stage() == ATTACK, "the retrigger starts Attack (%s)", stageName(g.stage()));
	CHECK(p.start.load() == atRise, "Attack starts at the level then: %g (want %g)", p.start.load(), atRise);
	CHECK(p.goal.load() == 10.f, "Attack's goal 10 V (%g)", p.goal.load());
	// Reset: back to 0 and 0.
	g.e.reset();
	CHECK(p.start.load() == 0.f && p.goal.load() == 0.f, "reset clears them (%g, %g)", p.start.load(), p.goal.load());
}

// Channel 1's previous stage: the one that ran before the running one, so
// the window can draw a stage that skipped others from where the last ended.
void published_prev_stage() {
	// Where Attack hands on to Decay with the gate held.
	long n = 0;
	{
		Eng g;
		g.f.params[F::ATTACK] = stageKnob(0.1f);
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;
		while (g.stage() != DECAY && n < 100000) {
			g.step();
			n++;
		}
		CHECK(g.stage() == DECAY, "Attack hands on to Decay (sample %ld)", n);
	}
	// The gate falls on the last Attack sample, and on the hand-off sample
	// itself (Attack hands straight on to Release).
	for (int late = 0; late < 2; late++) {
		Eng g;
		g.f.params[F::ATTACK] = stageKnob(0.1f);
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;
		g.run(n - 2 + late);
		CHECK(g.stage() == ATTACK, "late %d: still in Attack (%s)", late, stageName(g.stage()));
		const float ph = g.e.published().phase.load();
		g.gate[0] = 0.f;
		g.step();
		const rka::BeaconPublished& p = g.e.published();
		CHECK(g.stage() == RELEASE && p.prevStage.load() == ATTACK,
		      "late %d: fall as Attack completes: %s after %s (want RELEASE after ATTACK)", late,
		      stageName(g.stage()), stageName(p.prevStage.load()));
		CHECK(p.start.load() > 9.f, "late %d: Release starts near the top (%g V)", late, p.start.load());
		CHECK(p.prevPhase.load() == ph && ph > 0.999f && (late == 0 || ph == 1.f),
		      "late %d: Attack cut off at %g (want its phase then, %g)", late, p.prevPhase.load(), ph);
	}
	// Mid-Decay.
	{
		Eng g;
		g.f.params[F::ATTACK] = stageKnob(0.1f);
		g.f.params[F::DECAY] = stageKnob(0.1f);
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;
		g.run(n + 2400);
		CHECK(g.stage() == DECAY, "mid-Decay (%s)", stageName(g.stage()));
		const float ph = g.e.published().phase.load();
		CHECK(g.e.published().prevStage.load() == ATTACK, "Decay after Attack (%s)", stageName(g.e.published().prevStage.load()));
		g.gate[0] = 0.f;
		g.step();
		CHECK(g.stage() == RELEASE && g.e.published().prevStage.load() == DECAY,
		      "fall mid-Decay: %s after %s (want RELEASE after DECAY)", stageName(g.stage()),
		      stageName(g.e.published().prevStage.load()));
		CHECK(ph > 0.4f && ph < 0.6f && g.e.published().prevPhase.load() == ph,
		      "Decay cut off at its phase then: %g (want %g)", g.e.published().prevPhase.load(), ph);
	}
	// A full ADSR: Release after Sustain; then a retrigger: Attack after Release.
	{
		Eng g;
		g.f.params[F::ATTACK] = stageKnob(0.01f);
		g.f.params[F::DECAY] = stageKnob(0.01f);
		g.f.params[F::RELEASE] = stageKnob(1.f);
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;
		g.run(4800);
		CHECK(g.stage() == SUSTAIN && g.e.published().prevStage.load() == DECAY, "Sustain after Decay (%s after %s)",
		      stageName(g.stage()), stageName(g.e.published().prevStage.load()));
		g.gate[0] = 0.f;
		g.run(10);
		CHECK(g.stage() == RELEASE && g.e.published().prevStage.load() == SUSTAIN,
		      "Release after Sustain (%s after %s)", stageName(g.stage()), stageName(g.e.published().prevStage.load()));
		CHECK(g.e.published().prevPhase.load() == 1.f, "a cut-off Sustain counts as complete (%g)",
		      g.e.published().prevPhase.load());
		g.gate[0] = 5.f;
		g.step();
		CHECK(g.stage() == ATTACK && g.e.published().prevStage.load() == RELEASE,
		      "retrigger: Attack after Release (%s after %s)", stageName(g.stage()), stageName(g.e.published().prevStage.load()));
		g.e.reset();
		CHECK(g.e.published().prevStage.load() == IDLE && g.e.published().prevPhase.load() == 0.f,
		      "reset clears them (%s, %g)", stageName(g.e.published().prevStage.load()), g.e.published().prevPhase.load());
	}
	// Leaving Hold mid-slew: Hold's stages are no envelope's, so Release
	// counts as from idle.
	{
		Eng g;
		g.f.params[F::MODE] = 2.f;
		g.f.params[F::SUSTAIN] = 0.f;
		g.f.params[F::DECAY] = stageKnob(1.f);
		g.f.sustainCv.channels = 1;
		g.scv[0] = 8.f;
		g.run(9600);                  // settled at 8 V
		g.scv[0] = 0.f;
		g.run(100);                   // slewing down: Hold's D
		CHECK(g.stage() == DECAY, "Hold slewing down (%s)", stageName(g.stage()));
		g.f.params[F::MODE] = 0.f;
		g.f.gate.channels = 1;
		g.step();
		CHECK(g.stage() == RELEASE && g.e.published().prevStage.load() == IDLE,
		      "leaving Hold: %s after %s (want RELEASE after IDLE)", stageName(g.stage()),
		      stageName(g.e.published().prevStage.load()));
	}
	// Leaving Hold mid-slew with the gate high: Attack, also from idle.
	{
		Eng g;
		g.f.params[F::MODE] = 2.f;
		g.f.params[F::SUSTAIN] = 0.f;
		g.f.params[F::DECAY] = stageKnob(1.f);
		g.f.sustainCv.channels = 1;
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;              // high all through: no rise on return
		g.scv[0] = 8.f;
		g.run(9600);
		g.scv[0] = 0.f;
		g.run(100);
		CHECK(g.stage() == DECAY, "Hold slewing down (%s)", stageName(g.stage()));
		g.f.params[F::MODE] = 0.f;
		g.step();
		CHECK(g.stage() == ATTACK && g.e.published().prevStage.load() == IDLE,
		      "leaving Hold, gate high: %s after %s (want ATTACK after IDLE)", stageName(g.stage()),
		      stageName(g.e.published().prevStage.load()));
	}
	// TRIG: a short trigger; Attack runs on, then Release with prevStage ATTACK.
	{
		Eng g;
		g.f.params[F::MODE] = 1.f;
		g.f.params[F::ATTACK] = stageKnob(0.1f);
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;
		g.run(10);
		g.gate[0] = 0.f;
		g.run(100);
		CHECK(g.stage() == ATTACK, "TRIG: Attack runs on past the trigger (%s)", stageName(g.stage()));
		long k = 0;
		while (g.stage() == ATTACK && k < 100000) {
			g.step();
			k++;
		}
		CHECK(g.stage() == RELEASE && g.e.published().prevStage.load() == ATTACK,
		      "TRIG: Attack completes into %s after %s (want RELEASE after ATTACK)", stageName(g.stage()),
		      stageName(g.e.published().prevStage.load()));
	}
	// A fall half-way up Attack: Release after Attack, cut off at ~0.5.
	{
		Eng g;
		g.f.params[F::ATTACK] = stageKnob(0.1f);
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;
		g.run(2400);
		const float ph = g.e.published().phase.load();
		g.gate[0] = 0.f;
		g.step();
		const rka::BeaconPublished& p = g.e.published();
		CHECK(g.stage() == RELEASE && p.prevStage.load() == ATTACK && p.prevPhase.load() == ph && ph > 0.45f && ph < 0.55f,
		      "fall mid-Attack: %s after %s at %g (want ATTACK at %g)", stageName(g.stage()),
		      stageName(p.prevStage.load()), p.prevPhase.load(), ph);
	}
	// The user's case (build 2.0.36): GATE, A 4.64 s, D 300 ms, S 5 V, R 6 s,
	// the gate high 39.4 s x 11.78 % = 4.6413 s at 48 kHz: Attack ends at
	// 4.640 s, Decay runs ~1.3 ms, then Release, after Decay at ~0.0043.
	{
		Eng g;
		g.f.params[F::ATTACK] = stageKnob(4.64f);
		g.f.params[F::DECAY] = stageKnob(0.3f);
		g.f.params[F::SUSTAIN] = 0.5f;
		g.f.params[F::RELEASE] = stageKnob(6.f);
		g.f.gate.channels = 1;
		g.gate[0] = 5.f;
		const long high = long(39.4 * 0.1178 * 48000.0 + 0.5);
		g.run(high);
		const float ph = g.e.published().phase.load();
		CHECK(g.stage() == DECAY, "user's case: in Decay when the gate falls (%s)", stageName(g.stage()));
		g.gate[0] = 0.f;
		g.step();
		const rka::BeaconPublished& p = g.e.published();
		const float pp = p.prevPhase.load();
		CHECK(g.stage() == RELEASE && p.prevStage.load() == DECAY, "user's case: %s after %s (want RELEASE after DECAY)",
		      stageName(g.stage()), stageName(p.prevStage.load()));
		CHECK(pp == ph && std::fabs(pp - 0.0043f) < 0.0005f, "user's case: Decay cut off at %g (want %g, ~0.0043)", pp, ph);
		CHECK(p.start.load() > 9.9f && p.start.load() < 10.f, "user's case: Release from %g V (~9.95)", p.start.load());
	}
}

// The stage lights: updated every DIVISION samples, and a stage that lasted
// one sample keeps its light for the hold (longer than a UI frame).
void stage_lights() {
	rka::BeaconLights L;
	float b[4] = {-1.f, -1.f, -1.f, -1.f};
	const float tick = float(rka::BeaconLights::DIVISION) / 48000.f;
	// Decay lit for one sample at the start of a division, then off.
	for (int i = 0; i < rka::BeaconLights::DIVISION; i++)
		for (int k = 0; k < 4; k++)
			L.note(k, k == 1 && i == 0);
	L.update(tick, b);
	CHECK(b[1] == 1.f && b[0] == 0.f && b[2] == 0.f && b[3] == 0.f, "one sample of Decay lights D (%g %g %g %g)", b[0], b[1], b[2], b[3]);
	// Nothing lit since: D stays on through the hold, then goes out.
	int on = 1;
	for (int u = 0; u < 200 && b[1] > 0.f; u++) {
		for (int i = 0; i < rka::BeaconLights::DIVISION; i++)
			for (int k = 0; k < 4; k++)
				L.note(k, false);
		L.update(tick, b);
		on += b[1] > 0.f;
	}
	const float shown = on * tick;
	CHECK(shown >= rka::BeaconLights::HOLD_SECONDS && shown < rka::BeaconLights::HOLD_SECONDS + 2.f * tick,
	      "D shown %g s (hold %g s)", shown, rka::BeaconLights::HOLD_SECONDS);
	CHECK(rka::BeaconLights::HOLD_SECONDS >= 1.f / 30.f, "the hold covers a 30 Hz frame (%g s)", rka::BeaconLights::HOLD_SECONDS);
	CHECK(b[1] == 0.f, "then out (%g)", b[1]);
	// A stage held on stays on; reset puts every light out.
	for (int u = 0; u < 100; u++) {
		for (int i = 0; i < rka::BeaconLights::DIVISION; i++)
			L.note(2, true);
		L.update(tick, b);
	}
	CHECK(b[2] == 1.f, "S held on (%g)", b[2]);
	// A stage longer than the hold goes out by the first update after it
	// ends (one light at a time at a change); one shorter than the hold
	// stays lit for the hold counted from its start, no longer.
	{
		const int lens[2] = {4000, 1000};   // 83 ms and 21 ms at 48 kHz
		for (int len : lens) {
			rka::BeaconLights M;
			float m[4] = {};
			long i = 0, offAt = -1;
			for (; i < len; i++) {
				M.note(0, true);
				if ((i + 1) % rka::BeaconLights::DIVISION == 0)
					M.update(tick, m);
			}
			for (; i < 48000; i++) {
				M.note(0, false);
				if ((i + 1) % rka::BeaconLights::DIVISION == 0) {
					M.update(tick, m);
					if (m[0] == 0.f && offAt < 0)
						offAt = i + 1;
				}
			}
			const long holdEnd = long(rka::BeaconLights::HOLD_SECONDS * 48000.f);
			const long wantOff = len > holdEnd ? (len / rka::BeaconLights::DIVISION + 2) * rka::BeaconLights::DIVISION
			                                   : holdEnd + 2 * rka::BeaconLights::DIVISION;
			CHECK(offAt > 0 && offAt <= wantOff, "A lit for %d samples: out at sample %ld (by %ld)", len, offAt, wantOff);
			CHECK(offAt >= (len < holdEnd ? holdEnd : len), "A lit for %d samples: on to sample %ld at least", len, offAt);
		}
	}
	L.reset();
	L.update(tick, b);
	CHECK(b[0] == 0.f && b[1] == 0.f && b[2] == 0.f && b[3] == 0.f, "reset: all out");
}

// 48 -> 96 kHz mid-Attack: no step, and Attack ends at its time in seconds.
void rate_change() {
	Eng g(48000.f);
	g.f.params[F::ATTACK] = stageKnob(0.1f);
	const double t = stageSeconds(g.f.params[F::ATTACK]);
	g.f.params[F::PUSH] = 1.f;
	g.run(2000);
	const float before = g.r.out[0];
	g.e.setSampleRate(96000.f);
	g.step();
	const double a = std::fabs(2.0 * g.f.params[F::SHAPE] - 1.0);
	const double bound = 10.0 * (1.0 + 3.0 * a) / (96000.0 * t);
	const float d = g.r.out[0] - before;
	CHECK(g.stage() == ATTACK && d > 0.f && d <= bound * 1.5 + 1e-4,
	      "the level carries on at the new rate (%g -> %g, step %g, bound %g)", before, g.r.out[0], d, bound);
	long n = 1;
	while (g.stage() == ATTACK && n < 100000) {
		g.step();
		n++;
	}
	const double want = (t - 2000.0 / 48000.0) * 96000.0;
	CHECK(std::fabs((n - 1) - want) <= 1.0, "Attack ends at its time: %ld samples at 96 kHz after the change (want %.1f)",
	      n - 1, want);
}

// Shape jumping 0 -> 1 in one sample mid-Attack is smoothed (about 5 ms):
// OUT moves no more than the stage's own largest step (at Shape 0 or 1)
// x 1.5 + 0.1 mV. Once settled, Shape is exactly the target.
void shape_smoothing() {
	const float fs = 48000.f;
	auto attackMaxStep = [&](float shape) {
		Eng g(fs);
		g.f.params[F::SHAPE] = shape;
		g.f.params[F::PUSH] = 1.f;
		float prev = 0.f, worst = 0.f;
		for (int i = 0; i < 2000; i++) {
			g.step();
			if (g.stage() != ATTACK)
				break;
			worst = std::fmax(worst, std::fabs(g.r.out[0] - prev));
			prev = g.r.out[0];
		}
		return worst;
	};
	const float own = std::fmax(attackMaxStep(0.f), attackMaxStep(1.f));
	Eng g(fs);
	g.f.params[F::SHAPE] = 0.f;
	g.f.params[F::PUSH] = 1.f;
	g.run(240);   // half of the 10 ms Attack
	CHECK(g.stage() == ATTACK, "mid-Attack (%s)", stageName(g.stage()));
	g.f.params[F::SHAPE] = 1.f;
	float prev = g.r.out[0], worst = 0.f;
	long n = 0;
	while (g.stage() == ATTACK && n < 2000) {
		g.step();
		n++;
		if (g.stage() == ATTACK || g.r.out[0] == 10.f)
			worst = std::fmax(worst, std::fabs(g.r.out[0] - prev));
		prev = g.r.out[0];
	}
	CHECK(worst <= own * 1.5f + 1e-4f, "Shape 0 -> 1 mid-Attack: largest step %g V (own %g V)", worst, own);
	// Release to idle, wait, then a new gate: the voice matches a lone one
	// at Shape 1 exactly, so the smoothing landed on the target.
	g.f.params[F::PUSH] = 0.f;
	g.run(96000);
	CHECK(g.stage() == IDLE && g.r.out[0] == 0.f, "idle again (%s)", stageName(g.stage()));
	Voice ref;
	VoiceOut o;
	g.f.params[F::PUSH] = 1.f;
	long bad = 0;
	for (int i = 0; i < 30000; i++) {
		g.step();
		ref.process(refIn(g, 0, GATE_MODE), o);
		bad += !same(g.r, 0, o);
	}
	CHECK(bad == 0, "settled Shape is the target: a new envelope equals a lone Voice at Shape 1 (%ld differ)", bad);
}

} // namespace

int main() {
	poly_independent();
	mono_cv_to_all();
	poly_cv_per_channel();
	channel_count();
	hold_collapses_channels();
	push();
	schmitt();
	mode_switch();
	inv_exact();
	published();
	published_start_goal();
	published_prev_stage();
	stage_lights();
	rate_change();
	shape_smoothing();
	return rkatest::finish("test_engine");
}
