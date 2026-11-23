# Spindrift Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spindrift, a 20 HP stereo granular processor that is Mutable
Instruments Beads (grains, delay mode, four qualities, reverb, wavetable
synth) running Beads' own code, in the Instrument look, with a grain
window.

**Architecture:** Beads' DSP and the stmlib it needs are vendored into
`src/spindrift/beads/` and `src/spindrift/stmlib/` and compiled as one
unity file, Shoal-style, every change marked `// SPINDRIFT:`. A pristine
copy is kept for tests only and compiled under renamed namespaces so the
tests can compare the two. A Rack-free `SpindriftEngine` does what Beads'
`beads.cc` and `ui.cc` did (block loop at Beads' codec rate, rate
conversion at the edges, synth switch, mutes, Seed and Quality buttons,
gain, levels); `Spindrift.cpp` only maps the panel onto it.

**Tech Stack:** C++11 plugin (C++17 for the vendored unity file and the
tests), VCV Rack 2.6.6 SDK, `tools/gen_panels.py`, the repo's offline
tests (`tests.mk`, `src/testing.hpp`).

**Spec:** `docs/specs/2026-11-22-spindrift-design.md` (read it first;
this plan argues from it). Upstream: `~/ws/plugins/eurorack` at commit
`f4cfa76`, files under `beads/` and `stmlib/`.

## Global Constraints

- Module slug `Spindrift`, folder `src/spindrift/`, 20 HP, panel as the
  spec's "Panel (20 HP)" positions; title SPINDRIFT, subtitle GRANULAR
  CLOUD.
- Vendored code is changed only with a `// SPINDRIFT:` mark and a line in
  `src/spindrift/CHANGES.md`; the MIT notice stays in every file;
  `LICENSE-beads.md` at the repo root.
- The vendored unity file and the reference are compiled with
  `-fno-unsafe-math-optimizations` (as `SHOAL_FW_FLAGS` does), in the
  plugin and in the tests, so both share floating-point semantics.
- Levels: ±10 V = Beads' full scale ±1, in and out; no polarity
  inversion; gate output mode gives a 0/10 V gate; triggers and gates
  detect with `SchmittTrigger` at 0.1 V / 1 V.
- Beads' rates: Digital 48 kHz, Vintage Digital 32 kHz, Clean Tape
  48 kHz (2:1 inside), Dirty Tape 24 kHz; blocks at 4 kHz block rate (12,
  8, 6 frames). Mutes are 250 ms. Synth mode after 10 s, output under
  1 % of full scale.
- Delay memory 786,432 bytes (3 x 262,144); `buffer.bin` holds 768,000
  of them.
- No allocation, lock or I/O on the audio thread (TESTING.md rule 6);
  instances independent (rule 8); Linux and Windows builds before merge
  (rule 11).
- Every test build handed to the user gets `tools/bump_version.sh` first
  (CLAUDE.md).
- Panels from `tools/gen_panels.py` only; never hand-edit `res/`,
  `res-src/` or `SpindriftLayout.hpp`.

## Review Focus

1. **Rack at 44.1 or 96 kHz, and a rate change while frozen.** Expected:
   same sound as at 48 kHz within the converters' error; a frozen buffer
   survives the change (it is in Beads' rate). Test in Task 5.
2. **Hostile input:** ±12 V, DC, NaN or inf on any input. Expected: output
   finite, bounded by Beads' limiter at about ±10 V, recovers within one
   second of the input becoming sane. Test in Task 7.
3. **A `buffer.bin` that does not fit:** another quality, mono vs stereo,
   a newer format version, truncated, missing. Expected: an empty,
   unfrozen buffer and a log line; never a crash or a read past the end.
   Test in Task 8.
4. **Quality pressed several times while frozen.** Expected: only the last
   choice is applied, once, on unfreeze, with one 250 ms mute. Test in
   Task 4.
5. **Inputs plugged and unplugged around the 10 s synth threshold and
   between mono and stereo.** Expected: each real change mutes 250 ms and
   clears the buffer; no change while frozen; never synth mode while an
   input is patched. Test in Task 5.

---

### Task 1: The panel

**Files:**
- Modify: `tools/gen_panels.py` (a `"Spindrift"` entry in `PANELS`, with
  its column lists beside `_ED_X`)
- Generated: `res-src/spindrift/`, `res/spindrift/`,
  `src/spindrift/SpindriftLayout.hpp`

**Interfaces:**
- Produces: `SpindriftLayout.hpp` ids `PARAM_TIME, PARAM_SIZE,
  PARAM_DENSITY, PARAM_SHAPE, PARAM_PITCH, PARAM_TIME_ATT, PARAM_SIZE_ATT,
  PARAM_SHAPE_ATT, PARAM_PITCH_ATT, PARAM_FEEDBACK, PARAM_DRYWET,
  PARAM_REVERB, PARAM_FREEZE, PARAM_SEED, PARAM_QUALITY`; inputs
  `INPUT_TIME_CV, INPUT_SIZE_CV, INPUT_DENSITY_CV, INPUT_SHAPE_CV,
  INPUT_PITCH_CV, INPUT_FEEDBACK_CV, INPUT_DRYWET_CV, INPUT_REVERB_CV,
  INPUT_FREEZE, INPUT_SEED, INPUT_L, INPUT_R`; outputs `OUTPUT_L,
  OUTPUT_R`; screen `GRAINS`.

- [ ] **Step 1:** Add the entry. The draft that drew the approved mockup
  is the spec's positions: top columns `[7.8, 23.5, 50.8, 78.1, 93.8]`,
  bottom `[7.8, 29.3, 50.8, 72.3, 93.8]`; knobs y 48, attenuverters 61,
  CVs 73, Density hero (50.8, 53.0) face r 15, ticks 24, major 4, words
  `(-135,"12") (-90,"8") (-45,"4") (0,"0") (45,"4") (90,"8") (135,"12")`;
  y 90 Freeze, Feedback, Dry/Wet, Reverb, Seed; y 102 IN L, three mix
  CVs, OUT L; y 116 IN R, GATE, Quality, TRIG, OUT R; caps for Freeze,
  Seed, Quality; tiles on the outputs; traces CV → attenuverter → knob
  for the four, CV → knob for Density and the three mix knobs; screen
  `("GRAINS", 5.0, 19.5, 91.6, 14.0)` with a ruler `px = 91.6/320`, sides
  `"b"`; `label_at` Density `"face"`, `INPUT_PITCH_CV` `"below"` (label
  V/OCT); notes REGULAR at (37.8, 69.5) and RANDOM at (63.8, 69.5), size
  1.3, dim; screws `screws_at(20, "tr", "bl")`; preview turns as the
  mockup.
- [ ] **Step 2:** Run `make panels` then `python3 tools/test_gen_panels.py`.
  Expected: the generator's checks pass; `res/spindrift/Spindrift.svg`
  and `-dark.svg` exist.
- [ ] **Step 3:** Run `python3 tools/gen_panels.py --preview --inkscape
  "$(command -v inkscape)"` and look at `build/preview/Spindrift-300.png`
  against `docs/specs/2026-11-22-spindrift-mockup.png` (rule 12).
- [ ] **Step 4:** Commit: `Spindrift: panel`.

### Task 2: Vendor Beads, and the reference beside it

**Files:**
- Create: `src/spindrift/beads/...`, `src/spindrift/stmlib/...` (the files
  the spec's Research table lists, plus `beads/resources.h/.cc` and the
  stmlib headers and `.cc` files they include: `dsp/units.cc`,
  `utils/random.cc`), paths as upstream
- Create: `src/spindrift/BeadsFirmware.cpp` (unity file: `#include`s every
  vendored `.cc`)
- Create: `src/spindrift/testdata/ref/` (the same files, untouched) and
  `src/spindrift/testdata/RefBeads.hpp/.cpp`
- Create: `src/spindrift/CHANGES.md`, `LICENSE-beads.md`,
  `tools/check_spindrift_vendor.sh`
- Modify: `Makefile` (per-object flags for `BeadsFirmware.cpp.o`, as
  Shoal's `Firmware.cpp.o`), `tests.mk` (`SPINDRIFT_FW_FLAGS`, rules)
- Test: `src/spindrift/test_vendor.cpp`

**Interfaces:**
- Produces: `rkaref::BeadsRef` (in `RefBeads.hpp`, no Beads types in its
  header):
  ```cpp
  namespace rkaref {
  struct RefParams { float time, timeAtt, timeCv; bool timeHasCv;
                     float size, sizeAtt, sizeCv; bool sizeHasCv;
                     float shape, shapeAtt, shapeCv; bool shapeHasCv;
                     float pitch, pitchAtt, pitchCv; bool pitchHasCv;  // pitch in semitones, as CvReader gives it
                     float density, densityCv;
                     float feedback, dryWet, reverb;
                     int seedState;   // 0 off, 1 on, 2 locked (SeedButtonState)
                     bool gateOutput; };
  class BeadsRef {
   public:
    BeadsRef();  ~BeadsRef();
    void seed(uint32_t s);                  // stmlib::Random::Seed
    void configure(int media, bool mono);   // Processor::Configure
    void processBlock(const RefParams& p, const uint8_t* seedGateFlags,  // nullptr: unpatched
                      const bool* freeze, const float* inLR, float* outLR, size_t frames); // interleaved, ±1
    void synthBlock(const RefParams& p, const uint8_t* seedGateFlags, const bool* freeze,
                    int resolution, float* outLR, size_t frames);
  };
  }
  ```
  `RefBeads.cpp` is compiled with `-Dbeads=beadsref -Dstmlib=stmlibref`
  and `-Isrc/spindrift/testdata/ref`, so it links beside the vendored
  copy in one test binary.

- [ ] **Step 1:** Copy the files unchanged from `f4cfa76`. Write
  `tools/check_spindrift_vendor.sh`: with `~/ws/plugins/eurorack` present,
  `testdata/ref/` must equal upstream byte for byte, and every difference
  between `src/spindrift/{beads,stmlib}` and upstream must be on a line
  carrying `SPINDRIFT:` (or in a file `CHANGES.md` names); without the
  checkout it says "skipped". Wire it into `make test`.
- [ ] **Step 2: Write the failing test** `test_vendor.cpp`:
  `ref_runs` (a `BeadsRef` at Digital, stereo, 1 s of a 220 Hz sine at
  ±0.5, Seed locked, Density 0.7: output finite, RMS > 0.01);
  `vendored_matches_ref_unchanged` (the same through the vendored
  `beads::Processor` driven the same way: every sample equal, `==`).
- [ ] **Step 3:** Run `make -f tests.mk build/test/spindrift/test_vendor &&
  build/test/spindrift/test_vendor`. Expected: FAIL (no rules, then no
  `BeadsRef`); after the rules and `RefBeads.cpp`, PASS with the vendored
  copy still unchanged (GCC and Clang accept its VLAs as an extension;
  Task 3 removes them).
- [ ] **Step 4:** Commit: `Spindrift: vendor Beads (f4cfa76) and its
  reference`.

### Task 3: The marked changes, held to the reference

**Files:**
- Modify (marked): `stmlib/utils/random.h/.cc`, `beads/dsp/processor.cc`,
  `beads/dsp/granular_processor.h`, `beads/dsp/synth.cc`,
  `beads/dsp/delay_grain.h`, `beads/dsp/wavetable_grain.h`,
  `beads/dsp/multitap_delay.h`, `beads/dsp/wavetable_voice.h`
- Create: `src/spindrift/BeadsContext.hpp`
- Modify: `tools/check_shoal_globals.sh` (an `ALLOWED_TLS` override from
  the environment), `tests.mk` (`spindrift-globals` on
  `build/test/spindrift/BeadsFirmware.o`, run by `make test`)
- Test: `src/spindrift/test_vendor.cpp` (extend)

**Interfaces:**
- Produces:
  ```cpp
  namespace spindrift {
  struct BeadsContext { uint32_t rng; };          // all of Beads' mutable global state
  struct BeadsScope {                              // sets the thread's current context; restores on exit
    explicit BeadsScope(BeadsContext& c);  ~BeadsScope(); };
  constexpr size_t kMaxBlock = 12;                 // largest block handed to Beads
  constexpr size_t kDelayBytes = 786432, kSavedBytes = 768000;
  struct BeadsMemory { uint8_t* delay; uint16_t* reverb;   // allocated off the audio thread
    bool allocate(); void release(); };
  // Window accessors, all const, no allocation:
  struct GrainSnap { float position, span, pan, env; };   // position/span 0..1 of the buffer
  struct TapSnap   { float position, gain; };
  }
  ```
  `beads::DelayGrain::snap(GrainSnap&, int bufferFrames) const`,
  `beads::WavetableGrain::snap(GrainSnap&) const` (position = wave 0..1),
  `beads::Processor::grains(GrainSnap* out, int max) const -> int`,
  `beads::Processor::taps(TapSnap* out, int max) const -> int`,
  `beads::Processor::writeHead() const -> float` (0..1),
  `beads::Synth::wave() const -> float`.

- [ ] **Step 1: Write the failing tests:** `two_contexts_differ` (two
  Processors, contexts seeded 1 and 2, same input: outputs differ);
  `same_seed_same_output`; `matches_ref_every_mode`: for each media 0..3,
  mono and stereo, at Beads' own rate, 3 s each of grain mode with
  Density at 0.2, 0.5 (Seed pulses every 50 ms), 0.8, with every
  attenuverter at ±0.6 and no CV, then with CVs; delay mode (Size 1.0);
  frozen halfway; Seed locked with a clock on SEED; synth mode for each
  resolution; gate output: vendored == reference, sample for sample.
  `snap_reports_grains` (Density 0.8: `grains()` returns > 0 entries, all
  in [0,1]).
- [ ] **Step 2:** Run it: fails (no `BeadsContext`).
- [ ] **Step 3:** Make the changes, each marked and listed in
  `CHANGES.md`: (1) `Random`'s state through `BeadsContext` via one
  thread-local current pointer, as Shoal's `rng_state_()`
  (`src/shoal/firmware/extern/stmlib_utils_random.h:39-61`); (2) the
  VLAs in `Processor::Process`, `GranularProcessor::Process` and
  `Synth::Render` become `kMaxBlock`-sized arrays (`kMaxBlock * ratio`
  where upsampled), with an assert on `size`; (3) buffers from
  `BeadsMemory`; (4) the accessors above, reading existing members only.
- [ ] **Step 4:** Run `make test`. Expected: test_vendor PASS;
  `spindrift-globals` clean (only the allowed TLS pointer);
  `check_spindrift_vendor.sh` clean.
- [ ] **Step 5:** Mutation: change one constant in vendored
  `grain_envelope.h`; `matches_ref_every_mode` must fail. Revert.
- [ ] **Step 6:** Commit: `Spindrift: Beads per instance, fixed blocks,
  window accessors (marked)`.

### Task 4: Controls (Beads' panel behaviour)

**Files:**
- Create: `src/spindrift/Controls.hpp`
- Test: `src/spindrift/test_controls.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace spindrift {
  float attenuverterLaw(float knob01);   // LAW_ATTENUVERTER, cv_reader.h
  float detuneLaw(float knob01);         // LAW_DETUNE: semitones, ±24, snaps 0,1,3,4,7,12,24
  struct PotFilter { void init(float lp, float hysteresis); float process(float v); };  // CvReader's one-pole + HysteresisFilter
  enum Quality { DIRTY_TAPE = 0, CLEAN_TAPE = 1, VINTAGE = 2, DIGITAL = 3 };   // Beads' Media order
  struct Buttons {                       // ui.cc, in samples of Beads' 4 kHz control rate
    void reset();                        // Seed latched, Digital, not frozen
    void freezePress();                  // with Seed held: latch Seed, not freeze
    void seedDown(); void seedUp(uint32_t heldTicks);
    void qualityDown(); void qualityUp(uint32_t heldTicks);   // ≥ 6000 ticks (1.5 s): gain to automatic
    void tick();                         // 4 kHz; handles the 4 s Seed hold
    bool frozenButton, seedLatched, seedHeld;
    Quality quality, pending;            // pending != quality while frozen
    bool autoGainRequest;                // set by the long press, cleared by the engine
    float seedLight(uint32_t ms) const;  // ui.cc:204-210: breathing when latched
  };
  }
  ```

- [ ] **Step 1: Write the failing tests:** `attenuverter_matches_cvreader`
  (101 points against the formula in `cv_reader.h:110-113`, `==`);
  `detune_snaps` (0.5 → 0; ends → ±24; the six snap plateaus);
  `seed_latched_at_reset`; `seed_press_unlatches`;
  `seed_hold_4s_latches` (16,000 ticks); `seed_plus_freeze_latches`
  (and Freeze does not toggle); `quality_steps_digital_vintage_clean_dirty`;
  `quality_long_press_sets_auto_gain_not_step`;
  `quality_while_frozen_is_pending_last_wins` (Review Focus 4: three
  presses while frozen, `quality` unchanged, `pending` = the third).
- [ ] **Step 2:** Run: fails (no header).
- [ ] **Step 3:** Implement `Controls.hpp` by porting `cv_reader.h` and
  `ui.cc` maths (Rack-free).
- [ ] **Step 4:** Run: PASS. Mutation: drop the attenuverter dead zone;
  `attenuverter_matches_cvreader` fails.
- [ ] **Step 5:** Commit: `Spindrift: controls`.

### Task 5: The engine

**Files:**
- Create: `src/spindrift/SpindriftEngine.hpp`, `src/spindrift/EdgeRate.hpp`
- Test: `src/spindrift/test_engine.cpp`

**Interfaces:**
- Consumes: Tasks 3 and 4.
- Produces:
  ```cpp
  namespace spindrift {
  struct Knobs { float time, size, shape, pitch, density;          // 0..1 knob positions
                 float timeAtt, sizeAtt, shapeAtt, pitchAtt;        // 0..1, 0.5 = noon
                 float feedback, dryWet, reverb; };
  struct Jacks { float time, size, shape, pitch, density, feedback, dryWet, reverb;   // volts
                 bool timeOn, sizeOn, shapeOn, pitchOn, densityOn, feedbackOn, dryWetOn, reverbOn;
                 float inL, inR; bool inLOn, inROn;
                 float freeze, seed; bool freezeOn, seedOn; };
  enum SynthPolicy { SYNTH_AUTO, SYNTH_ALWAYS, SYNTH_NEVER };
  enum GainMode { GAIN_FIXED, GAIN_AUTO, GAIN_MANUAL };
  struct Settings { SynthPolicy synth = SYNTH_AUTO; GainMode gain = GAIN_FIXED; float manualGain = 1.f;
                    bool gateOutput = false; bool keepFrozen = true; };
  enum Mode { MODE_GRAIN, MODE_DELAY, MODE_SYNTH };
  struct Out { float l, r; };
  class SpindriftEngine {
   public:
    explicit SpindriftEngine(uint32_t seed);
    bool allocate();                    // off the audio thread
    void setSampleRate(float rackRate); // off the audio thread; buffer cleared unless frozen
    void process(const Knobs&, const Jacks&, Out&);   // one Rack sample, volts
    Buttons& buttons();                 // panel presses go here (Task 4)
    Settings settings;
    Mode mode() const; Quality quality() const; bool mono() const; bool frozen() const;
    bool muted() const;                 // during the 250 ms
    int bufferImage(uint8_t* dst, size_t cap, BufferHeader& h) const;   // Task 8
    bool loadBufferImage(const uint8_t* src, size_t n, const BufferHeader& h);
  };
  }
  ```
  `EdgeRate` converts Rack's rate to Beads' rate and back for one stereo
  pair, set up in `setSampleRate`, no allocation in `process`.

- [ ] **Step 1: Write the failing tests:**
  - `matches_ref_at_native_rate` (Rack rate 48 kHz, Digital: the
    engine's output against `BeadsRef` driven with the same parameters,
    shifted by the engine's latency, within 1e-6 absolute in volts/10);
  - `levels` (±10 V in = ±1 to Beads; Dry/Wet 0 gives 0.75 of the input;
    gate output gives 0 V or 10 V on R);
  - `seed_delay_2_5ms` (a SEED edge reaches Beads 120 samples later at
    48 kHz);
  - `cv_scaling` (Pitch 1 V = 12 semitones at attenuverter full
    clockwise; other CVs ±5 V = ±1);
  - `synth_after_10s` (no inputs, quiet: synth at 10 s, not 9.9 s; never
    at add; leaves when `inLOn`; 250 ms of zero output each way);
  - `synth_policy_always_never`;
  - `mono_when_either_unpatched` (R unpatched: L copied to R, mono
    buffer, lengths 8/16/21.3/32 s);
  - `quality_change_clears_and_mutes` and `deferred_until_unfreeze`
    (Review Focus 4 end to end: three presses frozen, one change and one
    mute on unfreeze);
  - `plug_unplug_storm` (Review Focus 5: inputs toggled every 3 s for
    60 s: never synth while patched; each mono/stereo change one mute;
    none while frozen);
  - `rates` (Review Focus 1: 44.1, 48, 96 kHz: a 1 kHz sine dry at
    Dry/Wet 0 comes out at 0.75 within 0.1 dB; a rate change while
    frozen keeps the buffer: grains after equal grains before under a
    fixed seed, within the converters' error);
  - `no_allocation` (counting `operator new` across 10 s of every mode).
- [ ] **Step 2:** Run: fails.
- [ ] **Step 3:** Implement. Block loop: collect Rack samples through
  `EdgeRate` until a Beads block (12, 8 or 6 frames) is full; build
  `Parameters`, `ModulatedParameter`, `MixParameters` exactly as
  `beads.cc:176-238` does (with `PotFilter` per pot at Beads' control
  rate); call `Processor::Process` or `Synth::Render` inside a
  `BeadsScope`; synth switch and mutes as `beads.cc:276-293, 352-391`;
  gain through vendored or ported `GainControl` (`GAIN_FIXED` = its
  manual path at the gain that makes ±10 V full scale).
- [ ] **Step 4:** Run: PASS. Mutations: skip the SEED delay → 
  `seed_delay_2_5ms` fails; apply Quality while frozen → 
  `deferred_until_unfreeze` fails.
- [ ] **Step 5:** Commit: `Spindrift: engine`.

### Task 6: The window

**Files:**
- Create: `src/spindrift/GrainView.hpp` (Rack-free: what the engine
  publishes and the geometry the widget draws)
- Modify: `src/widgets.hpp` (`GrainWindow`, beside `TapeWindow`)
- Modify: `src/spindrift/SpindriftEngine.hpp` (publish per block)
- Test: `src/spindrift/test_view.cpp`, `src/spindrift/test_threads.cpp`
  (ThreadSanitizer, as Eddy's)

**Interfaces:**
- Produces:
  ```cpp
  namespace spindrift {
  struct GrainView {                       // relaxed atomics inside; copy() is lock-free
    void publish(const GrainSnap* g, int n, const TapSnap* t, int nt, float head,
                 Mode m, Quality q, bool mono, float seconds, int bank, bool frozen, bool pendingQuality,
                 Quality pending);
    struct Copy { GrainSnap grains[32]; int n; TapSnap taps[32]; int nt; float head;
                  Mode mode; Quality quality, pending; bool mono, frozen, pendingQuality; float seconds; int bank; };
    void copy(Copy& out) const;
  };
  std::string viewCaption(const GrainView::Copy&);   // "DIGITAL · STEREO · 4.0 S", "DIGITAL → DIRTY TAPE"
  std::string viewMode(const GrainView::Copy&);      // "GRAINS 23", "DELAY", "SYNTH · BANK 7"
  struct Dash { float x0, x1, y, alpha; };
  int grainDashes(const GrainView::Copy&, float w, float h, Dash* out);   // geometry in the window
  }
  ```
  `SpindriftEngine::view() const -> const GrainView&`; the buffer's
  waveform is an `rka::Overview` over the ring, `touch()`ed as Beads
  writes.

- [ ] **Step 1: Write the failing tests:** `captions` (each quality,
  mono/stereo, seconds to one decimal; pending as the spec's example;
  `SYNTH · BANK 7`); `dashes` (a grain at position 0.5, span 0.1, pan 0,
  env 1 → x0 = w/2, x1 = w/2 + 0.1w, y at the top, alpha 1; env 0 → none
  drawn); `threads_publish_copy` (engine on one thread at full rate,
  copier on another, 2 s: no TSan report, every copy self-consistent:
  `n` ≤ 32, all positions in [0,1]).
- [ ] **Step 2:** Run: fails.
- [ ] **Step 3:** Implement. `GrainWindow` draws as the spec's "The
  window": waveform at low alpha, ivory head, cyan dashes, the two
  captions top-left and top-right in D-DIN Condensed, in the
  self-illuminating layer like `TapeWindow`.
- [ ] **Step 4:** Run: PASS.
- [ ] **Step 5:** Commit: `Spindrift: window`.

### Task 7: Aliasing, hostile input, bench

**Files:**
- Create: `src/spindrift/test_alias.cpp`, `src/spindrift/test_hostile.cpp`,
  `src/spindrift/bench.inc`
- Modify: `tests.mk` (sanitizer rule for `test_hostile`; link
  `BeadsFirmware.cpp` into `bench`, `engine_globals`, `libm_calls` and the
  spindrift tests)

- [ ] **Step 1: Write the failing tests:**
  - `test_alias.cpp` (rule 16): at 44.1 and 48 kHz, each quality, a sine
    through grains at Pitch 0, +12, +24, the synth voice across its
    range, and `EdgeRate` alone: `AliasMeter` alias dB ≤ the reference's
    alias dB (BeadsRef at its own rate, same input, same seed) + 3 dB;
    `EdgeRate` alone below -90 dB. Mutation: `EdgeRate` bypassed at
    44.1 kHz → fails.
  - `test_hostile.cpp` (Review Focus 2): ±12 V, ±12 V DC, NaN and inf
    on each input and CV for 1 s each, every mode and quality: output
    finite, |out| ≤ 10.5 V; 1 s after sane input, output RMS within 1 dB
    of a fresh engine's; no allocation.
- [ ] **Step 2:** Run: fails until wired.
- [ ] **Step 3:** Fix what fails: NaN guards go in the engine before
  Beads (not in vendored code). `bench.inc` cases: `idle`; `outputs`
  (Digital, Density 0.85, 32 grains); `dirty`; `delay`; `synth`. Budgets
  ~1.5x measured, Mac and other (rule 10).
- [ ] **Step 4:** Run `make test` and `make bench`. Expected: PASS;
  `libm-calls` clean on Linux/CI (a per-sample libm call found in vendored
  code is replaced, marked).
- [ ] **Step 5:** Commit: `Spindrift: aliasing, hostile input, bench`.

### Task 8: The module, the menu and the saved buffer

**Files:**
- Create: `src/spindrift/Spindrift.cpp`, `src/spindrift/BufferFile.hpp`
  (Rack-free header and validation), `src/spindrift/test_buffer.cpp`,
  `src/spindrift/testdata/golden.json`
- Modify: `src/plugin.cpp`, `src/plugin.hpp`, `plugin.json` (slug
  `Spindrift`, tags Granular, Delay, Reverb, Oscillator, Effect),
  `Makefile` (`spindrift` in `MODULES`)

**Interfaces:**
- Produces: `struct BufferHeader { uint32_t magic /* 'SPDR' */, version /* 1 */;
  uint8_t quality, mono; uint32_t writePos, bytes; };`
  `bool validBuffer(const BufferHeader&, size_t fileBytes)`.
  JSON keys: `quality`, `seedLatched`, `frozen`, `synth`, `gain`,
  `manualGain`, `gateOutput`, `keepFrozen`, `version`.

- [ ] **Step 1: Write the failing tests** (`test_buffer.cpp`, Review
  Focus 3): `round_trip` (a frozen engine's image saved and loaded into a
  new engine with the same seed: identical output for 2 s);
  `rejects_other_quality`, `rejects_mono_mismatch`,
  `rejects_newer_version`, `rejects_truncated`, `missing_file` → empty,
  unfrozen buffer, `false` returned; `golden_json` (the module's JSON for
  defaults equals `testdata/golden.json`).
- [ ] **Step 2:** Run: fails.
- [ ] **Step 3:** Implement `Spindrift.cpp`: params with Beads' names and
  0..1 ranges, tooltips in Beads' terms; caps lit as the spec ("Panel",
  last two paragraphs); menu items as the spec's "Menu" (Synth mode,
  Input gain with a manual slider, Gate output, Keep the frozen buffer);
  `onSave` writes `buffer.bin` to patch storage only when frozen and
  keep is on, `onAdd` loads it and latches Freeze; engine allocated in
  the constructor; `onSampleRateChange` calls `setSampleRate`; bypass
  routes IN L to OUT L and IN R to OUT R (`configBypass`).
- [ ] **Step 4:** Run `make test`, `make`, `make smoke`; look at
  `build/screenshots/Spindrift*.png` (rule 12).
- [ ] **Step 5:** Commit: `Spindrift: module`.

### Task 9: The listening stop

- [ ] **Step 1:** `tools/bump_version.sh "Spindrift: first test build"`,
  `make install`.
- [ ] **Step 2:** Hand the user the spec's "Listening stop" list as a
  checklist (Time and Size with randomness, Density both ways and in the
  dead zone with Seed, Pitch +24, each quality, delay mode, the synth,
  Freeze saved and reloaded). Record their notes in the spec. Fixes go in
  as their own commits with tests.

### Task 10: Documentation, roadmap and release builds

**Files:**
- Create: `docs/spindrift/README.md` (manual, headings as Eddy's: The
  panel; The window; Controls; Modes; Quality; Inputs and outputs; Menu;
  With the patch; Patches to try; Bypass; Other details), its
  `Spindrift.png`
- Modify: `README.md` (modules table), `CHANGELOG.md`,
  `docs/future/ROADMAP.md` and `docs/future/spindrift.md` (built),
  `CLAUDE.md` (one line: Spindrift's core is vendored Beads in
  `src/spindrift/beads/`, changed only with `// SPINDRIFT:` and a
  `CHANGES.md` line; `make test` checks it)

- [ ] **Step 1:** Write the manual from the source and the spec, covering
  everything Beads' own manual covers.
- [ ] **Step 2:** `tools/cross_build.sh` (Linux and Windows) and
  `tools/mac_dist.sh`. Expected: packages in `packages/`, all build.
- [ ] **Step 3:** Commit: `Spindrift: manual, roadmap`.
