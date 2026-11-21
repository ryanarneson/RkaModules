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
	const std::vector<float> x = noiseVec(8000, 4.f, 8), y = noiseVec(8000, 4.f, 9);
	const long N = 4800;
	g.press(&F::recPress);
	for (long i = 0; i < 7000; i++) {
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
		const std::vector<float> x = noiseVec(6000, 4.f, 10);
		g.press(&F::recPress);
		for (long i = 0; i < 6000; i++) {
			if (i == 4800)
				g.press(&F::recPress);
			g.step(x[(size_t) i], 3.3f);
		}
		const Tape& t = g.e.live()->tape;
		bool ok = t.length == 4800;
		for (size_t k = 0; k < 4800 && ok; k++)
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
		const std::vector<float> x = noiseVec(12000, 3.f, 12);
		g.press(&F::recPress);
		for (long i = 0; i < 9600 + 480; i++) {
			if (i == 9600)
				g.press(&F::recPress);
			g.step(x[(size_t) i], x[(size_t) i]);
		}
		CHECK(g.r.playing, "Loop %s: Play comes on when the recording ends", loop ? "on" : "off");
		long edges = 0;
		bool was = g.r.eoc;
		for (long i = 0; i < 4 * 9600; i++) {
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
	g.f.params[F::MIX] = 1.f;
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
	g.run(10);
	CHECK(g.collected.count(old) == 1 && g.e.live() == nullptr, "and comes straight back");
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
	g.run(4800);
	CHECK(g.e.originalChanges() == oc, "not while recording");
	g.press(&F::recPress);
	g.run(4800);
	CHECK(g.e.originalChanges() != oc, "a recording moves it");
	oc = g.e.originalChanges();
	g.use(g.sine(0.5, 100.0, 2.f));
	CHECK(g.e.originalChanges() != oc, "an offer moves it");
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
	handoff();
	storm();
	counters();
	return rkatest::finish("eddy test_transport");
}
