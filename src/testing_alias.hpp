#pragma once
// Measuring aliasing in the offline tests (docs/DSP-ALIASING.md, TESTING.md
// rule 16). C++17, libm allowed; never included by plugin code.
//
// AliasMeter takes a rendered signal, its sample rate and the frequencies
// that are supposed to be in it (a tone, its harmonics below Nyquist, the
// partials a test put there), and tells how strong the strongest component
// is that is NOT one of them: an alias, an image, or interpolation error.
//
// Method. The first N samples (N the largest power of two that fits, at most
// MAX_N) are windowed with a Kaiser window, beta 20 (main lobe +-6.4 bins,
// sidelobes far under the floor below), and transformed (radix-2 FFT, double).
// A component's amplitude is read from the power in its main lobe, +-LOBE
// bins around a bin, so it does not depend on where the frequency falls
// between bins (no scalloping): for a sine of amplitude A,
//   sum over the lobe of |X_k|^2 = N sum(w^2) A^2 / 4.
// Expected frequencies own every bin within +-GUARD of their own; the bins
// below GUARD (DC and the lowest few hertz) are not looked at either, since
// the DC blocker and slow drifts are not aliasing. Everything else is "other".
//
// Resolution: fs / N a bin (1.46 Hz at 48 kHz with N = 32768); an expected
// frequency must be known to within a few bins (it owns +-GUARD = +-20
// bins, about +-29 Hz at that N). Floor: a clean float sine reads below
// -120 dB (test_alias_meter checks it, and that a planted alias at a known
// level reads within 1 dB of it).
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace rkatest {

class AliasMeter {
public:
	static constexpr size_t MAX_N = 65536;
	static constexpr int LOBE = 8;    // bins either side summed for a component's power
	static constexpr int GUARD = 20;  // bins either side an expected frequency owns
	static constexpr double BETA = 20.0;

	// x: n samples at fs. Uses the first N (a power of two, at most MAX_N).
	AliasMeter(const float* x, size_t n, double fs) : fs_(fs) {
		size_t N = 1;
		while (N * 2 <= n && N * 2 <= MAX_N)
			N *= 2;
		N_ = N;
		std::vector<std::complex<double>> X(N);
		double sw2 = 0.0;
		const double i0b = besselI0(BETA);
		for (size_t i = 0; i < N; i++) {
			const double t = 2.0 * (double) i / (double) (N - 1) - 1.0;
			const double w = besselI0(BETA * std::sqrt(std::max(0.0, 1.0 - t * t))) / i0b;
			sw2 += w * w;
			X[i] = std::complex<double>(w * (double) x[i], 0.0);
		}
		fft(X);
		const size_t half = N / 2;
		power_.assign(half + 1, 0.0);
		for (size_t k = 0; k <= half; k++)
			power_[k] = std::norm(X[k]);
		norm_ = (double) N * sw2 / 4.0;
	}

	size_t size() const { return N_; }
	double binHz() const { return fs_ / (double) N_; }

	// The amplitude (volts, peak) of the component at hz: the strongest lobe
	// within +-3 bins of it.
	double amplitude(double hz) const {
		const long k0 = bin(hz);
		double best = 0.0;
		for (long k = k0 - 3; k <= k0 + 3; k++)
			best = std::max(best, lobeAmp(k));
		return best;
	}

	struct Other {
		double amp = 0.0;   // volts, peak
		double hz = 0.0;
	};
	// The strongest component not within GUARD bins of any expected
	// frequency (nor of DC). guardHz widens what each expected frequency
	// owns, for a test whose tones carry a known skirt (the sidebands of a
	// slow level change: Eddy's wear dropouts).
	Other strongestOther(const std::vector<double>& expected, double guardHz = 0.0) const {
		const long half = (long) N_ / 2;
		const long g = std::max((long) GUARD, (long) std::ceil(guardHz / binHz()) + LOBE);
		std::vector<char> owned((size_t) half + 1, 0);
		for (long k = 0; k < GUARD && k <= half; k++)
			owned[(size_t) k] = 1;
		for (double hz : expected) {
			const long k0 = bin(hz);
			for (long k = k0 - g; k <= k0 + g; k++)
				if (k >= 0 && k <= half)
					owned[(size_t) k] = 1;
		}
		Other o;
		for (long k = 0; k <= half; k++) {
			if (owned[(size_t) k])
				continue;
			const double a = lobeAmp(k);
			if (a > o.amp) {
				o.amp = a;
				o.hz = (double) k;
			}
		}
		// Where it is: the strongest bin of that lobe.
		long at = (long) o.hz;
		for (long k = (long) o.hz - LOBE; k <= (long) o.hz + LOBE; k++)
			if (k >= 0 && k <= half && power_[(size_t) k] > power_[(size_t) at])
				at = k;
		o.hz = (double) at * binHz();
		return o;
	}

	// The strongest other component, in dB below `refAmp` volts (peak): the
	// level of a full tone, or of the strongest expected one.
	double otherDb(const std::vector<double>& expected, double refAmp, double guardHz = 0.0) const {
		return db(strongestOther(expected, guardHz).amp / refAmp);
	}
	// The same, relative to the strongest expected component.
	double aliasDb(const std::vector<double>& expected, double guardHz = 0.0) const {
		double ref = 0.0;
		for (double hz : expected)
			ref = std::max(ref, amplitude(hz));
		return otherDb(expected, ref, guardHz);
	}

	static double db(double ratio) {
		return 20.0 * std::log10(ratio > 1e-30 ? ratio : 1e-30);
	}

	// f0's harmonics (f0, 2 f0, ...) below Nyquist, as they land in a
	// signal at fs: the ones above Nyquist are aliases and not listed.
	static std::vector<double> harmonics(double f0, double fs) {
		std::vector<double> h;
		for (int k = 1; k * f0 < fs / 2.0 && k < 10000; k++)
			h.push_back(k * f0);
		return h;
	}
	// Where a frequency lands when sampled at fs (folded into [0, fs/2]).
	static double folded(double hz, double fs) {
		double f = std::fmod(std::fabs(hz), fs);
		return f > fs / 2.0 ? fs - f : f;
	}

private:
	long bin(double hz) const {
		return std::lround(hz / binHz());
	}
	// The amplitude of a component centred on bin k, from its lobe's power.
	// The lobe is mirrored at DC and Nyquist, as the spectrum is.
	double lobeAmp(long k) const {
		const long half = (long) N_ / 2;
		double p = 0.0;
		for (long j = k - LOBE; j <= k + LOBE; j++) {
			long m = j < 0 ? -j : j;
			if (m > half)
				m = 2 * half - m;
			if (m >= 0 && m <= half)
				p += power_[(size_t) m];
		}
		return std::sqrt(p / norm_);   // p = norm_ A^2, norm_ = N sum(w^2) / 4
	}

	static double besselI0(double x) {
		double sum = 1.0, term = 1.0;
		const double q = x * x * 0.25;
		for (int k = 1; k < 500; k++) {
			term *= q / ((double) k * k);
			sum += term;
			if (term < 1e-17 * sum)
				break;
		}
		return sum;
	}

	static void fft(std::vector<std::complex<double>>& a) {
		const size_t n = a.size();
		for (size_t i = 1, j = 0; i < n; i++) {
			size_t bit = n >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(a[i], a[j]);
		}
		const double pi = 3.14159265358979323846;
		for (size_t len = 2; len <= n; len <<= 1) {
			const double ang = -2.0 * pi / (double) len;
			for (size_t i = 0; i < n; i += len)
				for (size_t j = 0; j < len / 2; j++) {
					const std::complex<double> w(std::cos(ang * (double) j), std::sin(ang * (double) j));
					const std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w;
					a[i + j] = u + v;
					a[i + j + len / 2] = u - v;
				}
		}
	}

	double fs_;
	size_t N_ = 0;
	double norm_ = 1.0;
	std::vector<double> power_;
};

} // namespace rkatest
