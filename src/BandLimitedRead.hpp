#pragma once
// Reading a tape faster than its own rate without aliasing (Eddy; later
// Fetch). Rack-free, C++11, header-only. docs/DSP-ALIASING.md, source (3).
//
// A head moving s frames a sample (s > 1) plays the tape's content s times
// higher: what lies above 1/s of the tape's Nyquist lands above the output's
// Nyquist and folds back as aliases. No interpolator removes it; the content
// has to be low-passed under the new Nyquist as it is read. This read does
// that with a windowed-sinc kernel whose cutoff scales with the speed: it is
// laid over the tape s times wider than at speed 1, so it always cuts at the
// same place in the OUTPUT's spectrum, and has about 2 HALF_WIDTH s taps.
//
// The kernel, in output samples u (u = tape frames / s):
//   h(u) = CUTOFF sinc(CUTOFF u) kaiser(u / HALF_WIDTH, BETA)
// tabulated at STEPS points per output sample, linearly interpolated, and
// normalised by the sum of the taps used, so a constant reads exactly
// itself.
// See the constants for the passband and stopband this gives.
//
// At |speed| <= 1 the read is Tape::read / Tape::readWrapped, to the bit
// (the Hermite read: there is nothing to alias). Between 1 and 1 + BLEND the
// two are crossfaded linearly, so nothing steps as a glide or an audio-rate
// Speed CV crosses 1; from 1 + BLEND the band-limited read alone.
//
// Edges: as the Hermite reads, a tap outside the tape is held to it (taps
// beyond the tape and its tail read the last frame, before 0 the first), and
// a wrapped read wraps its taps round [0, length).
//
// Threads: build it anywhere (the constructor fills the table with libm);
// read() allocates nothing and calls no libm, from the thread that owns the
// tape.
#include <cmath>

#include "Tape.hpp"

namespace rka {

class BandLimitedRead {
public:
	// The kernel. With these the response, as a fraction of the output's
	// Nyquist, is flat within 0.02 dB to 0.8, -6 dB at 0.9 and at least
	// 57 dB down from 1 up (test_band_read measures it): whatever the speed
	// puts above the output's Nyquist folds back at least 57 dB down (67 dB
	// from 1.2 up), and content landing between 0.8 and 1 of Nyquist (17.6
	// to 22 kHz at 44.1 kHz) is rolled off rather than kept. The cost is
	// about 36 s taps a side a voice (72 at speed 2). With the stopband at
	// 1.2 and HALF_WIDTH 9 instead (half the taps), content landing between
	// 1 and 1.2 of Nyquist folds back only 3 to 8 dB down (measured
	// 2026-11-22: busy bench 1.07 % against 1.27 % of a core).
	static constexpr int HALF_WIDTH = 18;       // output samples either side
	static constexpr double CUTOFF = 0.9;       // of the output's Nyquist (-6 dB)
	static constexpr double BETA = 5.65;        // Kaiser: about 60 dB
	static constexpr int STEPS = 128;           // table points per output sample
	static constexpr float BLEND = 0.25f;       // speeds 1 .. 1.25 crossfade
	static constexpr float MAX_SPEED = 4.f;     // wider is read as 4 (Eddy holds it there)

	BandLimitedRead() {
		const double pi = 3.14159265358979323846;
		const double i0b = besselI0(BETA);
		for (int i = 0; i < TABLE; i++) {
			const double u = (double) i / STEPS;   // output samples
			const double t = u / HALF_WIDTH;
			double h = 0.0;
			if (t < 1.0) {
				const double x = CUTOFF * u;
				const double sinc = x == 0.0 ? 1.0 : std::sin(pi * x) / (pi * x);
				h = CUTOFF * sinc * besselI0(BETA * std::sqrt(1.0 - t * t)) / i0b;
			}
			table_[i] = (float) h;
		}
		for (int i = 0; i < TABLE - 1; i++)
			slope_[i] = table_[i + 1] - table_[i];
		slope_[TABLE - 1] = 0.f;
	}

	// Both sides of `tape` at frame p for a head moving `speed` frames a
	// sample (either sign), as Tape::read (wrap false) or Tape::readWrapped.
	void read(const Tape& tape, double p, float speed, bool wrap, float out[2]) const {
		float s = speed < 0.f ? -speed : speed;
		if (!(s > 1.f)) {   // NaN too
			hermite(tape, p, wrap, out);
			return;
		}
		s = s < MAX_SPEED ? s : MAX_SPEED;
		const float a = (s - 1.f) * (1.f / BLEND);
		if (a >= 1.f) {
			band(tape, p, s, wrap, out);
			return;
		}
		float h[2], b[2];
		hermite(tape, p, wrap, h);
		band(tape, p, s, wrap, b);
		out[0] = h[0] + a * (b[0] - h[0]);
		out[1] = h[1] + a * (b[1] - h[1]);
	}

private:
	static constexpr int TABLE = HALF_WIDTH * STEPS + 2;   // the last two are 0

	static void hermite(const Tape& tape, double p, bool wrap, float out[2]) {
		out[0] = wrap ? tape.readWrapped(0, p) : tape.read(0, p);
		out[1] = wrap ? tape.readWrapped(1, p) : tape.read(1, p);
	}

	float at(float u) const {
		const float lim = (float) (HALF_WIDTH * STEPS);
		u = u < lim ? u : lim;   // past the end: 0
		const int i = (int) u;
		return table_[i] + (u - (float) i) * slope_[i];
	}

	void band(const Tape& tape, double p, float s, bool wrap, float out[2]) const {
		out[0] = out[1] = 0.f;
		const long long n = (long long) tape.extent(wrap ? tape.length : tape.length + tape.tail);
		if (n <= 0 || !tape.ch[0])
			return;
		double q;
		if (wrap) {
			const double len = (double) n;
			q = p - std::floor(p / len) * len;
			if (!(q >= 0.0 && q < len))
				q = 0.0;
		}
		else {
			const double last = (double) (n - 1);
			q = p > 0.0 ? p : 0.0;
			q = q < last ? q : last;
		}
		const long long i0 = (long long) q;
		const float f = (float) (q - (double) i0);
		const float reach = (float) HALF_WIDTH * s;
		const int jlo = (int) std::floor(f - reach) + 1;
		const int jhi = (int) std::floor(f + reach);
		const float scale = (float) STEPS / s;
		const float* x0 = tape.ch[0];
		const float* x1 = tape.ch[1];
		float sum = 0.f, l = 0.f, r = 0.f;
		if (i0 + jlo >= 0 && i0 + jhi < n) {
			x0 += i0;
			x1 += i0;
			for (int j = jlo; j <= jhi; j++) {
				const float d = (float) j - f;
				const float c = at((d < 0.f ? -d : d) * scale);
				sum += c;
				l += c * x0[j];
				r += c * x1[j];
			}
		}
		else {
			for (int j = jlo; j <= jhi; j++) {
				long long k = i0 + j;
				if (wrap) {
					k %= n;
					if (k < 0)
						k += n;
				}
				else
					k = k < 0 ? 0 : k >= n ? n - 1 : k;
				const float d = (float) j - f;
				const float c = at((d < 0.f ? -d : d) * scale);
				sum += c;
				l += c * x0[k];
				r += c * x1[k];
			}
		}
		const float g = sum > 0.f ? 1.f / sum : 0.f;
		out[0] = l * g;
		out[1] = r * g;
	}

	static double besselI0(double x) {
		double sum = 1.0, term = 1.0;
		const double q = x * x * 0.25;
		for (int k = 1; k < 200; k++) {
			term *= q / ((double) k * k);
			sum += term;
			if (term < 1e-14 * sum)
				break;
		}
		return sum;
	}

	float table_[TABLE];
	float slope_[TABLE];
};

} // namespace rka
