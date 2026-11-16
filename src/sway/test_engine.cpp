// The optimised Sway engine (SwayEngine.hpp) against the code it replaced,
// kept frozen in testdata/ref/ (namespace rkaref): both render the same
// frames, seconds at a time, across ranges, channel counts, moving poly
// CVs, clocks, resets, holds, one-shot, patching and unpatching, parameter
// steps and a sample-rate change, and the outputs must agree. Built with
// the plugin's floating-point flags (tests.mk). Run with `make test`.
//
// Full scale is 5 V. What may differ: the sines (SIN, MORPH's sine and
// rounding, RAND's glide) from FastMath.hpp's sine, within 2.5e-7 of libm's,
// so 1e-6 V; the frequencies from fastExp2D, within 1e-15 of libm's, which
// moves a phase by under 1e-12 of a cycle in an hour; the fold's floor in
// place of fmod, a rounding. Measured on macOS arm64: every sample within
// -125 dBFS. Bound: every sample of every patched output within -100 dBFS
// (50 uV, far below anything an LFO's destination resolves); EOC, the
// channel count, the rate light and the clocked flag exactly equal; and an
// unpatched output reads 0 V.
#include "SwayEngine.hpp"
#include "testdata/ref/SwayEngine.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

using F = rka::SwayFrame;

namespace {

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float uniform() {  // [0, 1)
		s = s * 1664525u + 1013904223u;
		return float(s >> 8) / 16777216.f;
	}
};

const double PEAK_BOUND = -100.0;

double dbfs(double volts) {
	return 20.0 * std::log10(std::fmax(volts, 1e-30) / 5.0);
}

// The frame both engines read: params, 16 volts per input and a channel
// count, which outputs are patched, the range and the sample rate.
struct Setup {
	float params[F::PARAMS_LEN];
	float volts[F::INPUTS_LEN][16];
	int channels[F::INPUTS_LEN];
	bool patched[F::OUTPUTS_LEN];
	int range = rka::RANGE_NORMAL;
	float fs = 48000.f;

	Setup() {
		std::memset(volts, 0, sizeof(volts));
		for (int& c : channels)
			c = 0;
		for (bool& p : patched)
			p = true;
		// The module's defaults.
		float* p = params;
		p[F::RATE] = 1.f;
		p[F::PW] = 0.5f;
		p[F::PHASE] = 0.f;
		p[F::SKEW] = 0.5f;
		p[F::SMOOTH] = 0.f;
		p[F::MORPH] = 0.f;
		p[F::SPREAD] = 0.f;
		p[F::CHANNELS] = 1.f;
		for (int a = F::FM_ATT; a <= F::MORPH_ATT; a++)
			p[a] = 0.f;
		p[F::OFFSET] = 0.f;
		p[F::ONESHOT] = 0.f;
	}

	// Rack's port semantics: an unplugged input, or channels above a
	// cable's count, read 0 V.
	void plug(int input, int n) {
		channels[input] = n;
		for (int c = n; c < 16; c++)
			volts[input][c] = 0.f;
	}
};

struct Stats {
	double peak = 0.0;
	long eocMismatch = 0, otherMismatch = 0, unpatchedNonzero = 0, eocHigh = 0;
};

template <typename Change>
void render(const char* name, double seconds, Change change) {
	rka::SwayEngine opt;
	rkaref::SwayEngine ref;
	rka::SwayFrame f;
	rkaref::SwayFrame g;
	rka::SwayResult a;
	rkaref::SwayResult b;
	Setup s;
	Lcg rng(4242);
	Stats st;
	for (long i = 0; i < long(seconds * 48000.0); i++) {
		change(s, i, rng);
		for (int k = 0; k < F::PARAMS_LEN; k++)
			f.params[k] = g.params[k] = s.params[k];
		for (int k = 0; k < F::INPUTS_LEN; k++) {
			f.inputs[k].v = g.inputs[k].v = s.volts[k];
			f.inputs[k].channels = g.inputs[k].channels = s.channels[k];
		}
		for (int o = 0; o < F::OUTPUTS_LEN; o++)
			f.outputConnected[o] = g.outputConnected[o] = s.patched[o];
		f.range = g.range = s.range;
		opt.process(f, 1.f / s.fs, s.fs, a);
		ref.process(g, 1.f / s.fs, s.fs, b);
		if (a.channels != b.channels || a.rateLight != b.rateLight || a.oneShotLight != b.oneShotLight ||
		    opt.clocked != ref.clocked) {
			st.otherMismatch++;
			continue;
		}
		for (int o = 0; o < F::OUTPUTS_LEN; o++)
			for (int c = 0; c < a.channels; c++) {
				if (o == F::EOC) {
					st.eocMismatch += a.out[o][c] != b.out[o][c];
					st.eocHigh += b.out[o][c] > 0.f;
				}
				else if (!s.patched[o])
					st.unpatchedNonzero += a.out[o][c] != 0.f;
				else {
					const double d = std::fabs(double(a.out[o][c]) - b.out[o][c]);
					st.peak = d == d ? std::fmax(st.peak, d) : 1e30;
				}
			}
	}
	CHECK(dbfs(st.peak) < PEAK_BOUND, "%s: outputs differ by %.1f dBFS (bound %.0f)", name, dbfs(st.peak), PEAK_BOUND);
	CHECK(st.eocMismatch == 0, "%s: EOC differs on %ld channel-samples", name, st.eocMismatch);
	CHECK(st.otherMismatch == 0, "%s: channels, lights or clocked differ on %ld samples", name, st.otherMismatch);
	CHECK(st.unpatchedNonzero == 0, "%s: unpatched outputs nonzero on %ld channel-samples", name, st.unpatchedNonzero);
	// EOC must have fired, or the EOC comparison proves nothing.
	CHECK(st.eocHigh > 0, "%s: no end of cycle in the render", name);
	std::printf("%-26s peak difference %.1f dBFS, %ld EOC samples high\n", name, dbfs(st.peak), st.eocHigh);
}

float cv(long i, double hz, int c, double phase) {
	return 5.f * float(std::sin(2.0 * M_PI * (hz * double(i) / 48000.0 + phase + 0.071 * c)));
}

void testDefaults() {
	render("defaults", 10.0, [](Setup&, long, Lcg&) {});
}

// 16 channels, a moving poly CV on every CV input, a poly clock, resets
// and holds a little apart per channel, every attenuverter open.
void testBusy() {
	render("busy", 8.0, [](Setup& s, long i, Lcg&) {
		if (i == 0) {
			s.params[F::CHANNELS] = 16.f;
			s.params[F::RATE] = 3.f;
			s.params[F::SPREAD] = 0.5f;
			s.params[F::MORPH] = 0.5f;
			for (int a = F::FM_ATT; a <= F::MORPH_ATT; a++)
				s.params[a] = 0.7f;
			for (int k = 0; k < F::INPUTS_LEN; k++)
				s.plug(k, 16);
		}
		for (int k = F::FM; k <= F::MORPH_IN; k++)
			for (int c = 0; c < 16; c++)
				s.volts[k][c] = cv(i, 0.05 + 0.07 * k, c, 0.1 * k);
		// The clock goes away for a second, and comes back high.
		if (i == long(4.0 * 48000))
			s.plug(F::CLOCK, 0);
		if (i == long(5.0 * 48000))
			s.plug(F::CLOCK, 16);
		for (int c = 0; c < 16; c++) {
			if (s.channels[F::CLOCK] > 0)
				s.volts[F::CLOCK][c] = (i + 7 * c) % 12000 < 600 ? 10.f : 0.f;
			s.volts[F::RESET][c] = (i + 997 * c) % 70001 < 48 ? 10.f : 0.f;
			s.volts[F::HOLD][c] = (i + 3001 * c) % 50000 < 4000 ? 10.f : 0.f;
		}
	});
}

// Audio range (the band-limited edges), with a mono FM, rate, morph, skew,
// smooth and PW stepping every 0.2 s.
void testAudioSteps() {
	render("audio range, steps", 4.0, [](Setup& s, long i, Lcg& rng) {
		if (i == 0) {
			s.range = rka::RANGE_AUDIO;
			s.params[F::CHANNELS] = 3.f;
			s.params[F::FM_ATT] = 0.2f;
			s.plug(F::FM, 1);
		}
		s.volts[F::FM][0] = cv(i, 0.3, 0, 0.0);
		if (i % 9600 == 0) {
			s.params[F::RATE] = -7.f + 14.f * rng.uniform();
			s.params[F::MORPH] = rng.uniform();
			s.params[F::SKEW] = rng.uniform();
			s.params[F::SMOOTH] = -1.f + 2.f * rng.uniform();
			s.params[F::PW] = 0.01f + 0.98f * rng.uniform();
			s.params[F::PHASE] = rng.uniform();
			s.params[F::SPREAD] = rng.uniform();
			s.params[F::OFFSET] = rng.uniform() < 0.5f ? 0.f : 1.f;
		}
	});
}

// What the optimised engine caches or skips, changed under it: CHANNELS
// steps (channels joining later must be in step), FM unplugged, mono and
// poly in turn, outputs patched and unpatched, one-shot on and off with
// resets, the range, and the sample rate.
void testStructure() {
	render("structure changes", 12.0, [](Setup& s, long i, Lcg& rng) {
		if (i == 0) {
			s.params[F::RATE] = 4.f;
			s.params[F::FM_ATT] = 0.5f;
			s.params[F::SMOOTH] = -0.5f;
		}
		// Changes land between FM's own steps (every 4800 samples), so each
		// is the only thing that changed on its sample.
		const long step = 24000;
		if (i % step == 12000)
			s.params[F::RATE] = float(int(i / step) % 9 - 4);
		if (i % step == 2400) {
			const int k = int(i / step);
			s.params[F::CHANNELS] = float(1 + (k * 5) % 16);
			// Unplugged, mono, poly, mono, ...
			s.plug(F::FM, k % 4 == 0 ? 0 : k % 4 == 2 ? 2 + k % 15 : 1);
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				s.patched[o] = rng.uniform() < 0.6f;
			s.params[F::ONESHOT] = k % 5 == 3 ? 1.f : 0.f;
			s.range = k % 7 == 6 ? rka::RANGE_SLOW : k % 7 == 5 ? rka::RANGE_AUDIO : rka::RANGE_NORMAL;
			s.fs = k >= 12 ? 96000.f : 48000.f;
			s.plug(F::RESET, k % 4 == 0 ? 0 : 1);
		}
		// FM holds still for 0.1 s at a time, so a cached rate is reused
		// across samples and across a change of FM's channel count.
		for (int c = 0; c < s.channels[F::FM]; c++)
			s.volts[F::FM][c] = cv(i - i % 4800, 0.4, c, 0.0);
		// RESET high for 0.4 s of every 0.7 s, so some unpluggings catch it
		// high.
		if (s.channels[F::RESET] > 0)
			s.volts[F::RESET][0] = i % 33600 < 19200 ? 10.f : 0.f;
	});
}

// A sample-rate change with nothing else moving, at a rate the lower
// sample rate clamps (0.45 fs).
void testSampleRate() {
	render("sample-rate change", 3.0, [](Setup& s, long i, Lcg&) {
		if (i == 0) {
			s.range = rka::RANGE_AUDIO;
			s.params[F::RATE] = 7.f;
		}
		s.fs = (i / 24000) % 2 ? 16000.f : 48000.f;
	});
}

// Two instances run in turn each sample, as Rack runs them, the second
// with other settings and a poly FM: the first must give exactly what it
// gives alone.
void testInstancesIndependent() {
	Setup a, other;
	a.params[F::CHANNELS] = 4.f;
	other.params[F::CHANNELS] = 16.f;
	other.params[F::FM_ATT] = 1.f;
	other.plug(F::FM, 16);
	auto frame = [](Setup& s, rka::SwayFrame& f) {
		for (int k = 0; k < F::PARAMS_LEN; k++)
			f.params[k] = s.params[k];
		for (int k = 0; k < F::INPUTS_LEN; k++) {
			f.inputs[k].v = s.volts[k];
			f.inputs[k].channels = s.channels[k];
		}
		for (int o = 0; o < F::OUTPUTS_LEN; o++)
			f.outputConnected[o] = s.patched[o];
		f.range = s.range;
	};
	rka::SwayEngine solo, first, second;
	rka::SwayFrame fa, fo;
	rka::SwayResult x, y, z;
	long differ = 0;
	for (long i = 0; i < 96000; i++) {
		for (int c = 0; c < 16; c++)
			other.volts[F::FM][c] = cv(i, 0.3, c, 0.0);
		a.params[F::RATE] = other.params[F::RATE] = float((i / 9000) % 5);
		frame(a, fa);
		frame(other, fo);
		solo.process(fa, 1.f / 48000.f, 48000.f, x);
		first.process(fa, 1.f / 48000.f, 48000.f, y);
		second.process(fo, 1.f / 48000.f, 48000.f, z);
		for (int o = 0; o < F::OUTPUTS_LEN; o++)
			for (int c = 0; c < 4; c++)
				differ += x.out[o][c] != y.out[o][c];
	}
	CHECK(differ == 0, "an instance's output depends on another instance on %ld channel-samples", differ);
}

// ratioIndex without lround rounds as lround did: halves away from zero.
void testRatioIndex() {
	int differ = 0;
	for (int k = -40; k <= 40; k++)
		for (float d : {-1e-6f, 0.f, 1e-6f}) {
			const float x = 0.25f * k + d;
			differ += rka::ratioIndex(x, 0.f) != rkaref::ratioIndex(x, 0.f);
			differ += rka::ratioIndex(1.f, x) != rkaref::ratioIndex(1.f, x);
		}
	CHECK(differ == 0, "ratioIndex differs from lround's on %d values", differ);
	CHECK(rka::ratioIndex(1e30f, 0.f) == 7 && rka::ratioIndex(-1e30f, 0.f) == -7, "ratioIndex clamps");
}

// A NaN FM means no FM: the same clock ratio and free-running frequency as
// an FM of 0 V, mono or poly, clocked or not (controller's ruling, issue #8
// review).
void testNanFm() {
	CHECK(rka::ratioIndex(1.f, NAN) == 1 && rka::ratioIndex(-3.f, NAN) == -3, "ratioIndex with NaN FM %d",
	      rka::ratioIndex(1.f, NAN));
	for (int channels : {1, 16}) {
		Setup nanFm, zeroFm;
		for (Setup* s : {&nanFm, &zeroFm}) {
			s->params[F::RATE] = 2.f;
			s->params[F::FM_ATT] = 1.f;
			s->params[F::CHANNELS] = 16.f;
			s->plug(F::FM, channels);
		}
		for (int c = 0; c < channels; c++)
			nanFm.volts[F::FM][c] = c % 2 ? 0.f : NAN;
		rka::SwayEngine a, b;
		rka::SwayFrame fa, fb;
		rka::SwayResult ra, rb;
		for (Setup* s : {&nanFm, &zeroFm}) {
			rka::SwayFrame& f = s == &nanFm ? fa : fb;
			for (int k = 0; k < F::PARAMS_LEN; k++)
				f.params[k] = s->params[k];
			for (int k = 0; k < F::INPUTS_LEN; k++) {
				f.inputs[k].v = s->volts[k];
				f.inputs[k].channels = s->channels[k];
			}
			for (int o = 0; o < F::OUTPUTS_LEN; o++)
				f.outputConnected[o] = true;
		}
		long differ = 0;
		for (int i = 0; i < 4800; i++) {
			a.process(fa, 1.f / 48000.f, 48000.f, ra);
			b.process(fb, 1.f / 48000.f, 48000.f, rb);
			for (int c = 0; c < 16; c++)
				differ += a.voiceInputs[c].ratio != b.voiceInputs[c].ratio || a.voiceInputs[c].freeHz != b.voiceInputs[c].freeHz;
		}
		CHECK(differ == 0, "NaN FM (%d channels) differs from 0 V FM on %ld channel-samples", channels, differ);
		CHECK(a.voiceInputs[0].ratio == 2 && a.voiceInputs[0].freeHz == 4.0, "NaN FM: ratio %d, %f Hz",
		      a.voiceInputs[0].ratio, a.voiceInputs[0].freeHz);
	}
}

} // namespace

int main() {
	testDefaults();
	testBusy();
	testAudioSteps();
	testStructure();
	testSampleRate();
	testInstancesIndependent();
	testRatioIndex();
	testNanFm();
	return rkatest::finish("test_engine");
}
