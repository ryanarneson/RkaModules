#pragma once
// Offline sample-rate conversion for Eddy's tapes: a windowed sinc (Kaiser),
// 32 zero crossings a side, its cutoff lowered with the rate when it goes down.
// Not for the audio thread: it allocates its table and costs about 128
// multiply-adds a frame. C++11, no Rack dependency.
#include <cmath>
#include <cstdint>
#include <vector>

namespace rka {

namespace resampledetail {
static const int kZeroCrossings = 32;
static const int kStepsPerCrossing = 512;   // table resolution, linearly interpolated
static const double kBeta = 9.0;            // Kaiser: about -90 dB stopband

inline double besselI0(double x) {
	double sum = 1., term = 1.;
	const double q = x * x * 0.25;
	for (int k = 1; k < 200; k++) {
		term *= q / ((double) k * k);
		sum += term;
		if (term < 1e-14 * sum)
			break;
	}
	return sum;
}
}

// outFrames = floor(inFrames * ratio). (The nudge keeps 44100 * (48000 / 44100.)
// from landing on 47999.99999.)
inline uint64_t resampledFrames(uint64_t inFrames, double ratio) {
	if (!(ratio > 0.))
		return 0;
	return (uint64_t) std::floor((double) inFrames * ratio + 1e-6);
}

// Converts `in` (inFrames) to outFrames frames of `out`, ratio = out rate / in
// rate; the input is taken as silent outside its ends. Writes exactly outFrames.
inline void resample(const float* in, uint64_t inFrames, float* out, uint64_t outFrames, double ratio) {
	using namespace resampledetail;
	if (!(ratio > 0.)) {
		for (uint64_t j = 0; j < outFrames; j++)
			out[j] = 0.f;
		return;
	}
	const double fc = ratio < 1. ? ratio : 1.;   // cutoff, as a share of the input's Nyquist
	const int Z = kZeroCrossings, K = kStepsPerCrossing;
	// The kernel in units of zero crossings, x = 0..Z, one step spare for the interpolation.
	std::vector<double> tab((size_t) Z * K + 2, 0.);
	const double i0b = besselI0(kBeta);
	const double pi = 3.14159265358979323846;
	for (size_t i = 0; i < tab.size(); i++) {
		double x = (double) i / K;
		double t = x / Z;
		double w = t < 1. ? besselI0(kBeta * std::sqrt(1. - t * t)) / i0b : 0.;
		double s = x == 0. ? 1. : std::sin(pi * x) / (pi * x);
		tab[i] = s * w;
	}
	const double reach = Z / fc;   // input samples either side
	for (uint64_t j = 0; j < outFrames; j++) {
		const double pos = (double) j / ratio;
		int64_t lo = (int64_t) std::ceil(pos - reach), hi = (int64_t) std::floor(pos + reach);
		if (lo < 0)
			lo = 0;
		if (hi > (int64_t) inFrames - 1)
			hi = (int64_t) inFrames - 1;
		double acc = 0.;
		for (int64_t n = lo; n <= hi; n++) {
			double u = std::fabs((double) n - pos) * fc * K;
			size_t k = (size_t) u;
			double f = u - (double) k;
			if (k >= (size_t) Z * K) {
				k = (size_t) Z * K;
				f = 0.;
			}
			acc += (double) in[n] * (tab[k] + f * (tab[k + 1] - tab[k]));
		}
		out[j] = (float) (acc * fc);
	}
}

}
