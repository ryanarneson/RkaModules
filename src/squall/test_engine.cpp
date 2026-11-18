// Squall's engine: the knob law, events and outputs, Memory, Clock, Freeze,
// CV and saved state (spec: Controls and behaviour; plan Task 5).
#include "testing.hpp"
#include "squall/SquallEngine.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

using namespace rka;
using namespace rka::squall;
typedef SquallFrame F;

struct Rig {
	SquallEngine e;
	F f;
	float cv[3][16];
	SquallResult r;
	float fs;
	long long n = 0;

	explicit Rig(uint32_t seed = 1u, float rate = 1000.f) : e(seed), fs(rate) {
		for (int i = 0; i < 3; i++) {
			for (int k = 0; k < 16; k++)
				cv[i][k] = 0.f;
			f.cv[i].v = cv[i];
			f.cv[i].channels = 0;
		}
		for (int p = 0; p < F::PARAMS_LEN; p++)
			f.params[p] = 0.f;
		f.params[F::WEATHER] = 0.5f;
		f.params[F::GUST] = 0.f;
		f.params[F::MEMORY] = 0.f;
		f.clock = 0.f;
		f.clockPatched = false;
		f.freeze = 0.f;
		f.range = 0;
		f.loopLength = 8;
		e.setSampleRate(fs);
	}

	void set(float w, float g, float m) {
		f.params[F::WEATHER] = w;
		f.params[F::GUST] = g;
		f.params[F::MEMORY] = m;
	}

	// One sample; true when an event happened on it.
	bool step() {
		const long long before = e.debug().events;
		e.process(f, r);
		n++;
		return e.debug().events != before;
	}

	// Run until `count` events; returns the Step values and event times (s).
	void events(int count, std::vector<float>* values, std::vector<double>* times, long long maxSamples = 2000000000LL) {
		for (long long i = 0; i < maxSamples && (int) (values ? values->size() : times->size()) < count; i++)
			if (step()) {
				if (values)
					values->push_back(r.step);
				if (times)
					times->push_back(n / (double) fs);
			}
	}

	// A clocked run: `ticks` edges at `periodS`, 50 % duty.
	void clocked(int ticks, float periodS, std::vector<float>* values) {
		f.clockPatched = true;
		const int per = std::max(2, (int) (periodS * fs + 0.5f));
		for (int t = 0; t < ticks; t++)
			for (int i = 0; i < per; i++) {
				f.clock = i < per / 2 ? 10.f : 0.f;
				if (step() && values)
					values->push_back(r.step);
			}
	}
};

// --- The knob law --------------------------------------------------------

static void cv_law() {
	// Weather: knob 0.5, att 1, +5 V must equal knob 1 (identical runs).
	const int ctlKnob[3] = {F::WEATHER, F::GUST, F::MEMORY};
	const int ctlAtt[3] = {F::WEATHER_ATT, F::GUST_ATT, F::MEMORY_ATT};
	for (int c = 0; c < 3; c++) {
		Rig a(5u), b(5u), z(5u), y(5u), q(5u);
		a.set(0.5f, 0.5f, 0.5f);
		b.set(0.5f, 0.5f, 0.5f);
		z.set(0.5f, 0.5f, 0.5f);
		y.set(0.5f, 0.5f, 0.5f);
		q.set(0.5f, 0.5f, 0.5f);
		a.f.params[ctlKnob[c]] = 0.5f;
		a.f.params[ctlAtt[c]] = 1.f;
		a.cv[c][0] = 5.f;
		a.f.cv[c].channels = 1;
		b.f.params[ctlKnob[c]] = 1.f;
		z.f.params[ctlKnob[c]] = 0.5f;
		z.f.params[ctlAtt[c]] = -1.f;
		z.cv[c][0] = 5.f;
		z.f.cv[c].channels = 1;
		y.f.params[ctlKnob[c]] = 0.f;
		q.f.params[ctlAtt[c]] = 0.f;
		q.cv[c][0] = 5.f;
		q.f.cv[c].channels = 1;
		Rig base(5u);
		base.set(0.5f, 0.5f, 0.5f);
		bool sameAB = true, sameZY = true, sameQ = true;
		for (int i = 0; i < 200000; i++) {
			a.step();
			b.step();
			z.step();
			y.step();
			q.step();
			base.step();
			sameAB = sameAB && a.r.step == b.r.step && a.r.gate == b.r.gate;
			sameZY = sameZY && z.r.step == y.r.step && z.r.gate == y.r.gate;
			sameQ = sameQ && q.r.step == base.r.step && q.r.gate == base.r.gate;
		}
		CHECK(sameAB, "control %d: knob 0.5 + 5 V at att 1 must equal knob 1", c);
		CHECK(sameZY, "control %d: knob 0.5 + 5 V at att -1 must equal knob 0", c);
		CHECK(sameQ, "control %d: att 0 must ignore the CV", c);
	}
}

static void poly_cv_uses_channel_1() {
	Rig a(3u), b(3u);
	a.f.params[F::WEATHER_ATT] = 1.f;
	b.f.params[F::WEATHER_ATT] = 1.f;
	a.cv[0][0] = 3.f;
	a.cv[0][1] = -5.f;
	a.cv[0][2] = 5.f;
	a.f.cv[0].channels = 3;
	b.cv[0][0] = 3.f;
	b.f.cv[0].channels = 1;
	bool same = true;
	for (int i = 0; i < 100000; i++) {
		a.step();
		b.step();
		same = same && a.r.step == b.r.step;
	}
	CHECK(same, "a polyphonic CV must use channel 1 only");
}

static void nan_cv_reads_zero() {
	for (float bad : {NAN, INFINITY, -INFINITY}) {
		Rig a(4u), b(4u);
		for (int c = 0; c < 3; c++) {
			a.f.params[F::WEATHER_ATT + c] = 1.f;
			a.cv[c][0] = bad;
			a.f.cv[c].channels = 1;
		}
		bool same = true, finite = true;
		for (int i = 0; i < 100000; i++) {
			a.step();
			b.step();
			same = same && a.r.step == b.r.step;
			finite = finite && std::isfinite(a.r.step) && std::isfinite(a.r.swell);
		}
		CHECK(same && finite, "CV %g must read as 0 V", bad);
	}
}

// --- Events and outputs --------------------------------------------------

static void step_changes_only_at_events() {
	Rig rig(6u);
	rig.set(0.75f, 0.5f, 0.f);
	float prev = rig.r.step;
	bool ok = true;
	int changes = 0;
	rig.step();
	prev = rig.r.step;
	for (int i = 0; i < 300000; i++) {
		const bool ev = rig.step();
		if (rig.r.step != prev) {
			changes++;
			ok = ok && ev;
		}
		prev = rig.r.step;
	}
	CHECK(ok, "Step changed on a sample with no event");
	CHECK(changes > 50, "only %d changes in 300 s at Rain", changes);
}

static void step_values_uniform() {
	Rig rig(7u, 200.f);
	rig.set(1.f, 0.f, 0.f);
	std::vector<float> v;
	rig.events(5000, &v, nullptr);
	int bins[10] = {};
	for (float x : v) {
		const int b = std::min(9, std::max(0, (int) ((x + 5.f) / 1.f)));
		bins[b]++;
	}
	for (int b = 0; b < 10; b++)
		CHECK(bins[b] > 400 && bins[b] < 600, "bin %d holds %d of 5000", b, bins[b]);
}

static void ranges() {
	const float lo[4] = {-5.f, 0.f, -2.f, -1.f}, hi[4] = {5.f, 10.f, 2.f, 1.f};
	for (int range = 0; range < 4; range++) {
		Rig rig(8u, 1000.f);
		rig.set(1.f, 0.5f, 0.f);
		rig.f.range = range;
		float mn = 1e9f, mx = -1e9f;
		for (int i = 0; i < 100000; i++) {
			rig.step();
			mn = std::min(mn, std::min(rig.r.step, rig.r.swell));
			mx = std::max(mx, std::max(rig.r.step, rig.r.swell));
		}
		CHECK(mn >= lo[range] - 1e-4f && mx <= hi[range] + 1e-4f, "range %d: outputs span [%g, %g]", range, mn, mx);
		CHECK(mx - mn > 0.8f * (hi[range] - lo[range]), "range %d: outputs only span [%g, %g]", range, mn, mx);
	}
	// A range change moves Step at once and Swell without a step.
	Rig rig(9u, 1000.f);
	rig.set(0.5f, 0.f, 0.f);
	for (int i = 0; i < 20000; i++)
		rig.step();
	const float stepBefore = rig.r.step, swellBefore = rig.r.swell;
	rig.f.range = 1;
	rig.step();
	CHECK(std::fabs(rig.r.step - (stepBefore + 5.f)) < 1e-4f, "range change: Step %g -> %g, want +5 V at once", stepBefore, rig.r.step);
	CHECK(std::fabs(rig.r.swell - swellBefore) < 0.01f, "range change: Swell jumped %g -> %g", swellBefore, rig.r.swell);
}

static void gate_follows_weather() {
	for (float w : {0.f, 0.5f, 1.f}) {
		const float fs = 1000.f;
		Rig rig(10u, fs);
		rig.set(w, 0.f, 0.f);
		// Measure the first few gates' lengths (gate high runs from an event).
		int measured = 0;
		double total = 0.0;
		long long highRun = -1;
		const long long maxN = (long long) (w == 0.f ? 6000.0 * fs : 600.0 * fs);
		bool cut = false;
		for (long long i = 0; i < maxN && measured < 5; i++) {
			const bool ev = rig.step();
			const bool high = rig.r.gate > 5.f;
			if (ev && highRun > 0)
				cut = true;   // retriggered: this gate's length is not its own
			if (high) {
				if (highRun < 0)
					highRun = 0;
				highRun++;
			}
			else if (highRun > 0) {
				if (!cut) {
					total += highRun / (double) fs;
					measured++;
				}
				highRun = -1;
				cut = false;
			}
		}
		const double want = std::min(4.0, std::max(0.005, 0.25 / weatherRate(w)));
		CHECK(measured > 0, "w %g: no gate measured", w);
		if (measured > 0)
			CHECK(std::fabs(total / measured - want) <= 0.002 + 0.01 * want, "w %g: gate %g s, want %g", w, total / measured, want);
	}
}

static void light_on_each_event() {
	Rig rig(11u, 1000.f);
	rig.set(0.6f, 0.f, 0.f);
	int checked = 0;
	for (int i = 0; i < 600000 && checked < 5; i++) {
		if (rig.step()) {
			CHECK(rig.r.light, "the light must be on at an event");
			int on = 1;
			bool more = false;
			while (on < 2000) {
				more = rig.step();
				if (more || !rig.r.light)
					break;
				on++;
			}
			if (!more) {
				CHECK(std::abs(on - 100) <= 1, "light on for %d ms, want 100", on);
				checked++;
			}
		}
	}
	CHECK(checked == 5, "only %d lights measured", checked);
}

// --- Memory -------------------------------------------------------------

static void memory_zero_no_repeat() {
	Rig rig(12u, 200.f);
	rig.set(1.f, 0.f, 0.f);
	std::vector<float> v;
	rig.events(3000, &v, nullptr);
	int same = 0;
	for (size_t i = 8; i < v.size(); i++)
		if (v[i] == v[i - 8])
			same++;
	CHECK(same < 30, "Memory 0: %d values repeat at lag 8", same);
	CHECK(rig.e.debug().replays == 0, "Memory 0: %lld replays", rig.e.debug().replays);
}

static void memory_one_exact_loop() {
	for (int L : {4, 8, 16}) {
		Rig rig(13u, 1000.f);
		rig.set(0.9f, 0.5f, 1.f);
		rig.f.loopLength = L;
		std::vector<float> v;
		std::vector<double> t;
		rig.events(L + 6 * L, &v, &t);
		bool values = true, gaps = true;
		// From the first replay (event L copies e0): each copy against its
		// original, so a loop at the wrong pace fails too.
		for (size_t i = L + 1; i < v.size(); i++) {
			values = values && v[i] == v[i - L];
			const double g = t[i] - t[i - 1], g0 = t[i - L] - t[i - L - 1];
			gaps = gaps && std::fabs(g - g0) <= 1.5 / 1000.0;
		}
		CHECK(values, "L %d: Memory 1 must loop the values exactly", L);
		CHECK(gaps, "L %d: Memory 1 must loop the gaps to a sample", L);
	}
}

static void memory_share() {
	Rig rig(14u, 200.f);
	rig.set(1.f, 0.f, 0.7f);
	std::vector<float> v;
	rig.events(5000, &v, nullptr);
	const double share = rig.e.debug().replays / (double) rig.e.debug().events;
	CHECK(std::fabs(share - 0.7) < 0.05, "Memory 0.7: replay share %g", share);
}

static void replay_owns_timing() {
	Rig rig(15u, 1000.f);
	rig.set(0.9f, 1.f, 1.f);
	std::vector<float> v;
	rig.events(200, &v, nullptr);
	CHECK(rig.e.debug().gustDuringReplay == 0, "Gust fired %lld times during replayed gaps", rig.e.debug().gustDuringReplay);
	CHECK(rig.e.debug().replays > 150, "only %lld replays at Memory 1", rig.e.debug().replays);
}

static void locked_loop_scales_with_weather() {
	Rig rig(16u, 1000.f);
	rig.set(0.5f, 0.f, 1.f);
	rig.f.loopLength = 4;
	std::vector<float> v;
	std::vector<double> t;
	rig.events(12, &v, &t);
	double before = t[11] - t[7];   // one loop of 4 gaps
	const std::vector<float> loop(v.begin() + 8, v.end());
	rig.f.params[F::WEATHER] = 0.75f;
	std::vector<float> v2;
	std::vector<double> t2;
	rig.events(9, &v2, &t2);   // the first spans the change
	const double after = t2[8] - t2[4];
	const double want = weatherRate(0.5f) / weatherRate(0.75f);
	CHECK(std::fabs(after / before / want - 1.0) < 0.02, "loop time ratio %g, want %g", after / before, want);
	for (int i = 1; i < 9; i++)
		CHECK(std::find(loop.begin(), loop.end(), v2[i]) != loop.end(), "value %g is not one of the loop's", v2[i]);
}

// --- Clock and Freeze ----------------------------------------------------

static void clocked_events_on_edges_only() {
	Rig rig(17u, 1000.f);
	rig.set(0.75f, 0.5f, 0.3f);
	rig.f.clockPatched = true;
	const int per = 20;
	int events = 0;
	bool ok = true;
	for (int t = 0; t < 5000; t++)
		for (int i = 0; i < per; i++) {
			rig.f.clock = i < per / 2 ? 10.f : 0.f;
			if (rig.step()) {
				events++;
				ok = ok && i == 0;
			}
		}
	CHECK(ok, "a clocked event fell between edges");
	CHECK(events > 500, "only %d clocked events", events);
}

static void clocked_gate_uses_tick_period() {
	Rig rig(18u, 1000.f);
	rig.set(0.5f, 0.f, 0.f);   // p = 1/16
	rig.f.clockPatched = true;
	const int per = 100;       // 0.1 s ticks: average gap 1.6 s, gate 0.4 s
	int measured = 0;
	double total = 0.0;
	long long run = -1;
	bool cut = false;
	for (int t = 0; t < 4000 && measured < 6; t++)
		for (int i = 0; i < per; i++) {
			rig.f.clock = i < per / 2 ? 10.f : 0.f;
			const bool ev = rig.step();
			if (ev && run > 0)
				cut = true;
			if (rig.r.gate > 5.f) {
				if (run < 0)
					run = 0;
				run++;
			}
			else if (run > 0) {
				if (t > 3 && !cut) {   // the period is known after two edges
					total += run / 1000.0;
					measured++;
				}
				run = -1;
				cut = false;
			}
		}
	CHECK(measured > 0 && std::fabs(total / measured - 0.4) < 0.01, "clocked gate %g s, want 0.4", measured ? total / measured : 0.0);
}

static void clocked_first_gate_10ms() {
	Rig rig(19u, 1000.f);
	rig.set(1.f, 0.f, 0.f);   // every tick
	rig.f.clockPatched = true;
	rig.f.clock = 10.f;
	rig.step();
	int high = rig.r.gate > 5.f ? 1 : 0;
	for (int i = 0; i < 100 && rig.r.gate > 5.f; i++) {
		rig.step();
		if (rig.r.gate > 5.f)
			high++;
	}
	CHECK(std::abs(high - 10) <= 1, "the first clocked gate is %d ms, want 10", high);
}

static void clock_unpatch_mid_replay() {
	Rig rig(20u, 1000.f);
	rig.set(0.5f, 0.f, 1.f);   // p = 1/16: about 16 ticks per event
	rig.f.loopLength = 4;
	std::vector<float> v;
	rig.clocked(16 * 40, 0.05f, &v);   // 0.05 s ticks: about 0.8 s per event
	CHECK(v.size() >= 8, "only %zu clocked events", v.size());
	// Unpatch the clock mid-gap: timing carries on free-running.
	rig.f.clockPatched = false;
	rig.f.clock = 0.f;
	std::vector<double> t;
	const double t0 = rig.n / 1000.0;
	rig.events(8, nullptr, &t, 600000);
	CHECK(t.size() == 8, "after unpatching only %zu events in 600 s", t.size());
	if (!t.empty()) {
		CHECK(t[0] - t0 < 40.0, "the first free event came %g s after unpatching", t[0] - t0);
		for (size_t i = 1; i < t.size(); i++)
			CHECK(t[i] - t[i - 1] > 0.01, "a burst of catch-up events (%g s apart)", t[i] - t[i - 1]);
	}
}

static void history_survives_clock_change() {
	Rig rig(21u, 1000.f);
	rig.set(0.75f, 0.f, 1.f);
	rig.f.loopLength = 4;
	std::vector<float> v;
	rig.events(12, &v, nullptr);
	const std::vector<float> loop(v.begin() + 8, v.end());
	std::vector<float> c;
	rig.clocked(400, 0.05f, &c);
	CHECK(c.size() >= 8, "only %zu clocked events", c.size());
	for (float x : c)
		CHECK(std::find(loop.begin(), loop.end(), x) != loop.end(), "clocked value %g is not one of the loop's", x);
}

static void fast_clock_gate() {
	Rig rig(22u, 48000.f);
	rig.set(1.f, 1.f, 0.5f);
	rig.f.clockPatched = true;
	const int per = 48;   // 1 kHz
	int edges = 0;
	bool prev = false, sane = true;
	const long long before = rig.e.debug().events;
	for (int t = 0; t < 10000; t++)
		for (int i = 0; i < per; i++) {
			rig.f.clock = i < per / 2 ? 10.f : 0.f;
			rig.step();
			const bool high = rig.r.gate > 5.f;
			sane = sane && (rig.r.gate == 0.f || rig.r.gate == 10.f) && std::isfinite(rig.r.step) && std::isfinite(rig.r.swell);
			if (high && !prev)
				edges++;
			prev = high;
		}
	const long long events = rig.e.debug().events - before;
	CHECK(sane, "outputs must stay 0/10 V and finite");
	// Gust 1 still makes the odd lull at Stormy (a lull starts at p (1 - n)).
	CHECK(events > 9000, "Stormy at 1 kHz: %lld events of 10000 ticks", events);
	CHECK(edges >= events / 3, "only %d gate edges for %lld events", edges, events);
}

static void freeze_holds() {
	Rig rig(23u, 1000.f);
	rig.set(1.f, 0.f, 0.f);
	for (int i = 0; i < 3000; i++)
		rig.step();
	rig.f.freeze = 10.f;
	const long long ev0 = rig.e.debug().events;
	rig.step();
	const float step0 = rig.r.step;
	for (int i = 0; i < 200; i++)
		rig.step();
	const float swell0 = rig.r.swell;
	bool gateEnded = false;
	for (int i = 0; i < 20000; i++) {
		rig.step();
		CHECK(rig.r.step == step0, "Step moved under Freeze");
		if (rig.r.gate < 5.f)
			gateEnded = true;
		if (rig.r.step != step0)
			break;
	}
	CHECK(rig.e.debug().events == ev0, "%lld events under Freeze", rig.e.debug().events - ev0);
	CHECK(std::fabs(rig.r.swell - swell0) < 0.01f, "Swell drifted %g -> %g under Freeze", swell0, rig.r.swell);
	CHECK(gateEnded, "a high gate must end at its time under Freeze");
}

static void freeze_resumes() {
	Rig rig(24u, 1000.f);
	rig.set(0.5f, 0.5f, 0.f);
	for (int i = 0; i < 5000; i++)
		rig.step();
	const double before = rig.e.debug().waitLeft;
	rig.f.freeze = 10.f;
	for (int i = 0; i < 60000; i++)
		rig.step();
	rig.f.freeze = 0.f;
	const double after = rig.e.debug().waitLeft;
	CHECK(std::fabs(after - before) < 1e-9, "the wait moved under Freeze: %g -> %g", before, after);
}

// --- CV and state ---------------------------------------------------------

static void weather_cv_audio_rate() {
	Rig rig(25u, 48000.f);
	rig.set(0.5f, 0.f, 0.f);
	rig.f.params[F::WEATHER_ATT] = 1.f;
	rig.f.cv[0].channels = 1;
	long long events = 0;
	bool finite = true;
	const long long n = 200LL * 48000;
	for (long long i = 0; i < n; i++) {
		rig.cv[0][0] = (i / 120) % 2 ? 5.f : -5.f;   // 200 Hz square
		if (rig.step())
			events++;
		finite = finite && std::isfinite(rig.r.step) && std::isfinite(rig.r.swell);
	}
	const double want = 200.0 * 0.5 * (weatherRate(0.f) + weatherRate(1.f));
	CHECK(finite, "outputs must stay finite");
	CHECK(std::fabs(events / want - 1.0) < 0.15, "%lld events in 200 s, want about %g", events, want);
}

static void state_round_trip() {
	Rig a(26u, 1000.f);
	a.set(0.8f, 0.3f, 1.f);
	a.f.loopLength = 4;
	std::vector<float> v;
	a.events(12, &v, nullptr);
	SquallState s;
	a.e.getState(s);
	Rig b(99u, 1000.f);
	b.set(0.8f, 0.3f, 1.f);
	b.f.loopLength = 4;
	b.e.setState(s);
	std::vector<float> va, vb;
	a.events(8, &va, nullptr);
	b.events(8, &vb, nullptr);
	CHECK(va == vb, "a reloaded locked loop must carry on with the same values");
	SquallState s2;
	b.e.getState(s2);
	CHECK(s2.count == 16, "history count %d after 20 events", s2.count);
	b.step();
	CHECK(std::isfinite(b.r.swell), "Swell after a reload");
	// Swell resumes where it was: no jump on load.
	Rig c(27u, 1000.f), d(28u, 1000.f);
	c.set(0.9f, 0.f, 0.f);
	for (int i = 0; i < 20000; i++)
		c.step();
	SquallState sc;
	c.e.getState(sc);
	d.e.setState(sc);
	d.f = c.f;
	for (int k = 0; k < 3; k++)
		d.f.cv[k].v = d.cv[k];
	d.step();
	CHECK(std::fabs(d.r.swell - c.r.swell) < 0.01f, "Swell jumped on reload: %g -> %g", c.r.swell, d.r.swell);
	CHECK(d.r.step == c.r.step, "Step changed on reload: %g -> %g", c.r.step, d.r.step);
}

static void state_rejects_bad() {
	SquallState s;
	s.count = 17;
	for (int i = 0; i < 16; i++) {
		s.history[i].gap = NAN;
		s.history[i].value = 3.f;
		s.history[i].length = -1.f;
	}
	s.step01 = NAN;
	s.swellStage1 = INFINITY;
	s.swellOut = NAN;
	Rig rig(29u, 1000.f);
	rig.e.setState(s);
	SquallState g;
	rig.e.getState(g);
	CHECK(g.count == 0, "a count of 17 loads as an empty history (got %d)", g.count);
	CHECK(g.step01 == 0.5f, "a NaN step loads as 0.5 (got %g)", g.step01);
	CHECK(std::isfinite(g.swellStage1) && std::isfinite(g.swellOut), "a bad Swell loads finite");
	s.count = 3;
	rig.e.setState(s);
	rig.e.getState(g);
	CHECK(g.count == 3, "count 3 loads (got %d)", g.count);
	for (int i = 0; i < 3; i++)
		CHECK(std::isfinite(g.history[i].gap) && g.history[i].gap > 0.f && g.history[i].value >= 0.f && g.history[i].value <= 1.f && g.history[i].length > 0.f,
		      "event %d loads as defaults", i);
}

static void bad_rate_ignored() {
	Rig a(30u, 1000.f), b(30u, 1000.f);
	for (float bad : {0.f, -1.f, NAN, INFINITY, 1e9f})
		a.e.setSampleRate(bad);
	a.set(0.9f, 0.5f, 0.3f);
	b.set(0.9f, 0.5f, 0.3f);
	bool same = true;
	for (int i = 0; i < 100000; i++) {
		a.step();
		b.step();
		same = same && a.r.step == b.r.step && a.r.gate == b.r.gate;
	}
	CHECK(same, "bad rates must be ignored");
}

static void seeds_differ_and_repeat() {
	Rig a(31u), b(31u), c(32u);
	bool ab = true, ac = true;
	for (int i = 0; i < 100000; i++) {
		a.step();
		b.step();
		c.step();
		ab = ab && a.r.step == b.r.step && a.r.swell == b.r.swell;
		ac = ac && a.r.step == c.r.step;
	}
	CHECK(ab, "one seed must repeat");
	CHECK(!ac, "different seeds must differ");
}

int main() {
	cv_law();
	poly_cv_uses_channel_1();
	nan_cv_reads_zero();
	step_changes_only_at_events();
	step_values_uniform();
	ranges();
	gate_follows_weather();
	light_on_each_event();
	memory_zero_no_repeat();
	memory_one_exact_loop();
	memory_share();
	replay_owns_timing();
	locked_loop_scales_with_weather();
	clocked_events_on_edges_only();
	clocked_gate_uses_tick_period();
	clocked_first_gate_10ms();
	clock_unpatch_mid_replay();
	history_survives_clock_change();
	fast_clock_gate();
	freeze_holds();
	freeze_resumes();
	weather_cv_audio_rate();
	state_round_trip();
	state_rejects_bad();
	bad_rate_ignored();
	seeds_differ_and_repeat();
	return rkatest::finish("squall test_engine");
}
