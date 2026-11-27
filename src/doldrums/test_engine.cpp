// Doldrums' engine (doldrums/DoldrumsEngine.hpp): the pieces of Tasks 3 to 7
// tied to a frame of params and inputs, the staging, RUN, Mix and the rate,
// against the design's "Controls and behaviour", "Staging" and "Testing"
// (Repeatable, Stretch, Held, No clicks, the patch it was asked for, Staging)
// and the plan's Global Constraints; and Task 11's tapes handed in (a swap
// under the running engine, its tickets, the conversion at a rate change, the
// hold for the save, the frozen tape's number). 48 kHz and the Medium grain unless a
// test says otherwise; fixed seeds. Where a test needs a full tape it writes
// it straight through ring() (fast), then drives the engine sample by sample.
#include "doldrums/DoldrumsEngine.hpp"
#include "testing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace rka::doldrums;
using rka::hadal::Random;
typedef DoldrumsFrame F;

const double kPi = 3.14159265358979323846;

double db(double powerRatio) { return 10.0 * std::log10(powerRatio); }

// One engine at a rate, with its frame and the samples it has made.
struct Rig {
	std::unique_ptr<DoldrumsEngine> e;
	F f;
	DoldrumsResult r;
	float rate;
	long long t = 0;   // samples processed since setSampleRate
	std::vector<float> L, R, run;
	std::vector<int> staged;
	bool keep = true;

	explicit Rig(float rate_ = 48000.f, uint32_t seed = 1, int grain = GRAIN_MEDIUM) : e(new DoldrumsEngine), rate(rate_) {
		CHECK(e->allocate(rate), "allocate(%g)", rate);
		e->setSampleRate(rate);
		e->seed(seed);
		f.grainSize = grain;
		f.params[F::MIX] = 1.f;        // all wet, as these tests were written (the default moved to 0.5 at 2.0.49, the user's decision)
		f.params[F::POSITION] = 1.f;   // Position at now, as these tests were written (the default moved to 0.9 at 2.0.46; its own effect is test_engine's default_position_freezes_a_stretch)
	}
	int grain() const { return grainFrames(GrainSize(f.grainSize), rate); }
	int hop() const { return grain() / 2; }
	long long secs(double s) const { return (long long) std::llround(s * rate); }
	void step() {
		e->process(f, r);
		t++;
		if (keep) {
			L.push_back(r.outL);
			R.push_back(r.outR);
			run.push_back(r.run);
			staged.push_back(e->staged());
		}
	}
	template <class Fn> void go(long long n, Fn&& each) {
		for (long long i = 0; i < n; i++) {
			each(t);
			step();
		}
	}
	void go(long long n) {
		go(n, [](long long) {});
	}
	// The whole tape written straight, frame i of the tape from src(i, l, r).
	template <class Src> void tape(Src&& src) {
		Ring& ring = e->ring();
		for (long long i = 0; i < ring.capacity(); i++) {
			float l, r2;
			src(i, l, r2);
			ring.write(l, r2, false);
		}
	}
	// Samples since setSampleRate at which a grain boundary ran.
	std::vector<long long> boundaries() const {
		std::vector<long long> b;
		for (size_t i = 0; i < staged.size(); i++)
			if (staged[i] & DoldrumsEngine::STAGED_BOUNDARY) b.push_back((long long) i);
		return b;
	}
};

// RUN's edges: (sample, high).
std::vector<std::pair<long long, bool>> edges(const std::vector<float>& run, long long from = 0, long long to = -1) {
	std::vector<std::pair<long long, bool>> e;
	if (to < 0) to = (long long) run.size();
	bool last = from > 0 ? run[size_t(from - 1)] > 5.f : false;
	for (long long i = from; i < to; i++) {
		const bool h = run[size_t(i)] > 5.f;
		if (h != last) e.push_back({i, h});
		last = h;
	}
	return e;
}

// The shortest high stretch of RUN that ends (Eddy keeps a REC gate's recording of 50 ms or more).
long long shortestHigh(const std::vector<std::pair<long long, bool>>& e) {
	long long best = -1, up = -1;
	for (const auto& x : e) {
		if (x.second) up = x.first;
		else if (up >= 0) {
			const long long len = x.first - up;
			if (best < 0 || len < best) best = len;
		}
	}
	return best;
}

// Power spectrum of y[a, a + n), summed over Hann frames of 32768, half overlapped
// (compare spans of the same length).
struct Spectrum {
	std::vector<double> p;
	double binHz = 0;
	double band(double f, double half) const {
		const int a = std::max(1, int((f - half) / binHz)), b = std::min(int(p.size()) - 1, int((f + half) / binHz) + 1);
		double s = 0;
		for (int k = a; k <= b; k++) s += p[size_t(k)];
		return s;
	}
	double total() const {
		double s = 0;
		for (size_t k = 1; k < p.size(); k++) s += p[k];
		return s;
	}
};

Spectrum spectrum(const std::vector<float>& y, long long a, long long n, float rate) {
	const int N = 32768;
	rka::RealFft fft(N);
	float* in = rka::RealFft::alloc(N);
	float* out = rka::RealFft::alloc(N);
	Spectrum s;
	s.p.assign(N / 2, 0.0);
	s.binHz = rate / double(N);
	for (long long at = a; at + N <= a + n; at += N / 2) {
		for (int i = 0; i < N; i++) in[i] = y[size_t(at + i)] * float(0.5 - 0.5 * std::cos(2.0 * kPi * i / N));
		fft.forward(in, out);
		for (int k = 1; k < N / 2; k++) s.p[size_t(k)] += double(out[2 * k]) * out[2 * k] + double(out[2 * k + 1]) * out[2 * k + 1];
	}
	rka::RealFft::free(in);
	rka::RealFft::free(out);
	return s;
}

double meanSquare(const std::vector<float>& y, long long a, long long n) {
	double s = 0;
	for (long long i = a; i < a + n; i++) s += double(y[size_t(i)]) * y[size_t(i)];
	return n > 0 ? s / double(n) : 0.0;
}

// The worst |y[n] - y[n - 1]| over [a, b), as a share of what a signal under
// fTop explains: 2 sin(pi fTop / rate) times the largest |y| within w samples.
// A smooth signal under fTop stays at or below 1; a step goes far over.
double slopeRatio(const std::vector<float>& y, long long a, long long b, double fTop, double rate, int w = 480) {
	const int block = 64;
	const long long n = (long long) y.size();
	std::vector<float> peak(size_t(n / block + 1), 0.f);
	for (long long i = 0; i < n; i++) peak[size_t(i / block)] = std::max(peak[size_t(i / block)], std::fabs(y[size_t(i)]));
	const double k = 2.0 * std::sin(kPi * fTop / rate);
	double worst = 0;
	for (long long i = std::max(1LL, a); i < std::min(b, n); i++) {
		float p = 0.f;
		for (long long j = std::max(0LL, (i - w) / block); j <= std::min(n - 1, i + w) / block; j++) p = std::max(p, peak[size_t(j)]);
		const double d = std::fabs(double(y[size_t(i)]) - y[size_t(i - 1)]);
		worst = std::max(worst, d / (k * std::max(double(p), 1e-3)));
	}
	return worst;
}

// The chord the held tests use.
const double kChord[3] = {220.0, 277.18, 329.63};
float chord(long long i, float rate) {
	double s = 0;
	for (double f : kChord) s += std::sin(2.0 * kPi * f * double(i) / rate);
	return float(s);
}
double chordShare(const Spectrum& s) {
	double c = 0;
	for (double f : kChord) c += s.band(f, 15.0);
	return c / s.total();
}

// ---------------------------------------------------------------------------

// A busy frame for sample n: every control moving, Freeze by button and gate,
// TRIG, a Grain and a Wide change, three V/OCT channels.
struct Script {
	Random noise;
	float voct[3] = {};
	float rate;
	explicit Script(float rate_) : rate(rate_) { noise.seed(99); }
	void frame(F& f, long long n) {
		const double s = double(n) / rate;
		f.inL = 2.f * noise.uniform();
		f.inR = float(std::sin(2.0 * kPi * 330.0 * s));
		f.inRPatched = true;
		f.params[F::STRETCH] = float(0.2 + 0.5 * s / 6.0);
		f.params[F::POSITION] = 0.9f;   // 3 s back: inside what has been recorded
		f.params[F::POSITION_ATT] = 0.05f;
		f.positionCv = float(int(s / 0.7) % 3);
		f.params[F::HARMONICS] = 0.5f;
		f.params[F::BLUR] = 0.3f;
		f.params[F::TILT] = 0.4f;
		f.params[F::SHIFT] = 3.f;
		f.params[F::PARTIALS] = 12.f;
		f.params[F::WIDTH] = 0.5f;
		f.params[F::MIX] = s < 5.5 ? 1.f : 0.6f;
		f.params[F::FREEZE] = s >= 2.0 && s < 3.5 ? 1.f : 0.f;
		f.freezeGate = s >= 4.0 && s < 4.5 ? 10.f : 0.f;
		f.trig = (s >= 1.3 && s < 1.31) || (s >= 4.8 && s < 4.81) ? 10.f : 0.f;
		voct[0] = 0.f;
		voct[1] = 0.25f;
		voct[2] = float(0.583 + 0.1 * std::sin(s));
		f.voct = voct;
		f.voctChannels = 3;
		f.grainSize = s < 3.0 ? GRAIN_MEDIUM : GRAIN_SHORT;
		f.wide = s >= 5.0;
	}
};

void repeatable_and_block_independent() {
	const float rate = 48000.f;
	const long long n = 6 * 48000;
	std::vector<float> a[3], b[3], d[3];
	{
		Rig g(rate, 7);
		g.keep = false;
		Script sc(rate);
		for (long long i = 0; i < n; i++) {
			sc.frame(g.f, i);
			g.step();
			a[0].push_back(g.r.outL);
			a[1].push_back(g.r.outR);
			a[2].push_back(g.r.run);
		}
	}
	{
		// The same, in bursts of 1 to 700 samples, with another engine's work
		// and every read-only call between them.
		Rig g(rate, 7), other(rate, 3);
		g.keep = other.keep = false;
		Script sc(rate), sc2(rate);
		Random burst;
		burst.seed(5);
		long long i = 0, j = 0, sink = 0;
		while (i < n) {
			const long long len = 1 + burst.next() % 700;
			for (long long k = 0; k < len && i < n; k++, i++) {
				sc.frame(g.f, i);
				g.step();
				b[0].push_back(g.r.outL);
				b[1].push_back(g.r.outR);
				b[2].push_back(g.r.run);
			}
			for (int k = 0; k < 37; k++, j++) {
				sc2.frame(other.f, j);
				other.f.params[F::TILT] = -0.7f;
				other.step();
			}
			sink += g.e->state().now + g.e->ring().now() + g.e->latencyFrames() + (long long) g.e->stretchPace() + g.e->staged();
		}
		CHECK(sink != 0, "the reads were made");
	}
	{
		Rig g(rate, 8);
		g.keep = false;
		Script sc(rate);
		for (long long i = 0; i < n; i++) {
			sc.frame(g.f, i);
			g.step();
			d[0].push_back(g.r.outL);
			d[1].push_back(g.r.outR);
			d[2].push_back(g.r.run);
		}
	}
	for (int c = 0; c < 3; c++) {
		CHECK(std::memcmp(a[c].data(), b[c].data(), sizeof(float) * size_t(n)) == 0, "output %d: the same bits in bursts as sample by sample", c);
		long long firstDiff = -1;
		for (long long i = 0; i < n && firstDiff < 0; i++)
			if (a[c][size_t(i)] != b[c][size_t(i)]) firstDiff = i;
		CHECK(firstDiff < 0, "output %d first differs at %lld", c, firstDiff);
	}
	CHECK(std::memcmp(a[0].data(), d[0].data(), sizeof(float) * size_t(n)) != 0, "another seed: another output");
	double energy = meanSquare(a[0], n / 2, n / 2);
	CHECK(energy > 1e-3, "the script sounds (%g)", energy);
}

// A tape with a 5 ms burst of 1 kHz every second (at the half seconds),
// frozen, Position fully left: the bursts come out Stretch seconds apart.
void stretch_markers() {
	const float rate = 48000.f;
	const double stretches[3] = {2.0, 8.0, 32.0};
	const double seconds[3] = {22.0, 45.0, 95.0};
	for (int si = 0; si < 3; si++) {
		const double S = stretches[si];
		Rig g(rate, 11);
		const long long burst = g.secs(0.005);
		g.tape([&](long long i, float& l, float& r) {
			const long long at = (i + 24000) % 48000;   // bursts start at i = 24000 + k 48000
			float v = 0.f;
			if (at < burst) v = float(5.0 * std::sin(kPi * double(at) / double(burst)) * std::sin(2.0 * kPi * 1000.0 * double(i) / rate));
			l = r = v;
		});
		g.f.params[F::FREEZE] = 1.f;
		g.f.params[F::POSITION] = 0.f;
		g.f.params[F::STRETCH] = float(std::log2(S) / 6.0);
		g.go(g.secs(seconds[si]));
		// Energy per block of 512; a hump is a run of blocks with any energy.
		const int B = 512;
		std::vector<double> e(g.L.size() / B, 0.0);
		for (size_t i = 0; i < e.size() * B; i++) e[i / B] += double(g.L[i]) * g.L[i];
		std::vector<double> centres;
		size_t i = 0;
		while (i < e.size()) {
			if (e[i] <= 0.0) {
				i++;
				continue;
			}
			size_t j = i;
			double w = 0, m = 0;
			while (j < e.size() && e[j] > 0.0) {
				w += e[j];
				m += e[j] * (double(j) + 0.5) * B;
				j++;
			}
			if (i > 0 && j < e.size()) centres.push_back(m / w);   // whole humps only
			i = j;
		}
		CHECK(centres.size() >= 2, "x%g: %zu whole bursts heard", S, centres.size());
		for (size_t k = 1; k < centres.size(); k++) {
			const double gap = (centres[k] - centres[k - 1]) / rate;
			CHECK(std::fabs(gap - S) * rate <= g.grain(), "x%g: bursts %zu and %zu %.3f s apart (want %g within a grain)", S, k - 1, k, gap, S);
		}
	}
}

// Defaults: noise, then a chord, then silence in; Freeze pressed during the
// chord. Within a grain the output is the chord and stays so for 60 s.
void freeze_holds_the_moment() {
	const float rate = 48000.f;
	Rig g(rate, 21);
	Random noise;
	noise.seed(4);
	const long long chordAt = g.secs(2.0), press = g.secs(2.6), silence = g.secs(3.0);
	g.go(press + g.secs(61.0), [&](long long t) {
		g.f.inL = t < chordAt ? 1.5f * noise.uniform() : t < silence ? chord(t, rate) : 0.f;
		g.f.params[F::FREEZE] = t >= press ? 1.f : 0.f;
	});
	const long long from = press + g.grain() + g.e->latencyFrames();
	const Spectrum first = spectrum(g.L, from, g.secs(1.0), rate);
	CHECK(chordShare(first) > 0.95, "within a grain of the press: the chord (share %.4f)", chordShare(first));
	double levels[6];
	for (int k = 0; k < 6; k++) {
		const long long a = from + g.secs(10.0 * k);
		const Spectrum s = spectrum(g.L, a, g.secs(10.0), rate);
		CHECK(chordShare(s) > 0.97, "10 s block %d: the chord (share %.4f)", k, chordShare(s));
		levels[k] = meanSquare(g.L, a, g.secs(10.0));
		CHECK(levels[k] > 0.3, "10 s block %d sounds (%g)", k, levels[k]);
	}
	for (int k = 1; k < 6; k++)
		CHECK(std::fabs(db(levels[k] / levels[0])) < 1.5, "10 s block %d within 1.5 dB of the first (%.2f dB)", k, db(levels[k] / levels[0]));
	long long low = 0;
	for (long long t = from; t < (long long) g.run.size(); t++) low += g.run[size_t(t)] < 5.f;
	CHECK(low == 0, "RUN high throughout (%lld samples low)", low);
	CHECK(g.e->state().held && g.e->state().frozen, "held and frozen");
}

// Stretch at the stop, not frozen, then a minute of something else recorded
// over the whole tape: the held chord does not change.
void held_survives_overwriting() {
	const float rate = 48000.f;
	Rig g(rate, 22);
	const long long hold = g.secs(8.0), end = hold + g.secs(60.0);
	g.go(end, [&](long long t) {
		g.f.inL = t < hold ? chord(t, rate) : float(1.5 * std::sin(2.0 * kPi * 1500.0 * double(t) / rate));
		g.f.params[F::STRETCH] = t < hold ? 0.5f : 1.f;
	});
	const long long a = hold + g.secs(1.0);
	const Spectrum early = spectrum(g.L, a, g.secs(10.0), rate);
	const Spectrum late = spectrum(g.L, end - g.secs(10.0), g.secs(10.0), rate);
	CHECK(chordShare(early) > 0.97 && chordShare(late) > 0.97, "the chord, early %.4f and late %.4f", chordShare(early), chordShare(late));
	const double other = late.band(1500.0, 30.0) / late.total();
	CHECK(other < 1e-6, "nothing of the new input in the held sound (%.1f dB)", db(other + 1e-30));
	const double lv = db(meanSquare(g.L, end - g.secs(10.0), g.secs(10.0)) / meanSquare(g.L, a, g.secs(10.0)));
	CHECK(std::fabs(lv) < 1.5, "level after a minute within 1.5 dB (%.2f dB)", lv);
	for (int k = 0; k < 3; k++) {
		const double d = db(late.band(kChord[k], 15.0) / early.band(kChord[k], 15.0));
		CHECK(std::fabs(d) < 3.0, "note %d within 3 dB (%.2f dB)", k, d);
	}
	CHECK(g.e->state().held && !g.e->state().frozen, "held, recording");
}

// RUN against tables written from the design's "Play once, and RUN".
struct RunCase {
	const char* name;
	std::vector<std::pair<long long, bool>> want, got;
};

void compare(const char* name, const std::vector<std::pair<long long, bool>>& want, const std::vector<std::pair<long long, bool>>& got) {
	CHECK(want.size() == got.size(), "%s: %zu edges, want %zu", name, got.size(), want.size());
	for (size_t i = 0; i < std::min(want.size(), got.size()); i++)
		CHECK(want[i] == got[i], "%s: edge %zu at %lld %s, want %lld %s", name, i, got[i].first, got[i].second ? "up" : "down", want[i].first,
		      want[i].second ? "up" : "down");
	const long long s = shortestHigh(got);
	CHECK(s < 0 || s >= 48000 / 20, "%s: every high stretch is 50 ms or more (shortest %lld)", name, s);
}

void run_gate() {
	const float rate = 48000.f;
	auto noiseTape = [](Rig& g) {
		Random n;
		n.seed(31);
		g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
	};
	const int dip = runDipFrames(rate);
	// A frozen pass of P grains at Stretch 1 from a lag of about 2.2 s.
	auto setPass = [&](Rig& g, int& P) {
		const long long cap = g.e->ring().capacity();
		const float pos = float(1.0 - double(g.secs(2.2)) / double(cap - g.grain()));
		g.f.params[F::POSITION] = pos;
		g.f.params[F::STRETCH] = 0.f;
		g.f.params[F::FREEZE] = 1.f;
		P = int(lagFrames(pos, cap, g.grain()) / g.hop()) + 1;
	};
	const int lat = Rig(rate).e->latencyFrames();
	{   // go round: up once, a dip at each return
		Rig g(rate);
		noiseTape(g);
		int P;
		setPass(g, P);
		g.go(g.secs(8.0));
		const long long R0 = g.boundaries().at(1) + lat;   // the first boundary only primes the tables
		const long long H = g.hop();
		std::vector<std::pair<long long, bool>> want = {{R0, true}};
		for (int k = 1; R0 + k * P * H + dip < (long long) g.run.size(); k++) {
			want.push_back({R0 + k * P * H, false});
			want.push_back({R0 + k * P * H + dip, true});
		}
		compare("go round", want, edges(g.run));
		CHECK(want.size() >= 5, "go round: at least two returns (%zu edges)", want.size());
	}
	{   // stop, frozen from the first sample: waits; a trigger, one pass; Freeze
		// let go and pressed again while stopped: one pass from the flag by
		// itself (the user's ruling at 2.0.49), then stopped; a trigger, one more
		Rig g(rate);
		noiseTape(g);
		int P;
		setPass(g, P);
		g.f.stopAtEnd = true;
		const long long H = g.hop();
		auto firstBoundaryFrom = [&](long long T) {
			const std::vector<long long> b = g.boundaries();
			return *std::lower_bound(b.begin(), b.end(), T);
		};
		g.go(g.secs(0.5));
		const long long T = g.t + 1234;
		g.go(T - g.t + g.secs(6.0), [&](long long t) { g.f.trig = t >= T && t < T + 100 ? 10.f : 0.f; });
		const long long B = firstBoundaryFrom(T);
		g.f.params[F::FREEZE] = 0.f;
		g.go(g.secs(1.0));
		CHECK(g.e->state().stopped && edges(g.run, B + lat + (P + 1) * H + 1).empty(), "stop: a thaw while stopped starts nothing");
		const long long press = g.t;
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(6.0));
		const auto pe = edges(g.run, press);
		CHECK(pe.size() == 2 && pe[0].second && !pe[1].second && pe[0].first <= press + H + lat,
		      "stop: Freeze pressed while stopped plays one pass by itself (%zu edges)", pe.size());
		const long long P1 = pe.size() == 2 ? pe[0].first : press, P2 = pe.size() == 2 ? pe[1].first : press;
		CHECK(std::llabs((P2 - P1) - (P + 1) * H) <= H, "of the flag's lag (%lld frames, want about %lld)", P2 - P1, (P + 1) * H);
		CHECK(g.e->state().stopped && g.e->state().frozen, "then frozen and stopped");
		const long long T2 = g.t + 77;
		g.go(T2 - g.t + g.secs(6.0), [&](long long t) { g.f.trig = t >= T2 && t < T2 + 100 ? 10.f : 0.f; });
		const long long B2 = firstBoundaryFrom(T2);
		const auto te = edges(g.run, T2);
		CHECK(te.size() == 2 && te[0].second && te[0].first == B2 + lat && !te[1].second, "stop: and a trigger plays one more (%zu edges)", te.size());
		compare("stop", {{B + lat, true}, {B + lat + (P + 1) * H, false}}, edges(g.run, 0, press));
		long long loud = 0;
		for (long long t = 0; t < B + lat; t++) loud += g.L[size_t(t)] != 0.f;
		for (long long t = B + lat + (P + 1) * H; t < P1; t++) loud += g.L[size_t(t)] != 0.f;
		for (long long t = P2; t < B2 + lat; t++) loud += g.L[size_t(t)] != 0.f;
		CHECK(loud == 0, "stop: silent before the trigger and between the passes (%lld samples not)", loud);
	}
	for (int stop = 0; stop < 2; stop++) {   // TRIG in mid pass
		Rig g(rate);
		noiseTape(g);
		int P;
		setPass(g, P);
		g.f.stopAtEnd = stop == 1;
		const long long H = g.hop();
		g.go(2000, [&](long long t) { g.f.trig = stop && t >= 10 && t < 58 ? 10.f : 0.f; });   // in stop, a trigger starts the first pass
		const long long R0 = g.boundaries().at(1) + lat;
		const long long T = R0 + 2 * H + 100;
		g.go(T - g.t + P * H + 3 * H, [&](long long t) { g.f.trig = t >= T && t < T + 100 ? 10.f : 0.f; });
		const long long B = R0 - lat + 3 * H;
		std::vector<std::pair<long long, bool>> want = {{R0, true}, {B + lat, false}, {B + lat + dip, true}};
		if (stop) want.push_back({B + lat + (P + 1) * H, false});
		else {
			want.push_back({B + lat + P * H, false});
			want.push_back({B + lat + P * H + dip, true});
		}
		compare(stop ? "TRIG in mid pass, stop" : "TRIG in mid pass, go round", want, edges(g.run));
	}
	for (int stop = 0; stop < 2; stop++) {   // held at the Stretch stop, recording
		Rig g(rate);
		Random n;
		n.seed(3);
		g.f.params[F::STRETCH] = 1.f;
		g.f.stopAtEnd = stop == 1;
		const long long T = g.secs(1.0);
		g.go(g.secs(40.0), [&](long long t) {
			g.f.inL = n.uniform();
			g.f.trig = stop && t >= T && t < T + 100 ? 10.f : 0.f;
		});
		const auto e = edges(g.run);
		CHECK(e.size() == 1 && e[0].second, "held (%s): RUN up once and never down (%zu edges)", stop ? "stop" : "go round", e.size());
		if (stop && !e.empty()) CHECK(e[0].first > T, "held, stop: waits for the trigger (up at %lld)", e[0].first);
	}
	{   // nothing to travel through: frozen at Position fully right, stop
		Rig g(rate);
		noiseTape(g);
		g.f.stopAtEnd = true;
		g.go(g.secs(1.0));
		CHECK(edges(g.run).empty(), "stop: a patch waits for a trigger");
		g.f.params[F::FREEZE] = 1.f;
		const long long press = g.t;
		g.go(g.secs(30.0));
		const auto e = edges(g.run);
		CHECK(e.size() == 1 && e[0].second && e[0].first > press && e[0].first <= press + g.hop() + lat,
		      "nothing to travel through: up within a hop of the freeze and never down (%zu edges)", e.size());
		CHECK(g.e->state().held, "held");
	}
	{   // stop, a recording pass running (by TRIG): Freeze starts a pass from
		// the flag (RUN dips, as at any new pass under a high RUN), which plays
		// once to the end of the frozen tape; then stopped and silent until a
		// trigger, which plays one more (Pass.hpp's rule)
		Rig g(rate);
		Random n;
		n.seed(5);
		g.f.stopAtEnd = true;
		g.f.params[F::POSITION] = 0.9f;   // frozen, a pass of about 3 s at Stretch 1
		g.f.params[F::STRETCH] = 0.f;
		g.go(g.secs(4.0), [&](long long t) {
			g.f.inL = n.uniform();
			g.f.trig = t >= 480 && t < 528 ? 10.f : 0.f;
		});
		CHECK(g.run.back() > 5.f && !g.e->state().stopped, "stop, recording: a pass running");
		const long long press = g.t;
		const long long lag = lagFrames(0.9f, g.e->ring().capacity(), g.grain());
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(6.0), [&](long long) { g.f.inL = n.uniform(); });
		auto e = edges(g.run, press);
		CHECK(e.size() == 3 && !e[0].second && e[1].second && !e[2].second && e[1].first <= press + g.hop() + lat + dip,
		      "Freeze in a running pass: RUN dips for the new pass, then falls at its end (%zu edges)", e.size());
		if (e.size() == 3) {
			CHECK(std::llabs((e[2].first - e[1].first) - lag) <= 2 * g.grain(), "the pass is the flag's lag long (%lld frames, want about %lld)",
			      e[2].first - e[1].first, lag);
			CHECK(meanSquare(g.L, e[1].first, e[2].first - e[1].first) > 0.01, "and sounds");
			CHECK(meanSquare(g.L, e[2].first, (long long) g.L.size() - e[2].first) == 0.0 && g.e->state().stopped && g.e->state().frozen,
			      "then silent, frozen and stopped");
		}
		const long long T = g.t;
		g.go(g.secs(6.0), [&](long long t) { g.f.trig = t >= T && t < T + 48 ? 10.f : 0.f; });
		e = edges(g.run, T);
		CHECK(e.size() == 2 && e[0].second && !e[1].second, "and a trigger plays one pass (%zu edges)", e.size());
	}
	{   // frozen and stopped, Position turned fully right: nothing to travel
		// through, so held and sounding at once (as the same state opened from
		// a patch is)
		Rig g(rate);
		noiseTape(g);
		g.f.stopAtEnd = true;
		g.f.params[F::POSITION] = 0.5f;
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(1.0));
		CHECK(edges(g.run).empty() && g.e->state().stopped, "frozen in stop: stopped");
		const long long turn = g.t;
		g.f.params[F::POSITION] = 1.f;
		g.go(g.secs(5.0));
		const auto e = edges(g.run, turn);
		CHECK(e.size() == 1 && e[0].second && e[0].first <= turn + g.hop() + lat, "Position fully right: up within a hop (%zu edges)", e.size());
		CHECK(g.e->state().held && !g.e->state().stopped, "and held");
	}
}

// The patch the module was asked for (Eddy A's clip into Doldrums into Eddy
// B), as the manual's recipe has it: Stop chosen while the module plays,
// Position typed a little past the clip's length, Stretch at x1, the clip
// played, Freeze pressed as it ends (a pass plays once from the flag, then
// FROZEN STOPPED), the flag set on the clip's start, Stretch set to 4, a
// trigger: RUN high once for the pass, and nothing heard outside it once the
// freeze's own pass has ended.
void the_patch_it_was_asked_for() {
	const float rate = 48000.f;
	Rig g(rate, 41);
	Random n;
	n.seed(6);
	const long long clipAt = g.secs(2.0), clipEnd = g.secs(5.0);
	const long long cap = g.e->ring().capacity();
	const long long fade = g.secs(0.005);
	const long long lag = clipEnd - clipAt + fade;   // now at the freeze is the clip's end plus the fade
	auto posFor = [&](long long l) { return float(1.0 - double(l) / double(cap - g.grain())); };
	g.f.params[F::STRETCH] = 0.f;   // x1 until the freeze's pass has played
	auto clip = [&](long long t) {
		return t >= clipAt && t < clipEnd + fade ? float(n.uniform() * (1.0 + std::sin(2.0 * kPi * 3.0 * double(t) / rate))) : 0.f;
	};
	// Go round at first (the module plays); then Stop, and Position typed.
	g.go(g.secs(1.0), [&](long long t) { g.f.inL = clip(t); });
	CHECK(g.run.back() > 5.f, "go round: sounding");
	g.f.stopAtEnd = true;
	g.f.params[F::POSITION] = posFor(lag + g.secs(1.0));
	g.go(clipEnd - g.t, [&](long long t) { g.f.inL = clip(t); });
	CHECK(g.run.back() > 5.f && !g.e->state().stopped, "Stop chosen: the pass under way goes on");
	// Freeze as the clip ends: one pass from the flag, at x1 the flag's lag
	// long (the clip and the second before it), then FROZEN STOPPED, RUN low.
	const long long press = g.t;
	g.f.params[F::FREEZE] = 1.f;
	g.go(lag + g.secs(1.0) + g.secs(1.0), [&](long long t) { g.f.inL = clip(t); });
	const auto pe = edges(g.run, press);
	CHECK(pe.size() == 3 && !pe[0].second && pe[1].second && !pe[2].second, "Freeze: RUN dips for its pass, then falls (%zu edges)", pe.size());
	if (pe.size() == 3)
		CHECK(std::llabs((pe[2].first - pe[1].first) - (lag + g.secs(1.0))) <= 2 * g.grain(), "the freeze's pass is the flag's lag long (%lld frames)",
		      pe[2].first - pe[1].first);
	CHECK(g.e->state().stopped && g.e->state().frozen && g.run.back() < 5.f, "then frozen and stopped, RUN low");
	// The flag on the clip's start and Stretch to 4 (nothing sounds
	// meanwhile), then a trigger.
	const long long quiet = g.t;
	g.f.params[F::POSITION] = posFor(lag);
	g.f.params[F::STRETCH] = float(2.0 / 6.0);   // x4
	g.go(g.secs(1.0));
	const long long T = g.t;
	g.go(g.secs(30.0), [&](long long t) { g.f.trig = t >= T && t < T + 48 ? 10.f : 0.f; });
	std::vector<std::pair<long long, bool>> e = {{quiet, false}};
	for (const auto& x : edges(g.run, quiet)) e.push_back(x);
	CHECK(e.size() == 3 && !e[0].second && e[1].second && !e[2].second && e[1].first > T,
	      "RUN low after the freeze's pass, then high once for the pass (%zu edges)", e.size() - 1);
	if (e.size() == 3) {
		const double len = double(e[2].first - e[1].first) / rate;
		CHECK(std::fabs(len - 12.0) * rate <= 2.0 * g.grain(), "for 12 s within two grains (%.3f s)", len);
		long long first = -1, last = -1;
		double in = 0, out = 0;
		for (long long t = e[0].first; t < (long long) g.L.size(); t++) {
			const double p = double(g.L[size_t(t)]) * g.L[size_t(t)] + double(g.R[size_t(t)]) * g.R[size_t(t)];
			if (p > 0) {
				if (first < 0) first = t;
				last = t;
			}
			(t >= e[1].first && t < e[2].first ? in : out) += p;
		}
		CHECK(out <= in * 1e-8, "the wet outside RUN under -80 dB (%.1f dB)", db(out / in + 1e-30));
		CHECK(first >= e[1].first && last < e[2].first, "sound from %lld to %lld, RUN high from %lld to %lld", first, last, e[1].first,
		      e[2].first);
		CHECK(in > 0, "the pass sounds");
	}
}

// The user's ruling (Pass.hpp): a thaw returns the read point to the anchor
// at once. Frozen at Position fully right on a chord for 20 s while a 1 kHz
// tone arrives at the input; at the thaw, within a grain and the latency the
// output is the live tone, not the frozen chord (before the ruling it went on
// from the frozen moment for about 34 s at x8). In go round RUN dips at the
// new pass, as at any new pass under a high RUN.
void thaw_returns_to_live() {
	const float rate = 48000.f;
	for (int run = 0; run < 3; run++) {
		const int stop = run == 1;
		const double stretch = run == 2 ? 8.0 : 1.0;
		Rig g(rate, 131);
		g.f.stopAtEnd = stop != 0;
		g.f.params[F::STRETCH] = run == 2 ? 0.5f : 0.f;
		g.f.params[F::FREEZE] = 0.f;
		g.go(g.secs(3.0), [&](long long t) {
			g.f.inL = chord(t, rate);
			g.f.trig = stop && t >= 480 && t < 528 ? 10.f : 0.f;   // in stop, a pass by TRIG
		});
		g.f.params[F::FREEZE] = 1.f;
		auto tone = [&](long long t) { return float(std::sin(2.0 * kPi * 1000.0 * double(t) / rate)); };
		g.go(g.secs(20.0), [&](long long t) { g.f.inL = tone(t); });
		char set[40];
		std::snprintf(set, sizeof set, "%s, x%g", stop ? "stop" : "go round", stretch);
		CHECK(g.e->state().held && chordShare(spectrum(g.L, g.t - g.secs(4.0), g.secs(4.0), rate)) > 0.95, "%s: frozen, the chord held", set);
		const long long thaw = g.t;
		g.f.params[F::FREEZE] = 0.f;
		g.go(g.secs(4.0), [&](long long t) { g.f.inL = tone(t); });
		// The slice at the anchor ends at now: its first grains hold the frozen
		// tape's last frames, read at the pace, so the live input fills a slice
		// after a grain at x1 and after a grain times the stretch at x8 (the
		// new pass reads what came after the thaw at its own pace).
		const long long from = thaw + (long long) (stretch * g.grain()) + g.e->latencyFrames() + g.hop();
		g.go(from + g.secs(2.0) - g.t, [&](long long t) { g.f.inL = tone(t); });
		const Spectrum sp = spectrum(g.L, from, g.secs(2.0), rate);
		const double live = sp.band(1000.0, 15.0) / sp.total(), held = chordShare(sp);
		std::printf("  thaw_returns_to_live (%s): from %.2f s after the thaw, %.3f of the power at the live 1 kHz, %.3f at the chord\n", set,
		            double(from - thaw) / rate, live, held);
		CHECK(live > 0.9 && held < 0.02, "%s: after the thaw the live input (%.3f) not the frozen chord (%.3f)", set, live, held);
		// RUN: a new pass under a high RUN dips it for 1 ms, as at any new pass.
		const auto e = edges(g.run, thaw);
		CHECK(!g.e->state().held && g.run.back() > 5.f && e.size() == 2u && e[1].first - e[0].first == runDipFrames(rate),
		      "%s: RUN dips once at the thaw's new pass (%zu edges)", set, e.size());
	}
}

// Stopped (stop, the pass ended, waiting for TRIG): a thaw starts nothing,
// RUN stays low and the output silent.
void thaw_while_stopped_stays_stopped() {
	const float rate = 48000.f;
	Rig g(rate, 133);
	Random n;
	n.seed(23);
	g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
	g.f.stopAtEnd = true;
	g.f.params[F::POSITION] = 0.9f;   // frozen, a pass of about 3 s at Stretch 1
	g.f.params[F::STRETCH] = 0.f;
	g.f.params[F::FREEZE] = 1.f;
	g.go(g.secs(1.0));
	const long long T = g.t;
	g.go(g.secs(5.0), [&](long long t) { g.f.trig = t >= T && t < T + 48 ? 10.f : 0.f; });
	CHECK(g.e->state().stopped && g.run.back() < 5.f && edges(g.run, T).size() == 2u, "the pass by TRIG played and stopped");
	const long long thaw = g.t;
	g.f.params[F::FREEZE] = 0.f;
	g.go(g.secs(4.0), [&](long long) { g.f.inL = n.uniform(); });
	CHECK(edges(g.run, thaw).empty() && g.run.back() < 5.f, "thawed while stopped: RUN low throughout");
	CHECK(meanSquare(g.L, thaw, g.t - thaw) == 0.0 && g.e->state().stopped && !g.e->state().frozen, "and silent, stopped, recording");
}

// The user's rulings: a press in the tape window plays from there, and moves
// the read head only. The module raises DoldrumsFrame::press with the place
// (a fraction of the window); at the next grain the read point is there, a
// new pass (RUN's marker as for TRIG), and the flag has not moved. The knob
// and the CV keep the Position rule: the flag moves, the read point moves
// by the change, no new pass.
// Steps until the next boundary has run; returns its sample.
long long toBoundary(Rig& g) {
	for (int i = 0; i < 200000; i++) {
		g.step();
		if (g.staged.back() & DoldrumsEngine::STAGED_BOUNDARY) return g.t - 1;
	}
	return -1;
}
float posAt(const Rig& g, long long anchor) {
	const long long cap = g.e->state().capacity, now = g.e->state().now;
	return float(1.0 - double(now - anchor) / double(cap - g.grain()));
}
// The window's fraction for a read point `back` frames behind now.
float fractionBack(const Rig& g, long long back) { return float(1.0 - double(back) / double(g.e->state().capacity)); }
void press(Rig& g, float at) {
	g.f.press = true;
	g.f.pressAt = at;
	g.step();
	g.f.press = false;
}

void window_press_moves_the_read_head_only() {
	const float rate = 48000.f;
	for (int byPress = 0; byPress < 2; byPress++) {
		Rig g(rate, 141);
		Random n;
		n.seed(9);
		g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
		g.f.params[F::FREEZE] = 1.f;
		g.f.params[F::POSITION] = 0.f;   // the flag fully left
		g.f.params[F::STRETCH] = 0.f;    // x1: the head travels a second a second
		g.go(g.secs(4.0));
		toBoundary(g);
		const long long head = g.e->state().end, flag = g.e->state().anchor, now = g.e->state().now;
		CHECK(head - flag > g.secs(3.0), "the head has travelled from the flag (%lld frames)", head - flag);
		// A second behind the head: by a press there, or by the knob.
		const long long want = head - g.secs(1.0);
		const long long at = g.t;
		if (byPress) press(g, fractionBack(g, now - want));
		else g.f.params[F::POSITION] = posAt(g, want);
		toBoundary(g);
		const DoldrumsState st = g.e->state();
		if (byPress) {
			CHECK(std::llabs(st.end - want) <= 1, "a press: the head there at the next grain (end %lld, want %lld)", st.end, want);
			CHECK(st.anchor == flag, "a press: the flag has not moved (%lld, was %lld)", st.anchor, flag);
			g.go(g.grain() + g.e->latencyFrames());
			const auto e = edges(g.run, at);
			CHECK(e.size() == 2 && !e[0].second && e[1].first - e[0].first == runDipFrames(rate), "a press: RUN's 1 ms marker, as TRIG's (%zu edges)",
			      e.size());
			// It plays on from there at the pace.
			const long long e0 = g.e->state().end;
			g.go(g.secs(1.0));
			toBoundary(g);
			CHECK(std::llabs((g.e->state().end - e0) - g.secs(1.0)) <= 2 * g.hop() && g.e->state().anchor == flag,
			      "and travels on from there at x1 (%lld frames in a second)", g.e->state().end - e0);
		} else {
			const long long moved = st.anchor - flag;
			CHECK(st.end - head >= moved && st.end - head <= moved + 2 * g.hop(), "the knob: the head moves by the change (%lld, change %lld)",
			      st.end - head, moved);
			g.go(g.grain() + g.e->latencyFrames());
			CHECK(edges(g.run, at).empty(), "the knob: no new pass");
		}
	}
	// Several presses between two boundaries (a fast drag) are one new pass,
	// from the last place.
	Rig g(rate, 143);
	Random n;
	n.seed(10);
	g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
	g.f.params[F::FREEZE] = 1.f;
	g.f.params[F::POSITION] = 0.f;
	g.f.params[F::STRETCH] = 0.f;
	g.go(g.secs(2.0));
	toBoundary(g);
	const long long at = g.t, flag = g.e->state().anchor;
	for (int k = 0; k < 5; k++) {
		press(g, 0.1f * float(k + 1));
		g.go(100);
	}
	const long long b = toBoundary(g);
	CHECK(b > 0 && b - at < g.hop(), "the presses fell inside one hop");
	const long long want = g.e->state().now - (long long) std::llround(0.5 * double(g.e->state().capacity));
	CHECK(std::llabs(g.e->state().end - want) <= 1 && g.e->state().anchor == flag, "at the last press's place (end %lld, want %lld)",
	      g.e->state().end, want);
	g.go(g.hop() * 3);
	const auto e = edges(g.run, at);
	CHECK(e.size() == 2, "five presses in a hop: one new pass (%zu edges)", e.size());
	// At the end of the pressed pass the head returns to the flag (Go round).
	g.go(g.secs(16.0));
	toBoundary(g);
	CHECK(g.e->state().end - flag >= 0 && g.e->state().end - flag < g.secs(2.0) && g.e->state().anchor == flag,
	      "after the pass's end, playing from the flag again (%lld frames past it)", g.e->state().end - flag);
}

// In the Stop setting a press starts a pass from its place, as a trigger
// does from the flag: RUN rises, the rest of the tape plays, and the module
// stops again.
void window_press_in_stop_starts_a_pass() {
	const float rate = 48000.f;
	Rig g(rate, 145);
	Random n;
	n.seed(11);
	g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
	g.f.stopAtEnd = true;
	g.f.params[F::FREEZE] = 1.f;
	g.f.params[F::POSITION] = 0.5f;
	g.f.params[F::STRETCH] = 0.f;
	g.go(g.secs(1.0));
	CHECK(edges(g.run).empty() && g.e->state().stopped, "frozen in stop: stopped, RUN low");
	const long long at = g.t, flag = g.e->state().anchor;
	press(g, fractionBack(g, g.secs(3.0)));   // 3 s from the end
	g.go(g.secs(6.0));
	const auto e = edges(g.run, at);
	CHECK(e.size() == 2 && e[0].second && !e[1].second, "a press in stop: one pass, RUN up and down (%zu edges)", e.size());
	if (e.size() == 2)
		CHECK(std::llabs((e[1].first - e[0].first) - g.secs(3.0)) <= 2 * g.grain(), "of the tape from the press to the end (%lld frames)",
		      e[1].first - e[0].first);
	CHECK(g.e->state().stopped && g.e->state().anchor == flag, "and stopped again, the flag where it was");
}

// Frozen at the end (the flag fully right, nothing to travel through): a
// press plays from its place to the end of the tape once, then the hold at
// the end again, in either setting; the flag stays at the end throughout.
void window_press_at_the_end_plays_and_holds_again() {
	const float rate = 48000.f;
	for (int stop = 0; stop < 2; stop++) {
		Rig g(rate, 149);
		Random n;
		n.seed(13);
		g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
		g.f.stopAtEnd = stop != 0;
		g.f.params[F::FREEZE] = 1.f;
		g.f.params[F::POSITION] = 1.f;
		g.f.params[F::STRETCH] = 0.f;
		g.go(g.secs(1.0));
		const char* set = stop ? "stop" : "go round";
		CHECK(g.e->state().atEnd && g.e->state().held, "%s: frozen at the end, held", set);
		const long long now = g.e->state().now;
		press(g, fractionBack(g, g.secs(2.0)));
		toBoundary(g);
		CHECK(std::llabs(g.e->state().end - (now - g.secs(2.0))) <= 1 && !g.e->state().held && !g.e->state().atEnd && g.e->state().anchor == now,
		      "%s: a press: the head 2 s back, playing, the flag still at the end", set);
		g.go(g.secs(1.0));
		CHECK(!g.e->state().held && !g.e->state().stopped && g.e->state().end > now - g.secs(1.5), "%s: travelling to the end", set);
		const long long mid = g.t;
		g.go(g.secs(3.0));
		CHECK(g.e->state().atEnd && g.e->state().held && !g.e->state().stopped && g.e->state().end == now && g.run.back() > 5.f,
		      "%s: then held at the end again", set);
		CHECK(meanSquare(g.L, mid, g.t - mid) > 0.01, "%s: sounding throughout", set);
		long long low = 0;
		for (long long t = mid; t < g.t; t++) low += g.run[size_t(t)] < 5.f;
		CHECK(low <= runDipFrames(rate), "%s: RUN never falls, but for the new pass's marker (%lld samples low)", set, low);
	}
}

// Recording in Drift, a press sends the head to its place behind now, and it
// plays on from there; where the head is tied to the flag (recording at
// exactly Stretch 1, or following) a press is a trigger: a pass from the flag.
void window_press_when_recording() {
	const float rate = 48000.f;
	Rig g(rate, 147);
	Random n;
	n.seed(12);
	g.f.params[F::POSITION] = 0.9f;
	g.go(g.secs(6.0), [&](long long) { g.f.inL = n.uniform(); });
	toBoundary(g);
	const long long behind = g.e->state().anchor - g.e->state().end;
	CHECK(behind > g.secs(2.0), "recording at x8: the head has fallen behind the flag (%lld frames)", behind);
	press(g, fractionBack(g, g.secs(1.0)));
	toBoundary(g);
	DoldrumsState st = g.e->state();
	CHECK(std::llabs((st.now - st.end) - g.secs(1.0)) <= g.hop() && st.now - st.anchor > g.secs(2.5),
	      "a press 1 s back: the head there, ahead of the flag (%lld frames behind now; the flag %lld)", st.now - st.end, st.now - st.anchor);
	for (int follow = 0; follow < 2; follow++) {
		Rig t(rate, 151);
		t.f.params[F::POSITION] = 0.9f;
		t.f.params[F::STRETCH] = follow ? 0.5f : 0.f;
		t.f.follow = follow != 0;
		t.go(t.secs(4.0), [&](long long) { t.f.inL = n.uniform(); });
		const long long at = t.t;
		press(t, 0.5f);
		toBoundary(t);
		CHECK(t.e->state().end == t.e->state().anchor, "%s: a press leaves the head on the flag", follow ? "following" : "recording at x1");
		t.go(t.grain() + t.e->latencyFrames());
		const auto e = edges(t.run, at);
		CHECK(e.size() == 2 && !e[0].second, "%s: and is a trigger, a new pass (%zu edges)", follow ? "following" : "recording at x1", e.size());
	}
}

// The user's decision at 2.0.46: Position's default is 0.9, about 3 s back
// (the lag is (1 - p)(30 s - a grain)), so that at the defaults a freeze has
// tape for Stretch to act on: Freeze starts a pass 2.97 s back, which travels
// to the end of the tape at x8 (about 24 s) and returns (Go round). And
// frozen with the flag at the end, the engine says so (atEnd), apart from
// being held by Stretch's stop.
void default_position_freezes_a_stretch() {
	const float rate = 48000.f;
	Rig g(rate, 151);
	g.f.params[F::POSITION] = F().params[F::POSITION];   // the default, not the Rig's
	CHECK(F().params[F::POSITION] == 0.9f, "the default Position is 0.9: %g", F().params[F::POSITION]);
	Random n;
	n.seed(13);
	g.go(g.secs(5.0), [&](long long) { g.f.inL = n.uniform(); });
	const long long lag = lagFrames(0.9f, g.e->state().capacity, g.grain());
	CHECK(std::fabs(double(lag) / rate - 2.97) < 0.01, "the default lag: %.3f s", double(lag) / rate);
	g.f.params[F::FREEZE] = 1.f;
	const long long press = g.t;
	g.go(g.secs(60.0));
	const DoldrumsState st = g.e->state();
	CHECK(!st.atEnd, "frozen 3 s back: not at the end");
	// RUN's dips mark the returns: the first return about 24 s after the freeze.
	const auto e = edges(g.run, press + g.secs(1.0));
	CHECK(e.size() >= 2 && !e[0].second, "a return within the minute (%zu edges)", e.size());
	if (!e.empty()) {
		const double first = double(e[0].first - press) / rate;
		CHECK(std::fabs(first - 2.97 * 8.0) < 1.0, "the pass lasts about 24 s at x8 (%.2f s)", first);
	}
	// Position fully right: at the end, distinct from the Stretch stop.
	g.f.params[F::POSITION] = 1.f;
	g.go(g.secs(1.0));
	CHECK(g.e->state().held && g.e->state().atEnd, "frozen, Position fully right: held at the end");
	g.f.params[F::POSITION] = 0.5f;
	g.f.params[F::STRETCH] = 1.f;
	g.go(g.secs(1.0));
	CHECK(g.e->state().held && !g.e->state().atEnd, "frozen, the Stretch stop, the flag back in the tape: held, not at the end");
}

// The user's observation at 2.0.46 (Pass.hpp): recording, Stretch turned up
// then back to exactly 1 puts the read head back on the flag, so the output
// is again Position's lag (plus about a grain and the latency) behind the
// input. Silence, x8 for 6 s (the head falls about 5 s behind the flag), x1
// again, then a tone at 7 s: it is heard Position's lag later, not 5 s more.
void stretch_1_follows_position_after_a_detour() {
	const float rate = 48000.f;
	Rig g(rate, 161);
	g.f.params[F::POSITION] = 0.9f;
	g.f.params[F::STRETCH] = 0.5f;   // x8
	const long long T = g.secs(7.0);
	g.go(g.secs(12.0), [&](long long t) {
		if (t == g.secs(6.0)) g.f.params[F::STRETCH] = 0.f;   // x1
		g.f.inL = t >= T ? float(std::sin(2.0 * kPi * 1000.0 * double(t) / rate)) : 0.f;
	});
	const double lag = double(lagFrames(0.9f, g.e->state().capacity, g.grain()));
	long long heard = -1;
	for (long long a = T; a + 480 <= (long long) g.L.size(); a += 480)
		if (meanSquare(g.L, a, 480) > 0.01) {
			heard = a;
			break;
		}
	const double late = double(heard - T) - lag;
	std::printf("  stretch_1_follows_position_after_a_detour: the tone heard %.3f s after it came in (Position's lag %.3f s)\n",
	            double(heard - T) / rate, lag / rate);
	CHECK(heard > 0 && late > -g.grain() && late < g.grain() + g.e->latencyFrames() + g.hop(),
	      "back at x1: heard Position's lag later, within a grain and the latency (%.3f s over)", late / rate);
	CHECK(g.e->state().end == g.e->state().anchor, "the head on the flag");
}

// While recording: Follow (Pass.hpp). Recording at Position 0.9 (2.97 s
// back), a tone burst at 6 s at x8 and another at 10 s at Stretch's stop:
// each is heard Position's lag later, within a grain and the latency, and
// for no longer than it lasted (it is not stretched, and the stop does not
// hold it); RUN stays high throughout. In Drift the same input is stretched
// and held (the contrast, so the test can fail).
void follow_is_live_at_any_stretch() {
	const float rate = 48000.f;
	for (int follow = 1; follow >= 0; follow--) {
		Rig g(rate, 171);
		g.f.follow = follow != 0;
		g.f.params[F::POSITION] = 0.9f;
		g.f.params[F::STRETCH] = 0.5f;   // x8
		const long long T1 = g.secs(6.0), T2 = g.secs(10.0), len = g.secs(0.5), stop = g.secs(8.5);
		g.go(g.secs(15.0), [&](long long t) {
			if (t == stop) g.f.params[F::STRETCH] = 1.f;   // the stop
			const bool on = (t >= T1 && t < T1 + len) || (t >= T2 && t < T2 + len);
			g.f.inL = on ? float(std::sin(2.0 * kPi * 1000.0 * double(t) / rate)) : 0.f;
		});
		const long long lag = lagFrames(0.9f, g.e->state().capacity, g.grain());
		// The onset: within a grain, the latency and a hop of the lag; the end
		// also has the last grain's own length to play out.
		const long long slack = g.grain() + g.e->latencyFrames() + g.hop(), tail = slack + g.grain();
		// The sounding blocks of 480 in [from, to).
		auto heard = [&](long long from, long long to, long long& first, long long& last) {
			first = last = -1;
			for (long long a = from; a + 480 <= to; a += 480)
				if (meanSquare(g.L, a, 480) > 1e-4) {
					if (first < 0) first = a;
					last = a + 480;
				}
		};
		long long f1, l1, f2, l2;
		heard(T1, T2 + g.secs(1.5), f1, l1);
		heard(T2 + g.secs(1.5), (long long) g.L.size(), f2, l2);
		const auto e = edges(g.run, g.secs(1.0));
		if (follow) {
			std::printf("  follow_is_live_at_any_stretch: x8, heard %.3f s to %.3f s after the burst; the stop, %.3f s to %.3f s (lag %.3f s)\n",
			            double(f1 - T1) / rate, double(l1 - T1) / rate, double(f2 - T2) / rate, double(l2 - T2) / rate, double(lag) / rate);
			CHECK(f1 > 0 && std::llabs(f1 - T1 - lag) < slack && std::llabs(l1 - (T1 + len) - lag) < tail,
			      "Follow, x8: the burst heard Position's lag late, as long as it was (%.3f s to %.3f s late)", double(f1 - T1) / rate,
			      double(l1 - T1 - len) / rate);
			CHECK(f2 > 0 && std::llabs(f2 - T2 - lag) < slack && std::llabs(l2 - (T2 + len) - lag) < tail,
			      "Follow, the Stretch stop: the burst heard Position's lag late, as long as it was, not held (%.3f s to %.3f s late)",
			      double(f2 - T2) / rate, double(l2 - T2 - len) / rate);
			CHECK(e.empty() && g.run.back() > 5.f, "Follow: RUN high throughout (%zu edges)", e.size());
			const DoldrumsState st = g.e->state();
			CHECK(st.following && !st.held && st.end == st.anchor, "Follow: following, on the flag, not held");
		} else {
			// Drift: at x8 the head, a pass begun at the start, is still in
			// the silence before the first burst (it reaches it about 72 s in),
			// and at the stop it holds that silence: nothing of either burst is
			// heard where Follow hears it, nor anywhere in the 15 s.
			const double heardAt = meanSquare(g.L, T1 + lag - slack, len + tail + 2 * slack) + meanSquare(g.L, T2 + lag - slack, len + tail + slack);
			CHECK(f1 < 0 && f2 < 0 && heardAt < 1e-12 && meanSquare(g.L, g.secs(1.0), g.secs(14.0)) < 1e-12,
			      "Drift: neither burst heard (first at %lld, %lld; %g where Follow hears them)", f1, f2, heardAt);
			CHECK(g.e->state().held && !g.e->state().following, "Drift, the Stretch stop: held");
		}
	}
}

// Whole humps of energy in y from `from` on, by blocks of 512: their centres.
std::vector<double> humpCentres(const std::vector<float>& y, long long from) {
	const int B = 512;
	std::vector<double> e(y.size() / B, 0.0);
	for (size_t i = size_t(from / B) * B; i < e.size() * B; i++) e[i / B] += double(y[i]) * y[i];
	std::vector<double> centres;
	size_t i = size_t(from / B);
	while (i < e.size()) {
		if (e[i] <= 0.0) {
			i++;
			continue;
		}
		size_t j = i;
		double w = 0, m = 0;
		while (j < e.size() && e[j] > 0.0) {
			w += e[j];
			m += e[j] * (double(j) + 0.5) * B;
			j++;
		}
		if (i > size_t(from / B) && j < e.size()) centres.push_back(m / w);   // whole humps only
		i = j;
	}
	return centres;
}

// Follow, then Freeze: Stretch takes effect. A 5 ms burst of 1 kHz every
// second recorded in Follow at x8 (heard a second apart, live), then
// frozen: the pass travels at x8 and the bursts come out 8 s apart.
void follow_then_freeze_stretches() {
	const float rate = 48000.f;
	Rig g(rate, 173);
	g.f.follow = true;
	g.f.params[F::POSITION] = 0.5f;   // 14.8 s back
	g.f.params[F::STRETCH] = 0.5f;    // x8
	const long long burst = g.secs(0.005);
	auto input = [&](long long t) {
		const long long at = (t + 24000) % 48000;
		g.f.inL = at < burst ? float(5.0 * std::sin(kPi * double(at) / double(burst)) * std::sin(2.0 * kPi * 1000.0 * double(t) / rate)) : 0.f;
	};
	g.go(g.secs(20.0), input);
	const std::vector<double> live = humpCentres(g.L, g.secs(16.0));
	CHECK(live.size() >= 2, "Follow: %zu bursts heard live", live.size());
	for (size_t k = 1; k < live.size(); k++)
		CHECK(std::fabs((live[k] - live[k - 1]) / rate - 1.0) * rate <= g.grain(), "Follow: bursts %zu and %zu %.3f s apart (want 1 within a grain)",
		      k - 1, k, (live[k] - live[k - 1]) / rate);
	const long long freeze = g.t;
	g.f.params[F::FREEZE] = 1.f;
	g.go(g.secs(34.0), [&](long long) { g.f.inL = 0.f; });
	const std::vector<double> c = humpCentres(g.L, freeze + g.grain() + g.e->latencyFrames());
	CHECK(c.size() >= 3, "frozen: %zu whole bursts heard", c.size());
	for (size_t k = 1; k < c.size(); k++) {
		const double gap = (c[k] - c[k - 1]) / rate;
		CHECK(std::fabs(gap - 8.0) * rate <= g.grain(), "frozen at x8: bursts %zu and %zu %.3f s apart (want 8 within a grain)", k - 1, k, gap);
	}
	CHECK(!g.e->state().following && g.e->state().frozen, "frozen: not following");
}

// The fix wave's ruling (Pass.hpp): a fresh engine (a patch reopened) with
// Stretch at its stop holds the silence the tape held when it opened; the
// input plays for 5 s and the output stays silent; Position moved: within a
// grain or two the output is the input's spectrum, held there, not silence.
void reopened_held_wakes_on_position() {
	const float rate = 48000.f;
	Rig g(rate, 175);
	g.f.params[F::POSITION] = 0.9f;
	g.f.params[F::STRETCH] = 1.f;   // the stop
	g.go(g.secs(5.0), [&](long long t) { g.f.inL = chord(t, rate); });
	CHECK(g.e->state().held && meanSquare(g.L, g.secs(4.0), g.secs(1.0)) == 0.0, "held on the silence from before the input");
	const long long move = g.t;
	g.f.params[F::POSITION] = 0.92f;
	g.go(g.secs(3.0), [&](long long t) { g.f.inL = chord(t, rate); });
	const long long from = move + 2 * g.hop() + g.grain() + g.e->latencyFrames();
	const Spectrum sp = spectrum(g.L, from, g.secs(1.5), rate);
	const double level = meanSquare(g.L, from, g.secs(1.5));
	std::printf("  reopened_held_wakes_on_position: after the move, level %.3f, %.3f of the power at the chord\n", level, chordShare(sp));
	CHECK(level > 0.1 && chordShare(sp) > 0.95, "Position moved: the input's chord (level %g, share %.3f)", level, chordShare(sp));
	CHECK(g.e->state().held, "still held");
}

// ---------------------------------------------------------------------------
// No clicks: each event of the design's list, on a tape of a 220 Hz sine whose
// level drifts slowly, the input the same sine going on.

const double kTone = 220.0;
float toneAt(long long i, float rate) {
	const double s = double(i) / rate;
	return float((0.8 + 0.4 * std::sin(2.0 * kPi * s / 7.0)) * std::sin(2.0 * kPi * kTone * s));
}

struct Event {
	const char* name;
	void (*setup)(Rig&);
	void (*at)(Rig&, long long t, long long T);   // the frame at t, the event at T
	bool tapeCheck;
};

void no_clicks() {
	const float rate = 48000.f;
	const Event events[] = {
		{"Freeze on by the button", [](Rig&) {}, [](Rig& g, long long t, long long T) { g.f.params[F::FREEZE] = t >= T; }, false},
		{"Freeze off by the button", [](Rig&) {},
		 [](Rig& g, long long t, long long T) { g.f.params[F::FREEZE] = t >= T - g.secs(1.0) && t < T; }, true},
		{"Freeze on by the gate", [](Rig&) {}, [](Rig& g, long long t, long long T) { g.f.freezeGate = t >= T ? 10.f : 0.f; }, false},
		{"Freeze off by the gate", [](Rig&) {},
		 [](Rig& g, long long t, long long T) { g.f.freezeGate = t >= T - g.secs(1.0) && t < T ? 10.f : 0.f; }, true},
		{"a return", [](Rig& g) {
			 g.f.params[F::FREEZE] = 1.f;
			 g.f.params[F::STRETCH] = 0.f;
			 g.f.params[F::POSITION] = 0.95f;
		 }, [](Rig&, long long, long long) {}, false},
		{"TRIG", [](Rig& g) {
			 g.f.params[F::FREEZE] = 1.f;
			 g.f.params[F::POSITION] = 0.5f;
		 }, [](Rig& g, long long t, long long T) { g.f.trig = t >= T && t < T + 480 ? 10.f : 0.f; }, false},
		{"Position by the knob", [](Rig& g) {
			 g.f.params[F::FREEZE] = 1.f;
			 g.f.params[F::POSITION] = 0.3f;
		 }, [](Rig& g, long long t, long long T) { g.f.params[F::POSITION] = t >= T ? 0.8f : 0.3f; }, false},
		{"Position by a stepped CV", [](Rig& g) {
			 g.f.params[F::POSITION] = 0.3f;
			 g.f.params[F::POSITION_ATT] = 1.f;
		 }, [](Rig& g, long long t, long long T) { g.f.positionCv = t >= T ? 3.f : 0.f; }, false},
		{"a V/OCT step of two octaves", [](Rig& g) {
			 g.f.params[F::HARMONICS] = 0.5f;
			 g.f.params[F::ROOT] = -15.f;   // A2, 110 Hz: the tone is its second tooth
		 }, [](Rig& g, long long t, long long T) {
			 static float v[1];
			 v[0] = t >= T ? 2.f : 0.f;
			 g.f.voct = v;
			 g.f.voctChannels = 1;
		 }, false},
		{"Partials swept", [](Rig& g) {
			 g.f.params[F::HARMONICS] = 0.5f;
			 g.f.params[F::ROOT] = -15.f;
		 }, [](Rig& g, long long t, long long T) {
			 g.f.params[F::PARTIALS] = float(std::max(1LL, std::min(32LL, 1 + (t - T) * 31 / g.secs(2.0))));
		 }, false},
		{"Width swept", [](Rig& g) {
			 g.f.params[F::HARMONICS] = 0.5f;
			 g.f.params[F::ROOT] = -15.f;
		 }, [](Rig& g, long long t, long long T) {
			 g.f.params[F::WIDTH] = float(std::max(0.0, std::min(1.0, double(t - T) / double(g.secs(2.0)))));
		 }, false},
		{"Harmonics from 0", [](Rig& g) { g.f.params[F::ROOT] = -15.f; },
		 [](Rig& g, long long t, long long T) { g.f.params[F::HARMONICS] = t >= T ? 0.5f : 0.f; }, false},
		{"Grain changed while sounding, Medium to Long", [](Rig&) {},
		 [](Rig& g, long long t, long long T) { g.f.grainSize = t >= T ? GRAIN_LONG : GRAIN_MEDIUM; }, false},
		{"Grain changed while sounding, Long to Short", [](Rig& g) { g.f.grainSize = GRAIN_LONG; },
		 [](Rig& g, long long t, long long T) { g.f.grainSize = t >= T ? GRAIN_SHORT : GRAIN_LONG; }, false},
		{"the stop reached", [](Rig&) {}, [](Rig& g, long long t, long long T) { g.f.params[F::STRETCH] = t >= T ? 1.f : 0.9f; }, false},
		{"the stop left", [](Rig&) {}, [](Rig& g, long long t, long long T) { g.f.params[F::STRETCH] = t >= T ? 0.5f : 1.f; }, false},
		{"play once ending", [](Rig& g) {
			 g.f.params[F::FREEZE] = 1.f;
			 g.f.params[F::STRETCH] = 0.f;
			 g.f.params[F::POSITION] = 0.95f;
			 g.f.stopAtEnd = true;
		 }, [](Rig& g, long long t, long long T) { g.f.trig = t >= T - g.secs(0.5) && t < T - g.secs(0.49) ? 10.f : 0.f; }, false},
	};
	const double fTop = 3.0 * kTone;
	for (const Event& ev : events) {
		Rig g(rate, 51);
		g.tape([&](long long i, float& l, float& r) { l = r = toneAt(i, rate); });
		const long long base = g.e->ring().now();
		ev.setup(g);
		const long long T = g.secs(3.0), end = T + g.secs(4.0);
		long long frozenEnd = -1;
		g.go(end, [&](long long t) {
			g.f.inL = toneAt(base + t, rate);
			ev.at(g, t, T);
			if (ev.tapeCheck && t == T) frozenEnd = g.e->ring().now();
		});
		const double wl = slopeRatio(g.L, T - g.secs(0.5), end, fTop, rate);
		const double wr = slopeRatio(g.R, T - g.secs(0.5), end, fTop, rate);
		CHECK(wl <= 1.0 && wr <= 1.0, "%s: no jump at the output beyond the slope (%.3f, %.3f)", ev.name, wl, wr);
		const double level = meanSquare(g.L, T - g.secs(0.5), g.secs(4.5));
		CHECK(level > 1e-3, "%s: it sounds (%g)", ev.name, level);
		if (ev.tapeCheck) {
			// The junction where recording stopped and went on: no step written.
			const int n = 2000;
			std::vector<float> l(n), r(n);
			CHECK(g.e->ring().copy(frozenEnd + n / 2, n, l.data(), r.data()), "%s: the tape is readable", ev.name);
			double worst = 0;
			for (int i = 1; i < n; i++) worst = std::max(worst, std::fabs(double(l[i]) - l[i - 1]));
			const double bound = 2.0 * std::sin(kPi * fTop / rate) * 1.2;
			CHECK(worst <= bound, "%s: no step on the tape (%.4f V, bound %.4f)", ev.name, worst, bound);
			CHECK(std::fabs(l[n / 2 - 1]) < 0.01f, "%s: the fade reached 0 at the freeze (%g)", ev.name, l[n / 2 - 1]);
		}
	}
	// A tape arriving from a patch is Task 11's (the file and the swap of a ring).
}

// Noise into the comb at Harmonics full, a V/OCT step of two octaves just
// after the first tables tick of a hop (the worst place for a comb read once a
// grain): the teeth of the new note begin to rise within a grain.
void comb_follows_voct_within_a_grain() {
	const float rate = 48000.f;
	for (int size = 0; size < 3; size++) {
		Rig g(rate, 61, size);
		Random n;
		n.seed(8);
		float v[1] = {0.f};
		g.f.voct = v;
		g.f.voctChannels = 1;
		g.f.params[F::HARMONICS] = 1.f;
		g.go(g.secs(3.0), [&](long long) { g.f.inL = n.uniform(); });
		long long T = g.boundaries().back() + 2 * stepGap(rate);
		while (T < g.t) T += g.hop();
		g.go(T - g.t);
		v[0] = 2.f;
		g.go(g.secs(3.0), [&](long long) { g.f.inL = n.uniform(); });
		// Short-time energy of the new note's teeth that the old one lacks (4 to 8
		// times 261.6 Hz; the old teeth stop at 8 x 65.4 = 523 Hz).
		const int N = 2048, hopA = 256;
		rka::RealFft fft(N);
		float* in = rka::RealFft::alloc(N);
		float* out = rka::RealFft::alloc(N);
		std::vector<std::pair<long long, double>> e;
		for (long long at = T - g.secs(1.0); at + N <= (long long) g.L.size(); at += hopA) {
			for (int i = 0; i < N; i++) in[i] = g.L[size_t(at + i)] * float(0.5 - 0.5 * std::cos(2.0 * kPi * i / N));
			fft.forward(in, out);
			double s = 0;
			for (int h = 4; h <= 8; h++) {
				const int k = int(std::lround(261.6256 * h * N / rate));
				for (int j = k - 1; j <= k + 1; j++) s += double(out[2 * j]) * out[2 * j] + double(out[2 * j + 1]) * out[2 * j + 1];
			}
			e.push_back({at + N, s});   // the frame's last sample
		}
		rka::RealFft::free(in);
		rka::RealFft::free(out);
		double steady = 0, before = 0;
		int ns = 0;
		for (const auto& x : e) {
			if (x.first > T + g.secs(2.0)) {
				steady += x.second;
				ns++;
			}
			if (x.first < T) before = std::max(before, x.second);
		}
		steady /= std::max(1, ns);
		long long rise = -1;
		for (const auto& x : e)
			if (x.first >= T && x.second > 0.01 * steady) {
				rise = x.first;
				break;
			}
		CHECK(before < 0.01 * steady, "size %d: the new teeth absent before the step (%.1f dB)", size, db(before / steady + 1e-30));
		CHECK(rise >= 0 && rise - T <= g.grain() + g.e->latencyFrames(), "size %d: the new teeth rise %lld samples after the step (bound %d)", size,
		      rise - T, g.grain() + g.e->latencyFrames());
	}
}

// The comb's loudness (C1 of the final review; Tables.hpp, "The makeup"):
// measured from the sound, per grain, one scale for both sides. At the
// defaults (48 kHz, Medium, x8, Position now, recording, Root C2, 8 partials,
// 25 cents), Harmonics 1: a tone whose partials lie on the teeth comes out
// at its own level; so does a chord on three roots; white noise sinks only by
// what the +18 dB bound leaves; a tone between the teeth is cut, not raised;
// silence stays exact silence. Levels are kept out of the soft bound (1 V
// partials), so the figures are the comb's.
float partialsAt(long long i, float rate, double hz, int partials) {
	const double s = double(i) / rate;
	double y = 0;
	for (int k = 1; k <= partials && k * hz < 0.45 * rate; k++) y += std::sin(2.0 * kPi * k * hz * s) / k;
	return float(y);
}

double combLevelDb(float h, int channels, const float* volts, float (*src)(long long, float, Random&)) {
	const float rate = 48000.f;
	Rig g(rate, 151);
	g.keep = true;
	float v[16] = {};
	for (int c = 0; c < channels; c++) v[c] = volts[c];
	if (channels > 0) {
		g.f.voct = v;
		g.f.voctChannels = channels;
	}
	g.f.params[F::HARMONICS] = h;
	Random n;
	n.seed(12);
	std::vector<float> in;
	g.go(g.secs(8.0), [&](long long t) {
		g.f.inL = src(t, rate, n);
		in.push_back(g.f.inL);
	});
	const long long a = g.secs(2.0), len = g.secs(6.0);
	const double out = 0.5 * (meanSquare(g.L, a, len) + meanSquare(g.R, a, len)), ref = meanSquare(in, a, len);
	return ref > 0 ? db(out / ref) : (out == 0.0 ? 0.0 : 1e9);
}

const double kC2 = 65.40639;
float sawC2(long long t, float rate, Random&) { return partialsAt(t, rate, kC2, 24); }
float chordC2(long long t, float rate, Random&) {
	return (partialsAt(t, rate, kC2, 24) + partialsAt(t, rate, kC2 * std::pow(2.0, 4.0 / 12), 24) +
	        partialsAt(t, rate, kC2 * std::pow(2.0, 7.0 / 12), 24)) / 3.f;
}
float whiteNoise(long long, float, Random& n) { return n.uniform(); }
float betweenTeeth(long long t, float rate, Random&) { return float(std::sin(2.0 * kPi * 1.5 * kC2 * double(t) / rate)); }
float silence(long long, float, Random&) { return 0.f; }

void comb_keeps_the_level() {
	const float none[1] = {0.f};
	const float triad[3] = {0.f, 4.f / 12, 7.f / 12};
	const double saw0 = combLevelDb(0.f, 0, none, sawC2), saw = combLevelDb(1.f, 0, none, sawC2);
	const double sawHalf = combLevelDb(0.5f, 0, none, sawC2);
	const double chord3 = combLevelDb(1.f, 3, triad, chordC2);
	const double noise1 = combLevelDb(1.f, 0, none, whiteNoise);
	const double mistuned = combLevelDb(1.f, 0, none, betweenTeeth);
	const double quiet = combLevelDb(1.f, 0, none, silence);
	std::printf("  comb_keeps_the_level: saw on C2 %+.2f dB at Harmonics 0, %+.2f at 0.5, %+.2f at 1; chord on 3 roots %+.2f;"
	            " white noise %+.2f; a sine between teeth 1 and 2 %+.2f\n",
	            saw0, sawHalf, saw, chord3, noise1, mistuned);
	CHECK(std::fabs(saw0) < 0.5, "Harmonics 0: the saw at its own level (%+.2f dB)", saw0);
	CHECK(std::fabs(saw) < 1.5, "Harmonics 1: a saw on the root within 1.5 dB of its input (%+.2f dB)", saw);
	CHECK(std::fabs(sawHalf) < 1.5, "Harmonics 0.5: the saw within 1.5 dB (%+.2f dB)", sawHalf);
	CHECK(std::fabs(chord3) < 1.5, "Harmonics 1: a chord on three roots within 1.5 dB (%+.2f dB)", chord3);
	// Flat noise would need about +25 dB, more than the bound's +18 (the
	// makeup sits at the bound: test_tables' comb_makeup_measured): it sinks
	// by the difference, -6.6 dB measured, and no more.
	CHECK(noise1 > -7.35 && noise1 < -5.85, "Harmonics 1: white noise %+.2f dB (-6.6 measured: the bound's)", noise1);
	// Between the teeth only the analysis window's skirt reaches a tooth:
	// -22.4 dB measured, the skirt raised by the bound and no more.
	CHECK(mistuned < -20.0, "Harmonics 1: a sine between the teeth is cut, not raised (%+.2f dB)", mistuned);
	CHECK(quiet == 0.0, "silence in: exact silence out");
}

// Mix 0: the input to the bit, undelayed, IN R from IN L unpatched; NaN reads 0.
// The latency, from a trigger's boundary to the first sound, is the same every grain.
void mix_and_latency() {
	const float rate = 48000.f;
	{
		Rig g(rate, 71);
		Random n;
		n.seed(9);
		g.f.params[F::MIX] = 0.f;
		g.f.params[F::TILT] = 0.5f;
		long long wrong = 0;
		std::vector<float> inL, inR;
		g.go(g.secs(2.0), [&](long long t) {
			g.f.inL = 5.f * n.uniform();
			g.f.inR = 5.f * n.uniform();
			g.f.inRPatched = t < g.secs(1.0);
			inL.push_back(g.f.inL);
			inR.push_back(g.f.inRPatched ? g.f.inR : g.f.inL);
		});
		for (size_t i = 0; i < inL.size(); i++) wrong += g.L[i] != inL[i] || g.R[i] != inR[i];
		CHECK(wrong == 0, "Mix 0: the input to the bit, undelayed (%lld samples differ)", wrong);
		const float bad[4] = {NAN, INFINITY, -INFINITY, -0.f};
		for (float b : bad) {
			g.f.inL = b;
			g.step();
			CHECK(g.r.outL == 0.f && g.r.outR == 0.f && std::signbit(g.r.outL) == std::signbit(b == b && !std::isinf(b) ? b : 0.f),
			      "input %g reads 0 V (%g, %g)", b, g.r.outL, g.r.outR);
		}
	}
	{
		// Wide passes through: off, a mono input gives sides equal to the bit; on,
		// sides of their own.
		for (int wide = 0; wide < 2; wide++) {
			Rig g(rate, 74);
			Random n;
			n.seed(18);
			g.f.wide = wide == 1;
			g.go(g.secs(2.0), [&](long long) { g.f.inL = n.uniform(); });
			long long same = 0;
			double lr = 0, ll = 0, rr = 0;
			for (size_t i = size_t(g.secs(1.0)); i < g.L.size(); i++) {
				same += g.L[i] == g.R[i];
				lr += double(g.L[i]) * g.R[i];
				ll += double(g.L[i]) * g.L[i];
				rr += double(g.R[i]) * g.R[i];
			}
			const double corr = lr / std::sqrt(ll * rr);
			if (wide)
				CHECK(std::fabs(corr) < 0.2, "Wide: sides of their own (correlation %.3f)", corr);
			else
				CHECK(same == (long long) g.L.size() - g.secs(1.0), "not Wide: a mono input gives equal sides");
		}
	}
	{
		// Mix is smoothed: from fully wet to fully dry over 10 ms, equal power on the way.
		Rig g(rate, 73);
		g.f.params[F::POSITION] = 0.f;   // 30 s back: silence, so the wet is 0
		g.go(g.secs(0.5));
		g.f.inL = 1.f;
		g.f.params[F::MIX] = 0.f;
		g.go(g.secs(0.02));
		const size_t at = g.L.size() - size_t(g.secs(0.02));
		const float half = g.L[at + size_t(g.secs(0.005)) - 1], whole = g.L[at + size_t(g.secs(0.01))];
		CHECK(g.L[at] > 0.f && g.L[at] < 0.01f && std::fabs(half - 0.7071f) < 0.01f && whole == 1.f,
		      "Mix ramps over 10 ms (%g at once, %g at 5 ms, %g at 10 ms)", g.L[at], half, whole);
	}
	for (float rate2 : {48000.f, 96000.f}) {
		Rig g(rate2, 72);
		Random n;
		n.seed(10);
		g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
		g.f.params[F::FREEZE] = 1.f;
		g.f.params[F::POSITION] = 0.5f;
		g.f.params[F::STRETCH] = 0.f;
		g.f.stopAtEnd = true;
		const int lat = g.e->latencyFrames();
		CHECK(lat == (Grain::kSteps + 1) * stepGap(rate2), "%g Hz: latency %d is %d steps of %d", rate2, lat, Grain::kSteps + 1, stepGap(rate2));
		g.go(g.secs(17.0), [&](long long t) { g.f.trig = t >= 100 && t < 148 ? 10.f : 0.f; });
		const std::vector<long long> b = g.boundaries();
		// First sound at a boundary plus the latency, and RUN up on that sample.
		long long first = -1;
		for (size_t i = 0; i < g.L.size() && first < 0; i++)
			if (g.L[i] != 0.f) first = (long long) i;
		CHECK(first >= 0 && std::find(b.begin(), b.end(), first - lat) != b.end() && g.run[size_t(first)] > 5.f && g.run[size_t(first - 1)] < 5.f,
		      "%g Hz: the first sound and RUN at a boundary plus %d (at %lld)", rate2, lat, first);
		// Every grain laid down the same delay after its boundary.
		long long off = 0, adds = 0;
		for (size_t i = 0; i < g.staged.size(); i++)
			if (g.staged[i] & DoldrumsEngine::STAGED_ADD) {
				adds++;
				off += std::find(b.begin(), b.end(), (long long) i - lat) == b.end();
			}
		CHECK(adds > 10 && off == 0, "%g Hz: %lld grains, %lld laid down off the latency", rate2, adds, off);
		// A trigger in mid pass: its first grain too.
		const long long T = g.t + 777;
		g.go(g.secs(2.0), [&](long long t) { g.f.trig = t >= T && t < T + 48 ? 10.f : 0.f; });
		const std::vector<long long> b2 = g.boundaries();
		const long long B = *std::lower_bound(b2.begin(), b2.end(), T);
		CHECK(g.run[size_t(B + lat - 1)] > 5.f || g.run[size_t(B + lat)] > 5.f, "%g Hz: sounding at the trigger", rate2);
		CHECK(edges(g.run, T).size() >= 1 && edges(g.run, T)[0].first == B + lat, "%g Hz: the trigger's first grain at its boundary plus %d", rate2, lat);
	}
}

// The steps of a grain, at every grain size at 44.1, 48, 96 and 192 kHz with
// the busiest tables: the same count every grain, a gap apart, all inside the
// hop, the tables' work on samples of its own, and a rebuild done in a hop.
void staging_fits() {
	const float rates[4] = {44100.f, 48000.f, 96000.f, 192000.f};
	for (float rate : rates)
		for (int size = 0; size < 3; size++) {
			Rig g(rate, 81, size);
			Random n;
			n.seed(12);
			float v[16];
			g.f.voct = v;
			g.f.voctChannels = 16;
			g.f.params[F::HARMONICS] = 1.f;
			g.f.params[F::BLUR] = 1.f;
			g.f.params[F::SHIFT] = 12.f;
			g.f.params[F::TILT] = 1.f;
			g.f.params[F::PARTIALS] = 32.f;
			g.f.params[F::WIDTH] = 0.f;
			g.f.params[F::ROOT] = -36.f;
			const int gap = stepGap(rate), H = g.hop();
			long long swaps0 = -1;
			g.go(6LL * H + 2 * gap, [&](long long t) {
				g.f.inL = n.uniform();
				for (int c = 0; c < 16; c++) v[c] = float(c) * 0.25f + float(t % 1000) * 1e-4f;   // always moving
			});
			// Until the first tables are made a boundary is tried every gap; from
			// the first grain on, one a hop.
			std::vector<long long> b = g.boundaries();
			while (b.size() > 1 && g.staged[size_t(b[0] + gap)] != DoldrumsEngine::STAGED_STEP0) b.erase(b.begin());
			b.insert(b.begin(), -1);
			CHECK(b.size() >= 6, "%g Hz size %d: %zu boundaries", rate, size, b.size());
			long long bad = 0, both = 0, late = 0;
			for (size_t i = 0; i < g.staged.size(); i++) {
				const int s = g.staged[i];
				both += s != 0 && (s & (s - 1)) != 0;   // more than one thing on a sample
			}
			// From the first grain on (the second boundary), boundaries a hop apart,
			// and each one's steps and its add at a gap, two gaps, ... ten gaps.
			for (size_t k = 1; k + 1 < b.size(); k++) {
				bad += b[k + 1] - b[k] != H;
				for (int j = 1; j <= Grain::kSteps + 1; j++) {
					const int want = j <= Grain::kSteps ? DoldrumsEngine::STAGED_STEP0 << (j - 1) : DoldrumsEngine::STAGED_ADD;
					bad += g.staged[size_t(b[k] + j * gap)] != want;
				}
				int steps = 0;
				long long lastAt = -1, closest = 1 << 30;
				for (long long t = b[k]; t < b[k + 1]; t++) {
					const int s = g.staged[size_t(t)];
					if (s & ~(DoldrumsEngine::STAGED_TABLES | DoldrumsEngine::STAGED_BOUNDARY)) {
						steps++;
						if (lastAt >= 0) closest = std::min(closest, t - lastAt);
						lastAt = t;
						late += t - b[k] >= H;
					}
				}
				bad += steps != Grain::kSteps + 1;
				bad += closest < gap;
			}
			CHECK(bad == 0 && both == 0 && late == 0, "%g Hz size %d: staging (%lld wrong, %lld samples doing two things, %lld late)", rate, size, bad,
			      both, late);
			// With every control moving, a rebuild is done within the hop: a swap at every boundary.
			swaps0 = g.e->tableSwaps();
			const long long b0 = (long long) g.boundaries().size();
			g.go(4LL * H, [&](long long t) {
				g.f.inL = n.uniform();
				for (int c = 0; c < 16; c++) v[c] = float(c) * 0.25f + float(t % 1000) * 1e-4f;
			});
			const long long boundaries = (long long) g.boundaries().size() - b0, swaps = g.e->tableSwaps() - swaps0;
			CHECK(swaps == boundaries, "%g Hz size %d: %lld swaps at %lld boundaries with V/OCT moving", rate, size, swaps, boundaries);
			// Harmonics at 0: a moving V/OCT starts no rebuild.
			g.f.params[F::HARMONICS] = 0.f;
			g.go(2LL * H);
			const long long s1 = g.e->tableSwaps();
			g.go(4LL * H, [&](long long t) {
				for (int c = 0; c < 16; c++) v[c] = float(c) * 0.25f + float(t % 1000) * 1e-4f;
			});
			CHECK(g.e->tableSwaps() == s1, "%g Hz size %d: Harmonics 0, V/OCT moving: %lld rebuilds", rate, size, g.e->tableSwaps() - s1);
		}
}

// Freeze and TRIG toggled every 1 to 100 samples for 10 s, in both settings:
// bounded, RUN both low and high, and sane once it stops.
void rapid_freeze_and_trig() {
	const float rate = 48000.f;
	for (int stop = 0; stop < 2; stop++) {
		Rig g(rate, 91);
		Random n, k;
		n.seed(13);
		k.seed(14);
		g.f.stopAtEnd = stop == 1;
		long long nextF = 1, nextT = 1, offTape = 0;
		bool fr = false, tr = false;
		const long long now0 = g.e->ring().now();
		g.go(g.secs(10.0), [&](long long t) {
			// The read point stays on the tape at every boundary.
			if (t > 0 && (g.staged.back() & DoldrumsEngine::STAGED_BOUNDARY)) {
				const DoldrumsState& st = g.e->state();
				offTape += st.grain > 0 && (st.end > st.now || st.end < st.now - st.capacity + st.grain);
			}
			g.f.inL = float(std::sin(2.0 * kPi * kTone * double(t) / rate));
			if (t >= nextF) {
				fr = !fr;
				nextF = t + 1 + k.next() % 100;
			}
			if (t >= nextT) {
				tr = !tr;
				nextT = t + 1 + k.next() % 100;
			}
			g.f.params[F::FREEZE] = fr ? 1.f : 0.f;
			g.f.freezeGate = (t / 37) % 2 ? 10.f : 0.f;
			g.f.trig = tr ? 10.f : 0.f;
		});
		long long bad = 0, high = 0, low = 0;
		for (size_t i = 0; i < g.L.size(); i++) {
			bad += !(std::fabs(g.L[i]) <= 10.f) || !(std::fabs(g.R[i]) <= 10.f);
			(g.run[i] > 5.f ? high : low)++;
		}
		CHECK(bad == 0, "%s: bounded (%lld samples not)", stop ? "stop" : "go round", bad);
		CHECK(high > 0 && low > 0, "%s: RUN both high (%lld) and low (%lld)", stop ? "stop" : "go round", high, low);
		CHECK(offTape == 0, "%s: the read point on the tape at every boundary (%lld not)", stop ? "stop" : "go round", offTape);
		{
			// No step written to the tape: what was written of the sine moves no
			// faster than the sine and the fade's steps explain.
			const long long now1 = g.e->ring().now();
			const int m = int(now1 - now0);
			std::vector<float> l((size_t) m), r((size_t) m);
			CHECK(m > g.secs(1.0) && g.e->ring().copy(now1, m, l.data(), r.data()), "%s: %d frames written while toggling", stop ? "stop" : "go round", m);
			double worst = 0;
			for (int i = 1; i < m; i++) worst = std::max(worst, std::fabs(double(l[size_t(i)]) - l[size_t(i - 1)]));
			const double bound = 2.0 * std::sin(kPi * kTone / rate) + 1.0 / double(g.secs(0.005));
			CHECK(worst <= bound * 1.001, "%s: no step on the tape (%.4f, bound %.4f)", stop ? "stop" : "go round", worst, bound);
		}
		// Stopped: Stretch 1 from now, go round, a trigger to start it.
		const size_t mark = g.L.size();
		g.f.params[F::FREEZE] = 0.f;
		g.f.freezeGate = 0.f;
		g.f.params[F::STRETCH] = 0.f;
		g.f.stopAtEnd = false;
		std::vector<float> in;
		g.go(g.secs(6.0), [&](long long t) {
			g.f.inL = n.uniform();
			in.push_back(g.f.inL);
			g.f.trig = t < (long long) mark + 100 ? 10.f : 0.f;
		});
		const long long a = g.secs(4.0);
		long long lowLate = 0;
		for (long long t = (long long) mark + a; t < (long long) g.run.size(); t++) lowLate += g.run[size_t(t)] < 5.f;
		const double lv = db(meanSquare(g.L, (long long) mark + a, g.secs(2.0)) / meanSquare(in, a, g.secs(2.0)));
		CHECK(lowLate == 0, "%s, after: RUN high (%lld samples low)", stop ? "stop" : "go round", lowLate);
		CHECK(std::fabs(lv) < 1.0, "%s, after: the level of the input within 1 dB (%.2f dB)", stop ? "stop" : "go round", lv);
	}
}

// A change of Grain and of the rate on each sample a grain's work is staged on.
void rate_and_grain_change_mid_grain() {
	const float rate = 48000.f;
	const int stages[] = {DoldrumsEngine::STAGED_BOUNDARY, DoldrumsEngine::STAGED_STEP0, DoldrumsEngine::STAGED_STEP0 << 1,
	                      DoldrumsEngine::STAGED_STEP0 << 2, DoldrumsEngine::STAGED_STEP0 << 3, DoldrumsEngine::STAGED_STEP0 << 4,
	                      DoldrumsEngine::STAGED_STEP0 << 5, DoldrumsEngine::STAGED_STEP0 << 6, DoldrumsEngine::STAGED_STEP0 << 7,
	                      DoldrumsEngine::STAGED_STEP0 << 8, DoldrumsEngine::STAGED_ADD, DoldrumsEngine::STAGED_TABLES};
	const double fTop = 3.0 * kChord[2];
	// Run until the stage `want` runs, `from` samples in (a hop at most more).
	auto toStage = [](Rig& g, int want, long long from) {
		g.go(from);
		for (int i = 0; i < 70000 && !(g.staged.back() & want); i++) g.step();
		CHECK(g.staged.back() & want, "stage %x reached", want);
	};
	// Octave-band powers of y[a, a + n), 100 Hz to 12.8 kHz.
	auto bands = [](const std::vector<float>& y, long long a, long long n, float r) {
		const Spectrum s = spectrum(y, a, n, r);
		std::vector<double> b;
		for (double f = 141.4; f < 12800.0; f *= 2.0) b.push_back(s.band(f, f * 0.29));
		return b;
	};
	for (int want : stages)
		for (int to : {GRAIN_LONG, GRAIN_SHORT}) {
			// Noise held on a frozen tape (nothing to travel through), with tables
			// that are slow to rebuild (Blur, 16 roots of 32 partials: longer than
			// a hop at the new size) and Tilt +6 dB an octave, so a grain given
			// tables of another size, or none, would be heard. The slice is still
			// on the tape, so it is taken again at the new size, by a dissolve.
			Rig g(rate, 101);
			Random n;
			n.seed(15);
			float v[16];
			for (int c = 0; c < 16; c++) v[c] = float(c) / 12.f;
			g.f.voct = v;
			g.f.voctChannels = 16;
			g.f.params[F::TILT] = 1.f;
			g.f.params[F::BLUR] = 1.f;
			g.f.params[F::HARMONICS] = 0.3f;
			g.f.params[F::PARTIALS] = 32.f;
			g.go(g.secs(1.5), [&](long long) { g.f.inL = 2.f * n.uniform(); });
			g.f.inL = 0.f;
			g.f.params[F::FREEZE] = 1.f;
			toStage(g, want, g.secs(1.0));
			const long long T = g.t;
			g.f.grainSize = to;
			g.go(g.secs(3.0));
			const double before = meanSquare(g.L, T - g.secs(1.0), g.secs(1.0));
			double hi = 0, lo = 1e30;
			for (long long a = T; a + 2400 <= g.t; a += 2400) {
				hi = std::max(hi, meanSquare(g.L, a, 2400));
				lo = std::min(lo, meanSquare(g.L, a, 2400));
			}
			const std::vector<double> b0 = bands(g.L, T - g.secs(1.0), g.secs(1.0), rate), b1 = bands(g.L, T + g.secs(2.0), g.secs(1.0), rate);
			double worst = 0;
			for (size_t k = 0; k < b0.size(); k++) worst = std::max(worst, std::fabs(db(b1[k] / b0[k])));
			CHECK(db(hi / before) < 4.0 && db(lo / before) > -3.0, "stage %x, to size %d: 50 ms blocks from %.2f to %.2f dB of before (the sizes overlap: up to +4)",
			      want, to, db(lo / before), db(hi / before));
			// 2.5 dB: with the comb on, a tooth is at least a bin wide, and a
			// Short bin is wider (measured 1.9 dB to Short, 1.0 to Long).
			CHECK(worst < 2.5, "stage %x, to size %d: the held spectrum, octave bands within %.2f dB", want, to, worst);
			CHECK(g.e->state().grain == grainFrames(GrainSize(to), rate) && g.e->state().held, "stage %x: held at the new size (%d)", want,
			      g.e->state().grain);
			// The same with the chord, for the slope: no click.
			Rig c(rate, 101);
			c.go(c.secs(1.5), [&](long long t) { c.f.inL = chord(t, rate); });
			c.f.params[F::FREEZE] = 1.f;
			toStage(c, want, c.secs(1.0));
			const long long Tc = c.t;
			c.f.grainSize = to;
			c.go(c.secs(3.0));
			const double w = std::max(slopeRatio(c.L, Tc - c.secs(0.5), c.t, fTop, rate), slopeRatio(c.R, Tc - c.secs(0.5), c.t, fTop, rate));
			CHECK(w <= 1.0, "stage %x, to size %d: no click (%.3f)", want, to, w);
			CHECK(chordShare(spectrum(c.L, Tc + c.secs(1.5), c.secs(1.5), rate)) > 0.97, "stage %x, to size %d: the held chord goes on", want, to);
		}
	{
		// Held at the Stretch stop, recording, the held place long recorded over:
		// the held slice keeps its size until it is let go.
		Rig g(rate, 102);
		g.f.params[F::TILT] = 1.f;   // -12 dB at the chord: losing the tables would be heard
		const long long hold = g.secs(3.0);
		g.go(hold + g.secs(35.0), [&](long long t) {
			g.f.inL = t < hold ? chord(t, rate) : float(1.5 * std::sin(2.0 * kPi * 1500.0 * double(t) / rate));
			g.f.params[F::STRETCH] = t < hold ? 0.5f : 1.f;
		});
		const long long T = g.t;
		g.f.grainSize = GRAIN_LONG;
		g.go(g.secs(4.0), [&](long long t) { g.f.inL = float(1.5 * std::sin(2.0 * kPi * 1500.0 * double(t) / rate)); });
		const Spectrum s = spectrum(g.L, T, g.secs(4.0), rate);
		CHECK(chordShare(s) > 0.97 && s.band(1500.0, 30.0) / s.total() < 1e-6, "recorded over: the held chord goes on (share %.4f)", chordShare(s));
		CHECK(g.e->state().grain == grainFrames(GRAIN_MEDIUM, rate), "recorded over: it keeps its size (%d)", g.e->state().grain);
		const double lv = db(meanSquare(g.L, T + g.secs(1.0), g.secs(3.0)) / meanSquare(g.L, T - g.secs(3.0), g.secs(3.0)));
		CHECK(std::fabs(lv) < 1.5, "recorded over: the level, tables and all (%.2f dB)", lv);
		const double w = slopeRatio(g.L, T - g.secs(0.5), g.t, fTop, rate);
		CHECK(w <= 1.0, "recorded over: no click (%.3f)", w);
		g.f.params[F::STRETCH] = 0.5f;
		g.go(g.secs(2.0), [&](long long t) { g.f.inL = float(1.5 * std::sin(2.0 * kPi * 1500.0 * double(t) / rate)); });
		CHECK(g.e->state().grain == grainFrames(GRAIN_LONG, rate) && !g.e->state().held, "let go: the new size (%d)", g.e->state().grain);
	}
	{
		// Noise held on a frozen tape 40000 frames from its oldest end (on the
		// tape at the Long size), Grain to Long, and Freeze let go before the new
		// tables swap in. The thaw takes the slice at the anchor again (the
		// user's ruling, Pass.hpp), held, on a tape now recording; its place is
		// soon recorded over, so the held slice is laid down from the buffers
		// that hold it, and the tables go back to its size: Long, Medium (asked
		// for before the thaw's boundary), Long once more while the place is
		// still on the tape at Long, then Medium for good (4 swaps; 2 before
		// the ruling, when the thaw took nothing). The thaw's new slice is a
		// dissolve: 50 ms blocks measured -3.5 to +1.8 dB, octave bands 1.5 dB.
		Rig g(rate, 104);
		Random n;
		n.seed(16);
		g.tape([&](long long, float& l, float& r) { l = r = 2.f * n.uniform(); });
		const long long cap = g.e->ring().capacity();
		g.f.params[F::FREEZE] = 1.f;
		g.f.params[F::STRETCH] = 1.f;
		g.f.params[F::POSITION] = float(1.0 - double(cap - 40000 - g.secs(0.005)) / double(cap - g.grain()));
		g.go(g.secs(2.0));
		long long T = g.boundaries().back() + 1;
		while (T < g.t) T += g.hop();
		g.go(T - g.t);
		const double before = meanSquare(g.L, T - g.secs(1.0), g.secs(1.0));
		const long long swaps = g.e->tableSwaps();
		g.f.grainSize = GRAIN_LONG;
		g.go(3LL * stepGap(rate));
		g.f.params[F::FREEZE] = 0.f;
		g.go(g.secs(3.0), [&](long long) { g.f.inL = 2.f * n.uniform(); });
		CHECK(g.e->tableSwaps() - swaps == 4, "race: the tables went to Long and back, twice (%lld swaps)", g.e->tableSwaps() - swaps);
		double lo = 1e30, hi = 0;
		for (long long a = T; a + 2400 <= g.t; a += 2400) {
			lo = std::min(lo, meanSquare(g.L, a, 2400));
			hi = std::max(hi, meanSquare(g.L, a, 2400));
		}
		CHECK(db(lo / before) > -4.5 && db(hi / before) < 3.0, "race: the held sound goes on, 50 ms blocks from %.2f to %.2f dB of before",
		      db(lo / before + 1e-30), db(hi / before));
		const std::vector<double> b0 = bands(g.L, T - g.secs(1.0), g.secs(1.0), rate), b1 = bands(g.L, T + g.secs(2.0), g.secs(1.0), rate);
		double worst = 0;
		for (size_t k = 0; k < b0.size(); k++) worst = std::max(worst, std::fabs(db(b1[k] / b0[k])));
		CHECK(worst < 2.0, "race: the held spectrum, octave bands within %.2f dB", worst);
		CHECK(g.e->state().grain == grainFrames(GRAIN_MEDIUM, rate) && g.e->state().held, "race: back at the held size (%d)", g.e->state().grain);
	}
	{
		// Stopped (play once, the pass over, its last grain faded): a change of
		// Grain, then a trigger: one pass at the new size.
		Rig g(rate, 105);
		Random n;
		n.seed(17);
		g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
		g.f.params[F::FREEZE] = 1.f;
		g.f.params[F::STRETCH] = 0.f;
		g.f.params[F::POSITION] = 0.95f;
		g.f.stopAtEnd = true;
		g.go(g.secs(3.0));
		CHECK(g.e->state().stopped && g.run.back() < 5.f, "stopped");
		const long long T = g.t;
		g.f.grainSize = GRAIN_LONG;
		g.go(g.secs(1.0));
		CHECK(edges(g.run, T).empty() && meanSquare(g.L, T, g.secs(1.0)) == 0.0, "stopped, Grain changed: still silent, RUN low");
		const long long T2 = g.t;
		g.go(g.secs(4.0), [&](long long t) { g.f.trig = t >= T2 && t < T2 + 48 ? 10.f : 0.f; });
		const auto e = edges(g.run, T2);
		CHECK(e.size() == 2 && e[0].second && g.e->state().grain == grainFrames(GRAIN_LONG, rate), "stopped: one pass at the new size (%zu edges, %d)",
		      e.size(), g.e->state().grain);
		CHECK(e.size() >= 1 && e[0].first <= T2 + grainFrames(GRAIN_LONG, rate) / 2 + g.e->latencyFrames(), "stopped: the trigger heard within a hop");
		// And a rate change while stopped: still stopped, silent, waiting.
		CHECK(g.e->allocate(96000.f), "allocate 96 kHz");
		g.e->setSampleRate(96000.f);
		const size_t at = g.L.size();
		g.go(96000LL * 2);
		bool quiet = true;
		for (size_t i = at + 96000; i < g.L.size(); i++) quiet = quiet && g.L[i] == 0.f && g.run[i] < 5.f;
		CHECK(quiet && g.e->state().stopped, "stopped, rate changed: silent and waiting for a trigger");
	}
	for (int want : stages)
		for (int frozen = 0; frozen < 2; frozen++) {
			// A rate change: the grains laid down play out; a tape that is
			// recording starts again from silence, and (until Task 11 converts it)
			// so does a frozen one.
			Rig g(rate, 103);
			g.f.params[F::FREEZE] = frozen ? 1.f : 0.f;
			g.go(g.secs(1.0), [&](long long t) { g.f.inL = chord(t, rate); });
			toStage(g, want, g.secs(0.5));
			const long long T = (long long) g.L.size();
			CHECK(g.e->allocate(96000.f), "allocate 96 kHz");
			g.e->setSampleRate(96000.f);
			g.rate = 96000.f;
			g.t = 0;
			CHECK(g.e->state().capacity == tapeFrames(96000.f) && g.e->ring().now() == (frozen ? 480 : 0), "stage %x: a new tape at 96 kHz (frozen, its 5 ms fade-out written as silence)", want);
			g.go(g.secs(2.0), [&](long long t) { g.f.inL = chord(t, 96000.f); });
			long long bad = 0;
			for (size_t i = size_t(T); i < g.L.size(); i++) bad += !(std::fabs(g.L[i]) <= 10.f);
			CHECK(bad == 0, "stage %x, frozen %d: bounded", want, frozen);
			// The tail of the old grains: no jump at the change.
			const double w = slopeRatio(g.L, T - 4800, T + 4800, fTop, 48000.0);
			CHECK(w <= 1.0, "stage %x, frozen %d: no click at the change (%.3f)", want, frozen, w);
			const double late = meanSquare(g.L, (long long) g.L.size() - 48000, 48000);
			if (frozen)
				CHECK(late == 0.0, "stage %x: frozen, the new tape is silent (%g)", want, late);
			else
				CHECK(late > 0.1, "stage %x: recording, the chord again (%g)", want, late);
		}
}

// A patch opened with Freeze on (the button or the gate) and stop chosen, with a
// pass to travel: silent, RUN low, until a trigger; then exactly one pass.
void opened_frozen_waits_in_stop() {
	const float rate = 48000.f;
	for (int byGate = 0; byGate < 2; byGate++) {
		Rig g(rate, 121);
		Random n;
		n.seed(19);
		g.tape([&](long long, float& l, float& r) { l = r = n.uniform(); });
		g.f.stopAtEnd = true;
		g.f.params[F::POSITION] = 0.9f;   // a pass of about 3 s at Stretch 1
		g.f.params[F::STRETCH] = 0.f;
		(byGate ? g.f.freezeGate : g.f.params[F::FREEZE]) = byGate ? 10.f : 1.f;
		g.go(g.secs(2.0));
		const char* how = byGate ? "the gate" : "the button";
		CHECK(edges(g.run).empty() && meanSquare(g.L, 0, (long long) g.L.size()) == 0.0, "opened frozen by %s, stop: silent, RUN low", how);
		CHECK(g.e->state().stopped && g.e->state().frozen, "opened frozen by %s: stopped, frozen", how);
		const long long T = g.t;
		g.go(g.secs(6.0), [&](long long t) { g.f.trig = t >= T && t < T + 48 ? 10.f : 0.f; });
		const auto e = edges(g.run);
		CHECK(e.size() == 2 && e[0].second && !e[1].second && e[0].first > T, "opened frozen by %s: one pass on the trigger (%zu edges)", how, e.size());
		CHECK(e.size() == 2 && meanSquare(g.L, e[0].first, e[1].first - e[0].first) > 0.01, "opened frozen by %s: the pass sounds", how);
	}
	{
		// Opened frozen in go round at Position 0: the pass starts as after any
		// reset, at the oldest whole grain. Read as a Freeze edge, it would start
		// a hop further in (Pass.hpp's guard for a tape moving after the edge).
		Rig g(rate, 124);
		g.tape([](long long, float& l, float& r) { l = r = 0.5f; });
		g.f.params[F::FREEZE] = 1.f;
		g.f.params[F::POSITION] = 0.f;
		long long end = -1, oldest = -1;
		g.go(g.secs(0.5), [&](long long) {
			if (end < 0 && g.t > 0 && (g.staged.back() & DoldrumsEngine::STAGED_BOUNDARY) && g.e->state().grain > 0) {
				end = g.e->state().end;
				oldest = g.e->state().now - g.e->state().capacity;
			}
		});
		CHECK(end == oldest + g.grain(), "opened frozen, go round, Position 0: the first slice ends a grain past the oldest frame (%lld, want %lld)",
		      end - oldest, (long long) g.grain());
	}
}

// Opened with Freeze on and Position fully right: nothing to travel through, so
// the tape's last moment sounds, held, RUN high, in either setting.
void opened_frozen_and_held_sounds() {
	const float rate = 48000.f;
	for (int stop = 0; stop < 2; stop++) {
		Rig g(rate, 122);
		g.tape([&](long long i, float& l, float& r) { l = r = chord(i, rate); });
		g.f.stopAtEnd = stop == 1;
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(3.0));
		const auto e = edges(g.run);
		CHECK(e.size() == 1 && e[0].second && e[0].first <= g.hop() + g.e->latencyFrames(), "%s: RUN up at once and never down (%zu edges)",
		      stop ? "stop" : "go round", e.size());
		CHECK(chordShare(spectrum(g.L, g.secs(1.0), g.secs(2.0), rate)) > 0.97 && g.e->state().held, "%s: the tape's last moment, held",
		      stop ? "stop" : "go round");
	}
}

// A cable already high at TRIG when the engine starts is no trigger.
void trig_high_at_start_fires_nothing() {
	const float rate = 48000.f;
	for (int stop = 0; stop < 2; stop++) {
		Rig g(rate, 123);
		Random n;
		n.seed(20);
		g.f.stopAtEnd = stop == 1;
		g.go(g.secs(2.0), [&](long long t) {
			g.f.inL = n.uniform();
			g.f.trig = t < g.secs(1.0) ? 10.f : 0.f;
		});
		const auto e = edges(g.run);
		if (stop)
			CHECK(e.empty() && g.e->state().stopped, "stop: a high TRIG at the start starts nothing (%zu edges)", e.size());
		else
			CHECK(e.size() == 1 && e[0].second, "go round: the first pass only, no dip (%zu edges)", e.size());
		const long long T = g.t;
		g.go(g.secs(1.0), [&](long long t) {
			g.f.inL = n.uniform();
			g.f.trig = t >= T && t < T + 48 ? 10.f : 0.f;
		});
		CHECK(edges(g.run, T).size() == (stop ? 1u : 2u), "%s: a real trigger afterwards is heard", stop ? "stop" : "go round");
	}
}

// Initialize: a silent tape and a new pass; setSampleRate needs allocate first.
void initialize_and_rate() {
	const float rate = 48000.f;
	Rig g(rate, 111);
	g.go(g.secs(2.0), [&](long long t) { g.f.inL = chord(t, rate); });
	const long long now = g.e->ring().now();
	auto recent = [&]() {
		std::vector<float> l(48000), r(48000);
		g.e->ring().copy(g.e->ring().now(), 48000, l.data(), r.data());
		return meanSquare(l, 0, 48000);
	};
	CHECK(meanSquare(g.L, g.secs(1.5), g.secs(0.5)) > 0.1 && recent() > 1.0, "it sounds, and the tape has the chord");
	CHECK(g.e->allocate(rate) && g.e->ring().now() == now && recent() > 1.0, "allocate at the running rate keeps the tape");
	g.e->setSampleRate(rate);
	CHECK(g.e->ring().now() == now && recent() > 1.0, "setSampleRate at the running rate keeps the tape");
	g.e->setSampleRate(44100.f);
	CHECK(g.e->ring().now() == now && recent() > 1.0 && g.e->state().capacity == tapeFrames(rate), "a rate not allocated is ignored");
	g.e->reset();
	std::vector<float> l(size_t(g.e->ring().capacity())), r(l.size());
	g.e->ring().copy(g.e->ring().now(), int(l.size()), l.data(), r.data());
	long long loud = 0;
	for (size_t i = 0; i < l.size(); i++) loud += l[i] != 0.f || r[i] != 0.f;
	CHECK(loud == 0, "reset: a silent tape (%lld frames not)", loud);
	g.f.inL = 0.f;
	g.go(g.secs(2.0));
	CHECK(meanSquare(g.L, (long long) g.L.size() - g.secs(1.0), g.secs(1.0)) == 0.0, "reset, silence in: exact silence out");
	CHECK(g.run.back() > 5.f && g.e->state().now > now, "reset: a new pass, the tape recording");
}


// Initialize and a rate change write no step onto the tape (I2 of the final
// review): the recording fades in from 0, as after a thaw. Measured before
// the fix: 5 V in, the frame before the reset 0 V (the tape cleared) and the
// first after it 5 V.
void no_step_on_the_tape_at_initialize_or_a_rate_change() {
	for (int what = 0; what < 2; what++) {
		const float rate = 48000.f;
		Rig g(rate, 113);
		g.f.inL = 5.f;
		g.go(g.secs(0.5));
		if (what == 0)
			g.e->reset();
		else {
			CHECK(g.e->allocate(44100.f), "allocate(44100)");
			g.e->setSampleRate(44100.f);
		}
		const long long at = g.e->ring().now();
		g.go(1000);
		const int fade = int(std::lround(0.005 * (what == 0 ? 48000.0 : 44100.0)));
		std::vector<float> l(600), r(600);
		CHECK(g.e->ring().copy(at + 600, 600, l.data(), r.data()), "copy");
		float worst = 0.f;
		for (int i = 1; i < 600; i++) worst = std::max(worst, std::fabs(l[i] - l[i - 1]));
		const char* name = what == 0 ? "Initialize" : "a rate change";
		CHECK(l[0] > 0.f && l[0] <= 5.f / fade * 1.0001f, "%s: the first frame recorded %g V (one step is %g)", name, l[0], 5.f / fade);
		CHECK(worst <= 5.f / fade * 1.0001f && l[599] == 5.f, "%s: the recording climbs a step a frame (largest step %g V, then %g)",
		      name, worst, l[599]);
	}
}

// Initialize while the wet sound plays (I3 of the final review): Mix moves
// from where it was over its ramp, it does not snap (measured before the fix:
// Mix 0, 5 V in, the output from 5 V to -0.18 V in one sample); and RUN stays
// high while the grains laid before it still sound, falling (in stop, which
// waits for TRIG after Initialize) once they have been heard.
void initialize_moves_mix_and_keeps_run() {
	const float rate = 48000.f;
	{
		Rig g(rate, 117);
		g.f.params[F::MIX] = 0.f;
		g.f.inL = 5.f;
		g.go(g.secs(2.0));
		const size_t at = g.L.size();
		g.e->reset();
		g.f.params[F::MIX] = 1.f;   // all wet again, as the rig is (the module's Initialize moves Mix to its default)
		g.go(g.secs(0.1));
		float worst = 0.f;
		for (size_t i = at - 10; i < g.L.size(); i++) worst = std::max(worst, std::fabs(g.L[i] - g.L[i - 1]));
		CHECK(worst < 0.05f, "Initialize from Mix 0: the largest step at the output %g V", worst);
	}
	for (int stop = 0; stop < 2; stop++) {
		Rig g(rate, 119);
		g.f.stopAtEnd = stop != 0;
		g.go(g.secs(3.0), [&](long long t) {
			g.f.inL = chord(t, rate);
			g.f.trig = stop && t >= 480 && t < 528 ? 10.f : 0.f;   // in stop, a pass by TRIG
		});
		CHECK(g.run.back() > 5.f, "%s: sounding before Initialize", stop ? "stop" : "go round");
		const long long at = (long long) g.run.size();
		g.e->reset();
		g.f.trig = 0.f;
		g.go(g.secs(1.5), [&](long long t) { g.f.inL = chord(t, rate); });
		CHECK(g.run[size_t(at)] > 5.f, "%s: RUN high on the sample after Initialize", stop ? "stop" : "go round");
		if (stop) {
			// Stopped after Initialize: no new grain; RUN falls once the last
			// grain laid has been heard, and nothing sounds after.
			long long fell = -1;
			for (long long i = at; i < (long long) g.run.size(); i++)
				if (g.run[size_t(i)] < 5.f) {
					fell = i;
					break;
				}
			double after = 0, before = 0;
			if (fell > at) {
				after = meanSquare(g.L, fell, (long long) g.L.size() - fell);
				before = meanSquare(g.L, at, fell - at);
			}
			CHECK(fell > at && fell - at <= g.grain() + g.e->latencyFrames(), "stop: RUN falls %lld samples after Initialize (bound %d)",
			      fell - at, g.grain() + g.e->latencyFrames());
			CHECK(before > 0.0 && after == 0.0, "stop: the tail sounds while RUN is high (%g) and nothing after (%g)", before, after);
		} else
			CHECK(edges(g.run, at).size() <= 2u && g.run.back() > 5.f, "go round: at most a dip, then high (%zu edges)", edges(g.run, at).size());
	}
}

// ---- Tapes handed in (Task 11; DoldrumsEngine.hpp, "Tapes handed in").

// A ring of the tape's length at `rate`, every frame (v, -v), as TapeStore makes them.
Ring* flatTape(float rate, float v) {
	Ring* r = new Ring;
	r->allocate(tapeFrames(rate));
	std::vector<float> l(size_t(r->capacity()), v), rr(l.size(), -v);
	r->fill(l.data(), rr.data());
	return r;
}
// The same with a 5 V tone at hz (DC is cut from the sound).
Ring* toneTape(float rate, double hz) {
	Ring* r = new Ring;
	r->allocate(tapeFrames(rate));
	std::vector<float> l(size_t(r->capacity()));
	for (size_t i = 0; i < l.size(); i++) l[i] = float(5.0 * std::sin(2.0 * kPi * hz * double(i) / rate));
	r->fill(l.data(), l.data());
	return r;
}
void image(const Ring& r, std::vector<float>& l, std::vector<float>& rr) {
	l.assign(size_t(r.capacity()), 0.f);
	rr.assign(l.size(), 0.f);
	r.copy(r.now(), int(r.capacity()), l.data(), rr.data());
}
// Every ring handed back, in order (the caller owns them).
std::vector<Ring*> collect(DoldrumsEngine& e) {
	std::vector<Ring*> v;
	while (Ring* r = e.retired()) v.push_back(r);
	return v;
}
bool has(const std::vector<Ring*>& v, const Ring* r) { return std::find(v.begin(), v.end(), r) != v.end(); }
void drop(std::vector<Ring*>& v) {
	for (Ring* r : v) delete r;
	v.clear();
}

// An offer is taken only for the ticket wanted now, frozen, at this rate's
// length; otherwise handed back at once, unused, and what is live stays.
// I1 of the final review: a stored tape asked for (expectStored, a patch
// opened with Freeze on and its tape kept) while the first sample is already
// thawed (the module was bypassed and Freeze clicked off before it ran):
// the wait ends within a sample, so a save is not left waiting for ever and a
// new freeze can be held still and saved. Measured before the fix:
// tapePending() true for ever and holdStill refused after a new freeze.
void a_wait_ends_on_a_thawed_sample() {
	const float rate = 48000.f;
	Rig g(rate, 205);
	const uint32_t t = g.e->expectStored();
	CHECK(g.e->tapePending(), "the stored tape asked for");
	g.f.params[F::FREEZE] = 0.f;
	g.go(1, [&](long long n) { g.f.inL = chord(n, rate); });
	CHECK(!g.e->tapePending(), "first sample thawed: no tape pending");
	CHECK(g.e->offer(flatTape(rate, 1.f), t), "the stored tape offered late");
	g.go(1);
	std::vector<Ring*> back = collect(*g.e);
	CHECK(back.size() == 1 && g.e->tapesTaken() == 0, "and handed back, not taken");
	drop(back);
	g.go(g.secs(0.5), [&](long long n) { g.f.inL = chord(n, rate); });
	g.f.params[F::FREEZE] = 1.f;
	g.go(g.secs(0.1));
	uint32_t since = 0;
	const Ring* held = g.e->holdStill(&since);
	CHECK(held != nullptr && held == g.e->live() && since == g.e->frozenSince(), "a new freeze: held still for the save");
	g.e->letGo();
	// Frozen on its first sample, the wait stands (the tape is wanted).
	Rig h(rate, 206);
	h.e->expectStored();
	h.f.params[F::FREEZE] = 1.f;
	h.go(h.secs(0.1));
	CHECK(h.e->tapePending(), "opened frozen: the stored tape still wanted");
}

void offers_are_checked() {
	const float rate = 48000.f;
	Rig g(rate, 201);
	g.go(g.secs(0.5), [&](long long t) { g.f.inL = chord(t, rate); });
	g.f.params[F::FREEZE] = 1.f;
	g.go(g.secs(0.2));
	const Ring* a = g.e->live();
	DoldrumsEngine::TapeJob job;
	CHECK(a == &g.e->ring() && !g.e->tapeWanted(job), "frozen, nothing wanted yet");
	auto tryOffer = [&](Ring* r, uint32_t ticket, const char* what) {
		CHECK(g.e->offer(r, ticket), "%s: offered", what);
		g.step();
		std::vector<Ring*> back = collect(*g.e);
		const bool refused = back.size() == 1 && back[0] == r && g.e->live() == a;
		drop(back);
		return refused;
	};
	CHECK(tryOffer(flatTape(rate, 1.f), 1, "nothing wanted"), "nothing wanted: handed back, A stays live");
	const uint32_t t = g.e->expectStored();
	CHECK(g.e->tapeWanted(job) && job.ticket == t && job.from == nullptr && job.toRate == rate, "the stored tape wanted");
	CHECK(tryOffer(flatTape(44100.f, 1.f), t, "another rate's length"), "another rate's length: handed back");
	CHECK(tryOffer(flatTape(rate, 1.f), t + 1, "another ticket"), "another ticket: handed back");
	CHECK(g.e->tapeWanted(job) && job.ticket == t, "still wanted after the refusals");
	// A thaw ends the wait: an offer for it is refused even once frozen again.
	g.f.params[F::FREEZE] = 0.f;
	g.go(1);
	CHECK(!g.e->tapeWanted(job), "thawed: not wanted");
	g.f.params[F::FREEZE] = 1.f;
	g.go(g.secs(0.1));
	a = g.e->live();
	CHECK(tryOffer(flatTape(rate, 1.f), t, "a wait a thaw ended"), "a wait the thaw ended: handed back");
	// Wanted while recording: the next sample, thawed, ends the wait
	// (a_wait_ends_on_a_thawed_sample), and the offer is handed back.
	g.f.params[F::FREEZE] = 0.f;
	g.go(10);
	const uint32_t t1 = g.e->expectStored();
	CHECK(tryOffer(flatTape(rate, 1.f), t1, "not frozen"), "not frozen: handed back");
	CHECK(!g.e->tapePending(), "not frozen: the wait ended");
	// Wanted while frozen: taken.
	g.f.params[F::FREEZE] = 1.f;
	g.go(g.secs(0.1));
	const uint32_t t2 = g.e->expectStored();
	a = g.e->live();
	Ring* b = flatTape(rate, 2.f);
	CHECK(g.e->offer(b, t2), "offered");
	const uint32_t taken = g.e->tapesTaken();
	g.step();
	std::vector<Ring*> back = collect(*g.e);
	CHECK(g.e->live() == b && &g.e->ring() == b && g.e->tapesTaken() == taken + 1 && back.size() == 1 && back[0] == a,
	      "frozen, the right ticket and length: taken, the old ring handed back");
	CHECK(!g.e->tapeWanted(job) && g.e->state().now == b->capacity(), "no longer wanted; the state is B's");
	drop(back);
	CHECK(b->now() == b->capacity(), "frozen: nothing written onto it");
	// Thawed, it records from where it stands, fading in over the engine's 5 ms.
	g.f.params[F::FREEZE] = 0.f;
	g.f.inL = 5.f;
	g.go(1);
	float l = 0.f, r = 0.f;
	CHECK(b->now() == b->capacity() + 1 && b->copy(b->now(), 1, &l, &r) && std::fabs(l - 5.f / 240.f) < 1e-6f,
	      "thawed: one step of the 5 ms fade-in (%g)", l);
}

// A rate change while frozen: the frozen ring is kept as the source, a
// conversion is wanted, asked for again at each new rate from the same source,
// and only the one for the rate in force goes live; the source then comes
// back. A thaw or reset ends the wait; not frozen, nothing is wanted.
void rate_change_converts_frozen_tape() {
	const float rate = 48000.f;
	{
		Rig g(rate, 203);
		g.go(g.secs(1.0), [&](long long t) { g.f.inL = chord(t, rate); });
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(0.1));
		const uint32_t since = g.e->frozenSince();
		const Ring* a = g.e->live();
		std::vector<float> al, ar;
		image(*a, al, ar);
		CHECK(g.e->allocate(44100.f), "allocate 44.1 kHz");
		g.e->setSampleRate(44100.f);
		std::vector<Ring*> back = collect(*g.e);
		DoldrumsEngine::TapeJob j1;
		CHECK(back.empty() && g.e->tapeWanted(j1) && j1.from == a && j1.fromRate == rate && j1.toRate == 44100.f,
		      "44.1 kHz: A kept and wanted converted from 48 kHz (%zu back)", back.size());
		g.rate = 44100.f;
		g.go(g.secs(1.5));
		const Ring* p441 = g.e->live();
		CHECK(meanSquare(g.L, (long long) g.L.size() - g.secs(0.5), g.secs(0.5)) == 0.0, "silent until it arrives (the old grains heard out)");
		// Again before it lands.
		CHECK(g.e->allocate(96000.f), "allocate 96 kHz");
		g.e->setSampleRate(96000.f);
		g.rate = 96000.f;
		back = collect(*g.e);
		DoldrumsEngine::TapeJob j2;
		CHECK(back.size() == 1 && back[0] == p441 && g.e->tapeWanted(j2) && j2.ticket != j1.ticket && j2.from == a && j2.fromRate == rate &&
		          j2.toRate == 96000.f,
		      "96 kHz: asked for again, from A at 48 kHz (not from the silent 44.1 kHz stand-in)");
		drop(back);
		g.go(10);
		Ring* late = new Ring;
		late->allocate(tapeFrames(44100.f));
		CHECK(g.e->offer(late, j1.ticket), "the 44.1 kHz conversion lands late");
		g.go(1);
		back = collect(*g.e);
		CHECK(back.size() == 1 && back[0] == late && g.e->tapeWanted(j2), "refused: made for a rate no longer in force");
		drop(back);
		// Same length, old ticket: still refused (a ring for an older wait never goes live).
		Ring* oldTicket = flatTape(96000.f, 1.f);
		CHECK(g.e->offer(oldTicket, j1.ticket), "offered");
		g.go(1);
		back = collect(*g.e);
		CHECK(back.size() == 1 && back[0] == oldTicket, "an old ticket at the right length: refused");
		drop(back);
		Ring* c = flatTape(96000.f, 0.f);
		{
			std::vector<float> l(size_t(c->capacity()), 0.f), r(l.size(), 0.f);
			// The chord's frames, newest last, as a conversion would put them (not resampled: the content is the test's).
			for (size_t i = 0; i < al.size(); i++) {
				l[l.size() - al.size() + i] = al[i];
				r[r.size() - ar.size() + i] = ar[i];
			}
			c->fill(l.data(), r.data());
		}
		const Ring* p96 = g.e->live();
		CHECK(g.e->offer(c, j2.ticket), "the 96 kHz conversion offered");
		const size_t T = g.L.size();
		g.go(g.secs(2.0));
		back = collect(*g.e);
		CHECK(g.e->live() == c && back.size() == 2 && has(back, a) && has(back, p96) && !g.e->tapeWanted(j2),
		      "taken: the stand-in and the source A handed back");
		drop(back);
		CHECK(meanSquare(g.L, (long long) g.L.size() - g.secs(0.5), g.secs(0.5)) > 0.05, "the frozen tape sounds again");
		CHECK(g.e->frozenSince() == since, "the same frozen tape's number throughout");
		(void) T;
	}
	{
		// There and back: 48, 44.1, 48 kHz. The wait at 48 kHz is from A itself; A
		// converted to its own rate is A to the bit.
		Rig g(rate, 205);
		g.go(g.secs(1.0), [&](long long t) { g.f.inL = chord(t, rate); });
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(0.1));
		const Ring* a = g.e->live();
		CHECK(g.e->allocate(44100.f), "allocate");
		g.e->setSampleRate(44100.f);
		g.go(100);
		CHECK(g.e->allocate(rate), "allocate");
		g.e->setSampleRate(rate);
		DoldrumsEngine::TapeJob j;
		CHECK(g.e->tapeWanted(j) && j.from == a && j.fromRate == rate && j.toRate == rate, "back at 48 kHz: A wanted at its own rate");
		std::vector<Ring*> back = collect(*g.e);
		drop(back);
	}
	{
		// A thaw ends the wait, and the source comes back.
		Rig g(rate, 207);
		g.go(g.secs(0.5), [&](long long t) { g.f.inL = chord(t, rate); });
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(0.1));
		const Ring* a = g.e->live();
		CHECK(g.e->allocate(44100.f), "allocate");
		g.e->setSampleRate(44100.f);
		DoldrumsEngine::TapeJob j;
		CHECK(g.e->tapeWanted(j), "wanted");
		g.f.params[F::FREEZE] = 0.f;
		g.go(1);
		std::vector<Ring*> back = collect(*g.e);
		CHECK(!g.e->tapeWanted(j) && back.size() == 1 && back[0] == a, "thawed: not wanted, A handed back");
		drop(back);
		Ring* late = flatTape(44100.f, 1.f);
		g.f.params[F::FREEZE] = 1.f;
		g.go(100);
		CHECK(g.e->offer(late, j.ticket), "offered");
		g.go(1);
		back = collect(*g.e);
		CHECK(back.size() == 1 && back[0] == late, "the conversion for the ended wait: refused");
		drop(back);
	}
	{
		// reset ends the wait.
		Rig g(rate, 209);
		g.f.params[F::FREEZE] = 1.f;
		g.go(g.secs(0.2));
		const Ring* a = g.e->live();
		CHECK(g.e->allocate(44100.f), "allocate");
		g.e->setSampleRate(44100.f);
		DoldrumsEngine::TapeJob j;
		CHECK(g.e->tapeWanted(j) && j.from == a, "wanted");
		g.e->reset();
		g.go(1);
		std::vector<Ring*> back = collect(*g.e);
		CHECK(!g.e->tapeWanted(j) && back.size() == 1 && back[0] == a, "reset: not wanted, A handed back");
		drop(back);
	}
	{
		// Not frozen: no conversion; the old ring comes back at once.
		Rig g(rate, 211);
		g.go(g.secs(0.2), [&](long long t) { g.f.inL = chord(t, rate); });
		const Ring* a = g.e->live();
		const uint32_t since = g.e->frozenSince();
		CHECK(g.e->allocate(44100.f), "allocate");
		g.e->setSampleRate(44100.f);
		DoldrumsEngine::TapeJob j;
		std::vector<Ring*> back = collect(*g.e);
		CHECK(!g.e->tapeWanted(j) && back.size() == 1 && back[0] == a && g.e->frozenSince() != since,
		      "recording: nothing wanted, the old ring handed back, a new tape's number");
		drop(back);
	}
}

// The hold for the save: granted only while the live ring stands still with
// nothing on its way; a thaw while held writes nothing until letGo, then the
// recording fades in as after any thaw.
void hold_puts_off_a_thaw() {
	const float rate = 48000.f;
	Rig g(rate, 213);
	g.f.inL = 5.f;
	g.go(g.secs(0.5));
	CHECK(g.e->holdStill() == nullptr, "recording: no hold");
	g.f.params[F::FREEZE] = 1.f;
	g.go(10);
	CHECK(g.e->holdStill() == nullptr, "frozen 10 samples, still fading: no hold");
	g.go(300);
	uint32_t since = 0;
	const Ring* h = g.e->holdStill(&since);
	CHECK(h == g.e->live() && since == g.e->frozenSince(), "frozen and still: held, numbered");
	const long long now = g.e->ring().now();
	g.f.params[F::FREEZE] = 0.f;
	g.go(1000);
	CHECK(g.e->ring().now() == now, "thawed while held: nothing written (%lld frames)", g.e->ring().now() - now);
	g.e->letGo();
	g.go(1);
	float l = 0.f, r = 0.f;
	CHECK(g.e->ring().now() == now + 1 && g.e->ring().copy(now + 1, 1, &l, &r) && std::fabs(l - 5.f / 240.f) < 1e-6f,
	      "let go: the thaw goes on, fading in from one step (%g)", l);
	g.go(100);
	CHECK(g.e->holdStill() == nullptr, "recording again: no hold");
	g.f.params[F::FREEZE] = 1.f;
	g.go(300);
	g.e->expectStored();
	CHECK(g.e->holdStill() == nullptr, "a tape on its way: no hold");
	g.e->reset();
	g.go(300);
	CHECK(g.e->holdStill() != nullptr, "nothing on its way, frozen and still: held");
	g.e->letGo();
}

// The frozen tape's number: new at a Freeze edge (button or gate), at reset
// and with a new silent tape at a rate change; not at a thaw, at the first
// sample's arming, at a take or at a conversion.
void frozen_since_numbers() {
	const float rate = 48000.f;
	Rig g(rate, 215);
	g.f.params[F::FREEZE] = 1.f;
	const uint32_t n0 = g.e->frozenSince();
	g.go(100);
	CHECK(g.e->frozenSince() == n0, "opened frozen: no edge, the same number");
	g.f.params[F::FREEZE] = 0.f;
	g.go(100);
	CHECK(g.e->frozenSince() == n0, "a thaw: the same number");
	g.f.params[F::FREEZE] = 1.f;
	g.go(100);
	const uint32_t n1 = g.e->frozenSince();
	CHECK(n1 != n0, "the button: a new number");
	g.f.params[F::FREEZE] = 0.f;
	g.go(100);
	g.f.freezeGate = 10.f;
	g.go(100);
	const uint32_t n2 = g.e->frozenSince();
	CHECK(n2 != n1, "the gate: a new number");
	g.e->expectStored();
	DoldrumsEngine::TapeJob j;
	g.e->tapeWanted(j);
	CHECK(g.e->offer(flatTape(rate, 1.f), j.ticket), "offered");
	g.go(100);
	CHECK(g.e->tapesTaken() == 1u && g.e->frozenSince() == n2, "a tape taken: the same number");
	CHECK(g.e->allocate(44100.f), "allocate");
	g.e->setSampleRate(44100.f);
	CHECK(g.e->frozenSince() == n2, "frozen at a rate change (a conversion on its way): the same number");
	g.e->reset();
	const uint32_t n3 = g.e->frozenSince();
	CHECK(n3 != n2, "reset: a new number");
	g.f.freezeGate = 0.f;
	g.go(100);
	CHECK(g.e->allocate(rate), "allocate");
	g.e->setSampleRate(rate);
	CHECK(g.e->frozenSince() != n3 && g.e->frozenSince() != 0u, "recording at a rate change: a new number, never 0");
	std::vector<Ring*> back = collect(*g.e);
	drop(back);
}

// A swap in stop: the new tape's pass waits for a trigger, as a patch opened
// frozen does; RUN falls once the old grains are heard out, and the trigger
// starts one pass of the new tape.
void swap_in_stop_waits() {
	const float rate = 48000.f;
	Rig g(rate, 217);
	g.f.stopAtEnd = true;
	g.f.params[F::POSITION] = 0.5f;
	g.go(g.secs(1.0), [&](long long t) { g.f.inL = chord(t, rate); });
	g.f.params[F::FREEZE] = 1.f;
	g.go(g.secs(0.2));
	const long long T0 = g.t;
	g.go(g.secs(1.0), [&](long long t) { g.f.trig = t >= T0 && t < T0 + 48 ? 10.f : 0.f; });
	CHECK(g.run.back() > 5.f, "a pass of A under way");
	g.e->expectStored();
	DoldrumsEngine::TapeJob j;
	g.e->tapeWanted(j);
	CHECK(g.e->offer(toneTape(rate, 330.0), j.ticket), "offered");
	const long long T = g.t;
	g.go(g.secs(2.0));
	const auto e1 = edges(g.run, T);
	CHECK(g.e->tapesTaken() == 1u && e1.size() == 1 && !e1[0].second && e1[0].first < T + g.grain() + g.e->latencyFrames() + 2 * g.hop(),
	      "taken in stop: RUN falls when the old grains are heard out (%zu edges)", e1.size());
	CHECK(meanSquare(g.L, (long long) g.L.size() - g.secs(0.5), g.secs(0.5)) == 0.0 && g.e->state().stopped, "waiting, silent");
	const long long T2 = g.t;
	g.go(g.secs(2.0), [&](long long t) { g.f.trig = t >= T2 && t < T2 + 48 ? 10.f : 0.f; });
	const auto e2 = edges(g.run, T2);
	CHECK(e2.size() == 1 && e2[0].second && meanSquare(g.L, (long long) g.L.size() - g.secs(0.5), g.secs(0.5)) > 0.01, "a trigger: B's pass");
	std::vector<Ring*> back = collect(*g.e);
	drop(back);
}

} // namespace

int main() {
	repeatable_and_block_independent();
	stretch_markers();
	freeze_holds_the_moment();
	held_survives_overwriting();
	run_gate();
	the_patch_it_was_asked_for();
	no_clicks();
	comb_follows_voct_within_a_grain();
	comb_keeps_the_level();
	thaw_returns_to_live();
	thaw_while_stopped_stays_stopped();
	window_press_moves_the_read_head_only();
	window_press_at_the_end_plays_and_holds_again();
	window_press_in_stop_starts_a_pass();
	window_press_when_recording();
	default_position_freezes_a_stretch();
	stretch_1_follows_position_after_a_detour();
	follow_is_live_at_any_stretch();
	follow_then_freeze_stretches();
	reopened_held_wakes_on_position();
	mix_and_latency();
	staging_fits();
	rapid_freeze_and_trig();
	rate_and_grain_change_mid_grain();
	initialize_and_rate();
	no_step_on_the_tape_at_initialize_or_a_rate_change();
	initialize_moves_mix_and_keeps_run();
	opened_frozen_waits_in_stop();
	opened_frozen_and_held_sounds();
	trig_high_at_start_fires_nothing();
	a_wait_ends_on_a_thawed_sample();
	offers_are_checked();
	rate_change_converts_frozen_tape();
	hold_puts_off_a_thaw();
	frozen_since_numbers();
	swap_in_stop_waits();
	return rkatest::finish("test_engine");
}
