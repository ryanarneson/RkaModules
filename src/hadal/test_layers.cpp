// Hadal's layers, offline: Rumble and Hiss (Task 2), Crackle (Task 3) and the
// Drift wander (Task 4). Spec: docs/specs/2026-11-16-hadal-design.md.
#include "testing.hpp"
#include "test_measure.hpp"
#include "Random.hpp"
#include "NoiseLayer.hpp"
#include "Crackle.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace hadaltest;

namespace {

const float FS = 48000.f;

template <class L>
void render(L& layer, float fs, float depth, float seconds, std::vector<float>& l, std::vector<float>& r,
            uint32_t seed = 1) {
	layer.setRate(fs);
	layer.seed(seed);
	layer.setDepth(depth);
	// Settle the filters (the 20 Hz high-pass rings for a while).
	for (int i = 0; i < (int) (fs * 0.5f); i++) {
		float a, b;
		layer.process(a, b);
	}
	const int n = (int) (fs * seconds);
	l.resize(n);
	r.resize(n);
	for (int i = 0; i < n; i++)
		layer.process(l[i], r[i]);
}

int nfftFor(float fs, double resolutionHz) {
	int n = 1024;
	while (fs / n > resolutionHz)
		n <<= 1;
	return n;
}

const float DEPTHS[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};

template <class L>
void centroidFalls(const char* name, double maxDeep, double minShallow, double resolution) {
	double prev = 1e9;
	for (float d : DEPTHS) {
		L layer;
		std::vector<float> l, r;
		render(layer, FS, d, 20.f, l, r);
		const int nfft = nfftFor(FS, resolution);
		const double c = centroid(psd(l, nfft), nfft, FS);
		std::printf("  %s depth %.2f: centroid %.1f Hz\n", name, d, c);
		CHECK(c < prev, "%s centroid must fall with depth: %.1f Hz at depth %.2f, after %.1f Hz", name, c, d, prev);
		if (d == 0.f)
			CHECK(c > minShallow, "%s centroid at depth 0 is %.1f Hz (want > %.0f)", name, c, minShallow);
		if (d == 1.f)
			CHECK(c < maxDeep, "%s centroid at depth 1 is %.1f Hz (want < %.0f)", name, c, maxDeep);
		prev = c;
	}
}

void rumble_centroid_falls_with_depth() {
	centroidFalls<rka::hadal::Rumble>("rumble", 60.0, 0.0, 1.0);
}

void hiss_centroid_falls_with_depth() {
	centroidFalls<rka::hadal::Hiss>("hiss", 1100.0, 3000.0, 10.0);
}

template <class L>
void levelFlat(const char* name) {
	for (float d : DEPTHS) {
		L layer;
		std::vector<float> l, r;
		render(layer, FS, d, 10.f, l, r);
		const double gl = db(rms(l)), gr = db(rms(r));
		std::printf("  %s depth %.2f: L %+.2f dB, R %+.2f dB re 1.0\n", name, d, gl, gr);
		CHECK(std::fabs(gl) <= 1.5 && std::fabs(gr) <= 1.5, "%s level at depth %.2f is %+.2f / %+.2f dB (want within 1.5)",
		      name, d, gl, gr);
	}
}

void layer_level_flat_across_depth() {
	levelFlat<rka::hadal::Rumble>("rumble");
	levelFlat<rka::hadal::Hiss>("hiss");
}

void rumble_little_below_15hz() {
	rka::hadal::Rumble layer;
	std::vector<float> l, r;
	render(layer, FS, 1.f, 30.f, l, r);
	const int nfft = nfftFor(FS, 0.5);
	const double share = bandShare(psd(l, nfft), nfft, FS, 0.0, 15.0);
	std::printf("  rumble below 15 Hz at depth 1: %.2f %%\n", 100 * share);
	CHECK(share < 0.05, "rumble has %.2f %% of its energy below 15 Hz (want < 5 %%)", 100 * share);
}

template <class L>
void noDc(const char* name) {
	for (float d : {0.f, 1.f}) {
		L layer;
		std::vector<float> l, r;
		render(layer, FS, d, 10.f, l, r);
		// 5e-3 of the layer's RMS (-46 dB): a 20-40 Hz band of noise averaged
		// over 10 s wanders by about 1e-3 with no DC at all, so a tighter
		// bound would fail on chance; a real offset is far larger.
		CHECK(std::fabs(mean(l)) < 5e-3 && std::fabs(mean(r)) < 5e-3, "%s DC at depth %.0f: %g / %g", name, d, mean(l),
		      mean(r));
	}
}

void no_dc() {
	noDc<rka::hadal::Rumble>("rumble");
	noDc<rka::hadal::Hiss>("hiss");
}

void rumble_correlated_hiss_not() {
	for (float d : {0.f, 0.5f, 1.f}) {
		std::vector<float> l, r;
		rka::hadal::Rumble ru;
		render(ru, FS, d, 10.f, l, r);
		const double c = corr(l, r);
		CHECK(c >= 0.8, "rumble L/R correlation at depth %.1f is %.3f (want >= 0.8)", d, c);
		rka::hadal::Hiss hi;
		render(hi, FS, d, 10.f, l, r);
		const double h = corr(l, r);
		CHECK(std::fabs(h) < 0.1, "hiss L/R correlation at depth %.1f is %.3f (want |c| < 0.1)", d, h);
	}
}

template <class L>
void noPitch(const char* name, double fLo, double fHi, double resolution) {
	for (float d : {0.f, 0.5f, 1.f}) {
		L layer;
		std::vector<float> l, r;
		render(layer, FS, d, 20.f, l, r);
		const int nfft = nfftFor(FS, resolution);
		const double peak = maxPeakDb(psd(l, nfft), nfft, FS, fLo, fHi);
		CHECK(peak < 12.0, "%s at depth %.1f has a spectral peak %.1f dB over its neighbourhood (want < 12)", name, d,
		      peak);
	}
}

void no_pitch() {
	noPitch<rka::hadal::Rumble>("rumble", 20.0, 400.0, 1.0);
	noPitch<rka::hadal::Hiss>("hiss", 300.0, 15000.0, 10.0);
}

template <class L>
void noLoop(const char* name) {
	L layer;
	std::vector<float> l, r;
	render(layer, FS, 0.5f, 20.f, l, r);
	// From 250 ms: below that a band-limited noise's own autocorrelation is
	// not yet gone; a loop or a repeat would be longer.
	const double a = maxAutocorr(l, (int) (0.25f * FS), (int) (10.f * FS));
	CHECK(a < 0.1, "%s autocorrelation reaches %.3f between 250 ms and 10 s (want < 0.1)", name, a);
}

void no_loop() {
	noLoop<rka::hadal::Rumble>("rumble");
	noLoop<rka::hadal::Hiss>("hiss");
}

void same_seed_repeats_other_differs() {
	std::vector<float> a, b, c, x, y, z;
	rka::hadal::Hiss h1, h2, h3;
	render(h1, FS, 0.5f, 1.f, a, x, 7);
	render(h2, FS, 0.5f, 1.f, b, y, 7);
	render(h3, FS, 0.5f, 1.f, c, z, 8);
	bool same = a == b && x == y;
	CHECK(same, "the same seed must repeat exactly");
	CHECK(std::fabs(corr(a, c)) < 0.1, "different seeds must differ (corr %.3f)", corr(a, c));
}

template <class L>
void stableAt(const char* name, double resolution) {
	L ref;
	std::vector<float> l, r;
	render(ref, FS, 0.5f, 10.f, l, r);
	const int n0 = nfftFor(FS, resolution);
	const double c0 = centroid(psd(l, n0), n0, FS);
	for (float fs : {44100.f, 96000.f, 192000.f, 768000.f}) {
		L layer;
		render(layer, fs, 0.5f, fs >= 192000.f ? 4.f : 10.f, l, r);
		bool finite = true;
		for (float v : l)
			finite = finite && v == v && v < 1e6f && v > -1e6f;
		CHECK(finite, "%s at %.0f Hz must stay finite", name, fs);
		const int n = nfftFor(fs, resolution);
		const double c = centroid(psd(l, n), n, fs);
		const double g = db(rms(l));
		std::printf("  %s at %.0f Hz: centroid %.1f Hz (48 kHz: %.1f), level %+.2f dB\n", name, fs, c, c0, g);
		CHECK(std::fabs(c / c0 - 1.0) < 0.10, "%s centroid at %.0f Hz is %.1f (48 kHz: %.1f)", name, fs, c, c0);
		CHECK(std::fabs(g) <= 1.5, "%s level at %.0f Hz is %+.2f dB", name, fs, g);
	}
}

void stable_at_all_rates() {
	stableAt<rka::hadal::Rumble>("rumble", 1.0);
	stableAt<rka::hadal::Hiss>("hiss", 10.0);
}


// ---- Crackle (Task 3) ----

struct Ticks {
	std::vector<float> l, r;
	std::vector<int> at;      // sample of each tick
	std::vector<float> peak;  // its largest |sample|, either side
	std::vector<float> pan;   // 0 = hard left, 1 = hard right
};

Ticks renderCrackle(float depth, float seconds, float density = 10.f, uint32_t seed = 1, float fs = FS) {
	rka::hadal::Crackle c;
	c.setRate(fs);
	c.seed(seed);
	c.setDepth(depth);
	c.setDensity(density);
	Ticks t;
	const int n = (int) (fs * seconds);
	t.l.resize(n);
	t.r.resize(n);
	for (int i = 0; i < n; i++) {
		c.process(t.l[i], t.r[i]);
		if (c.ticked()) {
			t.at.push_back(i);
			t.pan.push_back(c.lastPan());
		}
	}
	// Each tick's peak: the largest |sample| from it to the next tick.
	for (size_t k = 0; k < t.at.size(); k++) {
		const int end = k + 1 < t.at.size() ? t.at[k + 1] : n;
		float pk = 0;
		for (int i = t.at[k]; i < end; i++)
			pk = std::max(pk, std::max(std::fabs(t.l[i]), std::fabs(t.r[i])));
		t.peak.push_back(pk);
	}
	return t;
}

void crackle_rate() {
	Ticks t = renderCrackle(0.5f, 60.f);
	const double n = t.at.size();
	CHECK(n > 600 * 0.85 && n < 600 * 1.15, "crackle made %.0f ticks in 60 s at density 10 (want 600 +- 15 %%)", n);
	std::vector<double> gaps;
	for (size_t k = 1; k < t.at.size(); k++)
		gaps.push_back(t.at[k] - t.at[k - 1]);
	double m = 0, v = 0;
	for (double g : gaps)
		m += g;
	m /= gaps.size();
	for (double g : gaps)
		v += (g - m) * (g - m);
	const double cv = std::sqrt(v / gaps.size()) / m;
	std::printf("  crackle: %.0f ticks in 60 s, gap CV %.2f\n", n, cv);
	CHECK(cv >= 0.7, "crackle's tick gaps are too even (CV %.2f, want >= 0.7)", cv);
}

void crackle_level_flat_across_depth() {
	for (float d : DEPTHS) {
		Ticks t = renderCrackle(d, 60.f);
		const double g = db(std::sqrt(0.5 * (rms(t.l) * rms(t.l) + rms(t.r) * rms(t.r))));
		std::printf("  crackle depth %.2f: long-term level %+.2f dB re 1.0\n", d, g);
		CHECK(std::fabs(g) <= 1.5, "crackle's long-term level at depth %.2f is %+.2f dB (want within 1.5)", d, g);
	}
}

void crackle_peaks_bounded() {
	for (float d : DEPTHS) {
		for (float fs : {48000.f, 768000.f}) {
			Ticks t = renderCrackle(d, fs > 48000.f ? 10.f : 60.f, 25.f, 3, fs);
			float pk = 0;
			for (size_t i = 0; i < t.l.size(); i++)
				pk = std::max(pk, std::max(std::fabs(t.l[i]), std::fabs(t.r[i])));
			CHECK(pk <= rka::hadal::Crackle::PEAK, "crackle peaks at %.2f (cap %.2f) at depth %.2f, %.0f Hz", pk,
			      rka::hadal::Crackle::PEAK, d, fs);
		}
	}
}

void crackle_darker_and_longer_with_depth() {
	double prevC = 1e9, prevLen = 0;
	for (float d : {0.f, 0.5f, 1.f}) {
		Ticks t = renderCrackle(d, 30.f, 4.f);
		const int nfft = nfftFor(FS, 10.0);
		const double c = centroid(psd(t.l, nfft), nfft, FS);
		// Mean tick length to -40 dB of its own peak.
		double len = 0;
		int counted = 0;
		for (size_t k = 0; k + 1 < t.at.size(); k++) {
			int last = t.at[k];
			for (int i = t.at[k]; i < t.at[k + 1]; i++)
				if (std::max(std::fabs(t.l[i]), std::fabs(t.r[i])) > 0.01f * t.peak[k])
					last = i;
			len += (last - t.at[k]) / FS;
			counted++;
		}
		len /= counted > 0 ? counted : 1;
		std::printf("  crackle depth %.1f: centroid %.0f Hz, tick length %.2f ms\n", d, c, 1e3 * len);
		CHECK(c < prevC, "crackle must darken with depth (%.0f Hz after %.0f)", c, prevC);
		CHECK(len > prevLen, "crackle ticks must lengthen with depth (%.2f ms after %.2f)", 1e3 * len, 1e3 * prevLen);
		prevC = c;
		prevLen = len;
	}
}

void crackle_sizes_skewed() {
	Ticks t = renderCrackle(0.f, 60.f);
	std::vector<float> p = t.peak;
	std::sort(p.begin(), p.end());
	const float med = p[p.size() / 2], p95 = p[(size_t) (p.size() * 0.95)];
	CHECK(med < 0.4f * p95, "crackle tick sizes are not skewed: median %.3f vs 95th percentile %.3f", med, p95);
}

void crackle_spread() {
	Ticks t = renderCrackle(0.5f, 60.f);
	int left = 0, right = 0;
	for (float p : t.pan) {
		left += p < 1.f / 3.f;
		right += p > 2.f / 3.f;
	}
	const double n = t.pan.size();
	CHECK(left >= 0.25 * n && right >= 0.25 * n, "crackle pans: %d left, %d right of %.0f (want >= 25 %% each)", left,
	      right, n);
}

void crackle_no_rhythm() {
	Ticks t = renderCrackle(0.5f, 60.f);
	// The tick train at 1 kHz: 1 where a tick falls.
	std::vector<float> train((size_t) (60.f * 1000.f), 0.f);
	for (int at : t.at)
		train[(size_t) (at / (FS / 1000.f))] = 1.f;
	const double a = maxAutocorr(train, 50, 10000);
	CHECK(a < 0.1, "crackle's tick train autocorrelates at %.3f between 50 ms and 10 s (want < 0.1)", a);
}

} // namespace

int main() {
	rumble_centroid_falls_with_depth();
	hiss_centroid_falls_with_depth();
	layer_level_flat_across_depth();
	rumble_little_below_15hz();
	no_dc();
	rumble_correlated_hiss_not();
	no_pitch();
	no_loop();
	same_seed_repeats_other_differs();
	stable_at_all_rates();
	crackle_rate();
	crackle_level_flat_across_depth();
	crackle_peaks_bounded();
	crackle_darker_and_longer_with_depth();
	crackle_sizes_skewed();
	crackle_spread();
	crackle_no_rhythm();
	return rkatest::finish("hadal test_layers");
}
