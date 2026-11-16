// Hadal's layers, offline: Rumble and Hiss (Task 2), Crackle (Task 3) and the
// Drift wander (Task 4). Spec: docs/specs/2026-11-16-hadal-design.md.
#include "testing.hpp"
#include "test_measure.hpp"
#include "Random.hpp"
#include "NoiseLayer.hpp"

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
	return rkatest::finish("hadal test_layers");
}
