#pragma once
// Reading a tape at any speed without aliasing or losing the top of the
// band (Eddy; later Fetch). Rack-free, C++11, header-only.
// docs/DSP-ALIASING.md, source (3).
//
// Faster than 1. A head moving s frames a sample plays the tape's content s
// times higher: what lies above 1/s of the tape's Nyquist lands above the
// output's Nyquist and folds back as aliases. No interpolator removes it;
// the content has to be low-passed under the new Nyquist as it is read.
// This read does that with a windowed-sinc kernel whose cutoff scales with
// the speed: laid over the tape s times wider than at speed 1, it always
// cuts at the same place in the OUTPUT's spectrum (CUTOFF), with about
// 2 HALF_WIDTH s taps.
//
// At 1 and slower. Nothing aliases; the read only interpolates between
// frames. It uses the same window with the cutoff exactly at the tape's
// Nyquist (an interpolating kernel: zero at every other whole frame), so at
// a whole frame it returns that frame, to the bit, and between frames it
// keeps the band flat to 0.8 of Nyquist and the slow read's images 57 dB
// down at 0.4 of the rate. (A 4-point Hermite, used before 2026-11-22, lost
// up to 2.8 dB of the top octave between frames, 0.9 dB of white noise's
// level, and left images at -11 dB.)
//
// Between 1 and 1 + BLEND (1.02) the two reads are crossfaded linearly, so
// nothing steps as a glide or an audio-rate Speed CV passes through 1 or
// wobbles round it, and both are centred on the head, so nothing combs.
// There the speed-1 kernel still passes a little of what lands above
// Nyquist (-6 to -26 dB re a full tone), but at these speeds only content
// above 0.98 of the tape's Nyquist lands there, and it folds back no lower
// than 0.98 of the output's Nyquist (21.6 kHz at 44.1 kHz). From 1.02 up
// aliases are at least 57 dB down.
//
// To the bit: a whole frame read at 1 or slower is that frame, by
// construction (row 0 of the speed-1 kernel is 1 there and exactly 0
// elsewhere), whatever the compiler does. Between frames the read is the
// same arithmetic at every speed up to 1, but fast maths may round it
// differently where it is inlined into different callers (1e-7 V seen), so
// two callers agree to within that, not to the bit.
//
// The kernels, in output samples u (u = tape frames / max(s, 1)):
//   faster: h(u) = CUTOFF sinc(CUTOFF u) kaiser(u / HALF_WIDTH, BETA)
//   at 1:   h(u) = sinc(u) kaiser(u / HALF_WIDTH, BETA)
// tabulated at STEPS points per output sample, linearly interpolated, and
// normalised by the sum of the taps used, so a constant reads exactly
// itself. The speed-1 kernel is kept by phase (a row of 2 HALF_WIDTH taps
// for each 1/STEPS of a frame), which the compiler vectorises: slow
// playback is the common case.
//
// Edges: a tap outside the tape is held to it (taps beyond the tape and its
// tail read the last frame, before 0 the first), and a wrapped read wraps
// its taps round [0, length), as Tape::read and Tape::readWrapped do.
//
// Threads: build it anywhere (the constructor fills the tables with libm);
// read() allocates nothing and calls no libm, from the thread that owns the
// tape.
#include <cmath>

#include "Tape.hpp"

namespace rka {

class BandLimitedRead {
public:
	// The kernels. Faster than 1 the response, as a fraction of the
	// output's Nyquist, is flat within 0.02 dB to 0.8, -6 dB at 0.9 and at
	// least 57 dB down from 1 up (test_band_read measures it): whatever the
	// speed puts above the output's Nyquist folds back at least 57 dB down
	// (67 dB from 1.2 up), and content landing between 0.8 and 1 of Nyquist
	// (17.6 to 22 kHz at 44.1 kHz) is rolled off rather than kept. The cost
	// is about 36 s taps a side a voice (72 at speed 2). With the stopband
	// at 1.2 and HALF_WIDTH 9 instead (half the taps), content landing
	// between 1 and 1.2 of Nyquist folds back only 3 to 8 dB down (measured
	// 2026-11-22: busy bench 1.07 % against 1.27 % of a core). At 1 and
	// slower the speed-1 kernel (cutoff at Nyquist) is flat within 0.02 dB
	// to 0.8 of the tape's Nyquist and -6 dB at its Nyquist; 36 taps a side.
	static constexpr int HALF_WIDTH = 18;       // output samples either side
	static constexpr double CUTOFF = 0.9;       // faster than 1: of the output's Nyquist (-6 dB)
	static constexpr double BETA = 5.65;        // Kaiser: about 60 dB
	static constexpr int STEPS = 128;           // table points per output sample
	static constexpr float BLEND = 0.02f;       // speeds 1 .. 1.02 crossfade
	static constexpr float MAX_SPEED = 4.f;     // wider is read as 4 (Eddy holds it there)

	BandLimitedRead() {
		for (int i = 0; i < TABLE; i++)
			table_[i] = (float) kernel((double) i / STEPS, CUTOFF);
		table_[TABLE] = 0.f;
		// The speed-1 kernel by phase: row ph is the head ph / STEPS past a
		// frame, tap k the frame k - (HALF_WIDTH - 1) from it. Row 0 is 1 at
		// the frame and exactly 0 at every other: a whole frame reads itself.
		for (int ph = 0; ph <= STEPS; ph++) {
			float sum = 0.f;
			for (int k = 0; k < TAPS1; k++) {
				const int j = k - (HALF_WIDTH - 1);
				const int u = j * STEPS - ph;
				float c = (float) kernel((double) (u < 0 ? -u : u) / STEPS, 1.0);
				if (u % STEPS == 0)
					c = u == 0 ? 1.f : 0.f;
				poly_[ph][k] = c;
				sum += c;
			}
			polySum_[ph] = sum;
		}
	}

	// Both sides of `tape` at frame p for a head moving `speed` frames a
	// sample (either sign; NaN reads as stopped), as Tape::read (wrap false)
	// or Tape::readWrapped would place it.
	void read(const Tape& tape, double p, float speed, bool wrap, float out[2]) const {
		const float s = speed < 0.f ? -speed : speed;
		if (!(s > 1.f)) {   // NaN too
			atOne(tape, p, wrap, out);
			return;
		}
		const float w = s < MAX_SPEED ? s : MAX_SPEED;
		const float a = (s - 1.f) * (1.f / BLEND);
		if (a >= 1.f) {
			faster(tape, p, w, wrap, out);
			return;
		}
		float h[2], b[2];
		atOne(tape, p, wrap, h);
		faster(tape, p, w, wrap, b);
		out[0] = h[0] + a * (b[0] - h[0]);
		out[1] = h[1] + a * (b[1] - h[1]);
	}

private:
	static constexpr int TABLE = HALF_WIDTH * STEPS + 2;   // the last two are 0
	static constexpr int TAPS1 = 2 * HALF_WIDTH;           // taps at speed 1
	// Taps faster than 1: within +-HALF_WIDTH x MAX_SPEED of the head.
	static constexpr int MAX_TAPS = 2 * HALF_WIDTH * (int) MAX_SPEED + 2;

	static double kernel(double u, double cutoff) {
		const double pi = 3.14159265358979323846;
		const double t = u / HALF_WIDTH;
		if (!(t < 1.0))
			return 0.0;
		const double x = cutoff * u;
		const double sinc = x == 0.0 ? 1.0 : std::sin(pi * x) / (pi * x);
		return cutoff * sinc * besselI0(BETA * std::sqrt(1.0 - t * t)) / besselI0(BETA);
	}

	// Where the read is: the frame i0 and the fraction f past it, held to
	// the tape (and its tail) or wrapped round it. False if there is no tape.
	static bool place(const Tape& tape, double p, bool wrap, long long& n, long long& i0, float& f) {
		n = (long long) tape.extent(wrap ? tape.length : tape.length + tape.tail);
		if (n <= 0 || !tape.ch[0])
			return false;
		double q;
		if (wrap) {
			const double len = (double) n;
			q = p - std::floor(p / len) * len;
			if (!(q >= 0.0 && q < len))
				q = 0.0;
		}
		else {
			const double last = (double) (n - 1);
			q = p > 0.0 ? p : 0.0;   // NaN fails the test: 0
			q = q < last ? q : last;
		}
		i0 = (long long) q;
		f = (float) (q - (double) i0);
		return true;
	}
	static long long hold(long long k, long long n, bool wrap) {
		if (wrap) {
			k %= n;
			return k < 0 ? k + n : k;
		}
		return k < 0 ? 0 : k >= n ? n - 1 : k;
	}

	// At 1 and slower: the speed-1 kernel, the two rows either side of f
	// blended (the same as interpolating the table).
	void atOne(const Tape& tape, double p, bool wrap, float out[2]) const {
		out[0] = out[1] = 0.f;
		long long n, i0;
		float f;
		if (!place(tape, p, wrap, n, i0, f))
			return;
		if (f == 0.f) {   // a whole frame: row 0 is 1 there and 0 elsewhere
			out[0] = tape.ch[0][i0];
			out[1] = tape.ch[1][i0];
			return;
		}
		const float x = f * (float) STEPS;
		int ph = (int) x;
		ph = ph < STEPS ? ph : STEPS - 1;
		const float fr = x - (float) ph;
		const float* p0 = poly_[ph];
		const float* p1 = poly_[ph + 1];
		const long long first = i0 - (HALF_WIDTH - 1);
		float l = 0.f, r = 0.f;
		if (first >= 0 && first + TAPS1 <= n) {
			const float* a = tape.ch[0] + first;
			const float* b = tape.ch[1] + first;
			for (int k = 0; k < TAPS1; k++) {
				const float c = p0[k] + fr * (p1[k] - p0[k]);
				l += c * a[k];
				r += c * b[k];
			}
		}
		else {
			for (int k = 0; k < TAPS1; k++) {
				const long long at = hold(first + k, n, wrap);
				const float c = p0[k] + fr * (p1[k] - p0[k]);
				l += c * tape.ch[0][at];
				r += c * tape.ch[1][at];
			}
		}
		const float sum = polySum_[ph] + fr * (polySum_[ph + 1] - polySum_[ph]);
		const float g = sum > 0.f ? 1.f / sum : 0.f;
		out[0] = l * g;
		out[1] = r * g;
	}

	float at(float u) const {
		const float lim = (float) (HALF_WIDTH * STEPS);
		u = u < lim ? u : lim;   // past the end: 0
		const int i = (int) u;
		return table_[i] + (u - (float) i) * (table_[i + 1] - table_[i]);
	}

	// Faster than 1: the faster kernel laid s times wider.
	void faster(const Tape& tape, double p, float s, bool wrap, float out[2]) const {
		out[0] = out[1] = 0.f;
		long long n, i0;
		float f;
		if (!place(tape, p, wrap, n, i0, f))
			return;
		const float reach = (float) HALF_WIDTH * s;
		const int jlo = (int) std::floor(f - reach) + 1;
		const int jhi = (int) std::floor(f + reach);
		const float scale = (float) STEPS / s;
		const float* x0 = tape.ch[0];
		const float* x1 = tape.ch[1];
		float sum = 0.f, l = 0.f, r = 0.f;
		// The coefficients first, then the two sums over contiguous taps (a
		// loop the compiler vectorises).
		const int taps = jhi - jlo + 1;   // at most MAX_TAPS
		float c[MAX_TAPS];
		for (int t = 0; t < taps; t++) {
			const float d = (float) (jlo + t) - f;
			c[t] = at((d < 0.f ? -d : d) * scale);
		}
		if (i0 + jlo >= 0 && i0 + jhi < n) {
			x0 += i0 + jlo;
			x1 += i0 + jlo;
			for (int t = 0; t < taps; t++) {
				sum += c[t];
				l += c[t] * x0[t];
				r += c[t] * x1[t];
			}
		}
		else {
			for (int t = 0; t < taps; t++) {
				const long long k = hold(i0 + jlo + t, n, wrap);
				sum += c[t];
				l += c[t] * x0[k];
				r += c[t] * x1[k];
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

	float table_[TABLE + 1];   // one more 0, so table_[i + 1] is there for every i
	float poly_[STEPS + 1][TAPS1];   // the speed-1 kernel, by phase
	float polySum_[STEPS + 1];
};

} // namespace rka
