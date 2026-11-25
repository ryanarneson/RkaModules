// One Beacon voice (src/beacon/Voice.hpp): stage times, levels, part-way
// starts, ADSR / AR / Hold, RETRIG, the stage gates and TRIG, mode
// switches and hostile input, against the plan's Global Constraints.
#include "beacon/Voice.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace rka::beacon;

const char* stageName(Stage s) {
	static const char* n[] = {"IDLE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
	return (s >= IDLE && s <= RELEASE) ? n[s] : "?";
}

struct Rig {
	Voice v;
	VoiceIn in{};
	VoiceOut out{};
	float fs;
	float prev = 0.f;      // OUT on the sample before the last
	float maxStep = 0.f;   // largest |OUT step| since clearStep()
	Rig(float fs_, float a, float d, float s, float r, float shape, Mode mode = GATE_MODE) : fs(fs_) {
		v.setSampleRate(fs);
		v.reset();
		in.attack = a;
		in.decay = d;
		in.sustain = s;
		in.release = r;
		in.shape = shape;
		in.gate = false;
		in.retrig = false;
		in.mode = mode;
	}
	void step() {
		prev = v.level();
		v.process(in, out);
		const float d = std::fabs(out.level - prev);
		if (d > maxStep)
			maxStep = d;
	}
	void run(long n) {
		for (long i = 0; i < n; i++)
			step();
	}
	void clearStep() { maxStep = 0.f; }
	long samples(double seconds) const { return long(std::lround(seconds * fs)); }
};

// Counts the samples the voice stays in stage s, starting with the current
// one (the last step must already have entered s); stops after cap samples.
// On return the voice has taken one step into the next stage. lastLevel is
// OUT on the stage's last sample; phaseOk is false if the phase ever went
// backwards or past 1.
long runStage(Rig& r, Stage s, long cap, float* lastLevel = nullptr, bool* phaseOk = nullptr) {
	long n = 0;
	double lastP = -1.0;
	while (r.v.stage() == s && n < cap) {
		n++;
		const double p = r.v.phase();
		if (phaseOk && (p < lastP || p > 1.0 || p < 0.0))
			*phaseOk = false;
		lastP = p;
		if (lastLevel)
			*lastLevel = r.out.level;
		r.step();
	}
	return n;
}

// The point of Attack's full curve (0 to 1 over the stage, in this Shape)
// that is at `frac` of the way to 10 V: bisection on the curve itself, not
// the voice's own inverse.
double curvePoint(double frac, float shape) {
	double lo = 0.0, hi = 1.0;
	for (int i = 0; i < 60; i++) {
		const double mid = 0.5 * (lo + hi);
		(curveRise(float(mid), shape) < frac ? lo : hi) = mid;
	}
	return 0.5 * (lo + hi);
}

long capFor(double t, float fs) {
	return long(t * fs * 1.01) + 16;
}

// ---------------------------------------------------------------------------

void exact_times() {
	struct Case { float fs; float t; };
	std::vector<Case> cases;
	for (float fs : {22050.f, 44100.f, 48000.f, 96000.f, 768000.f})
		for (float t : {0.001f, 0.01f, 1.f})
			if (fs != 22050.f || t == 0.001f)   // the 1 ms floor below 44.1 kHz too
				cases.push_back({fs, t});
	cases.push_back({48000.f, 60.f});
	for (const Case& c : cases) {
		for (float shape : {0.f, 0.5f, 1.f}) {
			Rig r(c.fs, c.t, c.t, 5.f, c.t, shape);
			const double want = double(c.t) * c.fs;
			const long cap = capFor(c.t, c.fs);
			bool phaseOk = true;
			r.in.gate = true;
			r.step();
			const long na = runStage(r, ATTACK, cap, nullptr, &phaseOk);
			const long nd = runStage(r, DECAY, cap, nullptr, &phaseOk);
			CHECK(r.v.stage() == SUSTAIN, "fs %g T %g shape %g: after Decay the stage is %s", c.fs, c.t, shape,
			      stageName(r.v.stage()));
			r.run(4);
			r.in.gate = false;
			r.step();
			const long nr = runStage(r, RELEASE, cap, nullptr, &phaseOk);
			CHECK(r.v.stage() == IDLE, "fs %g T %g shape %g: after Release the stage is %s", c.fs, c.t, shape,
			      stageName(r.v.stage()));
			CHECK(std::fabs(na - want) <= 1.0, "fs %g T %g shape %g: Attack took %ld samples (want %.3f)", c.fs, c.t, shape,
			      na, want);
			CHECK(std::fabs(nd - want) <= 1.0, "fs %g T %g shape %g: Decay took %ld samples (want %.3f)", c.fs, c.t, shape,
			      nd, want);
			CHECK(std::fabs(nr - want) <= 1.0, "fs %g T %g shape %g: Release took %ld samples (want %.3f)", c.fs, c.t,
			      shape, nr, want);
			CHECK(phaseOk, "fs %g T %g shape %g: the phase went backwards or past 1", c.fs, c.t, shape);
		}
	}
	// 60 s at 768 kHz (46 M samples a stage, Review Focus 2): once, at the
	// classic end, where a float phase would stall long before 1.
	{
		const float fs = 768000.f, t = 60.f;
		Rig r(fs, t, t, 5.f, t, 1.f);
		const double want = double(t) * fs;
		const long cap = capFor(t, fs);
		bool phaseOk = true;
		float lastA = 0.f, lastD = 0.f, lastR = -1.f;
		r.in.gate = true;
		r.step();
		const long na = runStage(r, ATTACK, cap, &lastA, &phaseOk);
		const long nd = runStage(r, DECAY, cap, &lastD, &phaseOk);
		r.in.gate = false;
		r.step();
		const long nr = runStage(r, RELEASE, cap, &lastR, &phaseOk);
		CHECK(std::fabs(na - want) <= 1.0, "60 s at 768 kHz: Attack took %ld samples (want %.0f)", na, want);
		CHECK(std::fabs(nd - want) <= 1.0, "60 s at 768 kHz: Decay took %ld samples (want %.0f)", nd, want);
		CHECK(std::fabs(nr - want) <= 1.0, "60 s at 768 kHz: Release took %ld samples (want %.0f)", nr, want);
		CHECK(lastA == 10.f && lastD == 5.f && lastR == 0.f, "60 s at 768 kHz: stages end at %g, %g, %g", lastA, lastD,
		      lastR);
		CHECK(phaseOk, "60 s at 768 kHz: the phase went backwards or past 1");
	}
}

void sample_rate_change() {
	// 100 ms Attack: half at 48 kHz, then the rest at 96 kHz; the stage, phase
	// and level carry over, and the rest takes 50 ms at the new rate.
	Rig r(48000.f, 0.1f, 0.1f, 5.f, 0.1f, 0.5f);
	r.in.gate = true;
	r.run(2400);
	const double p = r.v.phase();
	const float lv = r.v.level();
	r.v.setSampleRate(96000.f);
	CHECK(r.v.stage() == ATTACK && r.v.phase() == p && r.v.level() == lv, "a rate change keeps stage, phase and level");
	r.fs = 96000.f;
	r.clearStep();
	r.step();
	const long rest = runStage(r, ATTACK, 10000);
	CHECK(std::fabs(rest - 4800.0) <= 1.0, "the rest of Attack took %ld samples at 96 kHz (want 4800)", rest);
	CHECK(r.maxStep <= 10.f / 9600.f * 1.5f + 1e-4f, "no jump at the rate change (largest step %g)", r.maxStep);
	// A hostile rate is ignored.
	r.v.setSampleRate(0.f);
	r.v.setSampleRate(-48000.f);
	r.v.setSampleRate(std::nanf(""));
	r.v.reset();
	r.in.gate = false;
	r.step();
	r.in.gate = true;
	r.step();
	const long again = runStage(r, ATTACK, 100000);
	CHECK(std::isfinite(r.v.level()) && std::fabs(again - 9600.0) <= 1.0,
	      "a zero, negative or NaN rate is ignored (Attack then took %ld samples, want 9600 at 96 kHz)", again);
}

void levels() {
	for (float shape : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		Rig r(48000.f, 0.013f, 0.021f, 3.7f, 0.017f, shape);
		float lastA = -1.f, lastD = -1.f, lastR = -1.f;
		r.in.gate = true;
		r.step();
		runStage(r, ATTACK, 2000, &lastA);
		runStage(r, DECAY, 2000, &lastD);
		r.run(10);
		r.in.gate = false;
		r.step();
		runStage(r, RELEASE, 2000, &lastR);
		CHECK(lastA == 10.f, "shape %g: Attack ends at %.9g (want 10 exactly)", shape, lastA);
		CHECK(lastD == 3.7f, "shape %g: Decay ends at %.9g (want 3.7 exactly)", shape, lastD);
		CHECK(lastR == 0.f, "shape %g: Release ends at %.9g (want 0 exactly)", shape, lastR);
		CHECK(r.v.stage() == IDLE && r.out.level == 0.f, "shape %g: idle at %g", shape, r.out.level);
	}
	// Sustain holds for ten minutes.
	{
		Rig r(44100.f, 0.01f, 0.3f, 3.7f, 1.f, 0.75f);
		r.in.gate = true;
		r.run(r.samples(0.5));
		CHECK(r.v.stage() == SUSTAIN, "in Sustain after 0.5 s (stage %s)", stageName(r.v.stage()));
		double worst = 0.0;
		const long n = r.samples(600.0);
		for (long i = 0; i < n; i++) {
			r.step();
			const double e = std::fabs(double(r.out.level) - 3.7);
			if (e > worst)
				worst = e;
		}
		CHECK(worst <= 1e-6 && r.v.stage() == SUSTAIN, "Sustain drifted %g V over ten minutes", worst);
	}
}

void part_way() {
	// Release from Sustain 5 V at 2 s takes 2 s.
	{
		Rig r(48000.f, 0.01f, 0.05f, 5.f, 2.f, 0.75f);
		r.in.gate = true;
		r.run(r.samples(0.2));
		r.in.gate = false;
		r.step();
		const float first = r.out.level;
		float last = -1.f;
		const long n = runStage(r, RELEASE, capFor(2.0, r.fs), &last);
		CHECK(std::fabs(n - 96000.0) <= 1.0, "Release from 5 V took %ld samples (want 96000)", n);
		CHECK(first <= 5.f && first > 4.99f && last == 0.f, "Release ran 5 V (%g) to 0 V (%g)", first, last);
	}
	// A retrigger at 8 V in Decay reaches 10 V in Attack's time.
	{
		Rig r(48000.f, 0.05f, 1.f, 0.f, 1.f, 0.75f);
		r.in.gate = true;
		r.step();
		runStage(r, ATTACK, 10000);
		while (r.v.stage() == DECAY && r.out.level > 8.f)
			r.step();
		const float from = r.out.level;
		r.in.retrig = true;
		r.step();
		r.in.retrig = false;
		CHECK(r.v.stage() == ATTACK, "RETRIG in Decay starts Attack (stage %s)", stageName(r.v.stage()));
		CHECK(r.out.level >= from && r.out.level - from < 0.01f, "Attack starts from %g (OUT %g)", from, r.out.level);
		float last = -1.f;
		const long n = runStage(r, ATTACK, 10000, &last) + 1;   // + the first sample
		// Attack picks up its curve where it is at 8 V and finishes it.
		const double want = 2400.0 * (1.0 - curvePoint(from / 10.0, 0.75f));
		CHECK(std::fabs(n - want) <= 2.0, "Attack from %g V took %ld samples (want %.0f, the rest of its curve)", from, n, want);
		CHECK(last == 10.f, "Attack from 8 V ends at %g", last);
	}
	// Decay with Sustain at 10 V ends at once.
	{
		Rig r(48000.f, 0.01f, 1.f, 10.f, 1.f, 0.75f);
		r.in.gate = true;
		r.step();
		runStage(r, ATTACK, 10000);
		CHECK(r.v.stage() == DECAY && r.out.level == 10.f, "Decay's one sample at 10 V (stage %s, %g)",
		      stageName(r.v.stage()), r.out.level);
		r.step();
		CHECK(r.v.stage() == SUSTAIN && r.out.level == 10.f, "the next sample is SUSTAIN (stage %s)",
		      stageName(r.v.stage()));
	}
}

void gate_mode() {
	// A gate shorter than Attack goes to Release from where it got.
	{
		Rig r(48000.f, 0.1f, 0.1f, 5.f, 0.05f, 0.75f);
		r.in.gate = true;
		r.run(r.samples(0.03));
		CHECK(r.v.stage() == ATTACK, "still in Attack after 30 ms (stage %s)", stageName(r.v.stage()));
		const float at = r.out.level;
		r.in.gate = false;
		r.step();
		CHECK(r.v.stage() == RELEASE, "the gate's end starts Release (stage %s)", stageName(r.v.stage()));
		CHECK(r.out.level <= at && at - r.out.level < 0.01f, "Release starts from %g (OUT %g)", at, r.out.level);
		bool sawDecay = false;
		for (long i = 0; i < r.samples(0.2); i++) {
			r.step();
			sawDecay |= r.v.stage() == DECAY || r.v.stage() == SUSTAIN;
		}
		CHECK(!sawDecay && r.v.stage() == IDLE, "no Decay or Sustain after a short gate (stage %s)",
		      stageName(r.v.stage()));
	}
	// A 1 ms gate with Attack 1 ms reaches 10 V (rates where 1 ms is whole samples).
	for (float fs : {48000.f, 96000.f})
	for (float shape : {0.f, 0.5f, 0.75f, 1.f}) {
		Rig r(fs, 0.001f, 0.1f, 5.f, 0.05f, shape);
		r.in.gate = true;
		float peak = 0.f;
		const long g = r.samples(0.001);
		for (long i = 0; i < g; i++) {
			r.step();
			peak = std::max(peak, r.out.level);
		}
		r.in.gate = false;
		for (int i = 0; i < 10; i++) {
			r.step();
			peak = std::max(peak, r.out.level);
		}
		CHECK(peak == 10.f, "fs %g shape %g: a 1 ms gate peaks at %.9g (want 10)", fs, shape, peak);
		CHECK(r.v.stage() == RELEASE, "fs %g: then Release (stage %s)", fs, stageName(r.v.stage()));
	}
}

// The distinct stages a run passes through, in order.
struct Trail {
	std::vector<Stage> s;
	void see(Stage x) {
		if (s.empty() || s.back() != x)
			s.push_back(x);
	}
	bool is(std::initializer_list<Stage> want) const { return std::vector<Stage>(want) == s; }
	void print(const char* what) const {
		std::printf("  %s:", what);
		for (Stage x : s)
			std::printf(" %s", stageName(x));
		std::printf("\n");
	}
};

// AR mode is a one-shot: any gate, from one sample to many times Attack,
// gives Attack to 10 V and then Release, and nothing else. (It replaced
// Trig mode, where a gate that outlasted Attack ran Decay and Sustain.)
void ar_mode() {
	const float fs = 48000.f, A = 0.05f, R = 0.03f;
	const long nA = 2400, nR = 1440;
	for (long width : {1L, 48L, nA / 2, nA - 1, nA, nA + 1, 2 * nA, 10 * nA, 1000000L}) {
		Rig r(fs, A, 0.1f, 5.f, R, 0.75f, AR_MODE);
		Trail t;
		bool dsGate = false, releaseFromTop = true;
		long attack = 0, release = 0;
		float peak = 0.f;
		Stage last = IDLE;
		for (long i = 0; i < nA + nR + 4800; i++) {
			r.in.gate = i < width;
			r.step();
			const Stage st = r.v.stage();
			t.see(st);
			dsGate |= r.out.stageGate[1] || r.out.stageGate[2];
			attack += st == ATTACK;
			release += st == RELEASE;
			if (st == RELEASE && last == ATTACK)
				releaseFromTop = r.out.level <= 10.f && r.out.level > 9.9f;
			last = st;
			peak = std::max(peak, r.out.level);
		}
		CHECK(t.is({ATTACK, RELEASE, IDLE}), "AR, a %ld-sample gate: Attack, Release, idle", width);
		if (!t.is({ATTACK, RELEASE, IDLE}))
			t.print("AR");
		CHECK(std::abs(attack - nA) <= 1 && std::abs(release - nR) <= 1, "AR, a %ld-sample gate: Attack %ld, Release %ld samples (want %ld, %ld)",
		      width, attack, release, nA, nR);
		CHECK(peak == 10.f && releaseFromTop, "AR, a %ld-sample gate: up to %g, Release from the top %d", width, peak, (int) releaseFromTop);
		CHECK(!dsGate, "AR, a %ld-sample gate: the D or S gate fired", width);
	}
	// Triggers faster than Attack: each resumes Attack where it is, so the
	// envelope still reaches 10 V in Attack's time from the first.
	{
		Rig r(fs, 0.2f, 0.1f, 5.f, 0.1f, 0.75f, AR_MODE);
		long top = -1;
		for (long i = 0; i < 48000 && top < 0; i++) {
			r.in.gate = i % 2400 < 48;   // 1 ms every 50 ms
			r.step();
			if (r.out.level == 10.f)
				top = i;
		}
		CHECK(std::labs(top - 9599) <= 2, "triggers every 50 ms reach 10 V at sample %ld (want 9599: Attack's 200 ms)", top);
	}
}

void retrig() {
	// RETRIG while high restarts Attack from the present level.
	Rig r(48000.f, 0.01f, 0.01f, 5.f, 0.1f, 0.75f);
	r.in.gate = true;
	r.run(r.samples(0.05));
	CHECK(r.v.stage() == SUSTAIN, "in Sustain (stage %s)", stageName(r.v.stage()));
	r.in.retrig = true;
	r.step();
	CHECK(r.v.stage() == ATTACK && r.out.level >= 5.f && r.out.level < 5.1f, "RETRIG high: Attack from 5 V (stage %s, %g)",
	      stageName(r.v.stage()), r.out.level);
	r.step();   // RETRIG held high: no new edge
	r.in.retrig = false;
	float last = -1.f;
	const long n = runStage(r, ATTACK, 10000, &last) + 1;   // + the first sample
	const double want = 480.0 * (1.0 - curvePoint(0.5, 0.75f));
	CHECK(std::fabs(n - want) <= 2.0 && last == 10.f, "RETRIG's Attack from 5 V took %ld samples to %g (want %.0f to 10)", n,
	      last, want);
	// RETRIG while low does nothing.
	r.run(r.samples(0.05));
	r.in.gate = false;
	r.run(r.samples(0.03));
	CHECK(r.v.stage() == RELEASE, "releasing (stage %s)", stageName(r.v.stage()));
	const double ph = r.v.phase();
	const float lv = r.out.level;
	r.in.retrig = true;
	r.step();
	r.in.retrig = false;
	CHECK(r.v.stage() == RELEASE && r.v.phase() > ph && r.out.level < lv, "RETRIG low does nothing (stage %s)",
	      stageName(r.v.stage()));
	// A rising GATE while releasing restarts Attack from the level.
	r.run(r.samples(0.02));
	const float at = r.out.level;
	r.in.gate = true;
	r.step();
	CHECK(r.v.stage() == ATTACK && r.out.level >= at && r.out.level - at < 0.25f,
	      "GATE while releasing: Attack from %g (stage %s, %g)", at, stageName(r.v.stage()), r.out.level);
	// RETRIG in AR mode while the gate is high restarts Attack too.
	Rig t(48000.f, 0.01f, 0.01f, 5.f, 0.1f, 0.75f, AR_MODE);
	t.in.gate = true;
	t.run(t.samples(0.05));
	t.in.retrig = true;
	t.step();
	CHECK(t.v.stage() == ATTACK, "RETRIG in AR mode (stage %s)", stageName(t.v.stage()));
}

// An Attack begun part-way picks up its full curve at the point with the
// level and finishes it, at every Shape: from a level L it takes
// Attack x (1 - p0), p0 the curve's point at L, with no jump at the start.
// So a retrigger, a gate back during Release and fast triggers behave as on
// an RC envelope (cèis, VCV's ADSR), not crawling up over a full Attack.
void attack_resumes() {
	const float fs = 48000.f;
	for (float shape : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		// RETRIG from Sustain at 6 V.
		{
			Rig r(fs, 0.1f, 0.01f, 6.f, 0.1f, shape);
			r.in.gate = true;
			r.run(r.samples(0.3));
			const float from = r.out.level;
			r.in.retrig = true;
			r.clearStep();
			r.step();
			r.in.retrig = false;
			const float first = r.out.level;
			float last = -1.f;
			const long n = runStage(r, ATTACK, 10000, &last) + 1;
			const double p0 = curvePoint(from / 10.0, shape), want = 4800.0 * (1.0 - p0);
			CHECK(std::fabs(n - want) <= 2.0 && last == 10.f, "shape %g: Attack from %g V took %ld samples (want %.0f)", shape, from, n, want);
			// The first sample is on the curve just past p0.
			const double at = 10.0 * curveRise(float(p0 + 1.0 / 4800.0), shape);
			CHECK(std::fabs(first - at) < 2e-3, "shape %g: the first sample %g, the curve there %g", shape, first, at);
		}
		// A gate back during Release, in ADSR and in AR mode.
		for (Mode mode : {GATE_MODE, AR_MODE}) {
			Rig r(fs, 0.1f, 0.05f, 6.f, 0.4f, shape, mode);
			r.in.gate = true;
			r.run(r.samples(0.3));
			r.in.gate = false;
			r.run(r.samples(0.15));
			const float from = r.out.level;
			r.in.gate = true;
			r.step();
			float last = -1.f;
			const long n = runStage(r, ATTACK, 10000, &last) + 1;
			const double want = 4800.0 * (1.0 - curvePoint(from / 10.0, shape));
			CHECK(std::fabs(n - want) <= 2.0, "shape %g, mode %d: Attack from %g V in Release took %ld samples (want %.0f)", shape,
			      int(mode), from, n, want);
		}
	}
}

// The largest step of an undisturbed full ADSR with these settings.
float ownStep(float fs, float a, float d, float s, float r, float shape) {
	Rig x(fs, a, d, s, r, shape);
	x.in.gate = true;
	x.run(x.samples(a + d + 0.01));
	x.in.gate = false;
	x.run(x.samples(r + 0.01));
	return x.maxStep;
}

void no_jumps() {
	const float fs = 48000.f, A = 0.01f, D = 0.02f, S = 5.f, R = 0.03f;
	for (float shape : {0.f, 0.5f, 0.75f, 1.f}) {
		const float bound = ownStep(fs, A, D, S, R, shape) * 1.5f + 1e-4f;
		auto check = [&](Rig& r, const char* what) {
			CHECK(r.maxStep <= bound, "shape %g, %s: largest step %g (bound %g)", shape, what, r.maxStep, bound);
		};
		{   // RETRIG mid-Attack
			Rig r(fs, A, D, S, R, shape);
			r.in.gate = true;
			r.run(r.samples(A * 0.4));
			r.in.retrig = true;
			r.run(r.samples(A + D + 0.01));
			check(r, "retrigger mid-Attack");
		}
		{   // the gate retriggered mid-Decay (one sample low)
			Rig r(fs, A, D, S, R, shape);
			r.in.gate = true;
			r.run(r.samples(A + D * 0.4));
			r.in.gate = false;
			r.step();
			r.in.gate = true;
			r.run(r.samples(A + D + 0.01));
			check(r, "retrigger mid-Decay");
		}
		{   // the gate retriggered mid-Release
			Rig r(fs, A, D, S, R, shape);
			r.in.gate = true;
			r.run(r.samples(A + D + 0.01));
			r.in.gate = false;
			r.run(r.samples(R * 0.4));
			r.in.gate = true;
			r.run(r.samples(A + D + 0.01));
			check(r, "retrigger mid-Release");
		}
		{   // a gate ending in Attack, and one in Decay
			Rig r(fs, A, D, S, R, shape);
			r.in.gate = true;
			r.run(r.samples(A * 0.5));
			r.in.gate = false;
			r.run(r.samples(R + 0.01));
			check(r, "gate ending in Attack");
			Rig q(fs, A, D, S, R, shape);
			q.in.gate = true;
			q.run(q.samples(A + D * 0.5));
			q.in.gate = false;
			q.run(q.samples(R + 0.01));
			check(q, "gate ending in Decay");
		}
	}
	// Attack's time swept 1 ms -> 60 s (and back) mid-stage: the rate changes,
	// never the level. Bound: the 1 ms Attack's own largest step.
	for (float shape : {0.f, 0.5f, 1.f}) {
		const float bound = ownStep(fs, 0.001f, 0.1f, S, 0.1f, shape) * 1.5f + 1e-4f;
		Rig r(fs, 0.001f, 0.1f, S, 0.1f, shape);
		r.in.gate = true;
		r.run(12);
		for (int i = 0; i <= 30; i++) {
			r.in.attack = 0.001f * std::pow(60000.f, i / 30.f);
			r.step();
		}
		r.run(2000);
		CHECK(r.v.stage() == ATTACK, "shape %g: still in Attack at 60 s", shape);
		for (int i = 30; i >= 0; i--) {
			r.in.attack = 0.001f * std::pow(60000.f, i / 30.f);
			r.step();
		}
		r.run(200);
		CHECK(r.maxStep <= bound, "shape %g: Attack time swept: largest step %g (bound %g)", shape, r.maxStep, bound);
	}
	// Shape swept 0 -> 1 through the middle half of each stage. Bound: the
	// largest step of the stage alone at any Shape on the way.
	{
		const float a = 0.1f, d = 0.1f, r_ = 0.1f;
		float own = 0.f;
		for (float s : {0.f, 0.25f, 0.5f, 0.75f, 1.f})
			own = std::max(own, ownStep(fs, a, d, S, r_, s));
		const float bound = own * 1.5f + 1e-4f;
		Rig r(fs, a, d, S, r_, 0.f);
		const long n = r.samples(0.1);
		auto sweep = [&]() {
			for (long i = 0; i < n; i++) {
				const float x = (float(i) / n - 0.25f) * 2.f;
				r.in.shape = x < 0.f ? 0.f : x > 1.f ? 1.f : x;
				r.step();
			}
		};
		r.in.gate = true;
		sweep();   // Attack
		sweep();   // Decay
		r.run(100);
		r.in.gate = false;
		sweep();   // Release
		CHECK(r.maxStep <= bound, "Shape swept mid-stage: largest step %g (bound %g)", r.maxStep, bound);
	}
}

void sustain_follows() {
	for (float fs : {44100.f, 48000.f, 96000.f}) {
		Rig r(fs, 0.01f, 0.01f, 10.f, 0.1f, 0.75f);
		r.in.gate = true;
		r.run(r.samples(0.05));
		CHECK(r.v.stage() == SUSTAIN && r.out.level == 10.f, "fs %g: sustaining at 10 V", fs);
		r.in.sustain = 0.f;
		r.clearStep();
		r.run(r.samples(0.02));
		const double bound = 10.0 * (1.0 - std::exp(-1.0 / (0.002 * fs))) + 1e-4;
		CHECK(r.maxStep <= bound && r.maxStep > 0.5 * bound, "fs %g: a Sustain step moves OUT by at most %g per sample (bound %g)",
		      fs, r.maxStep, bound);
		CHECK(std::fabs(r.out.level) <= 1e-3f && r.out.level >= 0.f, "fs %g: settled at %g after 20 ms", fs, r.out.level);
		// A 5 Hz LFO on Sustain reaches OUT within 0.5 dB.
		float lo = 99.f, hi = -99.f;
		const long n = r.samples(1.0);
		for (long i = 0; i < 2 * n; i++) {
			r.in.sustain = 5.f + 4.f * std::sin(2.0 * M_PI * 5.0 * i / fs);
			r.step();
			if (i >= n) {
				lo = std::min(lo, r.out.level);
				hi = std::max(hi, r.out.level);
			}
		}
		const double db = 20.0 * std::log10((hi - lo) / 8.0);
		CHECK(std::fabs(db) <= 0.5, "fs %g: the LFO reaches OUT at %+.3f dB", fs, db);
	}
	// Audio-rate noise on Sustain (Review Focus 4): OUT stays inside 0..10 V.
	{
		Rig r(48000.f, 0.001f, 0.001f, 5.f, 0.001f, 1.f);
		uint32_t seed = 12345;
		float lo = 99.f, hi = -99.f;
		for (long i = 0; i < 200000; i++) {
			seed = seed * 1664525u + 1013904223u;
			r.in.sustain = 10.f * float(seed >> 8) / 16777216.f;
			r.in.gate = (i / 300) % 3 != 0;
			r.step();
			lo = std::min(lo, r.out.level);
			hi = std::max(hi, r.out.level);
		}
		CHECK(lo >= 0.f && hi <= 10.f, "noise on Sustain: OUT spans %g .. %g", lo, hi);
	}
}

struct Pulses {
	int count = 0;
	std::vector<long> widths;
	long run = 0;
	void see(bool t) {
		if (t)
			run++;
		else if (run) {
			count++;
			widths.push_back(run);
			run = 0;
		}
	}
};

void gates_and_trig() {
	for (float fs : {44100.f, 48000.f, 768000.f}) {
		const long len = std::max(1L, long(std::lround(0.001 * fs)));
		Rig r(fs, 0.01f, 0.01f, 5.f, 0.01f, 0.75f);
		Pulses p;
		bool coherent = true;
		Trail t;
		auto watch = [&]() {
			r.step();
			const Stage s = r.v.stage();
			t.see(s);
			for (int i = 0; i < 4; i++)
				coherent &= r.out.stageGate[i] == (s == Stage(ATTACK + i));
			p.see(r.out.trig);
		};
		for (int i = 0; i < 10; i++)
			watch();
		r.in.gate = true;
		for (long i = 0; i < r.samples(0.05); i++)
			watch();
		r.in.gate = false;
		for (long i = 0; i < r.samples(0.03); i++)
			watch();
		CHECK(coherent, "fs %g: each stage gate is high exactly while its stage runs", fs);
		CHECK(t.is({IDLE, ATTACK, DECAY, SUSTAIN, RELEASE, IDLE}), "fs %g: the run is IDLE A D S R IDLE", fs);
		CHECK(p.count == 5, "fs %g: %d TRIG pulses (want 5: A, D, S, R, idle)", fs, p.count);
		bool wide = true;
		for (long w : p.widths)
			wide &= w == len;
		CHECK(wide, "fs %g: each TRIG is %ld samples wide", fs, len);
		if (!wide)
			for (long w : p.widths)
				std::printf("  width %ld\n", w);
		// Two starts 0.3 ms apart give one pulse 1.3 ms wide.
		Rig m(fs, 0.01f, 0.01f, 5.f, 0.01f, 0.75f);
		m.in.gate = true;
		m.run(m.samples(0.05));
		Pulses q;
		const long gap = m.samples(0.0003);
		m.in.retrig = true;
		m.step();
		q.see(m.out.trig);
		m.in.retrig = false;
		for (long i = 1; i < gap; i++) {
			m.step();
			q.see(m.out.trig);
		}
		m.in.retrig = true;
		for (long i = 0; i < m.samples(0.005); i++) {
			m.step();
			q.see(m.out.trig);
		}
		CHECK(q.count == 1 && q.widths.size() == 1 && q.widths[0] == gap + len,
		      "fs %g: starts 0.3 ms apart give %d pulse(s), the first %ld wide (want 1, %ld)", fs, q.count,
		      q.widths.empty() ? 0L : q.widths[0], gap + len);
	}
	// A stage that ends at once still shows its gate for one sample.
	{
		Rig r(48000.f, 0.01f, 0.5f, 10.f, 0.1f, 0.75f);
		r.in.gate = true;
		long dg = 0;
		for (long i = 0; i < r.samples(0.05); i++) {
			r.step();
			dg += r.out.stageGate[1];
		}
		CHECK(dg == 1, "Decay to Sustain 10 V shows its gate for %ld samples (want 1)", dg);
		// And from idle at 0 V: Release from 0 after Hold ends at once too.
		Rig h(48000.f, 0.01f, 0.01f, 0.f, 0.1f, 0.75f, HOLD_MODE);
		h.run(10);
		h.in.mode = GATE_MODE;
		long rg = 0;
		for (int i = 0; i < 10; i++) {
			h.step();
			rg += h.out.stageGate[3];
		}
		CHECK(rg == 1 && h.v.stage() == IDLE, "Release from 0 V shows its gate for %ld samples (want 1)", rg);
	}
}

void hold() {
	const float fs = 48000.f, A = 0.01f, D = 0.02f;
	// A 0 -> 10 V step rises in Attack's time, A then S; 10 -> 0 falls in Decay's, D then S.
	{
		Rig r(fs, A, D, 0.f, 1.f, 0.75f, HOLD_MODE);
		r.run(10);
		CHECK(r.v.stage() == SUSTAIN && r.out.level == 0.f, "settled at 0 (stage %s)", stageName(r.v.stage()));
		CHECK(r.v.phase() == 0.0, "Hold's phase reads 0");
		r.in.sustain = 10.f;
		Trail t;
		long n = 0;
		while (r.out.level < 10.f && n < 10000) {
			r.step();
			t.see(r.v.stage());
			n++;
		}
		CHECK(std::fabs(n - A * fs) <= 1.0, "0 -> 10 V took %ld samples (want %g)", n, A * fs);
		r.run(5);
		t.see(r.v.stage());
		CHECK(t.is({ATTACK, SUSTAIN}), "rising: A then S");
		CHECK(!r.out.stageGate[0] && !r.out.stageGate[1] && r.out.stageGate[2] && !r.out.stageGate[3], "S gate alone");
		r.in.sustain = 0.f;
		Trail u;
		n = 0;
		while (r.out.level > 0.f && n < 10000) {
			r.step();
			u.see(r.v.stage());
			n++;
		}
		CHECK(std::fabs(n - D * fs) <= 1.0, "10 -> 0 V took %ld samples (want %g)", n, D * fs);
		r.run(5);
		u.see(r.v.stage());
		CHECK(u.is({DECAY, SUSTAIN}), "falling: D then S");
	}
	// GATE and RETRIG are ignored; a negative input is followed.
	{
		Rig r(fs, A, D, -7.f, 1.f, 0.75f, HOLD_MODE);
		r.in.gate = true;
		r.in.retrig = true;
		r.run(r.samples(0.05));
		CHECK(r.out.level == -7.f && r.v.stage() == SUSTAIN, "follows -7 V with GATE high (OUT %g, %s)", r.out.level,
		      stageName(r.v.stage()));
		r.in.sustain = 99.f;
		r.run(r.samples(0.05));
		CHECK(r.out.level == 10.f, "the input clamps to +10 V (OUT %g)", r.out.level);
	}
	// A 0.1 Hz +/-5 V sine with Attack and Decay 1 ms reads S throughout.
	{
		Rig r(fs, 0.001f, 0.001f, 0.f, 1.f, 0.75f, HOLD_MODE);
		bool allS = true;
		double worst = 0.0;
		for (long i = 0; i < r.samples(10.0); i++) {
			r.in.sustain = float(5.0 * std::sin(2.0 * M_PI * 0.1 * i / fs));
			r.step();
			allS &= r.v.stage() == SUSTAIN && r.out.stageGate[2];
			worst = std::max(worst, std::fabs(double(r.out.level) - r.in.sustain));
		}
		CHECK(allS, "a slow sine reads settled throughout");
		CHECK(worst == 0.0, "a slow sine passes unchanged (off by %g)", worst);
	}
	// Hysteresis: settled within 1 mV, leaves only beyond 2 mV.
	{
		Rig r(fs, 60.f, 60.f, 0.f, 1.f, 0.75f, HOLD_MODE);   // 10 V per minute: slow
		r.run(10);
		r.in.sustain = 0.0015f;
		r.run(100);
		CHECK(r.v.stage() == SUSTAIN, "1.5 mV away stays settled (stage %s)", stageName(r.v.stage()));
		r.in.sustain = r.out.level + 0.003f;
		r.step();
		CHECK(r.v.stage() == ATTACK, "3 mV away leaves settled, rising (stage %s)", stageName(r.v.stage()));
		long n = 0;
		while (r.v.stage() == ATTACK && n < 100000) {
			const double err = r.in.sustain - double(r.out.level);
			CHECK(err > 0.001 - 1e-6, "rising while %g V away", err);
			if (err <= 0.001 - 1e-6)
				break;
			r.step();
			n++;
		}
		CHECK(r.v.stage() == SUSTAIN && r.in.sustain - r.out.level <= 0.001f + 1e-6f,
		      "settles once within 1 mV (stage %s, %g away)", stageName(r.v.stage()), r.in.sustain - r.out.level);
		// Input dithering +/-1.5 mV around the output every sample: no chatter.
		const float c = r.out.level;
		int changes = 0;
		Stage last = r.v.stage();
		for (int i = 0; i < 1000; i++) {
			r.in.sustain = c + ((i & 1) ? 0.0015f : -0.0015f);
			r.step();
			changes += r.v.stage() != last;
			last = r.v.stage();
		}
		CHECK(changes == 0, "1.5 mV dither: %d gate changes (want 0)", changes);
	}
	// +/-10 V steps every sample: OUT within +/-10 V, at most one gate change a sample.
	{
		Rig r(fs, 0.001f, 0.001f, 0.f, 1.f, 0.75f, HOLD_MODE);
		float lo = 99.f, hi = -99.f;
		bool oneGate = true;
		Pulses p;
		for (int i = 0; i < 20000; i++) {
			r.in.sustain = (i & 1) ? 10.f : -10.f;
			r.step();
			lo = std::min(lo, r.out.level);
			hi = std::max(hi, r.out.level);
			oneGate &= r.out.stageGate[0] + r.out.stageGate[1] + r.out.stageGate[2] == 1 && !r.out.stageGate[3];
			p.see(r.out.trig);
		}
		CHECK(lo >= -10.f && hi <= 10.f, "+/-10 V steps: OUT %g .. %g", lo, hi);
		CHECK(oneGate, "+/-10 V steps: exactly one of A, D, S high each sample");
	}
	// NaN input holds the level (mid-slew).
	{
		Rig r(fs, 0.1f, 0.1f, 10.f, 1.f, 0.75f, HOLD_MODE);
		r.run(r.samples(0.03));
		const float at = r.out.level;
		r.in.sustain = std::nanf("");
		r.run(100);
		CHECK(r.out.level == at, "NaN input holds %g (OUT %g)", at, r.out.level);
		r.in.sustain = 10.f;
		r.step();
		CHECK(r.out.level > at && r.out.level - at < 0.01f, "then the slew carries on from %g (%g)", at, r.out.level);
	}
	// TRIG fires on each change between A, D and S.
	{
		Rig r(fs, A, D, 0.f, 1.f, 0.75f, HOLD_MODE);
		r.run(100);
		Pulses p;
		r.in.sustain = 10.f;
		for (long i = 0; i < r.samples(0.05); i++) {
			r.step();
			p.see(r.out.trig);
		}
		r.in.sustain = 0.f;
		for (long i = 0; i < r.samples(0.05); i++) {
			r.step();
			p.see(r.out.trig);
		}
		CHECK(p.count == 4, "Hold: %d TRIG pulses for A, S, D, S (want 4)", p.count);
	}
}

void mode_switch() {
	const float fs = 48000.f;
	// GATE -> HOLD mid-Attack: the slew starts from the level, no jump.
	{
		Rig r(fs, 0.1f, 0.1f, 2.f, 0.1f, 0.75f);
		r.in.gate = true;
		r.run(r.samples(0.03));
		const float at = r.out.level;
		r.in.mode = HOLD_MODE;   // Sustain (2 V) is now the input to slew
		r.clearStep();
		r.step();
		CHECK(r.v.stage() == DECAY && r.out.level < at && at - r.out.level <= 10.f / 4800.f + 1e-5f,
		      "GATE -> HOLD: slews down from %g (stage %s, %g)", at, stageName(r.v.stage()), r.out.level);
	}
	// HOLD -> ADSR with the gate high: Attack from the level.
	{
		Rig r(fs, 0.01f, 0.01f, -4.f, 0.1f, 0.75f, HOLD_MODE);
		r.in.gate = true;
		r.run(r.samples(0.05));
		r.in.mode = GATE_MODE;
		r.in.sustain = 5.f;
		r.step();
		CHECK(r.v.stage() == ATTACK && r.out.level > -4.f && r.out.level < -3.f, "HOLD -> ADSR, high: Attack from -4 V (%s, %g)",
		      stageName(r.v.stage()), r.out.level);
		float last = 0.f;
		const long n = runStage(r, ATTACK, 10000, &last);
		CHECK(std::fabs(n - 480.0) <= 1.0 && last == 10.f, "and it takes Attack's time (%ld samples, to %g)", n, last);
	}
	// HOLD -> AR with the gate low: Release rises from a negative level to 0 V.
	{
		Rig r(fs, 0.01f, 0.01f, -4.f, 0.1f, 0.75f, HOLD_MODE);
		r.run(r.samples(0.05));
		r.in.mode = AR_MODE;
		r.in.sustain = 5.f;
		r.step();
		CHECK(r.v.stage() == RELEASE && r.out.level > -4.f && r.out.level < -3.9f,
		      "HOLD -> AR, low: Release from -4 V (%s, %g)", stageName(r.v.stage()), r.out.level);
		float last = -1.f;
		const long n = runStage(r, RELEASE, 10000, &last);
		CHECK(std::fabs(n - 4800.0) <= 1.0 && last == 0.f, "and rises to 0 V in Release's time (%ld samples, to %g)", n,
		      last);
		CHECK(r.v.stage() == IDLE, "then idle");
	}
	// ADSR <-> AR: the running stage carries on.
	{
		Rig r(fs, 0.1f, 0.1f, 5.f, 0.1f, 0.75f);
		r.in.gate = true;
		r.run(r.samples(0.03));
		const double ph = r.v.phase();
		r.in.mode = AR_MODE;
		r.step();
		CHECK(r.v.stage() == ATTACK && r.v.phase() > ph, "ADSR -> AR mid-Attack carries on");
		r.in.gate = false;   // AR mode: Attack completes
		r.run(10);
		CHECK(r.v.stage() == ATTACK, "and AR mode's rule applies to the next edge (stage %s)", stageName(r.v.stage()));
		r.in.mode = GATE_MODE;   // no edge: Attack still carries on, then releases
		r.step();
		CHECK(r.v.stage() == ATTACK, "AR -> ADSR with the gate already low carries on (stage %s)",
		      stageName(r.v.stage()));
		float last = 0.f;
		runStage(r, ATTACK, 10000, &last);
		CHECK(last == 10.f && r.v.stage() == RELEASE, "then Release from 10 V (stage %s)", stageName(r.v.stage()));
	}
	// Through all three positions at audio rate with the gate high: no jump
	// beyond the fastest stage's own step, gates coherent.
	{
		Rig r(fs, 0.01f, 0.01f, 5.f, 0.01f, 0.75f);
		r.in.gate = true;
		bool coherent = true;
		for (int i = 0; i < 48000; i++) {
			r.in.mode = Mode((i / 7) % 3);
			r.step();
			const Stage s = r.v.stage();
			for (int g = 0; g < 4; g++)
				coherent &= r.out.stageGate[g] == (s == Stage(ATTACK + g));
		}
		CHECK(coherent, "switching at audio rate: gates coherent");
		CHECK(r.maxStep <= 10.f / 480.f * 4.f * 1.5f + 1e-4f, "switching at audio rate: largest step %g", r.maxStep);
	}
}

void nan() {
	const float nanv = std::nanf(""), inf = INFINITY;
	for (Mode mode : {GATE_MODE, AR_MODE, HOLD_MODE}) {
		for (int field = 0; field < 6; field++) {
			for (float bad : {nanv, inf, -inf}) {
				Rig r(48000.f, 0.003f, 0.004f, 5.f, 0.005f, 0.75f, mode);
				bool finite = true, inside = true;
				for (int i = 0; i < 6000; i++) {
					const bool poison = (i / 250) % 2 == 1;
					r.in.gate = (i / 700) % 2 == 0;
					r.in.retrig = (i / 90) % 2 == 0;
					r.in.attack = (poison && (field == 0 || field == 5)) ? bad : 0.003f;
					r.in.decay = (poison && (field == 1 || field == 5)) ? bad : 0.004f;
					r.in.release = (poison && (field == 2 || field == 5)) ? bad : 0.005f;
					r.in.sustain = (poison && (field == 3 || field == 5)) ? bad : 5.f;
					r.in.shape = (poison && (field == 4 || field == 5)) ? bad : 0.75f;
					r.step();
					finite &= std::isfinite(r.out.level) && std::isfinite(r.v.level()) && std::isfinite(r.v.phase());
					inside &= r.out.level >= -10.f && r.out.level <= 10.f;
				}
				CHECK(finite, "mode %d, field %d = %g: OUT stays finite", int(mode), field, bad);
				CHECK(inside, "mode %d, field %d = %g: OUT stays within +/-10 V", int(mode), field, bad);
			}
		}
	}
}

} // namespace

int main() {
	exact_times();
	sample_rate_change();
	levels();
	part_way();
	gate_mode();
	ar_mode();
	attack_resumes();
	retrig();
	no_jumps();
	sustain_follows();
	gates_and_trig();
	hold();
	mode_switch();
	nan();
	return rkatest::finish("test_voice");
}
