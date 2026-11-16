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
	delayPowerOfTwo();
	diffuserEnergy();
	diffuserDecorrelates();
	rateFactor();
	return rkatest::finish("fog test_blocks");
}
