// Eddy's file code: WavFile.hpp (read, probe, write) and Resample.hpp. Built
// with the sanitizers and a counting allocator: operator new, and the malloc
// that dr_wav uses (its DRWAV_MALLOC hooks are set below, before it is
// included; the C library's own buffers are not seen).
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

static long long gAllocBytes = 0;

static void* countedMalloc(size_t n) {
	gAllocBytes += (long long) n;
	return std::malloc(n);
}
static void* countedRealloc(void* p, size_t n) {
	gAllocBytes += (long long) n;
	return std::realloc(p, n);
}
static void countedFree(void* p) {
	std::free(p);
}
#define DRWAV_MALLOC(sz) countedMalloc(sz)
#define DRWAV_REALLOC(p, sz) countedRealloc((p), (sz))
#define DRWAV_FREE(p) countedFree(p)

void* operator new(std::size_t n) {
	gAllocBytes += (long long) n;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	std::free(p);
}

#define RKA_WAV_IMPLEMENTATION
#include "WavFile.hpp"
#include "Resample.hpp"

namespace fs = std::filesystem;
using rka::WavInfo;

namespace {

struct TempDir {
	fs::path dir;
	TempDir() {
		dir = fs::temp_directory_path() / ("rka_eddy_files_" + std::to_string((long long) std::rand()) + "_" + std::to_string((long long) std::time(nullptr)));
		fs::create_directories(dir);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
	std::string file(const std::string& name) const {
		return (dir / fs::u8path(name)).u8string();
	}
};

uint32_t gRng = 12345;
float noise() {
	gRng ^= gRng << 13;
	gRng ^= gRng >> 17;
	gRng ^= gRng << 5;
	return (float) (gRng >> 8) / 8388608.f - 1.f;
}

void put16(std::string& s, uint32_t v) {
	s += (char) (v & 255);
	s += (char) ((v >> 8) & 255);
}
void put32(std::string& s, uint32_t v) {
	put16(s, v & 0xFFFF);
	put16(s, v >> 16);
}

// A WAV image by hand: format tag, the fmt fields, the data chunk's claimed
// size (or the real one), and the data.
std::string wavBytes(unsigned tag, unsigned channels, uint32_t rate, unsigned bits, const std::string& data, int64_t claimed = -1) {
	std::string s = "RIFF";
	put32(s, 36 + (uint32_t) data.size());
	s += "WAVEfmt ";
	put32(s, 16);
	put16(s, tag);
	put16(s, channels);
	put32(s, rate);
	unsigned bytes = bits / 8;
	put32(s, rate * channels * bytes);
	put16(s, channels * bytes);
	put16(s, bits);
	s += "data";
	put32(s, claimed < 0 ? (uint32_t) data.size() : (uint32_t) claimed);
	s += data;
	return s;
}

void writeBytes(const std::string& path, const std::string& bytes) {
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (f) {
		std::fwrite(bytes.data(), 1, bytes.size(), f);
		std::fclose(f);
	}
}

// The value the format tests put in frame i, channel c.
double tone(int i, int c) {
	return 0.8 * std::sin(0.05 * i + c);
}

std::string encode(unsigned tag, unsigned bits, unsigned channels, int frames) {
	std::string d;
	for (int i = 0; i < frames; i++)
		for (unsigned c = 0; c < channels; c++) {
			double v = tone(i, (int) c);
			if (tag == 3 && bits == 32) {
				float f = (float) v;
				d.append((const char*) &f, 4);
			}
			else if (tag == 3) {
				d.append((const char*) &v, 8);
			}
			else if (bits == 8) {
				d += (char) (uint8_t) (std::lround(v * 127.) + 128);
			}
			else if (bits == 16) {
				put16(d, (uint16_t) (int16_t) std::lround(v * 32767.));
			}
			else if (bits == 24) {
				int32_t x = (int32_t) std::lround(v * 8388607.);
				d += (char) (x & 255);
				d += (char) ((x >> 8) & 255);
				d += (char) ((x >> 16) & 255);
			}
			else {
				put32(d, (uint32_t) (int32_t) std::lround(v * 2147483647.));
			}
		}
	return d;
}

void round_trip() {
	TempDir t;
	const int n = 5000;
	std::vector<float> l(n), r(n), a(n, 9.f), b(n, 9.f);
	for (int i = 0; i < n; i++) {
		l[i] = noise();
		r[i] = noise();
	}
	std::string path = t.file("loop.wav"), why;
	CHECK(rka::writeWav(path, l.data(), r.data(), n, 44100, 1.f, why), "write: %s", why.c_str());
	CHECK(!fs::exists(path + ".tmp"), "the .tmp file is left behind");
	WavInfo info;
	CHECK(rka::probeWav(path, info, why) && info.sampleRate == 44100 && info.channels == 2 && info.frames == (uint64_t) n,
		"probe: %u Hz %u ch %llu frames (%s)", info.sampleRate, info.channels, (unsigned long long) info.frames, why.c_str());
	WavInfo got;
	CHECK(rka::readWav(path, a.data(), b.data(), n, got, why), "read: %s", why.c_str());
	CHECK(got.frames == (uint64_t) n && got.sampleRate == 44100, "info after read");
	CHECK(std::memcmp(l.data(), a.data(), n * sizeof(float)) == 0 && std::memcmp(r.data(), b.data(), n * sizeof(float)) == 0,
		"left or right differs from what was written");

	// The gain, and a write over an existing file.
	CHECK(rka::writeWav(path, l.data(), r.data(), n, 48000, 0.2f, why), "write again: %s", why.c_str());
	CHECK(rka::readWav(path, a.data(), b.data(), n, got, why) && got.sampleRate == 48000, "read again");
	bool gainOk = true;
	for (int i = 0; i < n; i++)
		gainOk = gainOk && a[i] == l[i] * 0.2f && b[i] == r[i] * 0.2f;
	CHECK(gainOk, "the gain is not applied as given");
	CHECK(!fs::exists(path + ".tmp"), ".tmp left after the second write");

	// A write that cannot happen says why and leaves nothing.
	CHECK(!rka::writeWav(t.file("no/such/folder/x.wav"), l.data(), r.data(), n, 44100, 1.f, why) && !why.empty(), "write into a missing folder");
}

void formats() {
	TempDir t;
	struct Case {
		const char* name;
		unsigned tag, bits;
		double tol;
	};
	const Case cases[] = {
		{"8", 1, 8, 0.012}, {"16", 1, 16, 1e-4}, {"24", 1, 24, 1e-6},
		{"32int", 1, 32, 1e-6}, {"f32", 3, 32, 1e-7}, {"f64", 3, 64, 1e-7},
	};
	const int n = 700;
	for (const Case& c : cases)
		for (unsigned ch : {1u, 2u, 4u}) {
			std::string path = t.file(std::string("f") + c.name + "_" + std::to_string(ch) + ".wav"), why;
			writeBytes(path, wavBytes(c.tag, ch, 32000, c.bits, encode(c.tag, c.bits, ch, n)));
			std::vector<float> l(n, 9.f), r(n, 9.f);
			WavInfo info;
			bool ok = rka::readWav(path, l.data(), r.data(), n, info, why);
			CHECK(ok, "%s bit, %u channels: %s", c.name, ch, why.c_str());
			if (!ok)
				continue;
			CHECK(info.channels == ch && info.sampleRate == 32000 && info.frames == (uint64_t) n, "%s/%u: info", c.name, ch);
			double worst = 0;
			for (int i = 0; i < n; i++) {
				worst = std::fmax(worst, std::fabs(l[i] - tone(i, 0)));
				worst = std::fmax(worst, std::fabs(r[i] - tone(i, ch == 1 ? 0 : 1)));
			}
			CHECK(worst <= c.tol, "%s bit, %u channels: error %g over %g", c.name, ch, worst, c.tol);
		}
}

void truncates() {
	TempDir t;
	std::string path = t.file("long.wav"), why;
	writeBytes(path, wavBytes(1, 2, 44100, 16, encode(1, 16, 2, 10000)));
	std::vector<float> l(10000, 9.f), r(10000, 9.f);
	WavInfo info;
	CHECK(rka::readWav(path, l.data(), r.data(), 4000, info, why), "%s", why.c_str());
	CHECK(info.frames == 4000, "read %llu frames", (unsigned long long) info.frames);
	CHECK(l[3999] != 9.f && l[4000] == 9.f && r[4000] == 9.f, "wrote past maxFrames");
	WavInfo p;
	CHECK(rka::probeWav(path, p, why) && p.frames == 10000, "probe still sees the whole file");
}

// Frequency (Hz) by rising zero crossings across the middle 80 %.
double measureHz(const std::vector<float>& y, double fs) {
	size_t a = y.size() / 10, b = y.size() - a;
	double first = 0, last = 0;
	int count = 0;
	for (size_t i = a; i + 1 < b; i++)
		if (y[i] < 0.f && y[i + 1] >= 0.f) {
			double pos = (double) i + (0. - y[i]) / (double) (y[i + 1] - y[i]);
			if (!count)
				first = pos;
			last = pos;
			count++;
		}
	return count > 1 ? (count - 1) / (last - first) * fs : 0.;
}
double rms(const std::vector<float>& y) {
	size_t a = y.size() / 10, b = y.size() - a;
	double s = 0;
	for (size_t i = a; i < b; i++)
		s += (double) y[i] * y[i];
	return std::sqrt(s / (double) (b - a));
}
std::vector<float> sine(double hz, double fs, size_t n, double amp) {
	std::vector<float> v(n);
	for (size_t i = 0; i < n; i++)
		v[i] = (float) (amp * std::sin(2 * 3.14159265358979323846 * hz * (double) i / fs + 0.3));
	return v;
}

void resample_pitch() {
	for (double inFs : {44100., 96000.}) {
		std::vector<float> in = sine(1000., inFs, (size_t) inFs, 0.5);
		double ratio = 48000. / inFs;
		uint64_t m = rka::resampledFrames(in.size(), ratio);
		std::vector<float> out(m);
		rka::resample(in.data(), in.size(), out.data(), m, ratio);
		double hz = measureHz(out, 48000.);
		double cents = 1200. * std::log2(hz / 1000.);
		CHECK(std::fabs(cents) < 1., "from %g: %g Hz (%g cents)", inFs, hz, cents);
		double db = 20. * std::log10(rms(out) / (0.5 / std::sqrt(2.)));
		CHECK(std::fabs(db) < 0.1, "from %g: level off by %g dB", inFs, db);
	}
	// Up, too: 22.05 kHz to 48 kHz.
	std::vector<float> in = sine(1000., 22050., 22050, 0.5);
	double ratio = 48000. / 22050.;
	uint64_t m = rka::resampledFrames(in.size(), ratio);
	std::vector<float> out(m);
	rka::resample(in.data(), in.size(), out.data(), m, ratio);
	CHECK(std::fabs(1200. * std::log2(measureHz(out, 48000.) / 1000.)) < 1., "upsampled pitch");
}

void resample_alias() {
	std::vector<float> in = sine(30000., 96000., 96000, 1.0);
	uint64_t m = rka::resampledFrames(in.size(), 0.5);
	std::vector<float> out(m);
	rka::resample(in.data(), in.size(), out.data(), m, 0.5);
	double db = 20. * std::log10(rms(out) / (1. / std::sqrt(2.)) + 1e-30);
	CHECK(db < -60., "a 30 kHz sine comes out at %g dB", db);
}

void resample_length() {
	CHECK(rka::resampledFrames(44100, 48000. / 44100.) == 48000, "44100 -> 48000");
	CHECK(rka::resampledFrames(96000, 0.5) == 48000, "96000 -> 48000");
	CHECK(rka::resampledFrames(13230000, 96000. / 44100.) == 28800000, "300 s at 44.1 kHz to 96 kHz");
	CHECK(rka::resampledFrames(10, 0.25) == 2 && rka::resampledFrames(1, 0.5) == 0 && rka::resampledFrames(0, 2.) == 0, "small");
	CHECK(rka::resampledFrames(100, 2.) == 200 && rka::resampledFrames(7, 1.) == 7, "exact ratios");
	// resample writes exactly outFrames, and 1:1 is a copy.
	std::vector<float> in(100), out(60, 5.f);
	for (auto& v : in)
		v = noise();
	rka::resample(in.data(), 100, out.data(), 50, 0.5);
	CHECK(out[49] != 5.f && out[50] == 5.f, "wrote past outFrames");
	std::vector<float> same(100, 5.f);
	rka::resample(in.data(), 100, same.data(), 100, 1.);
	double worst = 0;
	for (int i = 0; i < 100; i++)
		worst = std::fmax(worst, std::fabs(same[i] - in[i]));
	CHECK(worst < 1e-4, "1:1 is not a copy (%g)", worst);
}

void bad_files() {
	TempDir t;
	const int N = 64;
	std::vector<float> l(N), r(N);
	auto reset = [&] {
		std::fill(l.begin(), l.end(), 7.f);
		std::fill(r.begin(), r.end(), -7.f);
	};
	struct Bad {
		std::string name, path;
	};
	std::vector<Bad> bad;
	auto add = [&](const std::string& name, const std::string& bytes) {
		std::string p = t.file(name + ".wav");
		writeBytes(p, bytes);
		bad.push_back({name, p});
	};
	add("empty", "");
	std::string junk;
	for (int i = 0; i < 20; i++)
		junk += (char) (std::rand() & 255);
	add("random20", junk);
	std::string good = encode(1, 16, 2, 1000);
	add("half_data", wavBytes(1, 2, 44100, 16, good.substr(0, good.size() / 2), (int64_t) good.size()));
	add("huge_claim", wavBytes(1, 2, 44100, 16, good.substr(0, 400), 4000000000LL));
	add("zero_channels", wavBytes(1, 0, 44100, 16, good));
	add("rate_0", wavBytes(1, 2, 0, 16, good));
	add("rate_4M", wavBytes(1, 2, 4000000, 16, good));
	add("adpcm", wavBytes(2, 2, 44100, 4, good));
	add("odd_bits", wavBytes(1, 2, 44100, 12, good));
	add("no_audio", wavBytes(1, 2, 44100, 16, ""));
	bad.push_back({"folder", t.dir.u8string()});
	bad.push_back({"missing", t.file("not_there.wav")});

	long long before = gAllocBytes, worstOne = 0;
	for (const Bad& b : bad) {
		reset();
		long long start = gAllocBytes;
		WavInfo info;
		std::string why;
		bool ok = rka::readWav(b.path, l.data(), r.data(), N, info, why);
		bool ok2 = rka::probeWav(b.path, info, why);
		(void) ok2;
		CHECK(!ok, "%s was accepted", b.name.c_str());
		CHECK(!why.empty(), "%s: no reason given", b.name.c_str());
		bool kept = true;
		for (int i = 0; i < N; i++)
			kept = kept && l[i] == 7.f && r[i] == -7.f;
		CHECK(kept, "%s: the buffers were touched", b.name.c_str());
		worstOne = std::max(worstOne, gAllocBytes - start);
	}
	long long total = gAllocBytes - before;
	CHECK(total < 1000000, "bad files allocated %lld bytes (worst one %lld)", total, worstOne);

	// A name with non-ASCII characters is read like any other.
	std::string uni = t.file(u8"café 日本.wav");
	std::vector<float> a(N), b(N);
	for (int i = 0; i < N; i++)
		a[i] = b[i] = noise();
	std::string why;
	CHECK(rka::writeWav(uni, a.data(), b.data(), N, 48000, 1.f, why), "write unicode: %s", why.c_str());
	reset();
	WavInfo info;
	CHECK(rka::readWav(uni, l.data(), r.data(), N, info, why) && std::memcmp(l.data(), a.data(), N * sizeof(float)) == 0, "read unicode: %s", why.c_str());
}

}

int main() {
	round_trip();
	formats();
	truncates();
	resample_pitch();
	resample_alias();
	resample_length();
	bad_files();
	return rkatest::finish("eddy test_files");
}
