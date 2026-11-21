#pragma once
// Reading and writing WAV files for Eddy's tape, with no Rack dependency.
// The declarations are all that most code includes; the implementation is
// compiled where RKA_WAV_IMPLEMENTATION is defined before this header (the
// plugin's src/WavFile.cpp, and a test that wants its own build of it).
// C++11: this is part of the plugin.
#include <cstdint>
#include <string>

namespace rka {

struct WavInfo {
	uint32_t sampleRate = 0;
	uint32_t channels = 0;
	// probeWav: the frames the file holds. readWav: the frames put in l and r
	// (the smaller of the file's and maxFrames).
	uint64_t frames = 0;
};

// Reads at most maxFrames frames of the first two channels into l and r (a mono
// file fills both), as written (+-1.0), with no conversion of rate. On failure
// returns false, leaves l and r untouched and sets `why`. Never allocates more
// than the file's own decode block. (A read error half way through a file that
// passed every check is the one failure that can leave l and r part written.)
// Accepts 8, 16, 24 and 32-bit integer and 32 and 64-bit float PCM, 1 to 64
// channels, 1 Hz to 768 kHz, and only a file that holds all the data its header
// claims. `path` is UTF-8 (also on Windows).
bool readWav(const std::string& path, float* l, float* r, uint64_t maxFrames, WavInfo& info, std::string& why);
bool probeWav(const std::string& path, WavInfo& info, std::string& why);   // header only
// 32-bit float stereo. Writes path + ".tmp", then renames over path.
bool writeWav(const std::string& path, const float* l, const float* r, uint64_t frames, uint32_t sampleRate, float gain, std::string& why);

}

#ifdef RKA_WAV_IMPLEMENTATION

#define DR_WAV_IMPLEMENTATION
#include "dep/dr_wav.h"

#include <cstdio>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace rka {
namespace wavdetail {

static const uint32_t kMaxRate = 768000;
static const uint32_t kMaxChannels = 64;
static const unsigned kChunkFloats = 8192;   // the decode block, on the stack

#ifdef _WIN32
inline std::wstring widen(const std::string& s) {
	if (s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), NULL, 0);
	if (n <= 0)
		return std::wstring();
	std::wstring w((size_t) n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), &w[0], n);
	return w;
}
#endif

// The size of the file, or why not.
inline bool statFile(const std::string& path, uint64_t& size, std::string& why) {
#ifdef _WIN32
	struct _stat64 st;
	if (_wstat64(widen(path).c_str(), &st) != 0) {
#else
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
#endif
		why = "cannot open the file (missing or not readable)";
		return false;
	}
#ifdef _WIN32
	if (st.st_mode & _S_IFDIR) {
#else
	if (S_ISDIR(st.st_mode)) {
#endif
		why = "that is a folder, not a file";
		return false;
	}
	size = (uint64_t) st.st_size;
	return true;
}

// Opens, and checks everything a tape can rely on before any frame is read.
inline bool open(drwav& wav, const std::string& path, std::string& why) {
	uint64_t size = 0;
	if (!statFile(path, size, why))
		return false;
#ifdef _WIN32
	drwav_bool32 ok = drwav_init_file_w(&wav, widen(path).c_str(), NULL);
#else
	drwav_bool32 ok = drwav_init_file(&wav, path.c_str(), NULL);
#endif
	if (!ok) {
		why = "not a WAV file this build can read";
		return false;
	}
	bool bad = true;
	if (wav.channels < 1 || wav.channels > kMaxChannels)
		why = "unusable number of channels";
	else if (wav.sampleRate < 1 || wav.sampleRate > kMaxRate)
		why = "unusable sample rate";
	else if (wav.translatedFormatTag == DR_WAVE_FORMAT_PCM
		? !(wav.bitsPerSample == 8 || wav.bitsPerSample == 16 || wav.bitsPerSample == 24 || wav.bitsPerSample == 32)
		: (wav.translatedFormatTag != DR_WAVE_FORMAT_IEEE_FLOAT || !(wav.bitsPerSample == 32 || wav.bitsPerSample == 64)))
		why = "not PCM or float WAV (compressed files are not read)";
	else if (wav.totalPCMFrameCount == 0)
		why = "the file holds no audio";
	else if (wav.dataChunkDataPos > size || wav.dataChunkDataSize > size - wav.dataChunkDataPos)
		why = "the file is shorter than its header says";
	else
		bad = false;
	if (bad)
		drwav_uninit(&wav);
	return !bad;
}

}   // namespace wavdetail

bool probeWav(const std::string& path, WavInfo& info, std::string& why) {
	drwav wav;
	if (!wavdetail::open(wav, path, why))
		return false;
	info.sampleRate = wav.sampleRate;
	info.channels = wav.channels;
	info.frames = wav.totalPCMFrameCount;
	drwav_uninit(&wav);
	return true;
}

bool readWav(const std::string& path, float* l, float* r, uint64_t maxFrames, WavInfo& info, std::string& why) {
	drwav wav;
	if (!wavdetail::open(wav, path, why))
		return false;
	const uint32_t ch = wav.channels;
	uint64_t n = wav.totalPCMFrameCount;
	if (n > maxFrames)
		n = maxFrames;
	float buf[wavdetail::kChunkFloats];
	const uint64_t perChunk = wavdetail::kChunkFloats / ch;
	uint64_t done = 0;
	while (done < n) {
		uint64_t want = n - done;
		if (want > perChunk)
			want = perChunk;
		uint64_t got = drwav_read_pcm_frames_f32(&wav, want, buf);
		if (got != want) {
			why = "read error part way through the file";
			drwav_uninit(&wav);
			return false;
		}
		for (uint64_t i = 0; i < got; i++) {
			l[done + i] = buf[i * ch];
			r[done + i] = buf[i * ch + (ch > 1 ? 1 : 0)];
		}
		done += got;
	}
	info.sampleRate = wav.sampleRate;
	info.channels = ch;
	info.frames = done;
	drwav_uninit(&wav);
	return true;
}

bool writeWav(const std::string& path, const float* l, const float* r, uint64_t frames, uint32_t sampleRate, float gain, std::string& why) {
	const std::string tmp = path + ".tmp";
	drwav_data_format fmt;
	fmt.container = drwav_container_riff;
	fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
	fmt.channels = 2;
	fmt.sampleRate = sampleRate;
	fmt.bitsPerSample = 32;
	drwav wav;
#ifdef _WIN32
	const std::wstring wtmp = wavdetail::widen(tmp), wpath = wavdetail::widen(path);
	drwav_bool32 ok = drwav_init_file_write_sequential_pcm_frames_w(&wav, wtmp.c_str(), &fmt, frames, NULL);
#else
	drwav_bool32 ok = drwav_init_file_write_sequential_pcm_frames(&wav, tmp.c_str(), &fmt, frames, NULL);
#endif
	if (!ok) {
		why = "cannot create the file (folder missing or not writable)";
		return false;
	}
	float buf[wavdetail::kChunkFloats];
	const uint64_t perChunk = wavdetail::kChunkFloats / 2;
	uint64_t done = 0;
	bool good = true;
	while (done < frames && good) {
		uint64_t n = frames - done;
		if (n > perChunk)
			n = perChunk;
		for (uint64_t i = 0; i < n; i++) {
			buf[2 * i] = l[done + i] * gain;
			buf[2 * i + 1] = r[done + i] * gain;
		}
		good = drwav_write_pcm_frames(&wav, n, buf) == n;
		done += n;
	}
	drwav_uninit(&wav);
	bool renamed = false;
	if (good) {
#ifdef _WIN32
		renamed = MoveFileExW(wtmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
		renamed = std::rename(tmp.c_str(), path.c_str()) == 0;
#endif
		if (!renamed)
			why = "cannot replace the file";
	}
	else
		why = "write error (disk full?)";
	if (!renamed) {
#ifdef _WIN32
		DeleteFileW(wtmp.c_str());
#else
		std::remove(tmp.c_str());
#endif
	}
	return renamed;
}

}

#endif
