#pragma once
// What test_engine.cpp and test_transport.cpp share: an engine with a frame,
// and a stand-in for the module's worker thread that, between process()
// calls, offers sets, does the copies the engine asks for (a slice at a time,
// so a write to the tape during a copy would show), and collects what the
// engine retires. Not plugin code.
#include "eddy/EddyEngine.hpp"
#include "hadal/Random.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <set>
#include <vector>

namespace rig {

using rka::EddyEngine;
using rka::EddyFrame;
using rka::EddyResult;
using rka::Tape;
using rka::TapeSet;
typedef EddyFrame F;

const double PI = 3.14159265358979323846;

inline double db(double x) {
	return 20.0 * std::log10(x > 1e-30 ? x : 1e-30);
}

// The tape's frames [0, length + tail) of both sides, as they are now.
struct Snapshot {
	std::vector<float> ch[2];
	size_t length = 0, tail = 0;
	explicit Snapshot(const Tape& t) : length(t.length), tail(t.tail) {
		for (int c = 0; c < 2; c++)
			ch[c].assign(t.ch[c], t.ch[c] + t.length + t.tail);
	}
	bool equals(const Tape& t) const {
		if (t.length != length || t.tail != tail)
			return false;
		for (int c = 0; c < 2; c++)
			if (std::memcmp(ch[c].data(), t.ch[c], (length + tail) * sizeof(float)) != 0)
				return false;
		return true;
	}
};

inline bool sameTape(const Tape& a, const Tape& b) {
	if (a.length != b.length || a.tail != b.tail)
		return false;
	for (int c = 0; c < 2; c++)
		if (std::memcmp(a.ch[c], b.ch[c], (a.length + a.tail) * sizeof(float)) != 0)
			return false;
	return true;
}

struct Rig {
	EddyEngine e;
	F f;
	EddyResult r;
	float fs;
	std::vector<TapeSet*> made;
	std::multiset<TapeSet*> collected;
	// The worker: a copy takes copySamples calls of step() (0: done at once).
	int copySamples = 0;
	EddyEngine::Copy copying = EddyEngine::NONE;
	TapeSet* copySet = nullptr;
	size_t copyPos = 0;
	int copyLeft = 0;
	long copies = 0;
	bool wantedChangedMidCopy = false;
	bool tapeMatchedAtDone = true;   // every original-to-tape copy left the two equal
	std::function<void(EddyEngine::Copy)> onCopyDone;
	long t = 0;

	explicit Rig(float rate = 48000.f, uint32_t seed = 1, bool withSet = true) : e(seed), fs(rate) {
		e.setSampleRate(rate);
		if (withSet)
			e.offer(make());
	}
	~Rig() {
		for (TapeSet* s : made)
			delete s;
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	TapeSet* make(float rate = 0.f) {
		TapeSet* s = new TapeSet;
		s->allocate(rate > 0.f ? rate : fs);
		made.push_back(s);
		return s;
	}
	// A loaded set: frames [0, n) and a tail of `tail` frames from gen(k, l, r).
	TapeSet* loaded(size_t n, size_t tail, const std::function<void(size_t, float&, float&)>& gen, float rate = 0.f) {
		TapeSet* s = make(rate);
		for (size_t k = 0; k < n + tail; k++)
			gen(k, s->tape.ch[0][k], s->tape.ch[1][k]);
		s->tape.length = n;
		s->tape.tail = tail;
		for (int c = 0; c < 2; c++)
			std::memcpy(s->original.ch[c], s->tape.ch[c], (n + tail) * sizeof(float));
		s->original.length = n;
		s->original.tail = tail;
		s->overview.build(s->tape);
		return s;
	}
	TapeSet* sine(double seconds, double hz, float volts, double phase = 0.0, size_t tail = 0) {
		const double fsd = fs;
		return loaded((size_t) std::lround(seconds * fs), tail, [=](size_t k, float& l, float& r) {
			l = r = (float) (volts * std::sin(2 * PI * hz * (double) k / fsd + phase));
		});
	}
	TapeSet* noise(double seconds, float volts, uint32_t seed, size_t tail = 0) {
		rka::hadal::Random rnd;
		rnd.seed(seed);
		return loaded((size_t) std::lround(seconds * fs), tail, [&](size_t, float& l, float& r) {
			l = volts * rnd.uniform();
			r = volts * rnd.uniform();
		});
	}
	// Offers s and runs until the engine has taken it.
	void use(TapeSet* s) {
		while (!e.offer(s))
			step();
		for (int i = 0; i < 4 && e.live() != s; i++)
			step();
	}

	static void copyRange(Tape& to, const Tape& from, size_t a, size_t b) {
		for (int c = 0; c < 2; c++)
			std::memcpy(to.ch[c] + a, from.ch[c] + a, (b - a) * sizeof(float));
	}
	void worker() {
		if (copying == EddyEngine::NONE) {
			copying = e.copyWanted();
			if (copying != EddyEngine::NONE) {
				copySet = e.live();
				copyPos = 0;
				copyLeft = copySamples;
			}
		}
		if (copying != EddyEngine::NONE) {
			if (e.copyWanted() != copying)
				wantedChangedMidCopy = true;
			Tape& to = copying == EddyEngine::ORIGINAL_TO_TAPE ? copySet->tape : copySet->original;
			const Tape& from = copying == EddyEngine::ORIGINAL_TO_TAPE ? copySet->original : copySet->tape;
			const size_t n = from.length + from.tail;
			// A slice now, the rest over the next calls: what the engine
			// wrote meanwhile would show in the result.
			const size_t slice = copyLeft > 0 ? n / (size_t) (copyLeft + 1) : n - copyPos;
			const size_t end = std::min(n, copyPos + slice);
			copyRange(to, from, copyPos, end);
			copyPos = end;
			if (copyLeft-- <= 0) {
				copyRange(to, from, copyPos, n);
				to.length = from.length;
				to.tail = from.tail;
				if (copying == EddyEngine::ORIGINAL_TO_TAPE && !sameTape(copySet->tape, copySet->original))
					tapeMatchedAtDone = false;
				const EddyEngine::Copy c = copying;
				copying = EddyEngine::NONE;
				copies++;
				e.copyDone();
				if (onCopyDone)
					onCopyDone(c);
			}
		}
		while (TapeSet* s = e.collect())
			collected.insert(s);
	}

	void step(float l = 0.f, float rr = 0.f) {
		f.inL = l;
		f.inR = rr;
		e.process(f, r);
		f.recPress = f.dubPress = f.playPress = f.restorePress = false;
		t++;
		worker();
	}
	void run(long n) {
		for (long i = 0; i < n; i++)
			step();
	}
	void press(bool F::*b) {
		f.*b = true;
	}

	// The head's speed over n samples, frames a sample, from the result.
	double speedOver(int n) {
		const double len = r.seconds * fs;
		const double h0 = r.head;
		run(n);
		double d = (r.head - h0) * len;
		if (d > len / 2)
			d -= len;
		if (d < -len / 2)
			d += len;
		return d / n;
	}
};

// The Length knob that gives a window of `frames` on a tape of `tape` frames
// (the law: 10 ms x (tape / 10 ms)^k).
inline float lengthKnob(double frames, double tape, float fs) {
	const double lmin = 0.01 * fs;
	return (float) (std::log(frames / lmin) / std::log(tape / lmin));
}

// The frequency from the first to the last rising zero crossing.
inline double crossingsHz(const std::vector<float>& x, float fs) {
	int c = 0;
	double first = 0, last = 0;
	for (size_t i = 1; i < x.size(); i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f) {
			const double at = (double) i - x[i] / ((double) x[i] - x[i - 1]);
			if (c == 0)
				first = at;
			last = at;
			c++;
		}
	return c > 1 ? (c - 1) * (double) fs / (last - first) : 0.0;
}

inline double rms(const float* x, size_t n) {
	double s = 0;
	for (size_t i = 0; i < n; i++)
		s += (double) x[i] * x[i];
	return n ? std::sqrt(s / (double) n) : 0.0;
}

} // namespace rig
