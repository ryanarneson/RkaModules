#pragma once
// Eddy's read voices (EddyEngine.hpp): up to four heads reading tapes at once,
// so that a jump (the join, a retrigger, Restore, a window move, a new tape)
// fades from the old place to the new rather than stepping. Rack-free, C++11,
// header-only; no allocation, no libm. Audio thread only.
//
// A voice's envelope is linear (0..1) and heard as sin(pi/2 env): two voices
// fading opposite ways at the same rate are an equal-power crossfade. A fade
// moves by `perSample` each sample and by `perFrame` for each frame of tape
// the voice travels (the join's overlap is a stretch of tape, whatever the
// speed). At 1 the envelope is exactly 1 and the gain exactly 1.
//
// Each voice also carries a write head (TapeWriter) and a wear state, so it
// writes what it passes for as long as it sounds, at the strength of its own
// gain (the engine does the writing): a frame heard at gain g was written at
// weight g. A voice starts with a fresh writer, wear state and no moves
// waiting.
#include <cmath>
#include <cstdint>

#include "BandLimitedRead.hpp"
#include "FastMath.hpp"
#include "Tape.hpp"
#include "eddy/Wear.hpp"

namespace rka {
namespace eddy {

struct Voice {
	const Tape* src = nullptr;     // what it reads: a set's tape or its original
	const void* owner = nullptr;   // the set that holds src
	double pos = 0.0;              // frames; within [0, length) when wrap
	double env = 0.0;
	double perSample = 0.0, perFrame = 0.0;
	uint32_t born = 0;
	bool wrap = false;             // reads round the tape's end (a window that wraps)
	bool active = false;

	// The write head: last sample's move, written this sample (EddyEngine).
	TapeWriter writer;
	Wear wear{0u};
	struct Run {
		double a, b;
	};
	Run runs[2];
	int nRuns = 0;
	void queue(double a, double b) {
		if (nRuns < 2) {
			runs[nRuns].a = a;
			runs[nRuns].b = b;
			nRuns++;
		}
	}

	// Both sides where the voice is, for a head moving `speed` frames a
	// sample (BandLimitedRead.hpp).
	void read(const BandLimitedRead& reader, float speed, float out[2]) const {
		reader.read(*src, pos, speed, wrap, out);
	}
	float gain() const {
		return env >= 1.0 ? 1.f : fastSin2pi(0.25f * (float) env);
	}
	double length() const {
		return (double) src->extent(src->length);
	}
	// Moves by v frames; a wrapping voice stays within [0, length).
	void move(double v) {
		pos += v;
		if (wrap)
			pos = reduce(pos, length());
	}
	// Ends exactly at 1 (the fade stops) or at 0 (the voice stops); a fade
	// of half a turn of a 5 minute tape moves by about 1e-7 a frame.
	void fade(double travel) {
		const double d = perSample + perFrame * travel;
		if (d == 0.0)
			return;
		env += d;
		if (d > 0.0 && env >= 1.0) {
			env = 1.0;
			perSample = perFrame = 0.0;
		}
		else if (d < 0.0 && env <= 0.0) {
			env = 0.0;
			active = false;
		}
	}
	// Starts fading out at least as fast as `perSampleRate` (> 0) a sample.
	void fadeOut(double perSampleRate) {
		if (perSample > -perSampleRate)
			perSample = -perSampleRate;
		perFrame = perFrame < 0.0 ? perFrame : 0.0;
	}

	// p within [0, len); anything not finite (or no length) reads 0.
	static double reduce(double p, double len) {
		if (!(len > 0.0))
			return 0.0;
		if (p >= 0.0 && p < len)
			return p;
		double q = p - std::floor(p / len) * len;
		return q >= 0.0 && q < len ? q : 0.0;
	}
};

struct Voices {
	enum { N = 4 };
	Voice v[N];
	int newest = -1;   // the voice the write head follows, or -1
	uint32_t clock = 0;

	Voice* head() {
		return newest >= 0 ? &v[newest] : nullptr;
	}
	const Voice* head() const {
		return newest >= 0 ? &v[newest] : nullptr;
	}
	int count() const {
		int n = 0;
		for (int i = 0; i < N; i++)
			n += v[i].active ? 1 : 0;
		return n;
	}
	bool full() const {
		return count() == N;
	}

	// A new voice at p, fading in at the given rates while the newest fades
	// out at the same rates; or straight in at full (both rates 0). Its
	// writer is fresh and no moves wait; its wear state is the caller's to
	// set. Null, and nothing changes, if all four are in use.
	Voice* start(const Tape* src, const void* owner, double p, bool wrap, double perSample, double perFrame) {
		int slot = -1;
		for (int i = 0; i < N; i++)
			if (!v[i].active) {
				slot = i;
				break;
			}
		if (slot < 0)
			return nullptr;
		if (newest >= 0) {
			Voice& o = v[newest];
			if (perSample == 0.0 && perFrame == 0.0)
				o.active = false;
			else {
				o.perSample = -perSample;
				o.perFrame = -perFrame;
			}
		}
		Voice& n = v[slot];
		n.src = src;
		n.owner = owner;
		n.wrap = wrap;
		n.pos = wrap ? Voice::reduce(p, n.length()) : p;
		n.perSample = perSample;
		n.perFrame = perFrame;
		n.env = (perSample == 0.0 && perFrame == 0.0) ? 1.0 : 0.0;
		n.active = true;
		n.born = ++clock;
		n.writer.reset();
		n.nRuns = 0;
		newest = slot;
		return &n;
	}

	// All four in use: the oldest that is not the newest fades out at
	// `perSample` (the 1 ms cut).
	void cutOldest(double perSample) {
		int at = -1;
		for (int i = 0; i < N; i++)
			if (v[i].active && i != newest && (at < 0 || v[i].born < v[at].born))
				at = i;
		if (at >= 0)
			v[at].fadeOut(perSample);
	}
	// Every voice fades out; none is the newest any more.
	void fadeOutAll(double perSample) {
		for (int i = 0; i < N; i++)
			if (v[i].active)
				v[i].fadeOut(perSample);
		newest = -1;
	}
	void killAll() {
		for (int i = 0; i < N; i++)
			v[i].active = false;
		newest = -1;
	}
	bool reads(const Tape* t) const {
		for (int i = 0; i < N; i++)
			if (v[i].active && v[i].src == t)
				return true;
		return false;
	}
	bool owns(const void* o) const {
		for (int i = 0; i < N; i++)
			if (v[i].active && v[i].owner == o)
				return true;
		return false;
	}
	void retarget(const Tape* from, const Tape* to) {
		for (int i = 0; i < N; i++)
			if (v[i].active && v[i].src == from)
				v[i].src = to;
	}
};

} // namespace eddy
} // namespace rka
