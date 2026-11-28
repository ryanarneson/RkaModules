// Fog building blocks: DelayLine, Diffuser, RateDivider. Expected values are
// worked out here with libm (analytic sines, RMS), never with the blocks.
// Built with the plugin's floating-point flags (tests.mk). Run with `make test`.
#include "DelayLine.hpp"
#include "Diffuser.hpp"
#include "RateDivider.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>

namespace {

const double PI = 3.14159265358979323846;

void delayIntegerExact() {
	const int ds[] = {1, 2, 7, 100, 999};
	for (int d : ds) {
		rka::DelayLine dl;
		dl.allocate(1000);
		dl.write(1.f);
		for (int i = 0; i < d; i++)
			dl.write(0.f);
		CHECK(dl.readInt(d) == 1.f, "d=%d got %g", d, dl.readInt(d));
		CHECK(dl.readInt(d - 1) == 0.f, "d=%d neighbour", d);
		CHECK(dl.readInt(d + 1) == 0.f, "d=%d neighbour+", d);
	}
}

void delayFractionalSine() {
	const double fs = 48000.0, f = 1000.0;
	rka::DelayLine dl;
	dl.allocate(500);
	const int ds[] = {5, 20, 100, 300};
	for (int d : ds) {
		dl.clear();
		double worst = 0.0;
		const double dd = d + 0.37;
		for (int n = 0; n < 2000; n++) {
			dl.write((float) std::sin(2 * PI * f * n / fs));
			if (n > 400) {
				const double want = std::sin(2 * PI * f * (n - dd) / fs);
				worst = std::fmax(worst, std::fabs(dl.read((float) dd) - want));
			}
		}
		CHECK(worst < 1e-3, "d=%d worst error %g", d, worst);
	}
}

void delayClamps() {
	rka::DelayLine dl;
	dl.allocate(64);
	for (int n = 0; n < 500; n++)
		dl.write((float) std::sin(0.1 * n));
	const float lo = dl.read(-5.f), hi = dl.read(1e9f);
	CHECK(std::isfinite(lo) && std::isfinite(hi), "lo %g hi %g", lo, hi);
	CHECK(lo == dl.read(1.f), "low clamp to 1");
	CHECK(hi == dl.read((float) (dl.maxDelay() - 2)), "high clamp");
	CHECK(std::isfinite(dl.read(NAN)), "nan");
}

// The allpass read's gain is 1 at every frequency and fraction: a sine's RMS
// out is its RMS in within 0.01 dB, from 1 kHz to 20 kHz at 48 kHz, at ten
// fractions of a sample. The Hermite read beside it, the same way, is what
// the allpass is for: it must lose more than 1 dB at 16 kHz halfway between
// samples (so this test can tell the two apart).
void delayAllpassFlat() {
	const double fs = 48000.0;
	rka::DelayLine dl;
	dl.allocate(500);
	double worst = 0.0, hermiteLoss = 0.0;
	for (double f : {1000.0, 8000.0, 12000.0, 16000.0, 20000.0})
		for (int k = 0; k < 10; k++) {
			const float dd = 100.f + 0.1f * (float) k;
			dl.clear();
			float z = 0.f;
			double in = 0, out = 0, herm = 0;
			for (int n = 0; n < 9600; n++) {
				const float x = (float) std::sin(2 * PI * f * n / fs);
				dl.write(x);
				const float y = dl.readAllpass(dd, z);
				if (n >= 4800) { // whole cycles of every f
					in += (double) x * x;
					out += (double) y * y;
					const double h = dl.read(dd);
					herm += h * h;
				}
			}
			const double db = 10.0 * std::log10(out / in);
			worst = std::fmax(worst, std::fabs(db));
			CHECK(std::fabs(db) < 0.01, "allpass read at %g Hz, delay %g: %g dB", f, (double) dd, db);
			if (f == 16000.0 && k == 5)
				hermiteLoss = 10.0 * std::log10(herm / in);
		}
	CHECK(hermiteLoss < -1.0, "the Hermite read at 16 kHz, halfway: %g dB, expected a loss over 1 dB", hermiteLoss);
	std::printf("  delay_allpass_flat: worst %.4f dB (the Hermite read at 16 kHz, halfway: %.2f dB)\n", worst, hermiteLoss);
}

// The allpass read delays by what it is asked: a 1 kHz sine read at d + 0.37
// is the sine d + 0.37 samples back, as the Hermite read's is; and so it is
// while the delay moves slowly across whole samples (0.02 samples a sample,
// about Drift's fastest), and from the first sample after taking over from
// a Hermite read that handed it its state.
void delayAllpassDelay() {
	const double fs = 48000.0, f = 1000.0;
	rka::DelayLine dl;
	dl.allocate(500);
	for (int d : {5, 20, 100, 300}) {
		dl.clear();
		float z = 0.f;
		double worst = 0.0;
		const double dd = d + 0.37;
		for (int n = 0; n < 2000; n++) {
			dl.write((float) std::sin(2 * PI * f * n / fs));
			const float y = dl.readAllpass((float) dd, z);
			if (n > 400)
				worst = std::fmax(worst, std::fabs(y - std::sin(2 * PI * f * (n - dd) / fs)));
		}
		CHECK(worst < 1e-3, "allpass d=%d worst error %g", d, worst);
	}
	// Moving: 100 -> 140 samples and back, across 40 whole samples each way.
	dl.clear();
	float z = 0.f;
	double worst = 0.0;
	for (int n = 0; n < 6000; n++) {
		dl.write((float) std::sin(2 * PI * f * n / fs));
		const int m = n < 1000 ? 0 : n < 3000 ? n - 1000 : n < 5000 ? 5000 - n : 0;
		const double dd = 100.0 + 0.02 * m;
		const float y = dl.readAllpass((float) dd, z);
		if (n > 400)
			worst = std::fmax(worst, std::fabs(y - std::sin(2 * PI * f * (n - dd) / fs)));
	}
	CHECK(worst < 3e-3, "allpass read moving 0.02 samples a sample: worst error %g", worst);
	std::printf("  delay_allpass_delay: moving read, worst error %.5f\n", worst);
	// Taking over from the Hermite read.
	dl.clear();
	z = 0.f;
	worst = 0.0;
	const double dd = 77.63;
	for (int n = 0; n < 2000; n++) {
		dl.write((float) std::sin(2 * PI * f * n / fs));
		float y;
		if (n < 1000)
			z = y = dl.read((float) dd);
		else
			y = dl.readAllpass((float) dd, z);
		if (n >= 1000)
			worst = std::fmax(worst, std::fabs(y - std::sin(2 * PI * f * (n - dd) / fs)));
	}
	CHECK(worst < 1e-3, "allpass read taking over from Hermite: worst error %g", worst);
}

void delayAllpassClamps() {
	rka::DelayLine dl;
	dl.allocate(64);
	for (int n = 0; n < 500; n++)
		dl.write((float) std::sin(0.1 * n));
	float a = 0.f, b = 0.f, c = 0.f, d = 0.f, e = 0.f;
	const float lo = dl.readAllpass(-5.f, a), hi = dl.readAllpass(1e9f, b);
	CHECK(std::isfinite(lo) && std::isfinite(hi), "lo %g hi %g", lo, hi);
	CHECK(lo == dl.readAllpass(1.f, c), "low clamp to 1");
	CHECK(lo == dl.readInt(1), "a delay of 1 is the sample one back, exactly");
	CHECK(hi == dl.readAllpass((float) (dl.maxDelay() - 2), d), "high clamp");
	CHECK(std::isfinite(dl.readAllpass(NAN, e)) && std::isfinite(e), "nan");
	rka::DelayLine none;
	float z = 3.f;
	CHECK(none.readAllpass(10.f, z) == 0.f, "an unallocated line reads 0");
}

void delayPowerOfTwo() {
	rka::DelayLine dl;
	dl.allocate(1000);
	CHECK(dl.maxDelay() >= 1000, "maxDelay %d", dl.maxDelay());
	CHECK(((dl.maxDelay() + 1) & dl.maxDelay()) == 0, "buffer size not a power of two");
}

float rngNoise(uint32_t& s) {
	// Uniform noise with unit variance.
	s = s * 1664525u + 1013904223u;
	return ((float) (s >> 8) * (1.f / 16777216.f) - 0.5f) * 3.4641016f;
}

void diffuserEnergy() {
	rka::Diffuser<8> d;
	d.allocate(48000.f, 2.f);
	const float ms[] = {7.f, 13.f, 23.f, 37.f};
	d.configure(48000.f, ms, 4, 1.f, 12345);
	uint32_t s = 1;
	double ein = 0, eout = 0;
	for (int n = 0; n < 60000; n++) {
		float x[8];
		for (int i = 0; i < 8; i++)
			x[i] = rngNoise(s);
		double e = 0;
		for (int i = 0; i < 8; i++)
			e += (double) x[i] * x[i];
		d.process(x);
		double o = 0;
		for (int i = 0; i < 8; i++)
			o += (double) x[i] * x[i];
		if (n >= 20000) {
			ein += e;
			eout += o;
		}
	}
	const double rms = std::sqrt(eout / ein);
	CHECK(std::fabs(rms - 1.0) < 0.01, "rms ratio %g", rms);
}

void diffuserDecorrelates() {
	rka::Diffuser<8> d;
	d.allocate(48000.f, 2.f);
	const float ms[] = {7.f, 13.f, 23.f, 37.f};
	d.configure(48000.f, ms, 4, 1.f, 777);
	double e[8] = {}, tot = 0;
	for (int n = 0; n < 48000; n++) {
		float x[8] = {};
		if (n == 0)
			x[0] = 1.f;
		d.process(x);
		for (int i = 0; i < 8; i++) {
			e[i] += (double) x[i] * x[i];
			tot += (double) x[i] * x[i];
		}
	}
	CHECK(std::fabs(tot - 1.0) < 0.01, "total energy %g", tot);
	for (int i = 0; i < 8; i++)
		CHECK(e[i] / tot > 0.05 && e[i] / tot < 0.25, "ch %d share %g", i, e[i] / tot);
}

// A tone through the divider with an identity inner process: how many times
// the inner process ran in `n` host samples, and the passband gain in dB.
struct DividerRun {
	long calls;
	double passDb;
};

DividerRun runDivider(double fs, double toneHz, int n) {
	rka::RateDivider r;
	r.setHostRate((float) fs);
	long calls = 0;
	double ein = 0, eout = 0;
	for (int i = 0; i < n; i++) {
		const float x = (float) std::sin(2 * PI * toneHz * i / fs);
		float yl, yr;
		r.process(x, x, yl, yr, [&calls](float l, float rr, float& ol, float& orr) {
			calls++;
			ol = l;
			orr = rr;
		});
		if (i >= n / 10) {
			ein += (double) x * x;
			eout += (double) yl * yl;
		}
	}
	return {calls, 10.0 * std::log10(eout / ein)};
}

void rateFactor() {
	const float rates[] = {44100.f, 48000.f, 96000.f, 192000.f, 384000.f, 768000.f};
	const int want[] = {1, 1, 1, 2, 4, 8};
	for (int i = 0; i < 6; i++) {
		rka::RateDivider r;
		r.setHostRate(rates[i]);
		CHECK(r.factor() == want[i], "rate %g factor %d", rates[i], r.factor());
		CHECK(r.innerRate() <= 96000.f, "inner %g", r.innerRate());
		// The inner process runs once per `factor` host samples.
		const int n = 48000;
		const DividerRun d = runDivider(rates[i], 5000.0, n);
		CHECK(d.calls == n / want[i], "rate %g: %ld inner calls in %d samples, want %d", rates[i], d.calls, n, n / want[i]);
		// A 5 kHz tone passes flat at every rate.
		CHECK(std::fabs(d.passDb) < 0.1, "rate %g: passband %g dB", rates[i], d.passDb);
	}
}

} // namespace

int main() {
	delayIntegerExact();
	delayFractionalSine();
	delayClamps();
	delayAllpassFlat();
	delayAllpassDelay();
	delayAllpassClamps();
	delayPowerOfTwo();
	diffuserEnergy();
	diffuserDecorrelates();
	rateFactor();
	return rkatest::finish("fog test_blocks");
}
