// HadalEngine, offline (plan Task 5): levels and the knob law, the mix,
// patching and the mono normal, smoothing, Drift, headroom, seeds and bad
// input. Spec: docs/specs/2026-11-16-hadal-design.md.
#include "testing.hpp"
#include "test_measure.hpp"
#include "HadalEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace hadaltest;
typedef rka::HadalFrame F;

namespace {

const float FS = 48000.f;

struct Rig {
	rka::HadalEngine e;
	F f;
	float cvv[5][16] = {};
	explicit Rig(uint32_t seed = 1) : e(seed) {
		e.setSampleRate(FS);
		for (int i = 0; i < F::PARAMS_LEN; i++)
			f.params[i] = 0.f;
		f.params[F::DEPTH] = 0.5f;
		for (int i = 0; i < 5; i++) {
			f.cv[i].v = cvv[i];
			f.cv[i].channels = 0;
			f.patched[i] = true;
		}
	}
	// Levels only: everything else 0.
	void levels(float rumble, float hiss, float crackle) {
		f.params[F::RUMBLE] = rumble;
		f.params[F::HISS] = hiss;
		f.params[F::CRACKLE] = crackle;
	}
};

struct Out {
	std::vector<float> rumble, hiss, crackle, l, r;
};

Out run(Rig& g, float seconds, float settle = 0.5f) {
	rka::HadalResult res;
	for (int i = 0; i < (int) (settle * FS); i++)
		g.e.process(g.f, res);
	Out o;
	const int n = (int) (seconds * FS);
	o.rumble.resize(n);
	o.hiss.resize(n);
	o.crackle.resize(n);
	o.l.resize(n);
	o.r.resize(n);
	for (int i = 0; i < n; i++) {
		g.e.process(g.f, res);
		o.rumble[i] = res.rumble;
		o.hiss[i] = res.hiss;
		o.crackle[i] = res.crackle;
		o.l[i] = res.l;
		o.r[i] = res.r;
	}
	return o;
}

double maxStep(const std::vector<float>& x, size_t from, size_t to) {
	double m = 0;
	for (size_t i = std::max<size_t>(from, 1); i < to && i < x.size(); i++)
		m = std::max(m, (double) std::fabs(x[i] - x[i - 1]));
	return m;
}

// ---- levels and the knob law ----

void full_levels() {
	{
		Rig g;
		g.levels(1.f, 0.f, 0.f);
		Out o = run(g, 10.f);
		CHECK(std::fabs(db(rms(o.rumble) / 2.5)) <= 1.0, "Rumble at full: %.3f V RMS (want 2.5 +- 1 dB)", rms(o.rumble));
	}
	// Rumble's soft clip holds its peaks at the +-5 V audio standard (Drift 0).
	for (float d : {0.f, 0.5f, 1.f}) {
		Rig g;
		g.levels(1.f, 0.f, 0.f);
		g.f.params[F::DEPTH] = d;
		g.f.params[F::DRIFT] = 0.f;
		Out o = run(g, 30.f);
		float pk = 0;
		for (float v : o.rumble)
			pk = std::max(pk, std::fabs(v));
		std::printf("  Rumble at full, depth %.1f: %.3f V RMS, peak %.2f V\n", d, rms(o.rumble), pk);
		CHECK(pk <= 5.0f, "Rumble at full, depth %.1f, peaks at %.2f V (want <= 5)", d, pk);
	}
	{
		Rig g;
		g.levels(0.f, 1.f, 0.f);
		Out o = run(g, 10.f);
		CHECK(std::fabs(db(rms(o.hiss) / 1.6)) <= 1.0, "Hiss at full: %.3f V RMS (want 1.6 +- 1 dB)", rms(o.hiss));
	}
	{
		Rig g;
		g.levels(0.f, 0.f, 1.f);
		Out o = run(g, 60.f);
		const double want = rka::HadalEngine::CRACKLE_FULL_RMS;
		std::printf("  Crackle at full: %.4f V RMS long-term (stated %.4f), peak %.2f V\n", rms(o.crackle), want,
		            *std::max_element(o.crackle.begin(), o.crackle.end()));
		CHECK(std::fabs(db(rms(o.crackle) / want)) <= 1.0, "Crackle at full: %.4f V RMS (want %.4f +- 1 dB)",
		      rms(o.crackle), want);
		float pk = 0;
		for (float v : o.crackle)
			pk = std::max(pk, std::fabs(v));
		CHECK(pk <= 5.0f, "a crackle tick peaks at %.2f V (cap 5 V)", pk);
		// Option D (soft-clipped ticks, turned up): at least 85 mV RMS,
		// about 8 dB over the plain 5 V-peak scaling's 37 mV.
		CHECK(rms(o.crackle) >= 0.085, "Crackle at full: %.4f V RMS (want >= 0.085 V)", rms(o.crackle));
	}
	// The soft clip squeezes bright (high-peak) ticks more than dark ones,
	// so Crackle's level moves a little with Depth: within 2 dB of noon.
	{
		double at[3];
		const float depths[3] = {0.f, 0.5f, 1.f};
		for (int k = 0; k < 3; k++) {
			Rig g;
			g.levels(0.f, 0.f, 1.f);
			g.f.params[F::DEPTH] = depths[k];
			at[k] = rms(run(g, 60.f).crackle);
		}
		for (int k = 0; k < 3; k += 2) {
			std::printf("  Crackle at depth %.0f: %+.2f dB re noon\n", depths[k], db(at[k] / at[1]));
			CHECK(std::fabs(db(at[k] / at[1])) <= 2.0, "Crackle at depth %.0f is %+.2f dB re noon (want within 2)",
			      depths[k], db(at[k] / at[1]));
		}
	}
}

void audio_taper() {
	Rig a, b, z;
	a.levels(1.f, 0.f, 0.f);
	b.levels(0.5f, 0.f, 0.f);
	z.levels(0.f, 0.f, 0.f);
	const double ra = rms(run(a, 5.f).rumble), rb = rms(run(b, 5.f).rumble), rz = rms(run(z, 5.f).rumble);
	CHECK(std::fabs(db(rb / ra) + 12.04) <= 0.5, "knob 0.5 gives %.2f dB re full (want -12 +- 0.5)", db(rb / ra));
	CHECK(rz < 1e-6, "knob 0 must be silent (%.2e V RMS)", rz);
}

void mix_ratio() {
	for (int layer = 0; layer < 3; layer++) {
		Rig g;
		g.levels(layer == 0, layer == 1, layer == 2);
		g.f.patched[0] = g.f.patched[1] = g.f.patched[2] = false;   // every layer in the mix
		Out o = run(g, 2.f);
		const std::vector<float>& src = layer == 0 ? o.rumble : layer == 1 ? o.hiss : o.crackle;
		double worst = 0, scale = 1e-9;
		for (size_t i = 0; i < src.size(); i++) {
			// Crackle's jack is the mono sum of its panned sides, (L + R) / sqrt 2,
			// so a tick panned hard right still reaches it at -3 dB at most.
			const float mixed = layer == 2 ? (o.l[i] + o.r[i]) * 0.70710678f : o.l[i];
			worst = std::max(worst, (double) std::fabs(mixed - src[i] * rka::HadalEngine::MIX_GAIN));
			scale = std::max(scale, (double) std::fabs(src[i] * rka::HadalEngine::MIX_GAIN));
		}
		CHECK(worst <= 1e-4 * scale, "layer %d reaches L at %.2e off the fixed mix ratio (scale %.2f)", layer, worst,
		      scale);
	}
}

void cv_law() {
	const int params[5] = {F::DEPTH, F::RUMBLE, F::HISS, F::CRACKLE, F::DRIFT};
	for (int i = 0; i < 5; i++) {
		Rig g;
		g.f.params[params[i]] = 0.5f;
		g.f.params[F::DEPTH_ATT + i] = 1.f;
		g.cvv[i][0] = 5.f;
		g.f.cv[i].channels = 1;
		rka::HadalResult res;
		g.e.process(g.f, res);
		CHECK(std::fabs(g.e.control(i) - 1.f) < 1e-6, "control %d: knob 0.5, att 1, +5 V gives %.4f (want 1)", i,
		      g.e.control(i));
		g.f.params[F::DEPTH_ATT + i] = -1.f;
		g.e.process(g.f, res);
		CHECK(std::fabs(g.e.control(i)) < 1e-6, "control %d: knob 0.5, att -1, +5 V gives %.4f (want 0)", i,
		      g.e.control(i));
		g.f.params[F::DEPTH_ATT + i] = 0.f;
		g.e.process(g.f, res);
		CHECK(std::fabs(g.e.control(i) - 0.5f) < 1e-6, "control %d: att 0 must ignore CV (%.4f)", i, g.e.control(i));
	}
}

void poly_cv_uses_channel_1() {
	Rig g;
	g.f.params[F::HISS] = 0.f;
	g.f.params[F::HISS_ATT] = 1.f;
	g.cvv[2][0] = 5.f;
	g.cvv[2][1] = -10.f;
	g.f.cv[2].channels = 2;
	rka::HadalResult res;
	g.e.process(g.f, res);
	CHECK(std::fabs(g.e.control(2) - 0.5f) < 1e-6, "a polyphonic CV must use channel 1 (%.4f)", g.e.control(2));
}

// ---- patching and normalling ----

void patched_layer_leaves_mix() {
	Rig g;
	g.levels(1.f, 0.f, 0.f);
	g.f.patched[0] = true;
	Out in = run(g, 1.f);
	CHECK(rms(in.l) < 1e-6, "a patched Rumble must leave L (%.2e V RMS)", rms(in.l));
	g.f.patched[0] = false;
	Out back = run(g, 1.f, 0.05f);
	double worst = 0;
	for (size_t i = 0; i < back.l.size(); i++)
		worst = std::max(worst, (double) std::fabs(back.l[i] - back.rumble[i] * rka::HadalEngine::MIX_GAIN));
	CHECK(worst < 1e-4, "an unpatched Rumble must return to L in full (off by %.2e)", worst);
}

// The largest output step across a switch, against the steady largest step.
void switchNoClick(const char* what, void (*flip)(Rig&), const std::vector<float> Out::*side) {
	// Rumble: smooth from sample to sample, so a hard cut stands out
	// (hiss's own steps would hide one).
	Rig g;
	g.levels(1.f, 0.f, 0.f);
	g.f.patched[0] = false;
	Out before = run(g, 0.5f);
	const double steady = maxStep(before.*side, 1, before.l.size());
	flip(g);
	Out after = run(g, 0.05f, 0.f);
	std::vector<float> joined = before.*side;
	joined.insert(joined.end(), (after.*side).begin(), (after.*side).end());
	const double at = maxStep(joined, before.l.size() - 1, before.l.size() + 2000);
	CHECK(at <= 2.0 * steady + 0.05, "%s: a step of %.3f V at the switch (steady %.3f)", what, at, steady);
}

void patch_unpatch_no_click() {
	switchNoClick("patching RUMBLE", [](Rig& g) { g.f.patched[0] = true; }, &Out::l);
}

void mono_normal() {
	Rig g;
	g.levels(0.f, 1.f, 0.f);
	g.f.patched[1] = false;
	Out st = run(g, 5.f);
	g.f.patched[4] = false;   // R unpatched
	Out mono = run(g, 5.f);
	const double d = db(rms(mono.l) / rms(st.l));
	std::printf("  L as the mono sum: %+.2f dB re one side (hiss)\n", d);
	CHECK(std::fabs(d) <= 1.0, "L as the mono sum is %+.2f dB re one side on noise (want within 1)", d);
	// L is (L + R) / sqrt 2 of the stereo mix.
	Rig h;
	h.levels(0.f, 1.f, 0.f);
	h.f.patched[1] = false;
	h.f.patched[4] = false;
	rka::HadalResult res;
	for (int i = 0; i < 24000; i++)
		h.e.process(h.f, res);
	CHECK(std::fabs(res.l - (h.e.mixL() + h.e.mixR()) * 0.70710678f) < 1e-5, "L must be the mono sum when R is free");
}

void mono_normal_switch_no_click() {
	switchNoClick("unpatching R", [](Rig& g) { g.f.patched[4] = false; }, &Out::l);
}

void nothing_patched_no_work() {
	Rig g;
	g.levels(1.f, 1.f, 1.f);
	for (int i = 0; i < 5; i++)
		g.f.patched[i] = false;
	const long before = g.e.work();
	run(g, 0.5f);
	CHECK(g.e.work() == before, "with nothing patched Hadal must do no work (%ld samples ran)", g.e.work() - before);
	g.f.patched[1] = true;
	run(g, 0.1f, 0.f);
	CHECK(g.e.work() > before, "a patched output must start the work");
}

// ---- smoothing and Drift ----

void square_cv_no_zipper() {
	for (int i = 0; i < 5; i++) {
		Rig g;
		g.levels(0.5f, 0.5f, 0.f);
		g.f.patched[0] = g.f.patched[1] = g.f.patched[2] = false;
		Out steady = run(g, 0.5f);
		const double base = maxStep(steady.l, 1, steady.l.size());
		g.f.params[F::DEPTH_ATT + i] = 1.f;
		g.f.cv[i].channels = 1;
		rka::HadalResult res;
		double worst = 0, prev = 0;
		bool finite = true;
		for (int n = 0; n < (int) FS; n++) {
			g.cvv[i][0] = (n / 480) % 2 ? 5.f : -5.f;   // 50 Hz square
			g.e.process(g.f, res);
			finite = finite && res.l == res.l && std::fabs(res.l) <= 10.f;
			if (n)
				worst = std::max(worst, (double) std::fabs(res.l - prev));
			prev = res.l;
		}
		CHECK(finite, "square CV on input %d: output must stay finite and within 10 V", i);
		CHECK(worst <= 2.0 * base + 0.05, "square CV on input %d steps the output by %.3f V (steady %.3f)", i, worst,
		      base);
	}
}

// Per-second RMS (dB) of a signal.
std::vector<double> perSecondDb(const std::vector<float>& x) {
	std::vector<double> out;
	for (size_t at = 0; at + (size_t) FS <= x.size(); at += (size_t) FS) {
		double s = 0;
		for (size_t i = at; i < at + (size_t) FS; i++)
			s += (double) x[i] * x[i];
		out.push_back(10.0 * std::log10(s / FS));
	}
	return out;
}

std::vector<double> centroids(const std::vector<float>& x, float seconds) {
	std::vector<double> out;
	const size_t step = (size_t) (seconds * FS);
	for (size_t at = 0; at + step <= x.size(); at += step) {
		std::vector<float> seg(x.begin() + at, x.begin() + at + step);
		out.push_back(centroid(psd(seg, 4096), 4096, FS));
	}
	return out;
}

void drift_zero_static() {
	Rig g;
	g.levels(0.f, 1.f, 0.f);
	g.f.params[F::DRIFT] = 0.f;
	Out o = run(g, 60.f);
	std::vector<double> lv = perSecondDb(o.hiss), c = centroids(o.hiss, 5.f);
	const double lspan = *std::max_element(lv.begin(), lv.end()) - *std::min_element(lv.begin(), lv.end());
	const double cmin = *std::min_element(c.begin(), c.end()), cmax = *std::max_element(c.begin(), c.end());
	std::printf("  Drift 0: level spread %.2f dB, centroid %.0f-%.0f Hz\n", lspan, cmin, cmax);
	CHECK(lspan < 0.5, "at Drift 0 the level moves %.2f dB (want < 0.5)", lspan);
	CHECK(cmax / cmin < 1.03, "at Drift 0 the centroid moves %.1f %% (want < 3 %%)", 100 * (cmax / cmin - 1));
}

void drift_full_moves_within_ranges() {
	Rig g;
	g.levels(0.f, 1.f, 1.f);
	g.f.params[F::DRIFT] = 1.f;
	// 30 windows of 10 s; crackle's tick count read after each (the engine
	// keeps a running count, never a log: no allocation in process()).
	Out o;
	std::vector<long> perWindow;
	run(g, 0.f, 0.5f);
	for (int w = 0; w < 30; w++) {
		const long t0 = g.e.tickCount();
		Out part = run(g, 10.f, 0.f);
		perWindow.push_back(g.e.tickCount() - t0);
		o.hiss.insert(o.hiss.end(), part.hiss.begin(), part.hiss.end());
	}
	std::vector<double> lv = perSecondDb(o.hiss), c = centroids(o.hiss, 5.f);
	double m = 0;
	for (double v : lv)
		m += v;
	m /= lv.size();
	const double lo = *std::min_element(lv.begin(), lv.end()) - m, hi = *std::max_element(lv.begin(), lv.end()) - m;
	double cm = 0;
	for (double v : c)
		cm += std::log2(v);
	cm /= c.size();
	double clo = 1e9, chi = -1e9;
	for (double v : c) {
		clo = std::min(clo, std::log2(v) - cm);
		chi = std::max(chi, std::log2(v) - cm);
	}
	std::printf("  Drift full: level %+.2f..%+.2f dB, centroid %+.2f..%+.2f oct around their means\n", lo, hi, clo, chi);
	CHECK(lo >= -5.0 && hi <= 5.0, "Drift full moves the level %+.2f..%+.2f dB (want within +-5)", lo, hi);
	CHECK(hi - lo >= 2.0, "Drift full barely moves the level (%.2f dB)", hi - lo);
	CHECK(clo >= -0.433 && chi <= 0.433, "Drift full moves the colour %+.2f..%+.2f oct (want within +-0.43)", clo, chi);
	CHECK(chi - clo >= 0.1, "Drift full barely moves the colour (%.2f oct)", chi - clo);
	long lo10 = 1 << 30, hi10 = 0;
	for (long n : perWindow) {
		lo10 = std::min(lo10, n);
		hi10 = std::max(hi10, n);
	}
	std::printf("  Drift full: crackle %ld-%ld ticks per 10 s\n", lo10, hi10);
	CHECK(lo10 >= 15 && hi10 <= 300, "crackle made %ld-%ld ticks per 10 s (want 3-25 per second, with slack)", lo10, hi10);
}

// ---- headroom and safety ----

void headroom() {
	for (float d : {0.f, 0.5f, 1.f}) {
		Rig g;
		g.levels(1.f, 1.f, 1.f);
		g.f.params[F::DEPTH] = d;
		g.f.params[F::DRIFT] = 1.f;
		g.f.patched[0] = g.f.patched[1] = g.f.patched[2] = false;
		Out o = run(g, 60.f);
		float pk = 0;
		for (size_t i = 0; i < o.l.size(); i++)
			pk = std::max(pk, std::max(std::fabs(o.l[i]), std::fabs(o.r[i])));
		CHECK(pk <= 10.f, "all layers at full, depth %.1f: mix peaks at %.2f V (want <= 10)", d, pk);
	}
	Rig g;   // the defaults
	g.levels(0.8f, 0.3f, 0.f);
	g.f.params[F::DRIFT] = 0.25f;
	g.f.patched[0] = g.f.patched[1] = g.f.patched[2] = false;
	const long before = g.e.limited();
	run(g, 60.f);
	CHECK(g.e.limited() == before, "at the defaults the soft limit must never act (%ld samples)", g.e.limited() - before);
}

void stereo_mix() {
	Rig g;
	g.levels(1.f, 1.f, 0.f);
	g.f.patched[0] = g.f.patched[1] = g.f.patched[2] = false;
	Out o = run(g, 10.f);
	std::vector<float> m(o.l.size());
	for (size_t i = 0; i < m.size(); i++)
		m[i] = 0.5f * (o.l[i] + o.r[i]);
	const double loss = db(std::sqrt(0.5 * (rms(o.l) * rms(o.l) + rms(o.r) * rms(o.r))) / rms(m));
	CHECK(loss <= 3.0, "the mix loses %.2f dB summed to mono (want <= 3)", loss);
	Rig h;
	h.levels(1.f, 0.f, 0.f);
	h.f.patched[0] = false;
	Out ru = run(h, 10.f);
	CHECK(corr(ru.l, ru.r) >= 0.8, "Rumble in the mix: L/R correlation %.3f (want >= 0.8)", corr(ru.l, ru.r));
}

void seeds_differ_and_repeat() {
	Rig a(42), b(42), c(43);
	for (Rig* g : {&a, &b, &c})
		g->levels(0.5f, 0.5f, 0.5f);
	Out oa = run(a, 1.f), ob = run(b, 1.f), oc = run(c, 1.f);
	CHECK(oa.l == ob.l && oa.r == ob.r, "one seed must rerun bit-identically");
	CHECK(std::fabs(corr(oa.hiss, oc.hiss)) < 0.1, "two seeds must differ (hiss corr %.3f)", corr(oa.hiss, oc.hiss));
}

// Initialize restarts from the seed: after any history, a reset engine
// matches a fresh one bit for bit.
void reset_matches_fresh() {
	Rig fresh(7), used(7);
	for (Rig* g : {&fresh, &used}) {
		g->levels(0.5f, 0.5f, 1.f);
		g->f.params[F::DRIFT] = 1.f;
	}
	run(used, 7.f);
	used.e.reset();
	Out a = run(fresh, 3.f, 0.f), b = run(used, 3.f, 0.f);
	size_t first = a.l.size();
	for (size_t i = 0; i < a.l.size() && first == a.l.size(); i++)
		if (a.l[i] != b.l[i] || a.r[i] != b.r[i] || a.crackle[i] != b.crackle[i])
			first = i;
	CHECK(first == a.l.size(), "after reset the output differs from a fresh engine from sample %zu", first);
}

// Initialize runs under Rack's engine lock: it must not rebuild the gain
// tables (a sample-rate change does, off the audio path). Timed against
// setSampleRate on the same engine.
void reset_is_cheap() {
	rka::HadalEngine e(1);
	double tRate = 1e30, tReset = 1e30;
	for (int k = 0; k < 5; k++) {
		auto t0 = std::chrono::steady_clock::now();
		e.setSampleRate(48000.f);
		auto t1 = std::chrono::steady_clock::now();
		e.reset();
		auto t2 = std::chrono::steady_clock::now();
		tRate = std::min(tRate, std::chrono::duration<double>(t1 - t0).count());
		tReset = std::min(tReset, std::chrono::duration<double>(t2 - t1).count());
	}
	std::printf("  reset %.3f ms, setSampleRate %.3f ms\n", 1e3 * tReset, 1e3 * tRate);
	CHECK(tReset < tRate / 20.0, "reset takes %.3f ms against setSampleRate's %.3f ms (want under 1/20)", 1e3 * tReset,
	      1e3 * tRate);
}

// The rate given to the constructor (#35): the same engine as one built at
// 48 kHz and then set to that rate, and an unusable one means 48 kHz.
void constructed_at_rate() {
	Rig g;
	g.levels(1.f, 1.f, 1.f);
	g.f.params[F::DRIFT] = 1.f;
	auto same = [&](rka::HadalEngine& a, rka::HadalEngine& b) {
		rka::HadalResult ra, rb;
		for (int i = 0; i < 96000; i++) {
			a.process(g.f, ra);
			b.process(g.f, rb);
			if (ra.l != rb.l || ra.r != rb.r || ra.rumble != rb.rumble || ra.hiss != rb.hiss || ra.crackle != rb.crackle)
				return false;
		}
		return true;
	};
	{
		rka::HadalEngine a(3, 96000.f), b(3);
		b.setSampleRate(96000.f);
		CHECK(same(a, b), "an engine built at 96 kHz differs from one built at 48 kHz then set to 96 kHz");
	}
	{
		// The difference must be visible: 48 kHz and 96 kHz engines differ.
		rka::HadalEngine a(3, 96000.f), b(3);
		CHECK(!same(a, b), "an engine built at 96 kHz runs like one at 48 kHz (rate not honoured)");
	}
	for (float bad : {0.f, -1.f, NAN, INFINITY, 1e9f}) {
		rka::HadalEngine a(3, bad), b(3);
		CHECK(same(a, b), "an engine built at unusable rate %g must run at 48 kHz", bad);
	}
}

// The module passes Rack's rate to the constructor so the gain tables are
// built once, not at 48 kHz and again at the real rate (#35): building at a
// rate costs about one setSampleRate, not two.
void construction_builds_tables_once() {
	double tBuild = 1e30, tRate = 1e30;
	for (int k = 0; k < 5; k++) {
		auto t0 = std::chrono::steady_clock::now();
		rka::HadalEngine e(1, 96000.f);
		auto t1 = std::chrono::steady_clock::now();
		e.setSampleRate(44100.f);
		auto t2 = std::chrono::steady_clock::now();
		tBuild = std::min(tBuild, std::chrono::duration<double>(t1 - t0).count());
		tRate = std::min(tRate, std::chrono::duration<double>(t2 - t1).count());
	}
	std::printf("  construct at 96 kHz %.3f ms, setSampleRate %.3f ms\n", 1e3 * tBuild, 1e3 * tRate);
	CHECK(tBuild < 1.5 * tRate, "constructing at a rate takes %.3f ms against setSampleRate's %.3f ms (want under 1.5x)",
	      1e3 * tBuild, 1e3 * tRate);
}

void bad_rate_ignored() {
	Rig ref, g;
	ref.levels(0.f, 1.f, 0.f);
	g.levels(0.f, 1.f, 0.f);
	for (float bad : {0.f, -1.f, NAN, INFINITY, 1e9f})
		g.e.setSampleRate(bad);
	Out a = run(ref, 2.f), b = run(g, 2.f);
	CHECK(a.hiss == b.hiss, "unusable sample rates must be ignored (output changed)");
}

void nan_cv_safe() {
	for (float bad : {NAN, INFINITY, -INFINITY}) {
		Rig g;
		g.levels(0.5f, 0.5f, 0.5f);
		for (int i = 0; i < 5; i++) {
			g.f.params[F::DEPTH_ATT + i] = 1.f;
			g.cvv[i][0] = bad;
			g.f.cv[i].channels = 1;
		}
		Out o = run(g, 1.f);
		bool ok = true;
		for (size_t i = 0; i < o.l.size(); i++)
			ok = ok && o.l[i] == o.l[i] && o.r[i] == o.r[i] && std::fabs(o.l[i]) <= 10.f && std::fabs(o.r[i]) <= 10.f;
		CHECK(ok, "a non-finite CV must leave the outputs finite and bounded");
	}
}

} // namespace

int main() {
	full_levels();
	audio_taper();
	mix_ratio();
	cv_law();
	poly_cv_uses_channel_1();
	patched_layer_leaves_mix();
	patch_unpatch_no_click();
	mono_normal();
	mono_normal_switch_no_click();
	nothing_patched_no_work();
	square_cv_no_zipper();
	drift_zero_static();
	drift_full_moves_within_ranges();
	headroom();
	stereo_mix();
	seeds_differ_and_repeat();
	reset_matches_fresh();
	reset_is_cheap();
	constructed_at_rate();
	construction_builds_tables_once();
	bad_rate_ignored();
	nan_cv_safe();
	return rkatest::finish("hadal test_engine");
}
