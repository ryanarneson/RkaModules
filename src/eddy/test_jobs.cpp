// Eddy's worker jobs (TapeJobs.hpp): loading a file, the patch's stored
// tape, storing, exporting and converting sets. Built with the sanitizers and
// the sanitizer's malloc and free hooks, which see every allocation (Tape's
// calloc, dr_wav's malloc, operator new): the bytes live now and the most
// that were live at once. The WAVs are made here, in a temporary folder that
// is deleted at the end.
#include "testing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#define RKA_WAV_IMPLEMENTATION
#include "eddy/TapeJobs.hpp"

// Declared here: not every compiler installs <sanitizer/allocator_interface.h>.
extern "C" int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void*, size_t),
                                                         void (*free_hook)(const volatile void*));
extern "C" size_t __sanitizer_get_allocated_size(const volatile void* p);

static long long gLive = 0, gPeak = 0;
static void mallocHook(const volatile void*, size_t n) {
	gLive += (long long) n;
	if (gLive > gPeak)
		gPeak = gLive;
}
static void freeHook(const volatile void* p) {
	if (p)
		gLive -= (long long) __sanitizer_get_allocated_size(p);
}

extern "C" const char* __asan_default_options() {
	return "detect_stack_use_after_return=0";
}

namespace fs = std::filesystem;
using rka::Tape;
using rka::TapeSet;
using rka::WavInfo;

namespace {

const double PI = 3.14159265358979323846;

struct TempDir {
	fs::path dir;
	TempDir() {
		dir = fs::temp_directory_path() / ("rka_eddy_jobs_" + std::to_string((long long) std::rand()) + "_" + std::to_string((long long) std::time(nullptr)));
		fs::create_directories(dir);
	}
	~TempDir() {
		std::error_code ec;
		fs::permissions(dir, fs::perms::owner_all, fs::perm_options::add, ec);
		fs::remove_all(dir, ec);
	}
	std::string file(const std::string& name) const {
		return (dir / fs::u8path(name)).u8string();
	}
	std::string sub(const std::string& name) const {
		fs::create_directories(dir / name);
		return (dir / name).u8string();
	}
};

uint32_t gRng = 777;
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

// A WAV image by hand (not through WavFile's writer): format tag, the fmt
// fields, the data chunk's claimed size (or the real one), and the data.
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
// 32-bit float frames, interleaved.
std::string floatData(const std::vector<float>& l, const std::vector<float>& r) {
	std::string d;
	for (size_t i = 0; i < l.size(); i++) {
		d.append((const char*) &l[i], 4);
		d.append((const char*) &r[i], 4);
	}
	return d;
}
std::string monoFloatData(const std::vector<float>& x) {
	std::string d;
	for (float v : x)
		d.append((const char*) &v, 4);
	return d;
}
std::string mono16Data(const std::vector<float>& x) {
	std::string d;
	for (float v : x)
		put16(d, (uint16_t) (int16_t) std::lround(v * 32767.f));
	return d;
}

void writeBytes(const std::string& path, const std::string& bytes) {
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (f) {
		std::fwrite(bytes.data(), 1, bytes.size(), f);
		std::fclose(f);
	}
}

bool sameBits(const float* a, const float* b, size_t n) {
	return std::memcmp(a, b, n * sizeof(float)) == 0;
}
bool sameTape(const Tape& a, const Tape& b) {
	return a.length == b.length && a.tail == b.tail && sameBits(a.ch[0], b.ch[0], a.length + a.tail)
		&& sameBits(a.ch[1], b.ch[1], a.length + a.tail);
}
// Frames [0, length + tail) of a set's tape from gen(k, side).
template <typename G>
void fill(Tape& t, size_t length, size_t tail, G gen) {
	for (size_t k = 0; k < length + tail; k++)
		for (int c = 0; c < 2; c++)
			t.ch[c][k] = gen(k, c);
	t.length = length;
	t.tail = tail;
}
TapeSet* newSet(float fs) {
	TapeSet* s = new TapeSet;
	s->allocate(fs);
	return s;
}

// The frequency from rising zero crossings over the middle 80 %.
double measureHz(const float* y, size_t n, double fs) {
	size_t a = n / 10, b = n - a;
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

// Units in the last place between two floats of the same sign.
long ulps(float a, float b) {
	int32_t x, y;
	std::memcpy(&x, &a, 4);
	std::memcpy(&y, &b, 4);
	return std::labs((long) x - (long) y);
}

// ---- Loading a file.

void load_levels() {
	TempDir t;
	// Full scale and some exact values, different on each side.
	std::vector<float> l = {1.f, -1.f, 0.5f, -0.25f, 0.f, 0.125f, -0.75f, 1.f};
	std::vector<float> r = {-1.f, 1.f, -0.5f, 0.25f, 0.0625f, 0.f, 0.75f, -1.f};
	const std::string stereo = t.file("levels.wav");
	writeBytes(stereo, wavBytes(3, 2, 48000, 32, floatData(l, r)));
	TapeSet* out = nullptr;
	std::string why;
	CHECK(rka::eddy::loadFile(stereo, 48000.f, out, why), "stereo: %s", why.c_str());
	if (out) {
		bool ok = out->tape.length == l.size() && out->tape.tail == 0 && out->fs == 48000.f;
		for (size_t i = 0; ok && i < l.size(); i++)
			ok = out->tape.ch[0][i] == 5.f * l[i] && out->tape.ch[1][i] == 5.f * r[i];
		CHECK(ok, "the tape is not the file x 5 V (frame 0: %g %g)", out->tape.ch[0][0], out->tape.ch[1][0]);
		CHECK(out->tape.ch[0][0] == 5.f && out->tape.ch[0][1] == -5.f, "+-1.0 is not +-5 V");
		CHECK(sameTape(out->tape, out->original), "the original is not the tape");
		CHECK(!out->tapeStale, "a loaded set is marked stale");
		float top = -1e9f, bottom = 1e9f;
		for (int c = 0; c < rka::Overview::COLS; c++) {
			top = std::max(top, out->overview.hi[c].load());
			bottom = std::min(bottom, out->overview.lo[c].load());
		}
		CHECK(top == 5.f && bottom == -5.f, "the overview is not built (%g..%g)", bottom, top);
		delete out;
	}

	// A mono file: the same on both sides.
	const std::string mono = t.file("mono.wav");
	writeBytes(mono, wavBytes(3, 1, 48000, 32, monoFloatData(l)));
	out = nullptr;
	CHECK(rka::eddy::loadFile(mono, 48000.f, out, why), "mono: %s", why.c_str());
	if (out) {
		bool ok = out->tape.length == l.size();
		for (size_t i = 0; ok && i < l.size(); i++)
			ok = out->tape.ch[0][i] == 5.f * l[i] && out->tape.ch[1][i] == 5.f * l[i];
		CHECK(ok, "mono is not on both sides at x 5");
		delete out;
	}
}

void load_rates() {
	TempDir t;
	for (uint32_t rate : {44100u, 96000u}) {
		const size_t n = rate;   // one second
		std::vector<float> x(n);
		for (size_t i = 0; i < n; i++)
			x[i] = (float) (0.5 * std::sin(2 * PI * 1000.0 * (double) i / rate + 0.3));
		const std::string path = t.file("sine" + std::to_string(rate) + ".wav");
		writeBytes(path, wavBytes(3, 1, rate, 32, monoFloatData(x)));
		TapeSet* out = nullptr;
		std::string why;
		CHECK(rka::eddy::loadFile(path, 48000.f, out, why), "%u Hz: %s", rate, why.c_str());
		if (!out)
			continue;
		CHECK(out->tape.length == rka::resampledFrames(n, 48000.0 / rate), "%u Hz: %zu frames", rate, out->tape.length);
		const double hz = measureHz(out->tape.ch[0], out->tape.length, 48000.0);
		const double cents = 1200.0 * std::log2(hz / 1000.0);
		CHECK(std::fabs(cents) < 1.0, "%u Hz: %g Hz (%g cents)", rate, hz, cents);
		double peak = 0;
		for (size_t i = out->tape.length / 10; i < out->tape.length * 9 / 10; i++)
			peak = std::max(peak, (double) std::fabs(out->tape.ch[1][i]));
		CHECK(std::fabs(peak - 2.5) < 0.05, "%u Hz: peak %g V, not 2.5 V", rate, peak);
		CHECK(sameTape(out->tape, out->original), "%u Hz: the original is not the tape", rate);
		delete out;
	}
}

// One set's bytes at rate fs: two tapes of two sides, capacity, tail and guards.
long long setBytes(float fs) {
	TapeSet s;
	s.allocate(fs);
	return (long long) sizeof(TapeSet)
		+ 4LL * (long long) (s.tape.capacity() + s.tape.tailCapacity() + 2 * Tape::GUARD) * (long long) sizeof(float);
}

void load_long() {
	TempDir t;
	const float fs = 8000.f;
	const size_t cap = (size_t) (rka::eddy::TAPE_MAX_S * fs);
	const size_t n = cap + 20000;   // 2.5 s over
	std::vector<float> x(n);
	for (size_t i = 0; i < n; i++)
		x[i] = (float) ((double) (i % 1000) / 1000.0 - 0.5);
	const std::string path = t.file("long.wav");
	writeBytes(path, wavBytes(1, 1, 8000, 16, mono16Data(x)));
	x.clear();
	x.shrink_to_fit();

	const long long one = setBytes(fs);
	const long long base = gLive;
	gPeak = gLive;
	TapeSet* out = nullptr;
	std::string why;
	CHECK(rka::eddy::loadFile(path, fs, out, why), "%s", why.c_str());
	const long long peak = gPeak - base;
	CHECK(peak <= one + (1 << 20), "loading took %lld bytes at most, one set is %lld", peak, one);
	if (out) {
		CHECK(out->tape.length == cap && out->original.length == cap, "%zu frames, capacity %zu", out->tape.length, cap);
		CHECK(std::fabs(out->tape.ch[0][cap - 1] - 5.f * ((float) ((cap - 1) % 1000) / 1000.f - 0.5f)) < 1e-3f, "the last frame");
		delete out;
	}
	CHECK(gLive == base, "%lld bytes left after the set is freed", gLive - base);

	// At another rate: one set, and the part of the file the conversion
	// needs (twice the capacity at half the rate), never the whole file
	// (three times that here). A 1 kHz engine keeps the file small.
	const float slow = 1000.f;
	const size_t cap1 = (size_t) (rka::eddy::TAPE_MAX_S * slow);
	const size_t n2 = 3 * 2 * cap1;
	std::vector<float> y(n2);
	for (size_t i = 0; i < n2; i++)
		y[i] = (float) (0.5 * std::sin(2 * PI * 50.0 * (double) i / 2000.0));
	const std::string path2 = t.file("long2k.wav");
	writeBytes(path2, wavBytes(1, 1, 2000, 16, mono16Data(y)));
	y.clear();
	y.shrink_to_fit();
	const long long one1 = setBytes(slow);
	const long long needed = 2LL * (long long) sizeof(float) * (long long) (2 * cap1 + 200);
	const long long base2 = gLive;
	gPeak = gLive;
	out = nullptr;
	CHECK(rka::eddy::loadFile(path2, slow, out, why), "%s", why.c_str());
	const long long peak2 = gPeak - base2;
	CHECK(peak2 <= one1 + needed + (1 << 20), "loading at another rate took %lld bytes, one set and the part needed are %lld", peak2,
		one1 + needed);
	if (out) {
		CHECK(out->tape.length == cap1, "%zu frames, capacity %zu", out->tape.length, cap1);
		const double hz = measureHz(out->tape.ch[0], out->tape.length, slow);
		CHECK(std::fabs(1200.0 * std::log2(hz / 50.0)) < 1.0, "%g Hz, not 50", hz);
		delete out;
	}
	CHECK(gLive == base2, "%lld bytes left after the set is freed", gLive - base2);
}

// Each bad file of Task 2's test (WavFile): refused, `out` untouched, the
// reason given, nothing left allocated and nothing near a tape allocated.
void load_bad() {
	TempDir t;
	std::vector<std::pair<std::string, std::string>> bad;
	auto add = [&](const std::string& name, const std::string& bytes) {
		const std::string p = t.file(name + ".wav");
		writeBytes(p, bytes);
		bad.push_back({name, p});
	};
	std::string good;
	for (int i = 0; i < 2000; i++)
		put16(good, (uint16_t) (int16_t) (i * 7));
	add("empty", "");
	std::string junk;
	for (int i = 0; i < 20; i++)
		junk += (char) (std::rand() & 255);
	add("random20", junk);
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

	TapeSet sentinel;
	std::string why;
	why.reserve(4096);
	{
		// Once first, so what the C library keeps for itself after a first
		// call (stat, fopen) is not counted against a file.
		TapeSet* out = &sentinel;
		rka::eddy::loadFile(bad[0].second, 48000.f, out, why);
	}
	for (const auto& b : bad) {
		TapeSet* out = &sentinel;
		// The reason's own string is made and freed inside the count.
		const long long base = gLive;
		gPeak = gLive;
		bool ok, gave;
		{
			std::string reason;
			ok = rka::eddy::loadFile(b.second, 48000.f, out, reason);
			gave = !reason.empty();
		}
		const long long left = gLive - base, peak = gPeak - base;
		CHECK(!ok, "%s was accepted", b.first.c_str());
		CHECK(out == &sentinel, "%s: out was changed", b.first.c_str());
		CHECK(gave, "%s: no reason", b.first.c_str());
		CHECK(left == 0, "%s: %lld bytes left allocated", b.first.c_str(), left);
		CHECK(peak < (1 << 20), "%s: %lld bytes allocated on the way", b.first.c_str(), peak);
		if (ok && out != &sentinel)
			delete out;
	}

	// A file too short to give one frame at this rate.
	const std::string tiny = t.file("tiny.wav");
	writeBytes(tiny, wavBytes(3, 1, 96000, 32, monoFloatData({0.5f})));
	TapeSet* out = &sentinel;
	CHECK(!rka::eddy::loadFile(tiny, 8000.f, out, why) && out == &sentinel && !why.empty(), "one frame at 96 kHz into 8 kHz");

	// A name with non-ASCII characters loads like any other.
	const std::string uni = t.file(u8"café 日本.wav");
	writeBytes(fs::u8path(uni).string(), wavBytes(3, 1, 48000, 32, monoFloatData({0.5f, -0.5f})));
	out = nullptr;
	CHECK(rka::eddy::loadFile(uni, 48000.f, out, why) && out && out->tape.length == 2, "non-ASCII name: %s", why.c_str());
	delete out;
}

// ---- Export.

void export_round_trip() {
	TempDir t;
	const float fs = 44100.f;
	TapeSet* set = newSet(fs);
	const size_t n = 3000;
	fill(set->tape, n, 0, [](size_t k, int c) { return k == 100 ? (c ? -9.f : 9.f) : 4.9f * noise(); });
	const std::string path = t.file("out.wav");
	std::string why;
	CHECK(rka::eddy::exportTape(path, *set, fs, why), "export: %s", why.c_str());
	CHECK(!fs::exists(path + ".tmp"), "the .tmp file is left behind");

	// The file itself: 32-bit float stereo at the rate, the tape / 5.
	WavInfo info;
	CHECK(rka::probeWav(path, info, why) && info.channels == 2 && info.sampleRate == 44100 && info.frames == n, "the file's format");
	std::vector<float> l(n), r(n);
	CHECK(rka::readWav(path, l.data(), r.data(), n, info, why), "%s", why.c_str());
	CHECK(l[100] == 9.f * 0.2f && r[100] == -9.f * 0.2f, "9 V is %g in the file (clipped?)", l[100]);

	// Loaded again: the tape, within one unit in the last place (x / 5 x 5
	// in float is not always x: see the report), and 9 V is 9 V.
	TapeSet* back = nullptr;
	CHECK(rka::eddy::loadFile(path, fs, back, why), "load: %s", why.c_str());
	if (back) {
		CHECK(back->tape.length == n, "%zu frames back", back->tape.length);
		long worst = 0;
		for (size_t k = 0; k < n && back->tape.length == n; k++)
			for (int c = 0; c < 2; c++)
				worst = std::max(worst, ulps(back->tape.ch[c][k], set->tape.ch[c][k]));
		CHECK(worst <= 1, "loaded back %ld units in the last place out", worst);
		CHECK(std::fabs(back->tape.ch[0][100] - 9.f) < 1e-5f && std::fabs(back->tape.ch[1][100] + 9.f) < 1e-5f, "9 V came back as %g",
			back->tape.ch[0][100]);
		delete back;
	}

	// An empty tape is not exported.
	TapeSet* empty = newSet(fs);
	CHECK(!rka::eddy::exportTape(t.file("empty.wav"), *empty, fs, why) && !why.empty(), "an empty tape was exported");
	CHECK(!fs::exists(t.file("empty.wav")), "a file was made for an empty tape");
	delete empty;
	delete set;
}

// ---- The patch's storage.

// A set whose tape and original differ, both with tails.
TapeSet* storedSet(float fs, size_t n, size_t tapeTail, size_t origTail) {
	TapeSet* s = newSet(fs);
	fill(s->tape, n, tapeTail, [](size_t, int) { return 7.f * noise(); });
	fill(s->original, n, origTail, [](size_t k, int c) { return (float) (3.0 * std::sin(0.01 * (double) k + c)); });
	s->overview.build(s->tape);
	return s;
}

void stored_round_trip() {
	TempDir t;
	const float fs = 48000.f;
	TapeSet* set = storedSet(fs, 24000, 480, 300);
	const std::string dir = t.sub("store");
	std::string why;
	CHECK(rka::eddy::storeTape(dir, *set, fs, true, true, why), "store: %s", why.c_str());
	TapeSet* back = nullptr;
	CHECK(rka::eddy::loadStored(dir, fs, back, why), "load: %s", why.c_str());
	if (back) {
		CHECK(sameTape(back->tape, set->tape), "the tape differs (length %zu tail %zu)", back->tape.length, back->tape.tail);
		CHECK(sameTape(back->original, set->original), "the original differs (length %zu tail %zu)", back->original.length,
			back->original.tail);
		CHECK(why.empty(), "a note on a whole store: %s", why.c_str());
		CHECK(back->fs == fs && !back->tapeStale, "fs or stale flag");
		bool built = false;
		for (int c = 0; c < rka::Overview::COLS; c++)
			built = built || back->overview.hi[c].load() != 0.f;
		CHECK(built, "the overview is not built");
		delete back;
	}

	// Golden: the stored format, made by hand (not by storeTape): tape.wav
	// and original.wav are 32-bit float stereo in volts, length + tail
	// frames; tails.wav is one frame, the tape's tail on the left and the
	// original's on the right.
	const std::string gold = t.sub("golden");
	std::vector<float> tl = {1.f, 2.f, 3.f, 4.f, 9.5f, -9.5f}, tr = {-1.f, -2.f, -3.f, -4.f, 0.5f, 0.25f};
	std::vector<float> ol = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}, orr = {6.f, 5.f, 4.f, 3.f, 2.f, 1.f};
	writeBytes(gold + "/tape.wav", wavBytes(3, 2, 48000, 32, floatData(tl, tr)));
	writeBytes(gold + "/original.wav", wavBytes(3, 2, 48000, 32, floatData(ol, orr)));
	writeBytes(gold + "/tails.wav", wavBytes(3, 2, 48000, 32, floatData({2.f}, {2.f})));
	back = nullptr;
	CHECK(rka::eddy::loadStored(gold, fs, back, why), "golden: %s", why.c_str());
	if (back) {
		CHECK(back->tape.length == 4 && back->tape.tail == 2 && back->original.length == 4 && back->original.tail == 2, "golden lengths");
		bool ok = true;
		for (size_t k = 0; k < 6; k++)
			ok = ok && back->tape.ch[0][k] == tl[k] && back->tape.ch[1][k] == tr[k] && back->original.ch[0][k] == ol[k]
				&& back->original.ch[1][k] == orr[k];
		CHECK(ok, "golden values (stored volts are not scaled)");
		delete back;
	}
	delete set;
}

void stored_unchanged_not_written() {
	TempDir t;
	const float fs = 48000.f;
	TapeSet* set = storedSet(fs, 4800, 480, 480);
	const std::string dir = t.sub("store");
	std::string why;
	CHECK(rka::eddy::storeTape(dir, *set, fs, true, true, why), "%s", why.c_str());
	const char* names[3] = {"tape.wav", "original.wav", "tails.wav"};
	// Set every file's time well back, so a write shows.
	const auto old = fs::file_time_type::clock::now() - std::chrono::hours(48);
	for (const char* n : names)
		fs::last_write_time(fs::path(dir) / n, old);
	CHECK(rka::eddy::storeTape(dir, *set, fs, false, false, why), "%s", why.c_str());
	for (const char* n : names)
		CHECK(fs::last_write_time(fs::path(dir) / n) == old, "%s was written with both flags off", n);
	// The tape alone: the original's file stays as it was.
	CHECK(rka::eddy::storeTape(dir, *set, fs, true, false, why), "%s", why.c_str());
	CHECK(fs::last_write_time(fs::path(dir) / "tape.wav") != old, "the tape was not written");
	CHECK(fs::last_write_time(fs::path(dir) / "original.wav") == old, "the original was written with its flag off");
	delete set;
}

// Review Focus 5: patch storage that is partial or wrong.
void stored_partial() {
	TempDir t;
	const float fs = 48000.f;
	TapeSet* set = storedSet(fs, 9600, 480, 480);
	std::string why;
	auto stored = [&](const std::string& name) {
		const std::string d = t.sub(name);
		CHECK(rka::eddy::storeTape(d, *set, fs, true, true, why), "%s: %s", name.c_str(), why.c_str());
		return d;
	};
	auto both = [](const TapeSet* s, const Tape& want) { return s && sameTape(s->tape, want) && sameTape(s->original, want); };

	// Only tape.wav: it serves as both.
	{
		const std::string d = stored("only_tape");
		fs::remove(fs::path(d) / "original.wav");
		TapeSet* out = nullptr;
		CHECK(rka::eddy::loadStored(d, fs, out, why) && both(out, set->tape), "only tape.wav: %s", why.c_str());
		CHECK(!why.empty(), "only tape.wav: no note");
		delete out;
	}
	// Only original.wav: it serves as both.
	{
		const std::string d = stored("only_original");
		fs::remove(fs::path(d) / "tape.wav");
		TapeSet* out = nullptr;
		CHECK(rka::eddy::loadStored(d, fs, out, why) && both(out, set->original), "only original.wav: %s", why.c_str());
		CHECK(!why.empty(), "only original.wav: no note");
		delete out;
	}
	// The two at different lengths: the tape wins, as both.
	{
		const std::string d = stored("lengths");
		TapeSet* shorter = storedSet(fs, 4800, 480, 480);
		CHECK(rka::eddy::storeTape(d, *shorter, fs, false, true, why), "%s", why.c_str());
		TapeSet* out = nullptr;
		CHECK(rka::eddy::loadStored(d, fs, out, why) && both(out, set->tape), "different lengths: %s", why.c_str());
		CHECK(!why.empty(), "different lengths: no note");
		delete out;
		delete shorter;
	}
	// The two at different rates: the tape wins (converted), as both.
	{
		const std::string d = stored("rates");
		std::vector<float> x(9600, 1.f);
		CHECK(rka::writeWav(d + "/original.wav", x.data(), x.data(), x.size(), 44100, 1.f, why), "%s", why.c_str());
		TapeSet* out = nullptr;
		CHECK(rka::eddy::loadStored(d, fs, out, why) && both(out, set->tape), "different rates: %s", why.c_str());
		CHECK(!why.empty(), "different rates: no note");
		delete out;
	}
	// One cut to half its bytes (a crash part way through a write): the
	// other serves as both.
	for (int which = 0; which < 2; which++) {
		const std::string d = stored(which ? "cut_original" : "cut_tape");
		const fs::path p = fs::path(d) / (which ? "original.wav" : "tape.wav");
		fs::resize_file(p, fs::file_size(p) / 2);
		TapeSet* out = nullptr;
		CHECK(rka::eddy::loadStored(d, fs, out, why) && both(out, which ? set->tape : set->original), "%s cut: %s",
			which ? "original" : "tape", why.c_str());
		CHECK(!why.empty(), "a cut file: no note");
		delete out;
	}
	// Both cut: nothing to load.
	{
		const std::string d = stored("cut_both");
		for (const char* n : {"tape.wav", "original.wav"}) {
			const fs::path p = fs::path(d) / n;
			fs::resize_file(p, fs::file_size(p) / 2);
		}
		TapeSet sentinel;
		TapeSet* out = &sentinel;
		CHECK(!rka::eddy::loadStored(d, fs, out, why) && out == &sentinel && !why.empty(), "both cut");
	}
	// tails.wav missing: loads, every frame counted as tape.
	{
		const std::string d = stored("no_tails");
		fs::remove(fs::path(d) / "tails.wav");
		TapeSet* out = nullptr;
		CHECK(rka::eddy::loadStored(d, fs, out, why) && out && out->tape.length == 9600 + 480 && out->tape.tail == 0,
			"no tails.wav: %s", why.c_str());
		CHECK(!why.empty(), "no tails.wav: no note");
		delete out;
	}
	// An empty folder, and a missing one: false, out untouched, a reason,
	// and nothing left allocated once the reason is gone (it is a string of
	// its own, freed before the count: a reused one keeps what it grew to).
	for (const std::string& d : {t.sub("empty"), t.file("missing")}) {
		TapeSet sentinel;
		TapeSet* out = &sentinel;
		const long long base = gLive;
		{
			std::string reason;
			CHECK(!rka::eddy::loadStored(d, fs, out, reason) && out == &sentinel && !reason.empty(), "%s", d.c_str());
		}
		CHECK(gLive == base, "%lld bytes left", gLive - base);
	}
	// A folder that cannot be written: false and a reason, no crash; what
	// was there is still there and still loads.
	{
		const std::string d = stored("read_only");
		fs::permissions(d, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
		// Root (some CI containers) writes anyway: then there is nothing to test.
		const std::string probe = d + "/probe";
		std::FILE* f = std::fopen(probe.c_str(), "wb");
		if (f) {
			std::fclose(f);
			std::remove(probe.c_str());
			std::printf("stored_partial: the read-only folder is writable here (root?); that case is not tested\n");
		}
		else {
			TapeSet* other = storedSet(fs, 4800, 0, 0);
			why.clear();
			CHECK(!rka::eddy::storeTape(d, *other, fs, true, true, why) && !why.empty(), "a read-only folder was written");
			delete other;
		}
		TapeSet* out = nullptr;
		CHECK(rka::eddy::loadStored(d, fs, out, why) && out && sameTape(out->tape, set->tape), "the read-only folder no longer loads");
		delete out;
		fs::permissions(d, fs::perms::owner_all, fs::perm_options::add);
	}
	delete set;
}

// ---- Converting.

void convert_keeps_both() {
	const float from = 44100.f, to = 48000.f;
	TapeSet* set = storedSet(from, 44100, 441, 441);
	TapeSet* out = nullptr;
	std::string why;
	CHECK(rka::eddy::convert(*set, from, to, out, why), "%s", why.c_str());
	if (out) {
		const double ratio = (double) to / (double) from;
		CHECK(out->fs == to, "fs %g", out->fs);
		CHECK(out->tape.length == 48000 && out->original.length == 48000, "lengths %zu %zu", out->tape.length, out->original.length);
		CHECK(out->tape.tail == 480 && out->original.tail == 480, "tails %zu %zu", out->tape.tail, out->original.tail);
		CHECK(!sameBits(out->tape.ch[0], out->original.ch[0], 48000), "the tape and the original came out the same");
		// Each is its own source converted.
		for (int which = 0; which < 2; which++) {
			const Tape& src = which ? set->original : set->tape;
			const Tape& got = which ? out->original : out->tape;
			std::vector<float> want(48480);
			rka::resample(src.ch[1], src.length + src.tail, want.data(), want.size(), ratio);
			CHECK(sameBits(got.ch[1], want.data(), want.size()), "%s is not its source converted", which ? "original" : "tape");
		}
		// The original's sine keeps its pitch.
		const double hz = measureHz(out->original.ch[0], 48000, to);
		const double want = 0.01 * from / (2 * PI);
		CHECK(std::fabs(1200.0 * std::log2(hz / want)) < 1.0, "original's pitch %g, want %g", hz, want);
		bool built = false;
		for (int c = 0; c < rka::Overview::COLS; c++)
			built = built || out->overview.hi[c].load() != 0.f;
		CHECK(built, "the overview is not built");
		delete out;
	}

	// A stale set: the tape is made from the original.
	set->tapeStale = true;
	out = nullptr;
	CHECK(rka::eddy::convert(*set, from, to, out, why) && out && sameTape(out->tape, out->original) && !out->tapeStale,
		"a stale set's tape is not its original");
	delete out;
	set->tapeStale = false;

	// The same rate: a copy, to the bit.
	out = nullptr;
	CHECK(rka::eddy::convert(*set, from, from, out, why) && out && sameTape(out->tape, set->tape) && sameTape(out->original, set->original),
		"a conversion to the same rate is not a copy");
	delete out;

	// An empty set converts to an empty set; an unusable rate is refused.
	TapeSet* empty = newSet(from);
	out = nullptr;
	CHECK(rka::eddy::convert(*empty, from, to, out, why) && out && out->tape.length == 0 && out->fs == to, "empty");
	delete out;
	TapeSet sentinel;
	out = &sentinel;
	CHECK(!rka::eddy::convert(*set, from, 0.f, out, why) && out == &sentinel && !why.empty(), "rate 0 accepted");
	delete empty;
	delete set;
}

}

namespace {

// ---- The save's decision (StoreRecord, planSave, savedAs).

rka::eddy::SaveFacts facts(uint32_t t, uint32_t o, float fs) {
	rka::eddy::SaveFacts f;
	f.haveSet = f.known = true;
	f.tapeLength = f.originalLength = 1000;
	f.tapeChanges = t;
	f.originalChanges = o;
	f.fs = fs;
	return f;
}

void save_plan() {
	using rka::eddy::SavePlan;
	using rka::eddy::StoreRecord;
	StoreRecord r;
	// Nothing known about the files: both written.
	rka::eddy::SaveFacts f = facts(5, 7, 48000.f);
	SavePlan p = rka::eddy::planSave(f, r);
	CHECK(!p.remove && p.tape && p.original && !p.tapeFromOriginal, "a first save writes both");
	rka::eddy::savedAs(r, f, p, true);
	p = rka::eddy::planSave(f, r);
	CHECK(!p.remove && !p.tape && !p.original, "nothing changed: nothing written");
	// The tape moves alone: the tape alone.
	f.tapeChanges = 6;
	p = rka::eddy::planSave(f, r);
	CHECK(p.tape && !p.original, "the tape's counter moved: the tape alone");
	// A failed write forgets: both next time.
	rka::eddy::savedAs(r, f, p, false);
	p = rka::eddy::planSave(f, r);
	CHECK(p.tape && p.original, "after a failed write: both");
	rka::eddy::savedAs(r, f, p, true);
	// I1: while the stored tape loads, nothing (above all no removal), even
	// with an empty tape live.
	rka::eddy::SaveFacts loading = f;
	loading.loading = true;
	loading.tapeLength = loading.originalLength = 0;
	p = rka::eddy::planSave(loading, r);
	CHECK(!p.remove && !p.tape && !p.original, "a save while the stored tape loads touched the folder");
	// No live set: nothing.
	rka::eddy::SaveFacts none = f;
	none.haveSet = false;
	p = rka::eddy::planSave(none, r);
	CHECK(!p.remove && !p.tape && !p.original, "no live set");
	// An empty tape, or Keep off: the folder removed, the record forgotten.
	rka::eddy::SaveFacts empty = f;
	empty.tapeLength = empty.originalLength = 0;
	p = rka::eddy::planSave(empty, r);
	CHECK(p.remove, "an empty tape is not removed");
	rka::eddy::SaveFacts off = f;
	off.keepTape = false;
	p = rka::eddy::planSave(off, r);
	CHECK(p.remove, "Keep off is not removed");
	rka::eddy::savedAs(r, off, p, true);
	p = rka::eddy::planSave(f, r);
	CHECK(p.tape && p.original, "after a removal: both");
	rka::eddy::savedAs(r, f, p, true);
	// I2: another rate (the record's files are at the old one): both.
	rka::eddy::SaveFacts other = f;
	other.fs = 44100.f;
	p = rka::eddy::planSave(other, r);
	CHECK(p.tape && p.original, "at another rate only one file was written");
	// Counters not known to be the live set's: both.
	rka::eddy::SaveFacts unknown = f;
	unknown.known = false;
	p = rka::eddy::planSave(unknown, r);
	CHECK(p.tape && p.original, "unknown counters");
	rka::eddy::savedAs(r, unknown, p, true);
	p = rka::eddy::planSave(f, r);
	CHECK(p.tape && p.original, "a save with unknown counters marked the files current");
	rka::eddy::savedAs(r, f, p, true);
	// M1: the tape is to become the original (a recording's copy pending):
	// tape.wav from the original, and again once the copy has moved the
	// tape's counter.
	rka::eddy::SaveFacts behind = f;
	behind.readsOriginal = true;
	behind.originalChanges = f.originalChanges + 1;
	behind.tapeLength = 0;   // a first recording: the tape itself is still empty
	p = rka::eddy::planSave(behind, r);
	CHECK(!p.remove && p.tape && p.tapeFromOriginal && p.original, "a recording's copy pending: tape from the original");
	rka::eddy::savedAs(r, behind, p, true);
	rka::eddy::SaveFacts copied = behind;
	copied.readsOriginal = false;
	copied.tapeLength = 1000;
	p = rka::eddy::planSave(copied, r);
	CHECK(p.tape && !p.original, "after the copy the tape is written again from itself, the original kept");
	// The baseline of a whole load: nothing to write.
	StoreRecord w;
	rka::eddy::loadedWhole(w, 9, 3, 48000.f);
	p = rka::eddy::planSave(facts(9, 3, 48000.f), w);
	CHECK(!p.tape && !p.original && !p.remove, "a whole load is written again");
}

// M6 and M3: what the folder holds beyond the record of current files. A
// stored tape that failed to load is left while the tape is empty (and
// replaced once there is a tape to store); a stored tape loaded cut short by
// the capacity is left while the tape is unchanged at that rate. Either is
// forgotten by the next write or removal.
void save_plan_held_keep() {
	using rka::eddy::SavePlan;
	using rka::eddy::StoreRecord;
	StoreRecord r;
	rka::eddy::loadFailed(r);
	rka::eddy::SaveFacts empty = facts(3, 3, 48000.f);
	empty.tapeLength = empty.originalLength = 0;
	SavePlan p = rka::eddy::planSave(empty, r);
	CHECK(!p.remove && !p.tape && !p.original, "a stored tape that failed to load: an empty tape leaves the folder alone");
	rka::eddy::SaveFacts off = empty;
	off.keepTape = false;
	CHECK(rka::eddy::planSave(off, r).remove, "Keep off still removes it");
	const rka::eddy::SaveFacts made = facts(4, 4, 48000.f);
	p = rka::eddy::planSave(made, r);
	CHECK(p.tape && p.original && !p.remove, "a tape made since is stored over it");
	rka::eddy::savedAs(r, made, p, true);
	CHECK(!r.held, "the write forgets it");
	CHECK(rka::eddy::planSave(empty, r).remove, "after that an empty tape removes the folder");
	// Cut: unchanged at its rate, nothing; changed, or at another rate, or
	// to become the original, both written.
	StoreRecord c;
	rka::eddy::loadedCut(c, 9, 3, 96000.f);
	CHECK(!c.tapeCurrent && !c.originalCurrent && c.keep, "a cut load is not current but kept");
	p = rka::eddy::planSave(facts(9, 3, 96000.f), c);
	CHECK(!p.remove && !p.tape && !p.original, "a cut load, unchanged: the longer stored tape is left");
	p = rka::eddy::planSave(facts(10, 3, 96000.f), c);
	CHECK(p.tape && p.original, "a cut load dubbed: both written");
	p = rka::eddy::planSave(facts(9, 3, 48000.f), c);
	CHECK(p.tape && p.original, "a cut load at another rate: both written");
	rka::eddy::SaveFacts behind = facts(9, 3, 96000.f);
	behind.readsOriginal = true;
	p = rka::eddy::planSave(behind, c);
	CHECK(p.tape && p.original, "a cut load to become the original (a recording): both written");
	rka::eddy::savedAs(c, facts(10, 3, 96000.f), rka::eddy::planSave(facts(10, 3, 96000.f), c), true);
	CHECK(!c.keep, "the write forgets it");
	// loadedWhole after a held record: the folder is current, not held.
	StoreRecord w;
	rka::eddy::loadFailed(w);
	rka::eddy::loadedWhole(w, 1, 1, 48000.f);
	CHECK(!w.held && w.tapeCurrent, "a whole load replaces a held record");
}

// M3: a tape held short by the capacity is reported as cut, by the
// conversion and by reading a file; one that fits is not.
void cut_reported() {
	Tape from, to;
	from.allocate(1000, 10);
	to.allocate(1500, 10);
	fill(from, 1000, 10, [](size_t k, int) { return (float) std::sin(0.01 * (double) k); });
	bool cut = false;
	rka::eddy::jobs::convertTape(to, from, 1.4, &cut);
	CHECK(!cut && to.length == 1400, "1000 frames x 1.4 into 1500: not cut (%zu)", to.length);
	rka::eddy::jobs::convertTape(to, from, 2.0, &cut);
	CHECK(cut && to.length == 1500, "1000 frames x 2 into 1500: cut to the capacity (%zu)", to.length);
	TempDir t;
	std::vector<float> x(1000, 0.1f);
	const std::string path = t.file("cut.wav");
	writeBytes(path, wavBytes(3, 2, 8000, 32, floatData(x, x)));
	rka::WavInfo info;
	std::string why;
	CHECK(rka::probeWav(path, info, why), "%s", why.c_str());
	Tape small;
	small.allocate(800, 10);
	cut = false;
	CHECK(rka::eddy::jobs::readInto(small, path, info, 0, 8000.f, why, &cut), "%s", why.c_str());
	CHECK(cut && small.length == 800, "1000 frames read into 800: cut (%zu)", small.length);
	Tape big;
	big.allocate(4000, 10);
	cut = false;
	CHECK(rka::eddy::jobs::readInto(big, path, info, 0, 16000.f, why, &cut), "%s", why.c_str());
	CHECK(!cut && big.length == 2000, "1000 frames at 8 kHz read at 16 kHz into 4000: not cut (%zu)", big.length);
	small.length = 0;
	CHECK(rka::eddy::jobs::readInto(small, path, info, 0, 16000.f, why, &cut), "%s", why.c_str());
	CHECK(cut && small.length == 800, "the same into 800 at 16 kHz: cut (%zu)", small.length);
}

// M7: a float file's samples that overflow at x 5 (1e38), and infinities
// and NaN, load as finite volts held to +-100 V; ordinary ones as before.
void load_huge() {
	TempDir t;
	std::vector<float> l = {1e38f, -1e38f, INFINITY, -INFINITY, NAN, 0.5f, 30.f, -0.25f};
	std::vector<float> r = {0.5f, 1e38f, 1.f, -1.f, 0.f, -0.5f, 1e-3f, 3e38f};
	const std::string path = t.file("huge.wav");
	writeBytes(path, wavBytes(3, 2, 48000, 32, floatData(l, r)));
	TapeSet* s = nullptr;
	std::string why;
	CHECK(rka::eddy::loadFile(path, 48000.f, s, why) && s, "%s", why.c_str());
	if (!s)
		return;
	bool ok = s->tape.length == l.size();
	for (size_t i = 0; i < s->tape.length; i++)
		for (int c = 0; c < 2; c++) {
			const float v = s->tape.ch[c][i];
			ok = ok && std::isfinite(v) && std::fabs(v) <= 100.f;
		}
	CHECK(ok, "every sample is finite and within 100 V");
	CHECK(s->tape.ch[0][0] == 100.f && s->tape.ch[0][1] == -100.f && s->tape.ch[1][1] == 100.f, "1e38 holds at +-100 V (%g, %g)",
	      s->tape.ch[0][0], s->tape.ch[0][1]);
	CHECK(s->tape.ch[0][4] == 0.f && s->tape.ch[0][2] == 0.f, "NaN and infinity read as 0 V");
	CHECK(s->tape.ch[0][5] == 2.5f && s->tape.ch[1][5] == -2.5f && s->tape.ch[0][6] == 100.f, "ordinary samples x 5, 30 held to 100 V");
	CHECK(sameTape(s->tape, s->original), "the original is the tape");
	delete s;
}

// I2: stored at one rate, loaded at another, the tape dubbed, saved, loaded
// again: the tape and the original still differ and each is right.
void stored_other_rate() {
	TempDir t;
	const std::string dir = t.sub("rates");
	TapeSet* set = storedSet(8000.f, 4000, 80, 80);
	std::string why;
	CHECK(rka::eddy::storeTape(dir, *set, 8000.f, true, true, why), "%s", why.c_str());
	bool whole = true;
	TapeSet* a = nullptr;
	CHECK(rka::eddy::loadStored(dir, 11025.f, a, why, whole) && a, "load at 11025: %s", why.c_str());
	CHECK(!whole, "a load at another rate is called whole");
	if (!a) {
		delete set;
		return;
	}
	// As the module: a whole load is the baseline; this one is not.
	rka::eddy::StoreRecord r;
	if (whole)
		rka::eddy::loadedWhole(r, 1, 1, 11025.f);
	// A dub: the tape changes, the original not.
	for (size_t k = 0; k < a->tape.length; k++)
		a->tape.ch[0][k] += 0.5f;
	rka::eddy::SaveFacts f = facts(2, 1, 11025.f);
	f.tapeLength = a->tape.length;
	f.originalLength = a->original.length;
	const rka::eddy::SavePlan p = rka::eddy::planSave(f, r);
	CHECK(p.tape && p.original, "after a converted load the save writes the tape alone");
	CHECK(rka::eddy::storePlanned(dir, *a, 11025.f, p, why), "%s", why.c_str());
	TapeSet* b = nullptr;
	whole = false;
	CHECK(rka::eddy::loadStored(dir, 11025.f, b, why, whole) && b, "reload: %s", why.c_str());
	if (b) {
		CHECK(whole && why.empty(), "the reload is not whole: %s", why.c_str());
		CHECK(sameTape(b->tape, a->tape), "the dubbed tape did not come back");
		CHECK(sameTape(b->original, a->original), "the original did not come back (the tape replaced it?)");
		CHECK(!sameBits(b->tape.ch[0], b->original.ch[0], b->tape.length), "tape and original are the same");
		delete b;
	}
	// And the same-rate load is whole.
	TapeSet* c = nullptr;
	CHECK(rka::eddy::loadStored(dir, 11025.f, c, why, whole) && whole, "a same-rate whole store is not whole");
	delete c;
	delete a;
	delete set;
}

// M1: a save between a recording's end and its copy to the tape stores the
// new recording as both, so a reload keeps it.
void stored_recording_copy() {
	TempDir t;
	const std::string dir = t.sub("rec");
	TapeSet* set = storedSet(48000.f, 4800, 480, 480);   // tape: the old take, original: the new one
	rka::eddy::StoreRecord r;
	rka::eddy::loadedWhole(r, 4, 4, 48000.f);   // the files held the old take as both...
	rka::eddy::SaveFacts f = facts(4, 5, 48000.f);   // ...the recording's end moved the original
	f.readsOriginal = true;
	f.tapeLength = set->tape.length;
	f.originalLength = set->original.length;
	const rka::eddy::SavePlan p = rka::eddy::planSave(f, r);
	std::string why;
	CHECK(rka::eddy::storePlanned(dir, *set, 48000.f, p, why), "%s", why.c_str());
	TapeSet* back = nullptr;
	CHECK(rka::eddy::loadStored(dir, 48000.f, back, why) && back && sameTape(back->tape, set->original)
		&& sameTape(back->original, set->original), "the new recording was not stored as both: %s", why.c_str());
	delete back;
	delete set;
}

}

int main() {
	__sanitizer_install_malloc_and_free_hooks(mallocHook, freeHook);
	load_levels();
	load_rates();
	load_long();
	load_bad();
	export_round_trip();
	stored_round_trip();
	stored_unchanged_not_written();
	stored_partial();
	convert_keeps_both();
	save_plan();
	save_plan_held_keep();
	cut_reported();
	load_huge();
	stored_other_rate();
	stored_recording_copy();
	return rkatest::finish("eddy test_jobs");
}
