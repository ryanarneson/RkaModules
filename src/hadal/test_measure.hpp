#pragma once
// Measurements for Hadal's offline tests: an FFT, a Welch power spectrum, the
// spectral centroid, band shares, the largest peak over a smoothed spectrum
// (pitch), and the normalised autocorrelation (loops and rhythm). C++17; never
// included by plugin code. Independent of the engine: plain double maths.
#include <cmath>
#include <complex>
#include <vector>

namespace hadaltest {

typedef std::complex<double> cd;

// In-place iterative radix-2 FFT; n must be a power of two.
inline void fft(std::vector<cd>& a) {
	const size_t n = a.size();
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const double ang = -2.0 * M_PI / (double) len;
		const cd wl(std::cos(ang), std::sin(ang));
		for (size_t i = 0; i < n; i += len) {
			cd w(1.0, 0.0);
			for (size_t j = 0; j < len / 2; j++) {
				const cd u = a[i + j], v = a[i + j + len / 2] * w;
				a[i + j] = u + v;
				a[i + j + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

// Welch power spectrum: Hann frames of nfft, half overlap; one value per bin
// 0..nfft/2 (power density, arbitrary scale).
inline std::vector<double> psd(const std::vector<float>& x, int nfft) {
	std::vector<double> p(nfft / 2 + 1, 0.0);
	std::vector<double> win(nfft);
	for (int i = 0; i < nfft; i++)
		win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / nfft);
	int frames = 0;
	std::vector<cd> buf(nfft);
	for (size_t at = 0; at + nfft <= x.size(); at += nfft / 2) {
		for (int i = 0; i < nfft; i++)
			buf[i] = cd(x[at + i] * win[i], 0.0);
		fft(buf);
		for (int k = 0; k <= nfft / 2; k++)
			p[k] += std::norm(buf[k]);
		frames++;
	}
	for (double& v : p)
		v /= frames > 0 ? frames : 1;
	return p;
}

inline double binHz(int k, int nfft, double fs) {
	return k * fs / nfft;
}

// Power-weighted mean frequency above fMin (skips DC).
inline double centroid(const std::vector<double>& p, int nfft, double fs, double fMin = 1.0) {
	double num = 0, den = 0;
	for (int k = 1; k < (int) p.size(); k++) {
		const double f = binHz(k, nfft, fs);
		if (f < fMin)
			continue;
		num += f * p[k];
		den += p[k];
	}
	return den > 0 ? num / den : 0.0;
}

// Share of the power between f0 and f1 (of all power above DC).
inline double bandShare(const std::vector<double>& p, int nfft, double fs, double f0, double f1) {
	double in = 0, all = 0;
	for (int k = 1; k < (int) p.size(); k++) {
		const double f = binHz(k, nfft, fs);
		all += p[k];
		if (f >= f0 && f < f1)
			in += p[k];
	}
	return all > 0 ? in / all : 0.0;
}

// The largest ratio, in dB, of a bin over the mean of the bins within +-1/3
// octave of it (itself excluded), for bins between fLo and fHi.
inline double maxPeakDb(const std::vector<double>& p, int nfft, double fs, double fLo, double fHi) {
	double worst = -1e9;
	for (int k = 1; k < (int) p.size(); k++) {
		const double f = binHz(k, nfft, fs);
		if (f < fLo || f > fHi)
			continue;
		const int k0 = (int) std::floor(k / std::pow(2.0, 1.0 / 3.0)), k1 = (int) std::ceil(k * std::pow(2.0, 1.0 / 3.0));
		double s = 0;
		int n = 0;
		for (int j = std::max(1, k0); j <= std::min((int) p.size() - 1, k1); j++) {
			if (j == k)
				continue;
			s += p[j];
			n++;
		}
		if (n < 4 || s <= 0)
			continue;
		worst = std::max(worst, 10.0 * std::log10(p[k] / (s / n)));
	}
	return worst;
}

// The largest |normalised autocorrelation| for lags in [lagMin, lagMax] samples.
inline double maxAutocorr(const std::vector<float>& x, int lagMin, int lagMax) {
	size_t n = 1;
	while (n < 2 * x.size())
		n <<= 1;
	double mean = 0;
	for (float v : x)
		mean += v;
	mean /= x.size();
	std::vector<cd> a(n, cd(0, 0));
	for (size_t i = 0; i < x.size(); i++)
		a[i] = cd(x[i] - mean, 0);
	fft(a);
	for (cd& v : a)
		v = cd(std::norm(v), 0);
	// Inverse FFT by conjugation.
	for (cd& v : a)
		v = std::conj(v);
	fft(a);
	const double r0 = a[0].real();
	double worst = 0;
	for (int lag = lagMin; lag <= lagMax && lag < (int) x.size(); lag++) {
		// Unbiased: scale by the overlap length.
		const double r = a[lag].real() / r0 * (double) x.size() / (double) (x.size() - lag);
		worst = std::max(worst, std::fabs(r));
	}
	return worst;
}

inline double rms(const std::vector<float>& x) {
	double s = 0;
	for (float v : x)
		s += (double) v * v;
	return std::sqrt(s / (x.empty() ? 1 : x.size()));
}

inline double mean(const std::vector<float>& x) {
	double s = 0;
	for (float v : x)
		s += v;
	return s / (x.empty() ? 1 : x.size());
}

inline double corr(const std::vector<float>& a, const std::vector<float>& b) {
	double ma = mean(a), mb = mean(b), sab = 0, saa = 0, sbb = 0;
	for (size_t i = 0; i < a.size(); i++) {
		sab += (a[i] - ma) * (b[i] - mb);
		saa += (a[i] - ma) * (a[i] - ma);
		sbb += (b[i] - mb) * (b[i] - mb);
	}
	return sab / std::sqrt(saa * sbb);
}

inline double db(double x) {
	return 20.0 * std::log10(x);
}

} // namespace hadaltest
