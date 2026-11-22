// Eddy's transport (src/eddy/EddyEngine.hpp): Rec and its tail, Play, sets
// arriving and leaving (Review Focus 4), the empty tape, rate changes,
// trigger storms (Review Focus 2) and the change counters. Built with the
// plugin's floating-point flags.
#include "eddy/test_rig.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace {

using namespace rig;

float peakOver(Rig& g, long n, float in = 0.f) {
	float p = 0;
	for (long i = 0; i < n; i++) {
		g.step(in, in);
		p = std::max(p, std::max(std::fabs(g.r.l), std::fabs(g.r.r)));
	}
	return p;
}

std::vector<float> noiseVec(size_t n, float volts, uint32_t seed) {
	rka::hadal::Random rnd;
	rnd.seed(seed);
	std::vector<float> x(n);
	for (float& v : x)
		v = volts * rnd.uniform();
	return x;
}

// A quick double tap on REC: REC started and stopped `ms` apart, by the
// button or the gate, over a tape playing a 2 V 100 Hz sine (or on an empty
// tape), with a 1 V 50 Hz sine at the input. What is left: the tape, the
// original, the overview's columns (after, and at every 16th sample on the
// way), the change counters, Play, and the largest step at the output.
enum TapBy { BUTTON_BUTTON, GATE_GATE, BUTTON_GATE, GATE_BUTTON };
const char* const TAP_NAMES[4] = {"button", "gate", "button, then gate", "gate, then button"};

struct Tap {
	bool tapeSame = false, originalSame = false, overviewSame = false, overviewKeptOnTheWay = true, countersSame = false;
	bool playing = false, recordingAfter = true, empty = false;
	size_t tapeLength = 0;
	bool tapeIsOriginal = false;
	float overviewTop = 0.f;   // the highest column after
	float topAtEnd = 0.f;      // the highest column as the second edge comes
	float worstStep = 0.f;
	long n = 0;                // samples between the two edges
};

const double TAP_W_TAPE = 2 * PI * 100.0 / 48000.0, TAP_W_IN = 2 * PI * 50.0 / 48000.0;
// 1.5 x the largest step of the tape's sine and the input's together.
const float TAP_STEP_BOUND = (float) (1.5 * (2.0 * TAP_W_TAPE + 1.0 * TAP_W_IN));

Tap doubleTap(double ms, TapBy by, bool emptyTape = false) {
	Rig g;
	g.f.inRPatched = false;
	TapeSet* set = nullptr;
	if (emptyTape) {
		g.run(10);
		set = g.e.live();
	}
	else {
		set = g.sine(1.0, 100.0, 2.f, 0.0, 480);
		g.use(set);
	}
	g.press(&F::playPress);
	long t = 0;
	auto in = [&]() { return (float) std::sin(TAP_W_IN * (double) t); };
	// 10.25 turns of the sine: the press lands on its 2 V peak, where a
	// cut would be heard.
	for (; t < 4920; t++)
		g.step(in(), in());
	const Snapshot tape(set->tape), original(set->original);
	std::vector<float> lo, hi;
	for (int c = 0; c < rka::Overview::COLS; c++) {
		lo.push_back(set->overview.lo[c].load());
		hi.push_back(set->overview.hi[c].load());
	}
	auto overviewIsOld = [&]() {
		for (int c = 0; c < rka::Overview::COLS; c++)
			if (set->overview.lo[c].load() != lo[(size_t) c] || set->overview.hi[c].load() != hi[(size_t) c])
				return false;
		return true;
	};
	const uint32_t tc = g.e.tapeChanges(), oc = g.e.originalChanges();
	Tap r;
	r.n = std::lround(ms * 48.0);
	float prev = g.r.l;
	auto tick = [&](long k) {
		for (long i = 0; i < k; i++, t++) {
			g.step(in(), in());
			r.worstStep = std::max(r.worstStep, std::fabs(g.r.l - prev));
			prev = g.r.l;
			if (t % 16 == 0 && !overviewIsOld())
				r.overviewKeptOnTheWay = false;
		}
	};
	if (by == BUTTON_BUTTON || by == BUTTON_GATE)
		g.press(&F::recPress);
	else
		g.f.recGate = 10.f;
	if (by == BUTTON_GATE) {
		// The gate rises with the button's recording under way and falls
		// to end it.
		tick(1);
		g.f.recGate = 10.f;
		tick(r.n - 1);
	}
	else
		tick(r.n);
	for (int c = 0; c < rka::Overview::COLS; c++)
		r.topAtEnd = std::max(r.topAtEnd, set->overview.hi[c].load());
	if (by == BUTTON_BUTTON || by == GATE_BUTTON)
		g.press(&F::recPress);   // GATE_BUTTON: the gate stays up
	else
		g.f.recGate = 0.f;
	tick(48000);
	g.f.recGate = 0.f;
	tick(4800);
	TapeSet* s = g.e.live();
	r.tapeSame = s == set && tape.equals(set->tape);
	r.originalSame = s == set && original.equals(set->original);
	r.overviewSame = s == set && overviewIsOld();
	r.countersSame = g.e.tapeChanges() == tc && g.e.originalChanges() == oc;
	r.playing = g.r.playing;
	r.recordingAfter = g.r.recording;
	r.empty = g.r.empty;
	r.tapeLength = s->tape.length;
	r.tapeIsOriginal = sameTape(s->tape, s->original);
	for (int c = 0; c < rka::Overview::COLS; c++)
		r.overviewTop = std::max(r.overviewTop, s->overview.hi[c].load());
	return r;
}

// Everything as it was, still playing, and no click.
bool leftAlone(const Tap& t) {
	return t.tapeSame && t.originalSame && t.overviewSame && t.overviewKeptOnTheWay && t.countersSame && t.playing && !t.recordingAfter &&
	       t.worstStep <= TAP_STEP_BOUND;
}
// The recording is the tape: its length, the original the same after the
// copy, playing, and the window showing it (the 1 V input, not the 2 V
// tape before it).
bool stood(const Tap& t) {
	return t.tapeLength == (size_t) t.n && t.tapeIsOriginal && t.playing && !t.recordingAfter && t.overviewTop > 0.9f &&
	       t.overviewTop < 1.1f && t.worstStep <= TAP_STEP_BOUND;
}

void say(const char* by, double ms, const Tap& t) {
	std::printf("  REC by %s, %3.0f ms: %s (tape %s %zu frames, original %s, overview %s%s, counters %s, %s, step %.4f V of %.4f)\n", by, ms,
	            leftAlone(t) ? "left alone" : stood(t) ? "recorded" : "NEITHER", t.tapeSame ? "same," : "new,", t.tapeLength,
	            t.originalSame ? "same" : "new", t.overviewSame ? "same" : "new", t.overviewKeptOnTheWay ? "" : " on the way",
	            t.countersSame ? "still" : "moved", t.playing ? "playing" : "stopped", t.worstStep, TAP_STEP_BOUND);
}

// The button tapped twice N ms apart over a playing tape: under 0.3 s the
// tape, the original, the overview (on the way too), the counters and Play
// are as they were, with no click; from 0.3 s it is a recording.
void double_tap_button() {
	for (double ms : {20.0, 49.0, 51.0, 100.0, 200.0, 250.0, 299.0}) {
		const Tap t = doubleTap(ms, BUTTON_BUTTON);
		say("button", ms, t);
		CHECK(leftAlone(t), "button, %.0f ms: the tape left alone", ms);
	}
	for (double ms : {300.0, 301.0, 350.0}) {
		const Tap t = doubleTap(ms, BUTTON_BUTTON);
		say("button", ms, t);
		CHECK(stood(t), "button, %.0f ms: a recording (%zu frames)", ms, t.tapeLength);
		// From 0.3 s, while it records, the window shows it and not the
		// old tape (its 2 V peaks gone).
		if (ms > 300.0)
			CHECK(t.topAtEnd < 1.1f, "button, %.0f ms: the window shows the recording before it ends (top %.2f V)", ms, t.topAtEnd);
	}
}

// The gate keeps 50 ms; a recording the button starts and the gate ends, or
// the other way round, has the button's 0.3 s.
void double_tap_gate_and_mixed() {
	for (double ms : {40.0, 49.0}) {
		const Tap t = doubleTap(ms, GATE_GATE);
		CHECK(leftAlone(t), "gate, %.0f ms: thrown away", ms);
	}
	for (double ms : {51.0, 60.0, 200.0}) {
		const Tap t = doubleTap(ms, GATE_GATE);
		CHECK(stood(t), "gate, %.0f ms: a recording (%zu frames)", ms, t.tapeLength);
	}
	for (TapBy by : {BUTTON_GATE, GATE_BUTTON}) {
		for (double ms : {60.0, 200.0, 299.0}) {
			const Tap t = doubleTap(ms, by);
			CHECK(leftAlone(t), "%s, %.0f ms: thrown away (the button's 0.3 s)", TAP_NAMES[by], ms);
		}
		const Tap t = doubleTap(301.0, by);
		CHECK(stood(t), "%s, 301 ms: a recording", TAP_NAMES[by]);
	}
}

// An empty tape stays empty under 0.3 s by the button.
void double_tap_empty() {
	for (double ms : {100.0, 299.0}) {
		const Tap t = doubleTap(ms, BUTTON_BUTTON, true);
		CHECK(t.empty && t.tapeLength == 0 && t.tapeSame && t.originalSame && t.countersSame && !t.recordingAfter,
		      "an empty tape, button %.0f ms: still empty (%zu frames)", ms, t.tapeLength);
	}
	const Tap t = doubleTap(301.0, BUTTON_BUTTON, true);
	CHECK(!t.empty && t.tapeLength == (size_t) t.n && t.tapeIsOriginal, "an empty tape, button 301 ms: a recording (%zu frames)", t.tapeLength);
}

// A rate change while a recording the gate started is past the gate's
// 50 ms but still in the buffer (under 0.3 s): it ends as a recording, as
// one past its minimum always has. One the button started is thrown away.
void rate_change_in_buffer() {
	for (int button = 0; button < 2; button++) {
		Rig g;
		TapeSet* set = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		g.use(set);
		const Snapshot tape(set->tape), original(set->original);
		const std::vector<float> x = noiseVec(4800, 3.f, 23);
		if (button)
			g.press(&F::recPress);
		else
			g.f.recGate = 10.f;
		for (size_t i = 0; i < 4800; i++)
			g.step(x[i], x[i]);
		g.e.setSampleRate(44100.f);
		g.run(10);
		bool same = set->original.length == 4800;
		for (size_t k = 0; k < 4800 && same; k++)
			same = set->original.ch[0][k] == x[k];
		if (button)
			CHECK(!set->tapeStale && tape.equals(set->tape) && original.equals(set->original),
			      "the button's 100 ms, a rate change: thrown away, the set unchanged");
		else
			CHECK(same && set->tapeStale && tape.equals(set->tape), "the gate's 100 ms, a rate change: the original is the recording (%zu frames)",
			      set->original.length);
	}
}

void rec_short() {
	Rig g;
	TapeSet* set = g.sine(0.5, 100.0, 2.f, 0.0, 480);
	g.use(set);
	const Snapshot tape(set->tape), original(set->original);
	const uint32_t oc = g.e.originalChanges();
	g.press(&F::playPress);
	g.run(4800);
	g.press(&F::recPress);
	g.run(1920);   // 40 ms
	CHECK(g.r.recording, "recording");
	g.press(&F::recPress);
	g.run(10);
	CHECK(!g.r.recording && g.r.playing, "40 ms: the recording is over and Play is on");
	const float loud = peakOver(g, 4800);
	CHECK(tape.equals(set->tape) && original.equals(set->original), "40 ms: thrown away, the tape and original before it intact");
	CHECK(g.copies == 0 && g.e.originalChanges() == oc, "nothing was copied or changed (%ld copies)", g.copies);
	CHECK(loud > 1.5f, "the tape plays again (peak %.2f V)", loud);
	CHECK(g.e.live() == set, "the same set");
}

void rec_full() {
	// At 2 kHz the 5 minutes are 600 000 frames.
	Rig g(2000.f);
	g.f.inRPatched = true;
	const std::vector<float> x = noiseVec(600100, 3.f, 7);
	g.run(10);
	const size_t cap = g.e.live()->original.capacity();
	CHECK(cap == 600000, "capacity %zu frames at 2 kHz", cap);
	g.press(&F::recPress);
	long ended = -1;
	for (long i = 0; i < 600050; i++) {
		g.step(x[(size_t) i], -x[(size_t) i]);
		if (ended < 0 && !g.r.recording)
			ended = i;
	}
	// The sample that records frame C - 1 ends it.
	CHECK(ended == 599999, "Rec ends by itself at capacity (on sample %ld)", ended);
	g.run(100);
	const TapeSet* s = g.e.live();
	CHECK(s->tape.length == cap && s->tape.ch[0][599999] == x[599999] && s->tape.ch[1][12345] == -x[12345],
	      "the tape is the whole recording (%zu frames)", s->tape.length);
	CHECK(g.r.playing && peakOver(g, 200) > 1.f, "and plays");
}

void rec_tail() {
	Rig g;
	g.f.inRPatched = true;
	// 0.4 s: a recording the button starts and ends stands from 0.3 s.
	const std::vector<float> x = noiseVec(22000, 4.f, 8), y = noiseVec(22000, 4.f, 9);
	const long N = 19200;
	g.press(&F::recPress);
	for (long i = 0; i < 21000; i++) {
		if (i == N)
			g.press(&F::recPress);
		g.step(x[(size_t) i], y[(size_t) i]);
	}
	const Tape& t = g.e.live()->tape;
	bool body = t.length == (size_t) N, tail = t.tail == 480;
	for (size_t k = 0; k < (size_t) N + 480 && body && tail; k++) {
		const bool ok = t.ch[0][k] == x[k] && t.ch[1][k] == y[k];
		(k < (size_t) N ? body : tail) = ok;
	}
	CHECK(body, "the recording is on the tape, %zu frames", t.length);
	CHECK(tail, "the m = 480 frames of input after the end are its tail (tail %zu)", t.tail);
	CHECK(sameTape(g.e.live()->tape, g.e.live()->original), "and the original is the same");
}

void rec_mono() {
	for (int patched = 0; patched < 2; patched++) {
		Rig g;
		g.f.inRPatched = patched != 0;
		const std::vector<float> x = noiseVec(20400, 4.f, 10);
		g.press(&F::recPress);
		for (long i = 0; i < 20400; i++) {
			if (i == 19200)
				g.press(&F::recPress);
			g.step(x[(size_t) i], 3.3f);
		}
		const Tape& t = g.e.live()->tape;
		bool ok = t.length == 19200;
		for (size_t k = 0; k < 19200 && ok; k++)
			ok = t.ch[0][k] == x[k] && t.ch[1][k] == (patched ? 3.3f : x[k]);
		CHECK(ok, patched ? "IN R patched: the sides are IN L and IN R" : "IN R unpatched: both sides are IN L");
	}
}

void rec_silences_old() {
	Rig g;
	g.use(g.sine(0.5, 100.0, 4.f, 0.0, 480));
	g.press(&F::playPress);
	const float before = peakOver(g, 4800);
	g.press(&F::recPress);
	g.run(481);
	const float during = peakOver(g, 9600);
	CHECK(before > 3.f && during == 0.f, "recording silence, the old tape falls silent within F (%.2f V, then %g)", before, during);
}

void rec_then_play() {
	for (int loop = 0; loop < 2; loop++) {
		Rig g;
		g.f.loop = loop != 0;
		CHECK(!g.r.playing, "Play off");
		const std::vector<float> x = noiseVec(19200 + 480, 3.f, 12);
		g.press(&F::recPress);
		for (long i = 0; i < 19200 + 480; i++) {
			if (i == 19200)
				g.press(&F::recPress);
			g.step(x[(size_t) i], x[(size_t) i]);
		}
		CHECK(g.r.playing, "Loop %s: Play comes on when the recording ends", loop ? "on" : "off");
		long edges = 0;
		bool was = g.r.eoc;
		for (long i = 0; i < 4 * 19200; i++) {
			g.step();
			edges += g.r.eoc && !was;
			was = g.r.eoc;
		}
		CHECK(loop ? edges >= 3 && g.r.playing : edges == 1 && !g.r.playing, "Loop %s: %ld turns, then Play %s", loop ? "on" : "off",
		      edges, g.r.playing ? "on" : "off");
	}
}

// Both sides code the place: (sin, cos) of a turn of the tape.
TapeSet* placeCoded(Rig& g, size_t n) {
	return g.loaded(n, 0, [=](size_t k, float& l, float& r) {
		l = (float) (4.0 * std::sin(2 * PI * (double) k / (double) n));
		r = (float) (4.0 * std::cos(2 * PI * (double) k / (double) n));
	});
}

void load_keeps_play() {
	{
		Rig g;
		g.use(g.sine(0.5, 100.0, 2.f, 0.0, 480));
		g.press(&F::playPress);
		g.run(10000);
		TapeSet* b = placeCoded(g, 48000);
		CHECK(g.e.offer(b), "offered");
		g.step();
		CHECK(g.e.live() == b && g.r.playing && g.r.head * 48000 < 2.0, "playing: the new tape plays from Start (head %.1f)",
		      g.r.head * 48000);
		g.run(1000);
		double p = std::atan2((double) g.r.l, (double) g.r.r) / (2 * PI) * 48000;
		CHECK(std::fabs(p - 1001) < 2.0, "and is heard from there (%.1f)", p);
	}
	{
		Rig g;
		g.use(g.sine(0.5, 100.0, 2.f, 0.0, 480));
		g.press(&F::playPress);
		g.run(10000);
		g.press(&F::playPress);
		g.run(1000);
		g.use(placeCoded(g, 48000));
		CHECK(!g.r.playing && peakOver(g, 4800) == 0.f, "stopped: a new tape stays stopped and silent");
	}
}

void load_ends_rec_and_dub() {
	{
		Rig g;
		g.press(&F::recPress);
		g.run(4800);
		CHECK(g.r.recording, "recording");
		TapeSet* b = g.sine(0.5, 100.0, 2.f);
		g.use(b);
		CHECK(!g.r.recording && g.e.live() == b, "a set arriving ends Rec");
		g.run(1000);
		CHECK(g.collected.size() == 1, "and the one recorded into is retired");
	}
	{
		Rig g;
		g.use(g.sine(0.5, 100.0, 2.f));
		g.press(&F::playPress);
		g.press(&F::dubPress);
		g.run(4800);
		CHECK(g.r.dubbing, "dubbing");
		g.use(g.sine(0.5, 100.0, 2.f));
		CHECK(!g.r.dubbing, "a set arriving ends Dub");
	}
}

// Silent, Dub and Restore ignored, empty. (Rec records into an empty set;
// with no set at all it is ignored too.)
void expectEmpty(Rig& g, const char* what, bool noSet) {
	// Mix 1 (the tape alone), settled: what is heard is the tape.
	g.f.params[F::MIX] = 1.f;
	g.run(960);
	if (noSet) {
		g.press(&F::recPress);
		g.step(3.f, 3.f);
		CHECK(!g.r.recording, "%s: Rec is ignored", what);
	}
	g.press(&F::dubPress);
	g.step(3.f, 3.f);
	CHECK(!g.r.dubbing, "%s: Dub is ignored", what);
	g.press(&F::restorePress);
	g.run(100);
	CHECK(g.e.copyWanted() == EddyEngine::NONE && g.copies == 0, "%s: Restore is ignored", what);
	g.press(&F::playPress);
	CHECK(peakOver(g, 4800, 3.f) == 0.f && g.r.empty, "%s: silent, and empty", what);
}

void empty() {
	{
		Rig g(48000.f, 1, false);
		g.run(10);
		expectEmpty(g, "no set", true);
		CHECK(g.r.empty, "no set: empty");
	}
	{
		Rig g;
		g.run(10);
		expectEmpty(g, "an empty set", false);
	}
	{
		Rig g;
		TapeSet* set = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		g.use(set);
		g.press(&F::playPress);
		g.run(4800);
		CHECK(!g.r.empty && peakOver(g, 4800) > 1.f, "a tape plays");
		g.e.clear();
		g.run(960);
		CHECK(set->tape.length == 0 && set->original.length == 0 && !g.r.playing, "cleared: the tape is emptied, Play off");
		expectEmpty(g, "cleared", false);
	}
}

void rate_change() {
	Rig g;
	TapeSet* a = g.sine(0.5, 100.0, 2.f, 0.0, 480);
	g.use(a);
	g.press(&F::playPress);
	g.run(4800);
	g.e.setSampleRate(44100.f);
	g.step();
	CHECK(g.r.l == 0.f && g.r.r == 0.f, "a rate change: silence at once");
	g.run(10);
	CHECK(g.collected.count(a) == 1 && g.e.live() == nullptr, "the old set comes back");
	CHECK(peakOver(g, 4800) == 0.f, "silent until a set at the new rate arrives");
	TapeSet* old = g.sine(0.5, 100.0, 2.f);   // made at 48 kHz
	CHECK(g.e.offer(old), "a set at the old rate is offered");
	bool neverLive = true;
	for (int i = 0; i < 10; i++) {
		g.step();
		neverLive = neverLive && g.e.live() != old;
	}
	CHECK(neverLive && g.collected.count(old) == 1 && g.e.live() == nullptr, "and comes straight back, never taken");
	TapeSet* b = g.make(44100.f);
	for (size_t k = 0; k < 22050; k++)
		b->tape.ch[0][k] = b->tape.ch[1][k] = b->original.ch[0][k] = b->original.ch[1][k] =
		    (float) (2.0 * std::sin(2 * PI * 100.0 * k / 44100.0));
	b->tape.length = b->original.length = 22050;
	g.use(b);
	CHECK(g.r.playing && peakOver(g, 4410) > 1.5f, "a set at the new rate plays");
}

void handoff() {
	Rig g;
	g.copySamples = 2000;
	std::vector<TapeSet*> offered(g.made);   // the rig's empty set
	auto offer = [&](TapeSet* s) {
		offered.push_back(s);
		g.use(s);
	};
	// During Rec.
	g.press(&F::recPress);
	g.run(4800);
	offer(g.sine(0.5, 100.0, 2.f, 0.0, 480));
	CHECK(!g.r.recording, "taken during Rec");
	// During Dub.
	g.press(&F::playPress);
	g.press(&F::dubPress);
	g.run(4800);
	offer(g.sine(0.5, 100.0, 2.f, 0.0, 480));
	CHECK(!g.r.dubbing, "taken during Dub");
	// During a copy: Restore, then a set offered while the worker copies.
	TapeSet* during = g.e.live();
	g.press(&F::restorePress);
	while (g.copying == EddyEngine::NONE)
		g.step();
	TapeSet* d = g.sine(0.5, 100.0, 2.f);
	offered.push_back(d);
	CHECK(g.e.offer(d), "offered while a copy runs");
	g.run(1000);
	CHECK(g.e.live() == during && g.copying != EddyEngine::NONE, "not taken while the copy runs");
	g.run(1100);
	CHECK(g.e.live() == d && g.tapeMatchedAtDone, "taken once it is done");
	// Two offers running: the second waits for the first to be taken.
	TapeSet* e1 = g.sine(0.5, 100.0, 2.f);
	TapeSet* e2 = g.sine(0.5, 100.0, 2.f);
	offered.push_back(e1);
	offered.push_back(e2);
	CHECK(g.e.offer(e1), "the first offer");
	long waited = 0;
	while (g.e.live() != e1 && waited < 2000) {
		CHECK(!g.e.offer(e2), "the second is refused while the first waits");
		g.step();
		waited++;
	}
	// (It waits for the last set's voices to fade, F.)
	CHECK(g.e.live() == e1 && waited <= 481 && g.e.offer(e2), "taken after %ld samples, the second is accepted", waited);
	g.run(1000);
	CHECK(g.e.live() == e2, "and taken in turn");
	// A copy that makes the original, and a set arriving behind it.
	g.e.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
	while (g.copying == EddyEngine::NONE)
		g.step();
	TapeSet* f2 = g.sine(0.5, 100.0, 2.f);
	offered.push_back(f2);
	CHECK(g.e.offer(f2), "offered during the second copy");
	g.run(3000);
	CHECK(g.e.live() == f2, "taken after it");
	// The end: a rate change retires the last.
	g.e.setSampleRate(96000.f);
	g.run(1000);
	bool once = g.collected.size() == offered.size();
	for (TapeSet* s : offered)
		once = once && g.collected.count(s) == 1;
	CHECK(once, "every set offered came back exactly once (%zu offered, %zu collected)", offered.size(), g.collected.size());
}

// The storm: all four triggers and both gates as 5 kHz squares, the buttons
// pressed on their edges, a copy asked for now and then, for 10 s.
void storm() {
	Rig g;
	g.copySamples = 300;
	TapeSet* set = g.sine(1.0, 100.0, 4.f, 0.0, 480);
	g.use(set);
	const Snapshot original(set->original);
	g.press(&F::playPress);
	g.run(4800);
	int most = 0;
	bool finite = true, bounded = true;
	const long n = 10 * 48000;
	auto square = [](long i, int phase) { return ((i * 10 + phase) / 48) % 2 == 1; };
	bool last[6] = {};
	for (long i = 0; i < n; i++) {
		bool sq[6];
		for (int k = 0; k < 6; k++)
			sq[k] = square(i, 7 * k);
		g.f.recGate = sq[0] ? 10.f : 0.f;
		g.f.dubGate = sq[1] ? 10.f : 0.f;
		g.f.playTrig = sq[2] ? 10.f : 0.f;
		g.f.restoreTrig = sq[3] ? 10.f : 0.f;
		g.f.recPress = sq[4] && !last[4];
		g.f.dubPress = sq[5] && !last[5];
		g.f.playPress = sq[2] && !last[2];
		g.f.restorePress = sq[3] && !last[3];
		for (int k = 0; k < 6; k++)
			last[k] = sq[k];
		if (i % 4000 == 0)
			g.e.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
		g.step(2.f * (float) std::sin(i * 0.01), 0.f);
		most = std::max(most, g.e.activeVoices());
		finite = finite && std::isfinite(g.r.l) && std::isfinite(g.r.r);
		bounded = bounded && std::fabs(g.r.l) <= 10.f && std::fabs(g.r.r) <= 10.f;
	}
	CHECK(most <= 4, "never more than 4 voices (%d)", most);
	CHECK(finite && bounded, "the output stays finite and under the limit");
	CHECK(!g.wantedChangedMidCopy, "at most one copy wanted at a time: none changed while the worker held it");
	CHECK(g.copies > 10, "copies were made during the storm (%ld)", g.copies);
	// The last edges: everything low (Rec and Dub off), then a Restore and
	// a PLAY trigger.
	g.f.recGate = g.f.dubGate = g.f.playTrig = g.f.restoreTrig = 0.f;
	g.run(100);
	g.f.restoreTrig = 10.f;
	g.run(10);
	g.f.restoreTrig = 0.f;
	g.run(100);
	g.f.playTrig = 10.f;
	g.run(10);
	g.f.playTrig = 0.f;
	g.run(48000);
	CHECK(!g.r.recording && !g.r.dubbing && g.r.playing, "a second later: not recording, not dubbing, playing");
	CHECK(g.e.copyWanted() == EddyEngine::NONE && g.copying == EddyEngine::NONE, "no copy wanted");
	CHECK(sameTape(set->tape, set->original), "restored: the tape is the original");
	CHECK(original.equals(set->original), "and the tape survived the storm (no recording stood)");
	// One voice, two only through the join (m = 480 samples a turn).
	long two = 0, none = 0;
	float peak = 0;
	for (int i = 0; i < 48000; i++) {
		g.step();
		two += g.e.activeVoices() > 1;
		none += g.e.activeVoices() == 0;
		peak = std::max(peak, std::fabs(g.r.l));
	}
	CHECK(two <= 481 && none == 0 && peak > 3.f, "settled: two voices for %ld samples a turn, none for %ld; peak %.2f V", two, none,
	      peak);
}

void counters() {
	Rig g;
	TapeSet* set = g.sine(0.5, 100.0, 2.f, 0.0, 480);
	g.use(set);
	uint32_t tc = g.e.tapeChanges(), oc = g.e.originalChanges();
	g.press(&F::playPress);
	g.run(48000);
	CHECK(g.e.tapeChanges() == tc && g.e.originalChanges() == oc, "playing at Wear 0, Dub off: neither moves");
	g.f.params[F::WEAR] = 0.5f;
	g.run(4800);
	CHECK(g.e.tapeChanges() != tc && g.e.originalChanges() == oc, "Wear: the tape's counter moves, the original's not");
	g.f.params[F::WEAR] = 0.f;
	g.run(4800);
	tc = g.e.tapeChanges();
	g.run(4800);
	CHECK(g.e.tapeChanges() == tc, "Wear back at 0: still");
	g.press(&F::dubPress);
	g.run(4800);
	CHECK(g.e.tapeChanges() != tc && g.e.originalChanges() == oc, "Dub: the tape's moves, the original's not");
	g.press(&F::dubPress);
	g.run(960);
	g.press(&F::restorePress);
	g.run(4800);
	CHECK(g.e.originalChanges() == oc, "Restore: the original's does not move");
	g.e.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
	g.run(4800);
	CHECK(g.e.originalChanges() != oc, "a tape-to-original copy moves it");
	oc = g.e.originalChanges();
	g.press(&F::recPress);
	g.run(19200);   // past the button's 0.3 s
	CHECK(g.e.originalChanges() == oc, "not while recording");
	g.press(&F::recPress);
	g.run(4800);
	CHECK(g.e.originalChanges() != oc, "a recording moves it");
	oc = g.e.originalChanges();
	g.use(g.sine(0.5, 100.0, 2.f));
	CHECK(g.e.originalChanges() != oc, "an offer moves it");
}

// readsOriginal(): from a recording's end until its copy to the tape is
// done (the original counter has moved and the tape is not yet it), and
// while a Restore waits or is under way; not otherwise.
void reads_original() {
	Rig g;
	g.copySamples = 2000;
	g.use(g.sine(0.5, 100.0, 2.f));
	g.run(100);
	CHECK(!g.e.readsOriginal(), "a loaded tape: reads the tape");
	const uint32_t oc = g.e.originalChanges();
	g.press(&F::recPress);
	g.run(19200);   // past the button's 0.3 s
	CHECK(!g.e.readsOriginal(), "while recording");
	g.press(&F::recPress);
	long first = -1, last = -1;
	bool movedFirst = false;
	for (long i = 0; i < 20000; i++) {
		g.step();
		if (g.e.readsOriginal()) {
			if (first < 0)
				first = i;
			last = i;
		}
		if (g.e.originalChanges() != oc && first < 0)
			movedFirst = true;   // the counter moved while the tape was not marked behind
	}
	CHECK(first >= 0 && !movedFirst, "after the recording's end, from before its counter moves (first %ld)", first);
	CHECK(last >= 0 && !g.e.readsOriginal() && g.copies > 0, "until the copy is done (last %ld, copies %ld)", last, g.copies);
	g.e.requestCopy(EddyEngine::ORIGINAL_TO_TAPE);
	g.run(1);
	bool during = false;
	for (int i = 0; i < 3000; i++) {
		g.step();
		during = during || g.e.readsOriginal();
	}
	CHECK(during && !g.e.readsOriginal(), "a Restore: while it waits or is under way, then not");
	g.e.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
	bool make = false;
	for (int i = 0; i < 3000; i++) {
		g.step();
		make = make || g.e.readsOriginal();
	}
	CHECK(!make, "Make this the original: the tape is what is heard");
}

// A rate change in the middle of a recording: the recording ends as one (the
// original is what was recorded, at its length), the set is retired flagged
// so the worker copies it to the tape, and the original's counter moves.
// Under its minimum it is thrown away as usual.
void rate_change_during_rec() {
	{
		Rig g;
		g.f.inRPatched = true;
		TapeSet* set = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		g.use(set);
		const Snapshot tape(set->tape);
		const std::vector<float> x = noiseVec(19200, 3.f, 21), y = noiseVec(19200, 3.f, 22);
		const uint32_t oc = g.e.originalChanges();
		g.press(&F::recPress);
		for (size_t i = 0; i < 19200; i++)
			g.step(x[i], y[i]);
		g.e.setSampleRate(44100.f);
		g.run(10);
		CHECK(g.collected.count(set) == 1, "the set is retired");
		bool same = set->original.length == 19200;
		for (size_t k = 0; k < 19200 && same; k++)
			same = set->original.ch[0][k] == x[k] && set->original.ch[1][k] == y[k];
		CHECK(same, "its original is the recording, %zu frames (want 19200)", set->original.length);
		CHECK(set->tapeStale && tape.equals(set->tape), "flagged: the tape is the one before, to be copied from the original");
		CHECK(g.e.originalChanges() != oc, "the original's counter moved");
	}
	{
		Rig g;
		TapeSet* set = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		g.use(set);
		const Snapshot tape(set->tape), original(set->original);
		const uint32_t oc = g.e.originalChanges();
		g.press(&F::recPress);
		g.run(1000);
		g.e.setSampleRate(44100.f);
		g.run(10);
		CHECK(g.collected.count(set) == 1 && !set->tapeStale && tape.equals(set->tape) && original.equals(set->original) &&
		          g.e.originalChanges() == oc,
		      "under its minimum: thrown away, the set unchanged and not flagged");
	}
}

// clear() and offer(): the last request wins.
void clear_orders() {
	// A clear() after an offer() that waits behind a copy: the set is taken
	// emptied.
	{
		Rig g;
		g.copySamples = 2000;
		g.use(g.sine(0.5, 100.0, 2.f, 0.0, 480));
		g.press(&F::playPress);
		g.run(4800);
		g.press(&F::restorePress);
		while (g.copying == EddyEngine::NONE)
			g.step();
		TapeSet* b = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		CHECK(g.e.offer(b), "offered during a copy");
		g.run(10);
		g.e.clear();
		g.run(4000);
		CHECK(g.e.live() == b && b->tape.length == 0 && b->original.length == 0 && g.r.empty && !g.r.playing,
		      "clear() after offer(): the set is taken emptied (%zu frames, %s)", b->tape.length, g.r.playing ? "playing" : "stopped");
	}
	// An offer() after a clear(), both before the engine's next sample: the
	// set is taken as it is, and plays.
	{
		Rig g;
		g.use(g.sine(0.5, 100.0, 2.f, 0.0, 480));
		g.press(&F::playPress);
		g.run(4800);
		TapeSet* b = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		g.e.clear();
		CHECK(g.e.offer(b), "offered");
		g.run(2000);
		CHECK(g.e.live() == b && b->tape.length == 24000 && g.r.playing && peakOver(g, 4800) > 1.5f,
		      "offer() after clear(): the set plays (%zu frames)", b->tape.length);
	}
	// clear() with no live set (after a rate change), then the next set taken
	// was offered before it: emptied. One offered after it: plays.
	{
		Rig g;
		g.use(g.sine(0.5, 100.0, 2.f, 0.0, 480));
		g.e.setSampleRate(44100.f);
		g.run(10);
		CHECK(g.e.live() == nullptr, "no live set");
		TapeSet* b = g.make(44100.f);
		b->tape.length = b->original.length = 22050;
		g.e.clear();
		g.run(100);
		CHECK(g.e.offer(b), "offered after the clear");
		g.run(100);
		CHECK(g.e.live() == b && b->tape.length == 22050, "a clear() with no set is superseded by a later offer()");
		g.e.setSampleRate(48000.f);
		g.run(10);
		CHECK(g.e.live() == nullptr, "no live set again");
		TapeSet* c = g.make(44100.f);   // at the wrong rate
		c->tape.length = c->original.length = 22050;
		TapeSet* e = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		TapeSet* d = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		g.e.clear();   // remembered: no set
		g.run(100);
		// A later offer() supersedes the waiting clear, even one the engine
		// refuses for its rate: that set comes back, and the next set taken
		// is not emptied.
		CHECK(g.e.offer(c), "a set at the wrong rate offered after the clear");
		g.run(10);
		CHECK(g.collected.count(c) == 1 && c->tape.length == 22050, "it comes back, untouched");
		CHECK(g.e.offer(e), "offered");
		g.run(100);
		CHECK(g.e.live() == e && e->tape.length == 24000, "the clear was superseded: the next set is taken intact (%zu frames)",
		      e->tape.length);
		g.e.setSampleRate(44100.f);
		g.run(10);
		g.e.setSampleRate(48000.f);
		g.step();
		// Offered before a second clear(): that clear applies to it.
		CHECK(g.e.offer(d), "offered");
		g.e.clear();
		g.run(2000);
		CHECK(g.e.live() == d && d->tape.length == 0, "a clear() after the offer() empties the set taken (%zu frames)", d->tape.length);
	}
}

// clear() while a rate change retires the set: a clear() with no usable set
// is remembered for the next set taken, unless a later offer() supersedes
// it. Both orders; every set offered comes back exactly once; no crash.
void clear_during_rate_change() {
	for (int order = 0; order < 3; order++) {
		Rig g;
		std::vector<TapeSet*> offered(g.made);
		TapeSet* a = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		offered.push_back(a);
		g.use(a);
		g.press(&F::playPress);
		g.run(4800);
		TapeSet* b = g.make(44100.f);
		for (size_t k = 0; k < 22050; k++)
			b->tape.ch[0][k] = b->tape.ch[1][k] = b->original.ch[0][k] = b->original.ch[1][k] =
			    (float) (2.0 * std::sin(2 * PI * 100.0 * k / 44100.0));
		b->tape.length = b->original.length = 22050;
		offered.push_back(b);
		g.e.setSampleRate(44100.f);
		if (order == 0) {
			// The clear first, before the same sample as the rate change (no
			// usable set: the live one is at the old rate and on its way
			// out); samples pass; then the offer: it wins and the set plays.
			g.e.clear();
			g.run(1000);
			CHECK(g.e.offer(b), "offered");
			g.run(4800);
			CHECK(g.e.live() == b && b->tape.length == 22050 && g.r.playing && peakOver(g, 4410) > 1.5f,
			      "clear() then offer() during a rate change: the set plays (%zu frames)", b->tape.length);
		}
		else if (order == 1) {
			// The offer first, then the clear: the set is taken emptied.
			CHECK(g.e.offer(b), "offered");
			g.e.clear();
			g.run(4800);
			CHECK(g.e.live() == b && b->tape.length == 0 && g.r.empty,
			      "offer() then clear() during a rate change: the set is taken emptied (%zu frames)", b->tape.length);
		}
		else {
			// A clear under way on the live set when the rate changes (its
			// voices still fading): it stops there, the old set comes back
			// as it was, and a set offered next plays.
			const Snapshot before(a->tape);
			g.e.setSampleRate(48000.f);   // undo the change above before the engine sees it
			g.e.clear();
			g.run(100);
			g.e.setSampleRate(44100.f);
			g.run(1000);
			CHECK(g.collected.count(a) == 1 && before.equals(a->tape), "the old set came back, not emptied");
			CHECK(g.e.offer(b), "offered");
			g.press(&F::playPress);
			g.run(4800);
			CHECK(g.e.live() == b && b->tape.length == 22050 && g.r.playing && peakOver(g, 4410) > 1.5f,
			      "a clear under way when the rate changed: the next set plays (%zu frames)", b->tape.length);
		}
		CHECK(g.collected.count(a) == 1, "the old set came back");
		g.e.setSampleRate(96000.f);
		g.run(1000);
		bool once = g.collected.size() == offered.size();
		for (TapeSet* s : offered)
			once = once && g.collected.count(s) == 1;
		CHECK(once, "order %d: every set offered came back exactly once (%zu offered, %zu collected)", order, offered.size(),
		      g.collected.size());
	}
}

// A Restore copy in flight, a rate change (the old set waits for the copy),
// then clear() while that set is still live: the clear waits for the next
// set taken. A set offered after it plays; one offered before it is emptied.
void clear_with_copy_in_flight_then_rate_change() {
	for (int order = 0; order < 2; order++) {
		Rig g;
		g.copySamples = 2000;
		std::vector<TapeSet*> offered(g.made);
		TapeSet* a = g.sine(0.5, 100.0, 2.f, 0.0, 480);
		offered.push_back(a);
		g.use(a);
		g.press(&F::playPress);
		g.run(4800);
		g.press(&F::restorePress);
		while (g.copying == EddyEngine::NONE)
			g.step();
		g.e.setSampleRate(44100.f);
		g.run(100);
		CHECK(g.e.live() == a, "the old set waits for its copy");
		TapeSet* b = g.make(44100.f);
		b->tape.length = b->original.length = 22050;
		offered.push_back(b);
		if (order == 0) {
			// The reviewer's probe: nothing offered until the old set has
			// gone back (its copy done), so for a while there is no set.
			g.e.clear();
			g.run(4000);
			CHECK(g.e.live() == nullptr && g.collected.count(a) == 1, "the old set came back after its copy, none live");
			CHECK(g.e.offer(b), "offered after the clear");
		}
		else {
			CHECK(g.e.offer(b), "offered before the clear");
			g.e.clear();
		}
		g.run(4000);
		CHECK(g.collected.count(a) == 1 && g.e.live() == b, "the old set came back after its copy; the new one is live");
		CHECK(order == 0 ? b->tape.length == 22050 : b->tape.length == 0,
		      order == 0 ? "clear() then offer(): the set is taken intact (%zu frames)"
		                 : "offer() then clear(): the set is taken emptied (%zu frames)",
		      b->tape.length);
		g.e.setSampleRate(96000.f);
		g.run(1000);
		bool once = g.collected.size() == offered.size();
		for (TapeSet* s : offered)
			once = once && g.collected.count(s) == 1;
		CHECK(once, "order %d: every set offered came back exactly once (%zu offered, %zu collected)", order, offered.size(),
		      g.collected.size());
	}
}

} // namespace

int main() {
	rec_short();
	rec_full();
	rec_tail();
	rec_mono();
	rec_silences_old();
	rec_then_play();
	load_keeps_play();
	load_ends_rec_and_dub();
	empty();
	rate_change();
	rate_change_during_rec();
	clear_orders();
	clear_during_rate_change();
	clear_with_copy_in_flight_then_rate_change();
	handoff();
	storm();
	counters();
	reads_original();
	double_tap_button();
	double_tap_gate_and_mixed();
	double_tap_empty();
	rate_change_in_buffer();
	return rkatest::finish("eddy test_transport");
}
