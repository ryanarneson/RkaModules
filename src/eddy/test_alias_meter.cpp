// rkatest::AliasMeter (src/testing_alias.hpp), the aliasing tests' measuring
// helper: it must find a planted alias at the level it was planted, wherever
// the frequencies fall between bins, and read a clean sine as clean.
#include "testing.hpp"
#include "testing_alias.hpp"

#include <cmath>
#include <vector>

namespace {

const double PI = 3.14159265358979323846;

std::vector<float> tones(double fs, size_t n, const std::vector<double>& hz, const std::vector<double>& amp) {
	std::vector<float> x(n);
	for (size_t i = 0; i < n; i++) {
		double s = 0.0;
		for (size_t k = 0; k < hz.size(); k++)
			s += amp[k] * std::sin(2.0 * PI * hz[k] * (double) i / fs + 0.3 * (double) k);
		x[i] = (float) s;
	}
	return x;
}

void plantedAlias() {
	// A 5 V tone and an "alias" at -40, -60 and -90 dB re 5 V, at 44.1 and
	// 48 kHz, on and off bin centres.
	for (double fs : {44100.0, 48000.0})
		for (double rel : {-40.0, -60.0, -90.0})
			for (double tone : {1000.0, 1000.37, 7123.4})
				for (double alias : {3001.0, 11025.73, 19876.5}) {
					const double a = 5.0 * std::pow(10.0, rel / 20.0);
					const std::vector<float> x = tones(fs, 40000, {tone, alias}, {5.0, a});
					const rkatest::AliasMeter m(x.data(), x.size(), fs);
					CHECK(m.size() == 32768, "N is the largest power of two in the signal: %zu", m.size());
					const double got = m.aliasDb({tone});
					CHECK(std::fabs(got - rel) < 1.0, "fs %.0f, tone %.2f Hz, alias %.2f Hz planted at %.0f dB: read %.2f dB",
					      fs, tone, alias, rel, got);
					const double where = m.strongestOther({tone}).hz;
					CHECK(std::fabs(where - alias) < 2.0 * m.binHz(), "found at %.1f Hz, planted at %.1f Hz", where, alias);
					const double amp = m.amplitude(tone);
					CHECK(std::fabs(rkatest::AliasMeter::db(amp / 5.0)) < 0.05, "the tone reads 5 V: %.4f V", amp);
					// Listed as expected, it is not "other".
					CHECK(m.aliasDb({tone, alias}) < -110.0, "both expected: nothing else (%.1f dB)", m.aliasDb({tone, alias}));
				}
}

void cleanSine() {
	for (double fs : {44100.0, 48000.0})
		for (double tone : {440.0, 5000.3, 0.4 * fs + 0.71}) {
			const std::vector<float> x = tones(fs, 32768, {tone}, {5.0});
			const rkatest::AliasMeter m(x.data(), x.size(), fs);
			const double got = m.aliasDb({tone});
			CHECK(got < -120.0, "a clean float sine at %.1f Hz (fs %.0f) reads below -120 dB: %.1f dB", tone, fs, got);
		}
}

void helpers() {
	CHECK(std::fabs(rkatest::AliasMeter::folded(30000.0, 48000.0) - 18000.0) < 1e-9, "30 kHz at 48 kHz lands at 18 kHz");
	CHECK(std::fabs(rkatest::AliasMeter::folded(-50000.0, 48000.0) - 2000.0) < 1e-9, "-50 kHz lands at 2 kHz");
	const std::vector<double> h = rkatest::AliasMeter::harmonics(5000.0, 48000.0);
	CHECK(h.size() == 4 && h.back() == 20000.0, "5 kHz at 48 kHz: four harmonics below Nyquist (%zu)", h.size());
	// Short signals use the largest power of two that fits.
	const std::vector<float> x = tones(48000.0, 5000, {1000.0}, {1.0});
	const rkatest::AliasMeter m(x.data(), x.size(), 48000.0);
	CHECK(m.size() == 4096, "5000 samples: N 4096 (%zu)", m.size());

	// A widened guard owns a skirt 300 Hz from the tone, not one 1 kHz away.
	const std::vector<float> y = tones(48000.0, 32768, {5000.0, 5300.0, 6000.0}, {5.0, 0.05, 0.005});
	const rkatest::AliasMeter my(y.data(), y.size(), 48000.0);
	CHECK(std::fabs(my.aliasDb({5000.0}) + 40.0) < 1.0, "the skirt is other without a wider guard (%.1f dB)", my.aliasDb({5000.0}));
	CHECK(std::fabs(my.aliasDb({5000.0}, 400.0) + 60.0) < 1.0, "a 400 Hz guard owns the skirt, not the 6 kHz (%.1f dB)",
	      my.aliasDb({5000.0}, 400.0));
}

} // namespace

int main() {
	plantedAlias();
	cleanSine();
	helpers();
	return rkatest::finish("eddy/test_alias_meter");
}
