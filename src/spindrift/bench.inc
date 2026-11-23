// Spindrift's declaration for the CPU bench, the libm check and the globals
// check (src/bench/BenchModule.hpp says what each part is for; TESTING.md
// rule 10 has the budgets). Not part of the plugin. The engine runs the
// vendored Beads, compiled once in src/spindrift/BeadsFirmware.cpp, which
// tests.mk links into the tools (BENCH_LINK).
#include "bench/BenchModule.hpp"
#include "spindrift/SpindriftEngine.hpp"

#include <cstdint>

namespace rkabench {

// (Inside rkabench::spindrift, the engine's namespace is ::spindrift.)
struct spindrift {
	struct Instance {
		::spindrift::SpindriftEngine engine;
		::spindrift::Knobs k;
		::spindrift::Jacks j;
		::spindrift::Out out;
		Cv::Cursor cur[8];
		uint32_t noise = 77777;

		// The module's defaults: knobs at their defaults, nothing patched,
		// Beads' memory allocated (as the module does off the audio thread).
		Instance() : engine(0x5eed) {
			engine.allocate();
			engine.setSampleRate(FS);
			defaults();
			for (int i = 0; i < 8; i++)
				cur[i] = Cv::cursor(i, 0);
			out.l = out.r = 0.f;
		}
		Instance(const Instance&) = delete;
		Instance& operator=(const Instance&) = delete;

		void defaults() {
			k.time = k.size = k.shape = k.pitch = k.density = 0.5f;
			k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
			k.feedback = k.dryWet = k.reverb = 0.f;
			j = ::spindrift::Jacks();
		}

		void process(float) {
			// The engine runs at 48 kHz, the bench's rate (set at construction).
			engine.process(k, j, out);
		}
		// The module's Initialize: knobs, buttons and menu to their defaults.
		void reset() {
			defaults();
			engine.buttons().reset();
			engine.settings = ::spindrift::Settings();
		}
		float probe() const {
			return out.l + out.r;
		}
		// Cheap white noise in +-5 V (no libm).
		float hiss() {
			noise = noise * 1664525u + 1013904223u;
			return ((float) (noise >> 8) * (1.f / 8388608.f) - 1.f) * 5.f;
		}
		// Noise into both inputs (a stereo source, so the grains stay).
		void stereoIn() {
			j.inLOn = j.inROn = true;
			j.inL = hiss();
			j.inR = hiss();
		}
		// At t == 0, before the engine's first sample: the quality it boots in.
		void quality(::spindrift::Quality q) {
			engine.buttons().quality = engine.buttons().pending = q;
		}
	};

	// Nothing patched, knobs at their defaults, the menu's synth set to Never.
	// (At the default, Auto, an unpatched Spindrift is the synth after 10 s:
	// the synth case.)
	static void idle(Instance& i, long t, const Cv&) {
		if (t == 0)
			i.engine.settings.synth = ::spindrift::SYNTH_NEVER;
	}

	// Digital, Density 0.85 (random grains) and Size 0.95: 30 to 32 of
	// Beads' 32 grains sounding at once; noise into both inputs, Dry/Wet at
	// 0.5, the outputs patched (the engine always computes both).
	static void outputs(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.k.density = 0.85f;
			i.k.size = 0.95f;
			i.k.dryWet = 0.5f;
		}
		i.stereoIn();
	}

	// The heaviest grain patch: as outputs, with Feedback and Reverb up and a
	// moving CV into all eight CV jacks (Time, Size and Shape within +-1 V,
	// Pitch +-0.5 V, Density +-1 V, the mix CVs +-1 V).
	static void busy(Instance& i, long t, const Cv& cv) {
		outputs(i, t, cv);
		if (t == 0) {
			i.k.feedback = 0.5f;
			i.k.reverb = 0.6f;
			i.j.timeOn = i.j.sizeOn = i.j.shapeOn = i.j.pitchOn = i.j.densityOn = true;
			i.j.feedbackOn = i.j.dryWetOn = i.j.reverbOn = true;
		}
		i.j.time = 0.2f * cv.next(i.cur[0]);
		i.j.size = 0.2f * cv.next(i.cur[1]);
		i.j.shape = 0.2f * cv.next(i.cur[2]);
		i.j.pitch = 0.1f * cv.next(i.cur[3]);
		i.j.density = 0.2f * cv.next(i.cur[4]);
		i.j.feedback = 0.2f * cv.next(i.cur[5]);
		i.j.dryWet = 0.2f * cv.next(i.cur[6]);
		i.j.reverb = 0.2f * cv.next(i.cur[7]);
	}

	// Dirty Tape (Beads at 24 kHz through the edge converters, 8-bit mu-law,
	// wow and flutter), otherwise as outputs.
	static void dirty(Instance& i, long t, const Cv& cv) {
		if (t == 0)
			i.quality(::spindrift::DIRTY_TAPE);
		outputs(i, t, cv);
	}

	// Delay mode (Size fully clockwise): Beads' taps, Feedback and Reverb
	// up, noise into both inputs.
	static void delay(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.k.size = 1.f;
			i.k.density = 0.8f;
			i.k.feedback = 0.5f;
			i.k.reverb = 0.5f;
			i.k.dryWet = 0.5f;
		}
		i.stereoIn();
	}

	// The synth (the menu's Always, as an unpatched Spindrift is after 10 s),
	// Density 0.85: the wavetable voice and its grains, Reverb up.
	static void synth(Instance& i, long t, const Cv&) {
		if (t == 0) {
			i.engine.settings.synth = ::spindrift::SYNTH_ALWAYS;
			i.k.density = 0.85f;
			i.k.dryWet = 0.5f;
			i.k.reverb = 0.5f;
		}
	}

	static const int BENCH_CASES = 6;
	static BenchCase<Instance> benchCase(int k) {
		// Budgets: 1.5 times the most an Apple M-series Mac measured in
		// three runs at 1, 4 or 16 instances (2026-11-22: idle 0.223,
		// outputs 1.347, busy 1.385, dirty 0.620, delay 0.736, synth 0.230,
		// each the one-instance figure; 16 instances cost less each),
		// rounded up to the next 0.05; elsewhere three times the Mac's,
		// provisional (not measured on x86-64).
		const BenchCase<Instance> cases[BENCH_CASES] = {
			// name     inputs   Apple arm64  elsewhere
			{"idle", idle, 0.35, 1.05},
			{"outputs", outputs, 2.05, 6.15},
			{"busy", busy, 2.1, 6.3},
			{"dirty", dirty, 0.95, 2.85},
			{"delay", delay, 1.15, 3.45},
			{"synth", synth, 0.35, 1.05},
		};
		return cases[k];
	}

	static const int LIBM_CASES = 6;
	static LibmCase<Instance> libmCase(int k) {
		const LibmCase<Instance> cases[LIBM_CASES] = {
			{"idle", idle, true},
			{"outputs", outputs, true},
			{"busy", busy, false},
			{"dirty", dirty, true},
			{"delay", delay, true},
			{"synth", synth, true},
		};
		return cases[k];
	}
};

} // namespace rkabench
