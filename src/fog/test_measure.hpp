#pragma once
// Test-only measurement helpers for Fog: decay time, RMS, correlation and
// spectral spread. They know nothing of the engine: double precision, libm,
// textbook methods. Never included by plugin code.
#include <cmath>
#include <cstddef>
#include <vector>

namespace fogtest {

const double PI = 3.14159265358979323846;

enum BiquadKind { LOW_PASS, HIGH_PASS, BAND_PASS };

// An RBJ cookbook biquad (band-pass: constant 0 dB peak gain) at hz.
template <typename T>
std::vector<double> biquad(const std::vector<T>& x, double rate, BiquadKind kind, double hz, double q) {
	const double w = 2.0 * PI * hz / rate;
	const double alpha = std::sin(w) / (2.0 * q), cw = std::cos(w);
	const double a0 = 1.0 + alpha;
	double b0, b1, b2;
	if (kind == LOW_PASS) {
		b0 = b2 = (1.0 - cw) / 2.0;
		b1 = 1.0 - cw;
	}
	else if (kind == HIGH_PASS) {
		b0 = b2 = (1.0 + cw) / 2.0;
		b1 = -(1.0 + cw);
	}
	else {
		b0 = alpha;
		b1 = 0.0;
		b2 = -alpha;
	}
	b0 /= a0;
	b1 /= a0;
	b2 /= a0;
	const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
	std::vector<double> y(x.size());
	double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	for (size_t n = 0; n < x.size(); n++) {
		const double xn = x[n];
		const double yn = b0 * xn + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1;
		x1 = xn;
		y2 = y1;
		y1 = yn;
		y[n] = yn;
	}
	return y;
}

// The analysis band: two identical 2nd-order RBJ band-pass sections at
// bandHz, Q = 4 (a band about a third of an octave wide, 12 dB/octave
// skirts), but never narrower than 250 Hz (Q = bandHz / 250 below 1 kHz):
// a narrow low band holds too few modes for a short decay to be measured
// steadily (at 250 Hz and Q = 4, a 0.5 s decay of noise read 0.90-1.21x
// its true T60 over 12 seeds; at Q = 1, 0.95-1.10x). One 2nd-order section is not enough when the highs decay much
// faster than the lows (Damp): its 6 dB/octave skirts let the slow lows
// below the band carry the Schroeder integral, and the fit reads their
// decay, not the band's. test_network's measure self-check shows the
// cascade reading a fast band beside slow lows.
//
// It filters the response time-reversed (and reverses the result back), so
// the band filter's own ringing lands before the onset instead of on the
// decay, which matters for the short T60s in a narrow low band (ISO 3382-2
// suggests the same for short decays).
inline std::vector<double> bandPass(const std::vector<float>& x, double rate, double bandHz) {
	const std::vector<float> rev(x.rbegin(), x.rend());
	const double q = bandHz / 250.0 < 4.0 ? bandHz / 250.0 : 4.0;
	std::vector<double> y = biquad(biquad(rev, rate, BAND_PASS, bandHz, q), rate, BAND_PASS, bandHz, q);
	return std::vector<double>(y.rbegin(), y.rend());
}

// Least-squares slope (dB per second) of the Schroeder curve sch (in dB)
// between its first crossings of hiDb and loDb. Returns 0 if loDb is not
// reached.
inline double fitSlope(const std::vector<double>& schDb, double rate, double hiDb, double loDb) {
	size_t a = 0;
	while (a < schDb.size() && schDb[a] > hiDb)
		a++;
	size_t b = a;
	while (b < schDb.size() && schDb[b] > loDb)
		b++;
	if (b >= schDb.size() || b <= a + 1)
		return 0.0;
	double st = 0, sy = 0, stt = 0, sty = 0;
	const double n = (double) (b - a);
	for (size_t i = a; i < b; i++) {
		const double t = (double) i / rate;
		st += t;
		sy += schDb[i];
		stt += t * t;
		sty += t * schDb[i];
	}
	return (n * sty - st * sy) / (n * stt - st * st);
}

// Schroeder backward integral, in dB re its start, of the energy e plus a
// tail energy beyond the end.
inline std::vector<double> schroederDb(const std::vector<double>& e, double tail) {
	std::vector<double> s(e.size());
	double acc = tail;
	for (size_t i = e.size(); i-- > 0;) {
		acc += e[i];
		s[i] = acc;
	}
	const double ref = s.empty() ? 1.0 : s[0];
	for (size_t i = 0; i < s.size(); i++)
		s[i] = 10.0 * std::log10(s[i] / ref + 1e-300);
	return s;
}

// T60 (seconds) of an impulse response in the band at bandHz: Schroeder
// backward integration of the band-passed energy (bandPass above), a
// straight-line fit from -5 to -35 dB, doubled (T30 x 2). A response that stops before it has fully
// decayed loses the energy beyond its end from the integral, which bends the
// curve down; that missing tail is estimated from the energy in the last
// half second and the decay rate, and added back (iterated a few times, as
// in Lundeby's method). Returns -1 if the fit cannot be made.
inline double measureT60(const std::vector<float>& ir, float rate, float bandHz) {
	const std::vector<double> y = bandPass(ir, rate, bandHz);
	std::vector<double> e(y.size());
	for (size_t i = 0; i < y.size(); i++)
		e[i] = y[i] * y[i];
	const size_t win = (size_t) (0.5 * rate);
	if (e.size() < 4 * win)
		return -1.0;
	double endE = 0;
	for (size_t i = e.size() - win; i < e.size(); i++)
		endE += e[i];
	endE /= (double) win; // mean energy per sample, centred win/2 before the end
	double t60 = 0;
	double slope = fitSlope(schroederDb(e, 0.0), rate, -5.0, -25.0);
	if (slope >= 0)
		return -1.0;
	t60 = -60.0 / slope;
	for (int it = 0; it < 8; it++) {
		const double k = 6.0 * std::log(10.0) / t60; // energy decay, per second
		const double tail = endE * std::exp(-k * 0.5 * win / rate) / (1.0 - std::exp(-k / rate));
		slope = fitSlope(schroederDb(e, tail), rate, -5.0, -35.0);
		if (slope >= 0)
			return -1.0;
		t60 = -60.0 / slope;
	}
	return t60;
}

inline double rms(const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1) {
	if (to > x.size())
		to = x.size();
	double s = 0;
	for (size_t i = from; i < to; i++)
		s += (double) x[i] * x[i];
	return to > from ? std::sqrt(s / (double) (to - from)) : 0.0;
}

// Pearson correlation of a and b over their common length.
inline double correlation(const std::vector<float>& a, const std::vector<float>& b) {
	const size_t n = a.size() < b.size() ? a.size() : b.size();
	double ma = 0, mb = 0;
	for (size_t i = 0; i < n; i++) {
		ma += a[i];
		mb += b[i];
	}
	ma /= (double) n;
	mb /= (double) n;
	double sab = 0, saa = 0, sbb = 0;
	for (size_t i = 0; i < n; i++) {
		const double da = a[i] - ma, db = b[i] - mb;
		sab += da * db;
		saa += da * da;
		sbb += db * db;
	}
	return sab / std::sqrt(saa * sbb + 1e-300);
}

// RMS spectral width (Hz) of x about centreHz: a Hann-windowed DFT at stepHz
// steps over centreHz +/- spanHz, and the square root of the power-weighted
// mean squared distance from centreHz.
inline double spectralSpread(const std::vector<float>& x, double rate, double centreHz, double spanHz, double stepHz) {
	const size_t n = x.size();
	std::vector<double> w(n);
	for (size_t i = 0; i < n; i++)
		w[i] = x[i] * (0.5 - 0.5 * std::cos(2.0 * PI * (double) i / (double) n));
	double sp = 0, s2 = 0;
	for (double f = centreHz - spanHz; f <= centreHz + spanHz + 1e-9; f += stepHz) {
		const double dw = 2.0 * PI * f / rate;
		const double c = std::cos(dw), s = std::sin(dw);
		double re = 0, im = 0, pr = 1, pi = 0;
		for (size_t i = 0; i < n; i++) {
			re += w[i] * pr;
			im -= w[i] * pi;
			const double t = pr * c - pi * s;
			pi = pr * s + pi * c;
			pr = t;
		}
		const double p = re * re + im * im;
		const double d = f - centreHz;
		sp += p;
		s2 += p * d * d;
	}
	return std::sqrt(s2 / (sp + 1e-300));
}

} // namespace fogtest
