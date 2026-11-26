// Doldrums' grain (doldrums/Grain.hpp): the analysis, the random-phase
// resynthesis and the overlap, against the design's "Grains", "Mix, inputs and
// outputs" (the level rule, over ten seconds) and "Testing" (Repeatable, Held,
// Level, Does nothing at rest, Stereo), and the plan's Spectral order and
// Stereo. Grains are driven back to back with no staging, every step in
// order, a fixed seed; the slice moves a hop each grain (Stretch 1) unless a
// test says otherwise. 48 kHz, the Medium grain (16384) unless a check says
// otherwise.
#include "doldrums/Grain.hpp"
#include "testing.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using namespace rka::doldrums;
using rka::hadal::Random;

const double kPi = 3.14159265358979323846;

TableSpec restSpec(int bins, float rate) {
	TableSpec s;
	std::memset(&s, 0, sizeof(s));
	s.bins = bins;
	s.rate = rate;
	s.shiftRatio = 1.f;
	return s;
}

double db(double ratio) { return 10.0 * std::log10(ratio); }   // of a power ratio

// One grain size's pieces, and the tables at rest for it.
struct Rig {
	int N, hop;
	float rate;
	GrainBuffers b;
	Grain g;
	Overlap o;
	Tables t;
	Random rng;
	bool wide = false, skipSpectral = false;
	std::vector<float> winL, winR;

	Rig(int grain, float rate_, uint32_t seed) : N(grain), hop(grain / 2), rate(rate_) {
		b.allocate(N);
		o.allocate(N);
		t.allocate(N / 2);
		tables(restSpec(N / 2, rate));
		rng.seed(seed);
		winL.assign(N, 0.f);
		winR.assign(N, 0.f);
	}
	void tables(const TableSpec& s) {
		t.start(s);
		while (!t.work(1 << 20)) {}
		t.swapIfReady();
	}
	// One grain, then a hop of output.
	template <class Sink> void grain(const float* l, const float* r, bool take, Sink&& sink) {
		g.begin(b, l, r, take);
		for (int s = 0; s < Grain::kSteps; s++) {
			if (skipSpectral && (s == Grain::SPECTRAL_L || s == Grain::SPECTRAL_R)) continue;
			g.step(Grain::Step(s), t, rng, wide);
		}
		o.add(g.outL(), g.outR(), N, 1.f);
		for (int i = 0; i < hop; i++) {
			float a, c;
			o.next(a, c);
			sink(a, c);
		}
	}
	// Stretch 1: each slice a hop on from the last, from src(l, r), until
	// at least `out` samples have come out.
	template <class Src, class Sink> void stream(Src&& src, long long out, Sink&& sink) {
		for (int i = 0; i < N; i++) src(winL[i], winR[i]);
		for (long long done = 0; done < out; done += hop) {
			grain(winL.data(), winR.data(), true, sink);
			std::memmove(winL.data(), winL.data() + hop, sizeof(float) * (N - hop));
			std::memmove(winR.data(), winR.data() + hop, sizeof(float) * (N - hop));
			for (int i = N - hop; i < N; i++) src(winL[i], winR[i]);
		}
	}
};

std::unique_ptr<Rig> rig(int grain = 16384, float rate = 48000.f, uint32_t seed = 1) {
	return std::unique_ptr<Rig>(new Rig(grain, rate, seed));
}

// White noise, uniform in [-1, 1): one stream, or two unrelated ones.
struct Noise {
	Random r;
	bool stereo;
	explicit Noise(uint32_t seed, bool stereo_ = false) : stereo(stereo_) { r.seed(seed); }
	void operator()(float& l, float& rr) {
		l = r.uniform();
		rr = stereo ? r.uniform() : l;
	}
};

struct Sine {
	double f, rate, amp, offset;
	long long n = 0;
	Sine(double f_, double rate_, double amp_ = 1.0, double offset_ = 0.0) : f(f_), rate(rate_), amp(amp_), offset(offset_) {}
	void operator()(float& l, float& r) {
		l = r = float(amp * std::sin(2.0 * kPi * f * double(n) / rate) + offset);
		n++;
	}
};

struct Collect {
	std::vector<float> l, r;
	void operator()(float a, float b) {
		l.push_back(a);
		r.push_back(b);
	}
};

// ---------------------------------------------------------------------------

void repeatable() {
	for (int wide = 0; wide < 2; wide++) {
		auto a = rig(16384, 48000.f, 7);
		a->wide = wide;
		Collect first, second, other;
		a->stream(Noise(3, true), 5 * 48000, first);
		// The same pieces again, reset and reseeded.
		a->o.reset();
		a->rng.seed(7);
		a->stream(Noise(3, true), 5 * 48000, second);
		auto c = rig(16384, 48000.f, 8);
		c->wide = wide;
		c->stream(Noise(3, true), 5 * 48000, other);
		const size_t n = first.l.size();
		const bool same = second.l.size() == n && std::memcmp(first.l.data(), second.l.data(), n * sizeof(float)) == 0 &&
		                  std::memcmp(first.r.data(), second.r.data(), n * sizeof(float)) == 0;
		CHECK(same, "repeatable (wide %d): two runs with seed 7 differ", wide);
		CHECK(std::memcmp(first.l.data(), other.l.data(), n * sizeof(float)) != 0, "repeatable (wide %d): seed 8 gives the same", wide);
	}
}

// Level and ripple for one grain size and signal: wet RMS over each 10 s of
// 60 s against the input's, the folded power over the hop (64 phases) and,
// for the waver, the spread of the level over each grain's length.
struct LevelResult { double worstDb, meanDb, rippleDb, spreadDb; };

template <class Src> LevelResult level(int grain, float rate, Src src, uint32_t seed = 11) {
	auto r = rig(grain, rate, seed);
	const long long ten = (long long) (10.0 * rate);
	const long long skip = grain;   // the first half grain has one grain only
	double inSum = 0.0;
	long long inCount = 0;
	auto counted = [&](float& l, float& rr) {
		src(l, rr);
		inSum += double(l) * l;
		inCount++;
	};
	double win = 0.0, worst = 0.0;
	long long n = 0;
	std::vector<double> windows;
	std::vector<double> fold(64, 0.0);
	std::vector<double> grainLevels;
	double g = 0.0;
	long long gCount = 0;
	auto sink = [&](float l, float) {
		if (n >= skip && n < skip + 6 * ten) {
			const double p = double(l) * l;
			win += p;
			fold[(size_t) (((n - skip) % r->hop) * 64 / r->hop)] += p;
			g += p;
			if (++gCount == grain) {
				grainLevels.push_back(g / grain);
				g = 0.0;
				gCount = 0;
			}
			if ((n - skip + 1) % ten == 0) {
				windows.push_back(win / ten);
				win = 0.0;
			}
		}
		n++;
	};
	r->stream(counted, skip + 6 * ten, sink);
	const double in = inSum / inCount;
	double all = 0.0;
	for (double w : windows) {
		worst = std::max(worst, std::fabs(db(w / in)));
		all += w / windows.size();
	}
	double lo = fold[0], hi = fold[0];
	for (double f : fold) {
		lo = std::min(lo, f);
		hi = std::max(hi, f);
	}
	double m = 0.0, m2 = 0.0;
	for (double x : grainLevels) {
		const double d = db(x / in);
		m += d;
		m2 += d * d;
	}
	m /= grainLevels.size();
	const double spread = std::sqrt(std::max(0.0, m2 / grainLevels.size() - m * m));
	if (windows.size() != 6) worst = 99.0;
	return {worst, std::fabs(db(all / in)), db(hi / lo), spread};
}

struct Size { int grain; float rate; };
const Size kSizes[] = {{8192, 48000.f}, {16384, 48000.f}, {32768, 48000.f}, {65536, 96000.f}, {131072, 192000.f}};

// Noise: within 1 dB over each 10 s; ripple at the hop's rate (max - min of
// the power folded onto 64 phases of the hop, over 60 s) under a bound set
// from what it measured on 2026-11-26: 0.09 to 0.18 dB with the seed here.
// A laying-down window whose squares do not sum flat (Hann) gives 3 dB.
const double kNoiseRippleDb = 0.3;

void level_noise_and_ripple() {
	for (const Size& s : kSizes) {
		const LevelResult r = level(s.grain, s.rate, Noise(5));
		std::printf("  noise %6d @ %6.0f: worst 10 s %.3f dB, ripple %.3f dB\n", s.grain, s.rate, r.worstDb, r.rippleDb);
		CHECK(r.worstDb < 1.0, "level_noise %d: %.3f dB off", s.grain, r.worstDb);
		CHECK(r.rippleDb < kNoiseRippleDb, "noise_ripple %d: %.3f dB", s.grain, r.rippleDb);
	}
}

// A pure sine wavers where grains overlap (the design's Mix): a tone fills
// only a few bins, so its random-phase output changes only every few grains'
// worth of samples, and 10 s at the Long grain holds too few of those to
// average within 1 dB every time. So for a sine, over kSeeds seeds at each
// size: the mean over 60 s within kSineMeanDb of the input's, every seed; the
// worst 10 s window under a regression bound per size (the worst measured
// plus 0.5 dB, not a promise); and the waver over each grain's length (the
// standard deviation of the level over a grain, in dB) pinned to a band.
// Measured 2026-11-26, 1 kHz, seeds 1 to 20 (per seed in the task report):
//   grain @ rate      mean 60 s (worst)  worst 10 s  waver per grain
//   8192 @ 48k        0.148 dB           0.603 dB    1.744 to 2.063 dB
//   16384 @ 48k       0.263 dB           0.835 dB    1.732 to 1.988 dB
//   32768 @ 48k       0.430 dB           1.162 dB    1.349 to 2.163 dB
//   65536 @ 96k       0.389 dB           1.081 dB    1.492 to 2.045 dB
//   131072 @ 192k     0.486 dB           1.278 dB    1.483 to 2.147 dB
// (Over 200 seeds at 32768 the worst 10 s window reached 2.09 dB.)
const int kSeeds = 20;
const double kSineMeanDb = 0.8;
const double kSineWorst10sDb[5] = {1.10, 1.34, 1.66, 1.58, 1.78};
const double kSineWaverLo = 1.1, kSineWaverHi = 2.5;

void level_sine() {
	int i = 0;
	for (const Size& s : kSizes) {
		double mean = 0.0, worst = 0.0, waverLo = 99.0, waverHi = 0.0;
		for (uint32_t seed = 1; seed <= (uint32_t) kSeeds; seed++) {
			const LevelResult r = level(s.grain, s.rate, Sine(1000.0, s.rate, 0.7), seed);
			mean = std::max(mean, r.meanDb);
			worst = std::max(worst, r.worstDb);
			waverLo = std::min(waverLo, r.spreadDb);
			waverHi = std::max(waverHi, r.spreadDb);
		}
		std::printf("  sine  %6d @ %6.0f, %d seeds: mean 60 s %.3f dB, worst 10 s %.3f dB, waver per grain %.3f to %.3f dB\n", s.grain,
		            s.rate, kSeeds, mean, worst, waverLo, waverHi);
		CHECK(mean < kSineMeanDb, "level_sine %d: the 60 s mean %.3f dB off", s.grain, mean);
		CHECK(worst < kSineWorst10sDb[i], "level_sine %d: a 10 s window %.3f dB off, bound %.2f", s.grain, worst, kSineWorst10sDb[i]);
		CHECK(waverLo > kSineWaverLo && waverHi < kSineWaverHi, "sine waver %d: %.3f to %.3f dB", s.grain, waverLo, waverHi);
		i++;
	}
}

void silence_is_silence() {
	{
		auto r = rig();
		int bad = 0;
		r->stream([](float& l, float& rr) { l = rr = 0.f; }, 5 * 48000, [&](float l, float rr) { bad += l != 0.f || rr != 0.f; });
		CHECK(bad == 0, "zeros in: %d samples not zero", bad);
	}
	{
		// NaN and inf in read as 0.
		auto r = rig();
		int bad = 0, k = 0;
		r->stream([&](float& l, float& rr) {
			l = k % 2 ? std::nanf("") : HUGE_VALF;
			rr = -HUGE_VALF;
			k++;
		}, 2 * 48000, [&](float l, float rr) { bad += l != 0.f || rr != 0.f; });
		CHECK(bad == 0, "NaN and inf in: %d samples not zero", bad);
	}
	{
		// One NaN sample in noise is that sample at 0, not a grain lost.
		auto a = rig(), b = rig();
		Noise na(10, true), nb(10, true);
		long long ka = 0, kb = 0;
		Collect ca, cb;
		a->stream([&](float& l, float& rr) {
			na(l, rr);
			if (ka++ == 20000) l = rr = std::nanf("");
		}, 2 * 48000, ca);
		b->stream([&](float& l, float& rr) {
			nb(l, rr);
			if (kb++ == 20000) l = rr = 0.f;
		}, 2 * 48000, cb);
		CHECK(ca.l == cb.l && ca.r == cb.r, "one NaN sample: not read as 0");
	}
	{
		// Samples so large that a loudness would overflow: held finite, out finite.
		auto r = rig();
		Noise n(14);
		int bad = 0;
		r->stream([&](float& l, float& rr) {
			n(l, rr);
			l *= 1e36f;
			rr = l;
		}, 2 * 48000, [&](float l, float rr) { bad += !std::isfinite(l) || !std::isfinite(rr); });
		CHECK(bad == 0, "huge samples: %d not finite out", bad);
	}
	// A grain of noise, then zeros: the last grain holding any of it is laid
	// at a hop and ends at a hop and a grain, so zeros from there on.
	auto r = rig();
	long long in = 0, n = 0, badAfter = 0, soundBefore = 0;
	Noise noise(9);
	auto src = [&](float& l, float& rr) {
		if (in++ < r->N) noise(l, rr);
		else l = rr = 0.f;
	};
	const long long end = r->hop + r->N;
	r->stream(src, 5 * 48000, [&](float l, float rr) {
		if (n >= end) badAfter += l != 0.f || rr != 0.f;
		else if (n >= r->hop) soundBefore += l != 0.f;
		n++;
	});
	CHECK(badAfter == 0, "a grain of sound then zeros: %lld samples not zero after %lld", badAfter, end);
	CHECK(soundBefore > r->N / 2, "a grain of sound then zeros: the sound came out (%lld)", soundBefore);
}

// Power in [f - w, f + w] Hz of x (8192-point Hann frames, end to end).
struct Spectrum {
	static const int kN = 8192;
	rka::RealFft fft{kN};
	float* in = rka::RealFft::alloc(kN);
	float* out = rka::RealFft::alloc(kN);
	~Spectrum() {
		rka::RealFft::free(in);
		rka::RealFft::free(out);
	}
	std::vector<double> power(const float* x, long long len) {
		std::vector<double> p(kN / 2, 0.0);
		for (long long at = 0; at + kN <= len; at += kN) {
			for (int i = 0; i < kN; i++) in[i] = x[at + i] * float(0.5 - 0.5 * std::cos(2.0 * kPi * i / kN));
			fft.forward(in, out);
			for (int k = 1; k < kN / 2; k++) p[k] += double(out[2 * k]) * out[2 * k] + double(out[2 * k + 1]) * out[2 * k + 1];
		}
		return p;
	}
	static double band(const std::vector<double>& p, double f, double w, double rate) {
		double s = 0.0;
		for (int k = 1; k < kN / 2; k++) {
			const double fk = k * rate / kN;
			if (fk >= f - w && fk <= f + w) s += p[k];
		}
		return s;
	}
};

// A chord held for ten minutes (Short grain): one grain taken, then take
// false with loud noise under it on the "tape".
void held_does_not_drift_or_repeat() {
	const float rate = 48000.f;
	auto r = rig(8192, rate, 21);
	const double notes[3] = {220.0, 277.18, 329.63};
	std::vector<float> chord(r->N), noise(r->N);
	double inPow;
	for (int i = 0; i < r->N; i++) {
		double x = 0.0;
		for (double f : notes) x += 0.4 * std::sin(2.0 * kPi * f * i / rate);
		chord[i] = float(x);
	}
	inPow = 3 * 0.4 * 0.4 / 2.0;
	Noise nz(4);
	for (int i = 0; i < r->N; i++) {
		float a, b;
		nz(a, b);
		noise[i] = 3.f * a;
	}
	const long long minute = 60LL * 48000, total = 10 * minute;
	std::vector<float> y;
	y.reserve(total + r->hop);
	auto sink = [&](float l, float) { y.push_back(l); };
	r->grain(chord.data(), chord.data(), true, sink);
	while ((long long) y.size() < total + r->N) r->grain(noise.data(), noise.data(), false, sink);
	const float* out = y.data() + r->N;   // past the taken grain's rise

	// Spectrum per minute: each note's band, and the whole, against minute 1.
	Spectrum sp;
	std::vector<double> first;
	double worstSpec = 0.0, noiseBand = 0.0;
	for (int m = 0; m < 10; m++) {
		const std::vector<double> p = sp.power(out + m * minute, minute);
		std::vector<double> bands;
		for (double f : notes) bands.push_back(Spectrum::band(p, f, 15.0, rate));
		bands.push_back(Spectrum::band(p, 1000.0, 1e6, rate));
		if (m == 0) {
			first = bands;
			noiseBand = Spectrum::band(p, 5000.0, 2000.0, rate) / bands[3];
		}
		for (size_t i = 0; i < bands.size(); i++) worstSpec = std::max(worstSpec, std::fabs(db(bands[i] / first[i])));
	}
	// Level per 10 s against the chord's.
	double worstLevel = 0.0;
	for (long long at = 0; at + 480000 <= total; at += 480000) {
		double s = 0.0;
		for (long long i = 0; i < 480000; i++) s += double(out[at + i]) * out[at + i];
		worstLevel = std::max(worstLevel, std::fabs(db(s / 480000 / inPow)));
	}
	// Autocorrelation at lags of m hops.
	double worstCorr = 0.0;
	int worstLag = 0;
	const long long span = total - 64LL * r->hop;
	for (int m = 1; m <= 64; m++) {
		const long long L = (long long) m * r->hop;
		double xy = 0.0, xx = 0.0, yy = 0.0;
		for (long long i = 0; i < span; i += 3) {
			const double a = out[i], b = out[i + L];
			xy += a * b;
			xx += a * a;
			yy += b * b;
		}
		const double c = xy / std::sqrt(xx * yy);
		if (std::fabs(c) > worstCorr) {
			worstCorr = std::fabs(c);
			worstLag = m;
		}
	}
	std::printf("  held 10 min: spectrum %.3f dB, level %.3f dB, autocorrelation %.3f at %d hops, noise band %.1f dB\n", worstSpec,
	            worstLevel, worstCorr, worstLag, db(noiseBand + 1e-30));
	CHECK(worstSpec < 0.5, "held: spectrum per minute %.3f dB from the first", worstSpec);
	CHECK(worstLevel < 1.0, "held: level per 10 s %.3f dB", worstLevel);
	CHECK(worstCorr < 0.2, "held: autocorrelation %.3f at %d hops", worstCorr, worstLag);
	CHECK(noiseBand < 1e-4, "held: the noise under it came through (%.1f dB)", db(noiseBand + 1e-30));

	// Held, the slices are not read: null slices give the same bits.
	auto a = rig(8192, rate, 5), b = rig(8192, rate, 5);
	Collect ca, cb;
	a->grain(chord.data(), chord.data(), true, ca);
	b->grain(chord.data(), chord.data(), true, cb);
	for (int i = 0; i < 40; i++) {
		a->grain(noise.data(), chord.data(), false, ca);
		b->grain(nullptr, nullptr, false, cb);
	}
	CHECK(ca.l == cb.l && ca.r == cb.r, "held: the slices were read");
}

// With the tables at rest the spectral steps change nothing: the same bits
// as a run without them. A DC offset and a 10 Hz tone put loudness under the
// cut, which the grain cuts itself.
void rest_matches_no_spectral_step() {
	auto src = [](uint32_t seed) {
		Noise n(seed, true);
		Sine low(10.0, 48000.0, 0.5, 0.3);
		return [n, low](float& l, float& r) mutable {
			float a, b, c, d;
			n(a, b);
			low(c, d);
			l = a + c;
			r = 0.5f * b + c;
		};
	};
	for (int wide = 0; wide < 2; wide++) {
		auto a = rig(16384, 48000.f, 3), b = rig(16384, 48000.f, 3), c = rig(16384, 48000.f, 3);
		a->wide = b->wide = c->wide = wide;
		b->skipSpectral = true;
		TableSpec tilt = restSpec(8192, 48000.f);
		tilt.tiltDbPerOct = 1.f;
		c->tables(tilt);
		Collect ca, cb, cc;
		a->stream(src(2), 4 * 48000, ca);
		b->stream(src(2), 4 * 48000, cb);
		c->stream(src(2), 4 * 48000, cc);
		CHECK(ca.l == cb.l && ca.r == cb.r, "rest (wide %d): differs from the run without the spectral steps", wide);
		CHECK(ca.l != cc.l, "rest (wide %d): Tilt changed nothing (the step is not live)", wide);
	}
}

void stereo_mono_stays_mono() {
	auto r = rig();
	Noise n(6);
	Sine s(700.0, 48000.0, 0.5);
	long long diff = 0, nonzero = 0;
	r->stream([&](float& l, float& rr) {
		float a, b, c, d;
		n(a, b);
		s(c, d);
		l = rr = a + c;
	}, 10 * 48000, [&](float l, float rr) {
		diff += std::memcmp(&l, &rr, sizeof(float)) != 0;
		nonzero += l != 0.f;
	});
	CHECK(diff == 0 && nonzero > 0, "mono stays mono: %lld samples differ", diff);
}

void stereo_keeps_a_level_pan() {
	auto r = rig();
	Noise n(12);
	double sl = 0.0, sr = 0.0;
	long long k = 0;
	r->stream([&](float& l, float& rr) {
		float a, b;
		n(a, b);
		l = a;
		rr = 0.25f * a;
	}, 30 * 48000, [&](float l, float rr) {
		if (k++ >= r->N) {
			sl += double(l) * l;
			sr += double(rr) * rr;
		}
	});
	const double pan = db(sr / sl);
	std::printf("  level pan: %.3f dB (in %.3f)\n", pan, db(0.0625));
	CHECK(std::fabs(pan - db(0.0625)) < 0.25, "level pan: %.3f dB, want %.3f", pan, db(0.0625));
}

// The phase of R against L at f, averaged over 30 s: blocks of 4800 samples
// (500 Hz on a bin), Hann-weighted, cross-spectra summed.
double phaseAt(const std::vector<float>& l, const std::vector<float>& r, size_t from, double f, double rate) {
	const int B = 4800;
	double cr = 0.0, ci = 0.0;
	for (size_t at = from; at + B <= l.size(); at += B) {
		double lr = 0, li = 0, rr = 0, ri = 0;
		for (int i = 0; i < B; i++) {
			const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / B);
			const double c = std::cos(2.0 * kPi * f * i / rate), s = -std::sin(2.0 * kPi * f * i / rate);
			lr += w * l[at + i] * c;
			li += w * l[at + i] * s;
			rr += w * r[at + i] * c;
			ri += w * r[at + i] * s;
		}
		cr += rr * lr + ri * li;
		ci += ri * lr - rr * li;
	}
	return std::atan2(ci, cr) * 180.0 / kPi;
}

void stereo_keeps_a_phase_difference() {
	const double f = 500.0, rate = 48000.0;
	auto r = rig();
	long long n = 0;
	std::vector<float> inL, inR;
	Collect out;
	r->stream([&](float& l, float& rr) {
		l = float(std::sin(2.0 * kPi * f * n / rate));
		rr = float(std::sin(2.0 * kPi * f * n / rate + kPi / 2));
		inL.push_back(l);
		inR.push_back(rr);
		n++;
	}, 30 * 48000, out);
	const double want = phaseAt(inL, inR, 0, f, rate);
	const double got = phaseAt(out.l, out.r, r->N, f, rate);
	double d = got - want;
	while (d > 180.0) d -= 360.0;
	while (d < -180.0) d += 360.0;
	std::printf("  phase difference: in %.2f deg, out %.2f deg\n", want, got);
	CHECK(std::fabs(std::fabs(want) - 90.0) < 0.5, "phase difference: the input's is %.2f", want);
	CHECK(std::fabs(d) < 10.0, "phase difference: %.2f deg out, %.2f in", got, want);
}

void wide_decorrelates() {
	auto r = rig();
	r->wide = true;
	double xy = 0, xx = 0, yy = 0;
	long long k = 0;
	r->stream(Noise(13), 30 * 48000, [&](float l, float rr) {
		if (k++ >= r->N) {
			xy += double(l) * rr;
			xx += double(l) * l;
			yy += double(rr) * rr;
		}
	});
	const double c = xy / std::sqrt(xx * yy);
	std::printf("  wide: correlation %.4f\n", c);
	CHECK(std::fabs(c) < 0.2, "wide: correlation %.4f", c);
}

// Two unrelated tones on alternate grains: every grain boundary is a dissolve,
// so no sample-to-sample step beyond what a signal band-limited to just above
// the higher tone can make at the output's peak (2 sin(pi F / rate) max|y|).
void dissolve_between_unrelated_slices() {
	const double rate = 48000.0, fa = 300.0, fb = 1100.0;
	auto r = rig();
	std::vector<float> a(r->N), b(r->N);
	Collect out;
	long long at = 0;
	for (int g = 0; g < 120; g++) {
		for (int i = 0; i < r->N; i++) {
			const double t = double(at + i) / rate;
			a[i] = float(std::sin(2.0 * kPi * fa * t));
			b[i] = float(std::sin(2.0 * kPi * fb * t));
		}
		r->grain(g % 2 ? b.data() : a.data(), g % 2 ? b.data() : a.data(), true, out);
		at += r->hop;
	}
	double peak = 0.0, step = 0.0;
	for (size_t i = r->N + 1; i < out.l.size(); i++) {
		peak = std::max(peak, (double) std::fabs(out.l[i]));
		step = std::max(step, (double) std::fabs(out.l[i] - out.l[i - 1]));
	}
	const double bound = 2.0 * std::sin(kPi * fb * 1.1 / rate) * peak;
	std::printf("  dissolve: largest step %.5f, bound %.5f (peak %.3f)\n", step, bound, peak);
	CHECK(peak > 0.3, "dissolve: no sound (%.3f)", peak);
	CHECK(step <= bound, "dissolve: a step of %.5f, bound %.5f", step, bound);
}

// The overlap itself: grains laid at the next sample, summed, gain applied,
// read once, sizes mixed, reset.
void overlap_lays_grains_down() {
	Overlap o;
	o.allocate(8);
	std::vector<float> ones(8, 1.f), twos(4, 2.f);
	std::vector<float> got;
	auto take = [&](int k) {
		for (int i = 0; i < k; i++) {
			float l, r;
			o.next(l, r);
			got.push_back(l + 10.f * r);
		}
	};
	o.add(ones.data(), ones.data(), 8, 1.f);
	take(4);
	o.add(ones.data(), twos.data(), 4, 0.5f);   // a smaller grain, at half gain
	take(4);
	o.add(ones.data(), ones.data(), 8, std::nanf(""));
	take(8);
	const float want[16] = {11, 11, 11, 11, 21.5f, 21.5f, 21.5f, 21.5f, 0, 0, 0, 0, 0, 0, 0, 0};
	CHECK(got.size() == 16 && std::memcmp(got.data(), want, sizeof(want)) == 0, "overlap: %g %g %g %g %g", got[0], got[3], got[4],
	      got[7], got[8]);
	o.add(ones.data(), ones.data(), 8, 1.f);
	o.reset();
	got.clear();
	take(8);
	bool zero = true;
	for (float x : got) zero = zero && x == 0.f;
	CHECK(zero, "overlap: reset left something");
	GrainBuffers bad;
	CHECK(!bad.allocate(1000) && !bad.allocate(16) && !bad.allocate(262144) && bad.grain() == 0, "bad grain sizes refused");
}

} // namespace

int main() {
	const auto t0 = std::chrono::steady_clock::now();
	repeatable();
	level_noise_and_ripple();
	level_sine();
	silence_is_silence();
	held_does_not_drift_or_repeat();
	rest_matches_no_spectral_step();
	stereo_mono_stays_mono();
	stereo_keeps_a_level_pan();
	stereo_keeps_a_phase_difference();
	wide_decorrelates();
	dissolve_between_unrelated_slices();
	overlap_lays_grains_down();
	std::printf("  %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	return rkatest::finish("doldrums/test_grain");
}
