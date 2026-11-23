# Spindrift: Beads in the Instrument look: design

Status: design, 2026-11-22, for the user's approval; checked by a
reviewer against the Beads source the same day, and corrected where it
misread Beads. Panel:
`2026-11-22-spindrift-mockup.png`, drawn through `tools/gen_panels.py`
from a draft description that is not in the repo (its positions are
listed under Panel); the window's contents in it are painted in by hand
as an illustration. Replaces the idea in `docs/future/spindrift.md`.

## Intent

The roadmap put Spindrift next once Fetch was moved down (2026-11-22).
The user's brief:

- "I'd specifically like to mimic as much of Beads as possible. that's
  the gold standard." Review Beads' source
  (`~/ws/plugins/eurorack/beads`) together with what Eddy built, and
  come up with a plan and a mockup.

Then, in the conversation, the user decided:

- **All of Beads**: the grain engine, delay mode, the four quality
  settings, the reverb, and the wavetable synth ("let's keep the
  wavetable *and* reverb as well as the others").
- **Beads' controls, under Beads' names, with Beads' behaviour**, laid
  out in the Instrument look; Density as the hero, lettered on the
  Beaufort scale; the grain window above it; Beads' one assignable CV
  replaced by a CV jack each for Feedback, Dry/Wet and Reverb.
- **Approach A**: bring Beads' DSP in as it is, every change marked.
- **Aliasing**: measure it and keep Beads' behaviour.
- Approved the playback that followed (synth-mode switching, the frozen
  buffer kept with the patch, the hidden settings in the menu, 20-odd HP,
  what the window shows) with "go for it".

So Spindrift is Beads, made to sound like Beads by running Beads' code,
in RKA's panel and with a window Beads never had. Anyone who knows Beads
should be able to play it without the manual; anyone who does not should
find in the manual everything Beads' own manual says.

The picture stays the sketch's: spindrift is the spray the wind tears
off the crests. Density is the wind, so its face is a Beaufort scale.

## Research, in short

Beads' firmware is MIT-licensed (Emilie Gillet, 2017-2020); RKA is
GPLv3, so it can be vendored with its notice kept. No reference plugin
beside this repo ports Beads (AudibleInstruments stops at Clouds), so
this is a port from the firmware, not from another port.

What the firmware is, by file (`beads/` in the eurorack repo):

| Part | Files | Lines |
|---|---|---|
| The chain: mode switch, dry/wet, reverb, limiters | `dsp/processor.h/.cc` | 470 |
| Buffer, feedback, quality resolution, SRC | `dsp/granular_processor.h`, `dsp/delay_line.h`, `dsp/mu_law.*`, `dsp/sample_rate_conversion_filters.h` | 1,100 |
| Grains | `dsp/granular_player.h`, `dsp/delay_granular_player.h`, `dsp/grain.h`, `dsp/delay_grain.h`, `dsp/grain_envelope.h`, `dsp/voice_stealing_envelope.h` | 850 |
| Parameters and random wander | `dsp/randomized_parameter.h`, `dsp/modulated_parameter.h` | 200 |
| Density: grain triggers, clock divider | `dsp/trigger_generator.*` | 425 |
| Delay mode | `dsp/delay_controller.*`, `dsp/delay_clock.*`, `dsp/multitap_delay.h`, `dsp/tap.h` | 940 |
| Reverb, wow and flutter | `dsp/fx/reverb.h`, `dsp/fx/fx_engine.h`, `dsp/fx/wow_and_flutter.h`, `dsp/limiter.h` | 700 |
| Wavetable synth | `dsp/synth.*`, `dsp/wavetable_*.h`, `resources.cc` (`wav_integrated_waves`, 49,920 samples) | 800 + tables |
| Panel behaviour (not vendored, re-written) | `beads.cc`, `ui.cc`, `cv_reader.*`, `gain_control.*`, `background_buffer_backup.*` | 1,400 |

From `stmlib` it needs `dsp/{dsp,filter,units,parameter_interpolator,
sample_rate_converter,hysteresis_quantizer,hysteresis_filter,
cosine_oscillator,delay_line}.h`, `utils/{random,gate_flags,dsp}.h`,
`stmlib.h` and the units tables.

Behaviour that matters and is easy to miss (all from the source):

- **The attenuverters are two controls in one.** With nothing in a
  parameter's CV jack, its attenuverter sets how much randomness that
  parameter gets. In grain mode each grain draws its own value: clockwise,
  a uniform random offset scaled by the knob; anticlockwise, a random
  amount of randomness per grain, up to three times the knob
  (`randomized_parameter.h:111-126`). Only where Beads passes a random
  walk in (Pitch, Time and Shape in delay mode; the synth voice's wave
  position) does the knob give a smooth walk, its sign choosing the
  walk's speed, x4 or x0.25 (`randomized_parameter.h:70`). With a cable
  in, clockwise scales the CV and anticlockwise lets the CV's positive
  part set how much random is added. The law has a small dead zone at
  noon (`cv_reader.h`, `LAW_ATTENUVERTER`).
- **Density is bipolar with a dead centre.** Anticlockwise of noon:
  regular grains, faster as it turns, up to 130.8 Hz. Clockwise: random
  (Poisson-like) grains, faster as it turns. Within 0.48 to 0.52: no
  grains of its own; each Seed press or trigger makes one
  (`trigger_generator.cc`). Density's CV is linear near noon and becomes
  exponential FM further out.
- **Seed is latched at power-up**, so the cloud runs. A press unlatches
  it; unlatched, grains come only while the button is held or the SEED
  gate is high, at Density's rate, and in Density's dead zone each press
  or trigger makes one grain. Latched, the dead zone makes no grains.
  Holding Seed 4 s, or pressing Freeze while holding Seed, latches it
  again (`trigger_generator.cc:95, 257, 280-287`). Latched, with a clock in the SEED
  jack, Density becomes a divider (left of noon: /1 to /16) or a
  probability (right of noon) on that clock. The SEED jack is read
  2.5 ms late so the CVs that come with it have settled.
- **Size** below a third plays grains backwards; turned fully clockwise
  (past 0.99, back below 0.98) it crosses over 1.5 s into **delay mode**.
- **Pitch** is ±24 semitones on a knob that snaps to 0, 1, 3, 4, 7, 12
  and 24 (`LAW_DETUNE`); its CV is 12 semitones a volt at the
  attenuverter's full clockwise.
- **Quality** (the Media button) cycles Digital (48 kHz, 16 bit), Vintage
  Digital (32 kHz, 12 bit), Clean Tape (12 bit, run at 24 kHz inside a
  48 kHz codec), Dirty Tape (8-bit mu-law at 24 kHz, wow and flutter, the
  codec itself slowed to 24 kHz). The buffer is one fixed block of memory,
  so its length depends on the setting: 4 s, 8 s, 10.7 s and 16 s in
  stereo, twice that in mono. Beads goes mono when either audio input is
  unpatched, copying the patched side to the other
  (`normalization_detector.h:67-70`). Feedback is soft-clipped harder at
  the lower resolutions. A change of quality, of mono and stereo, or into
  or out of synth mode clears the buffer and **mutes the output for
  250 ms** (`beads.cc:169-176, 292`); nothing is crossfaded. While frozen,
  all three changes are held back and applied on unfreeze
  (`ui.cc:416-418`, `beads.cc:352`).
- **The reverb** is Beads' own (an `FxEngine` network), brighter in
  Digital than in the other three.
- **Synth mode**: 10 s with no audio input patched and the output near
  silence (under 1 % of full scale) swaps the whole chain. Beads starts in
  grain mode; it does not enter synth mode at power-up
  (`beads.cc:340, 357-391`). Synth mode swaps the whole chain for a wavetable voice and
  a wavetable grain cloud. Feedback chooses one of 12 banks, Time the wave,
  Pitch the note (V/oct), Dry/Wet blends the voice and the cloud, Quality
  the bit depth (4, 16/6, 7, 16). Patching an input switches back.
- **Hidden settings** in shipping Beads: gate output mode (the right
  output becomes a gate per grain, the left a mono mix), manual input
  gain, autosave of a frozen buffer. Monophonic grains and buffer trim
  exist in the source but are compiled out (`ENABLE_ADVANCED_FEATURES` is
  commented out, `ui.h:43`), so they are not part of Beads as sold.
- **Full scale** is about ±10 V on the hardware: the synth's dry gain of
  0.5 is commented as "a 10Vpp output" (`synth.cc:98`). The dry path in
  grain mode is at 0.75 (`processor.cc:211`).

What Eddy left that Spindrift uses, and what it does not:

- **Used**: the window's approach (`TapeWindow` in `src/widgets.hpp`:
  glass, cyan strokes, a module-supplied view copied from relaxed atomics
  each frame), `Overview` (`src/Overview.hpp`) for the buffer's
  waveform, the aliasing meter (`rkatest::AliasMeter`), the patch-storage
  pattern for audio kept with the patch, the hostile-control test.
- **Not used**: `Tape`, `TapeWriter`, `BandLimitedRead`, `Resample`,
  `WavFile`, Eddy's voices and file jobs. Beads has its own buffer (a
  ring at 8, 12 or 16 bits), its own read (Hermite), its own SRC. Using
  Eddy's read would cost about 30 times as much across 32 grains and
  would not sound like Beads.

## Goals, non-goals, anti-goals

**Goals**

- Sounds and behaves like Beads: at the same settings, on the same
  input, Spindrift's output is Beads' output (a deterministic comparison
  against the firmware's code run unchanged is in Testing).
- Every Beads control and setting is reachable: on the panel if Beads has
  it on the panel, in the menu if Beads hides it behind a button combo.
- The window shows what Beads can only hint at with LEDs: the buffer, the
  record head, each grain, the taps in delay mode, the wave in synth
  mode, the quality and the buffer's length.
- Runs well in Rack: no allocation on the audio thread, instances
  independent, any sample rate, a frozen buffer survives saving the
  patch.

**Non-goals**

- Improving Beads' sound. Its aliasing, its quantisation, its limiter
  and its reverb are kept, measured and written down.
- Polyphony. Beads is one stereo instrument; issue #64 does not apply.
- Loading or saving audio files. Eddy does that; Fetch would.
- Beads' calibration, factory test and bootloader.

**Anti-goals** (failures even if it works)

- A granular processor that is "inspired by" Beads and sounds different.
- Beads' panel combos reproduced as combos: in Rack a hidden two-button
  gesture is worse than a menu item.
- A window that costs more frame time than the rest of the patch's
  widgets, or that reads the engine's memory unsafely.

## Approaches

Three were considered; the user chose **A**.

- **A. Vendor Beads' DSP as it is** (chosen). The DSP and the parts of
  stmlib it uses are copied into `src/spindrift/beads/` and
  `src/spindrift/stmlib/`, as Shoal vendors the O&C firmware: every edit
  carries a `// SPINDRIFT:` mark and a line in
  `src/spindrift/CHANGES.md`. Beads' chain runs at its own codec rates,
  with conversion to and from Rack's rate at the edges. Most faithful,
  least new code; the cost is code in another house style to live with.
- **B. Port line by line into RKA style** (our `DelayLine`, FastMath,
  Rack's rate). Easier to test the RKA way; every rewritten line a
  chance to drift from Beads; about three times the work. Rejected.
- **C. Build on Eddy's tape and band-limited read.** Less aliasing, about
  30 times the CPU at 32 grains, not Beads' sound. Rejected as against
  the brief.

## Panel (20 HP)

As the mockup, which passes the generator's checks at these positions
(mm). The top rows use five columns at x = 7.8, 23.5, 50.8, 78.1, 93.8
(Density in the middle one); the bottom rows five at x = 7.8, 29.3, 50.8,
72.3, 93.8.

- Window: 91.6 x 14.0 at (5.0, 19.5), with a ruler under it in tenths
  (px = 91.6 / 320).
- y = 48: **Time**, **Size**; Density, the hero, at (50.8, 53.0) on a
  face of radius 15; **Shape**, **Pitch**.
- y = 61: the four attenuverters under Time, Size, Shape and Pitch.
- y = 73: their CV jacks (Pitch's lettered V/OCT directly beneath it,
  clear of Seed's label), and Density's CV under the hero. Each traced to its control through its
  attenuverter.
- y = 90: the **Freeze** button (a lit cap) in the first column;
  **Feedback**, **Dry/Wet**, **Reverb**; the **Seed** button in the last.
- y = 102: IN L; Feedback, Dry/Wet and Reverb CVs (traced up); OUT L.
- y = 116: IN R; FREEZE gate (lettered GATE); the **Quality** button (a
  cap); SEED trigger (lettered TRIG); OUT R.
- **The audio jacks are where Wake's, Narrows's and Eddy's are** (the
  user asked, 2026-11-22): the inputs one over the other in the first
  column and the outputs in the last, at y = 102 and 116, L over R,
  outputs on ivory, 7.8 mm from the panel's edges.

Density's face is lettered 0 at noon and 4, 8, 12 both ways (Beaufort
force), with REGULAR under its left side and RANDOM under its right.
The Beaufort numbers are a picture of strength, not a unit: Density has
no unit Beads states, and its two sides are different processes.

The quality setting is not shown by a light. Beads shows it by the
colour of one LED (magenta, orange, teal, white); the Instrument look
allows only cyan for lights, and four lights with words would not fit.
It is lettered in the window instead, with the buffer's length. (My
call; the Quality cap is lit during Beads' 250 ms mute after a change,
and a setting chosen while frozen shows in the window as pending, e.g.
"DIGITAL → DIRTY TAPE".)

Seed's cap follows Beads' LED (`ui.cc:204-210`): latched, it breathes
slowly; unlatched, it is lit while the button is held or the SEED jack
fired in that block. Freeze's cap is lit while frozen, by the button or
the gate.

## Controls and behaviour

Everything here is Beads' behaviour, through Beads' code, unless it says
otherwise.

### The grain engine

Time, Size, Shape, Pitch, their attenuverters and CVs, Density and its
CV, Seed and Freeze behave exactly as listed under Research. Ranges and
laws are Beads': the knobs feed `Parameters` and `ModulatedParameter`
with the same 0..1 values the firmware's `CvReader` produces, through
the same pot laws (`LAW_ATTENUVERTER`, `LAW_DETUNE`) and the same
one-pole smoothing and hysteresis, ported into the module's control
code.

CV scaling, from Beads' calibration targets (`calibrator.h`): Pitch
12 semitones per volt; every other CV ±5 V for ±1 of the knob's travel.
A CV counts as patched when its jack has a cable (Beads' normalisation
probe).

### Delay mode

Size fully clockwise. Beads' `DelayController` and `MultitapDelay` as
they are: Time sets the delay (or, with a clock in SEED, a ratio of the
clock), Density the pattern of repeats, Shape their envelope, Pitch the
playback speed, Feedback the regeneration. The manual is written from
`delay_controller.cc`, not guessed.

### Quality

The Quality button steps Digital, Vintage Digital, Clean Tape, Dirty
Tape, and round, as Beads (`(media + 3) % 4`, starting on Digital).
Changing it clears the buffer and mutes for 250 ms, as on Beads. While
frozen, the new setting is chosen but held back until Freeze is
released, as on Beads; the window shows it as pending. A long press
(1.5 s) restores automatic input gain, as on Beads.

The chain runs at Beads' codec rate for the setting
(`processor.h:78-85`): 48 kHz for Digital and Clean Tape (Clean Tape
decimates 2:1 inside, as Beads does), 32 kHz for Vintage Digital, 24 kHz
for Dirty Tape (Beads' `ENABLE_HARDWARE_24K` is kept, so its dry signal
is 24 kHz too, as on the hardware). Blocks are Beads' 4 kHz block rate:
12 frames at 48 kHz, 8 at 32 kHz, 6 at 24 kHz. Spindrift converts
Rack's rate to that rate on the way in and back on the way out, with one
stereo converter pair, so Beads' code never sees Rack's rate.

### Reverb, Feedback, Dry/Wet

Knobs with a CV each (Beads has one CV for all three, with an Assign
button; three jacks replace it, which the user chose). Beads' laws:
Dry/Wet is equal-power-ish (`(1 - x)(1 + x)` and `x(2 - x)`), Reverb
sets mix and time together, the output limiter after everything.

### Synth mode

As Beads: Spindrift starts in grain mode, and enters synth mode when no
audio input has been patched for 10 s and the output has stayed under
1 % of full scale (and it is not frozen); it leaves the moment an audio
input is patched. Each switch mutes for 250 ms. A menu item, **Synth
mode**, chooses Automatic (Beads), Always or Never. The window says
SYNTH while in it.

### Levels

**Voltages (my call, from Beads' hardware and the house standard).**
Beads' full scale is ±10 V, so Spindrift maps ±10 V to Beads' ±1 on
the way in and ±1 to ±10 V on the way out. A ±5 V signal therefore sits
at half scale, with Beads' headroom above it, and comes out as Beads'
hardware would give it: at 0.75 dry (±3.75 V) with Dry/Wet at 0, and
the synth's dry oscillator at ±5 V. No polarity is inverted. In gate
output mode the right output is a 0/10 V gate (the house standard), not
Beads' raw -0.8 of full scale.

**Input gain.** Beads' `GainControl` has an automatic gain that settles
on the input's peak, and a manual gain (hold Quality, turn Feedback)
whose path also hard-clips at full scale and soft-clips the input in
Clean and Dirty Tape. **My call:** the default, **Fixed**, runs through
`GainControl`'s manual path at the gain that makes ±10 V full scale, so
hot Rack signals get Beads' own clipping and tape saturation. The menu
also offers **Automatic (as Beads)** and **Manual** (with a slider).
The long press on Quality goes to Automatic, as on Beads.

### Menu

- Synth mode: Automatic / Always / Never.
- Input gain: Fixed / Automatic / Manual (and its level).
- Gate output (Beads' gate output mode): the right output becomes a gate
  per grain, the left a mono mix. Off by default.
- Keep the frozen buffer with the patch (Beads' autosave). On by
  default.

Monophonic grains and buffer trim are left out (my call): they are
compiled out of shipping Beads, and the brief is Beads as sold. The
vendored code keeps them, unreachable, so they can be added later.

### The window

A new widget, `GrainWindow`, beside `TapeWindow` in `src/widgets.hpp`,
on the same glass and in the same cyan. Top-left, the quality, STEREO
or MONO, and the buffer's length ("DIGITAL · STEREO · 4.0 S"); top
right, the mode and the number of grains sounding ("GRAINS 23", "DELAY",
"SYNTH · BANK 7").

- **Grains**: the buffer as a faint waveform (an `Overview` over the
  ring, fixed on the glass as in the mockup, the record head moving
  across it as an ivory line; decided during the build, 2026-11-22), and each
  sounding grain as a short bright dash at its read point, its length
  the grain's span in the buffer, its height its pan (left at the top),
  its brightness its envelope. Frozen, the head stops and the waveform
  stays.
- **Delay**: the waveform and head, and a tick for each tap at its
  delay, as tall as its gain.
- **Synth**: the current wave of the bank (both neighbours blended as
  the voice hears them), and the grains as dashes at their wave
  position.

Data reaches it as Eddy's does: the engine publishes, per control block,
a fixed array of up to 32 grains (position, span, pan, envelope) and the
head in relaxed atomics; the widget copies them each frame. Reading the
grains needs a small marked accessor in `DelayGrain` and
`WavetableGrain`.

### Saved with the patch

Every parameter, the quality, the menu settings, Seed's latch and the
Freeze button. With the buffer frozen (by the button or the gate) and
"keep the frozen buffer" on, the buffer's raw bytes (768,000, what
Beads' backup saves) are written to the patch's storage folder as
`buffer.bin` with a small header (quality, mono, write position, format
version); on load they go back before the first sample is processed,
and the Freeze button is latched on, as Beads does after restoring a
backup (`beads.cc:344-347`), so a buffer frozen by the gate is not
overwritten when the gate is low after loading. Without a freeze nothing
is stored: Beads' buffer is a moment of input, not a recording.

### Bypass

L to L and R to R, as Rack's default.

## Code and files

As every module (CLAUDE.md, TESTING.md "Adding a module"), with Shoal's
rules for vendored code:

- `src/spindrift/beads/` and `src/spindrift/stmlib/`: the vendored files,
  paths as upstream, the MIT notice kept, `LICENSE-beads.md` at the repo
  root beside `LICENSE-shoal-firmware.md`. `src/spindrift/CHANGES.md`
  lists every change; each carries `// SPINDRIFT:`.
- **Changes expected** (the builder adds what the port finds):
  1. `stmlib::Random`'s single static state becomes per-instance (an
     instance pointer or a thread-local swapped in per `process`), so two
     Spindrifts do not share a random stream (TESTING.md rule 8), and the
     tests can seed it.
  2. Variable-length arrays in `Processor::Process` and
     `GranularProcessor::Process` become fixed arrays sized to the
     largest block the module passes.
  3. The buffers (786,432 bytes of delay memory, three 262,144-byte
     banks as `beads.cc:55-56`, since `DelayLine` keeps its freeze tail
     past the 768,000 saved bytes; and Beads' reverb buffer) are
     allocated when the module is added, never on the audio thread.
  4. Accessors for the window: each grain's read point, span, pan and
     envelope; each tap's delay and gain; the current wave.
  5. Synth mode's and the processor's selection moved out of
     `beads.cc` into a Rack-free `SpindriftEngine` (below), since
     `beads.cc` is not vendored.
- `src/spindrift/SpindriftEngine.hpp`: Rack-free. Owns the `Processor`,
  the `Synth`, the trigger and gate handling, the rate converters, the
  synth-mode switch, the 250 ms mutes, the 2.5 ms SEED delay, the
  input gain and the block loop (Beads' 4 kHz block rate: 12, 8 or 6
  frames, with one block of latency). Takes control values per block;
  renders.
- `src/spindrift/Controls.hpp`: Rack-free port of `CvReader`'s pot laws,
  smoothing and hysteresis, and `Ui`'s Seed latch, long presses and
  Quality stepping.
- `src/spindrift/Spindrift.cpp`: maps panel, jacks, menu and the patch
  storage onto the engine; no DSP.
- `GrainWindow` in `src/widgets.hpp`, its geometry Rack-free and tested.
- Rate conversion at the edges: Rack's `dsp::SampleRateConverter` is
  available but allocates nothing only once set up; set it up off the
  audio thread on a rate change. The builder may instead vendor stmlib's
  `SampleRateConverter` for the 2:1 case and use Rack's for the rest;
  whichever, it is measured in `test_alias.cpp` and the bench.
- `src/spindrift/bench.inc`, the panel entry in `tools/gen_panels.py`,
  plugin.json (slug `Spindrift`), manual `docs/spindrift/README.md`,
  README table, CHANGELOG, the roadmap and `docs/future/spindrift.md`
  marked built.

## Aliasing (docs/DSP-ALIASING.md)

Beads is kept as it is; this section names each source and says how it
is measured, as the rule asks.

1. **Grains read faster than 1** (Pitch above 0, or wander upwards):
   Beads reads with a Hermite interpolator and no low-pass. This aliases,
   and it is part of the sound. Not remedied.
2. **Grain envelopes** (audio-rate AM): Beads' envelopes have a minimum
   attack of 0.5 ms and release of 10 ms. Not remedied.
3. **The quality settings** decimate on purpose (24 and 32 kHz, 8 and 12
   bits); the converters at Spindrift's edges are the only filters added,
   and they are what keeps Spindrift's output under Rack's Nyquist when
   Beads' rate differs from Rack's.

`src/spindrift/test_alias.cpp` (rule 16) measures, at 44.1 and 48 kHz
and in each quality: a sine through grains at Pitch 0, +12 and +24; the
wavetable voice across its range; the converters alone. **The bound is
what Beads' code measures run unchanged at its own rate for that
quality (48, 32 or 24 kHz) on the same input, plus a stated margin**, so the test fails if Spindrift aliases more
than Beads, not if Beads aliases. The mutation that must fail it is
breaking the edge converters (Rack at 44.1 kHz into Beads' 48 kHz
without filtering).

## Testing

TESTING.md applies in full. Particular to Spindrift:

- **Beads to the bit.** The reference is Beads' `Processor` and `Synth`
  built unchanged from `~/ws/plugins/eurorack` (with stmlib's own
  `Random` reseeded), **with the same compiler flags as the plugin**
  (Rack's `-funsafe-math-optimizations` included, rule 9), fed the same
  `Parameters`, `GateFlags` and input per block. `beads.cc` is hardware
  glue and cannot be built, so the reference stops at `Processor` and
  `Synth`; the glue Spindrift re-implements (the SEED delay, the mutes,
  the synth switch, the pot laws, the gain) is tested on its own against
  the firmware's code read side by side (rule 2). With the same seed,
  at Beads' own rate for each quality (no edge converters), the engine's
  output matches the reference to float tolerance, in grain mode, delay
  mode, synth mode, frozen, Seed latched and unlatched, with a clock in
  SEED. This is the test that says "sounds like Beads"; a mutation in any
  vendored file's maths must fail it.
- **Controls.** Every pot law against Beads' `CvReader` maths (Pitch's
  snap points; the attenuverter's dead zone). The Seed latch: latched at
  add; a press unlatches; a 4 s hold or Seed + Freeze latches.
- **Density.** Rates at the knob's ends (130.8 Hz regular); unlatched,
  one grain per Seed press in the dead zone; latched, none there;
  divider and probability on an external clock.
- **Mode crossings.** Size to fully clockwise and back: the 1.5 s
  crossfade, no click above a stated level. Synth mode: not entered at
  add; entered after 10 s with no input and a quiet output; left when an
  input is patched; 250 ms of silence each way; the menu's three
  choices.
- **Quality.** Buffer length per setting, stereo and mono (4, 8, 10.7,
  16 s; doubled); the change clears the buffer and mutes 250 ms; frozen,
  the change waits for unfreeze (as do mono/stereo and synth changes).
- **Levels.** ±10 V in is full scale; the dry path at Dry/Wet 0 is 0.75;
  gate output mode gives 0/10 V.
- **Instances.** Two Spindrifts with the same controls and input but
  different seeds differ; with the same seed, they match (rule 8).
- **Saved state.** A golden JSON; a frozen buffer saved and loaded plays
  back the same grains under a fixed seed; a buffer file from another
  quality, a truncated file and a missing one each load as an empty,
  unfrozen buffer with a log line, not a crash.
- **Rates.** 44.1, 48, 96 kHz and a change while running: no allocation
  on the audio thread, the buffer cleared (a frozen buffer kept: it is
  in Beads' rate, not Rack's).
- **Window.** The view's geometry from a fixed set of grains; the copy
  is safe with the engine running on another thread (rule 7).
- **Bench.** Idle; Digital at Density full random with 32 grains;
  Dirty Tape; delay mode; synth mode. Budgets as rule 10.

## Listening stop

Before the build is called done, the user plays it with Beads in mind:
Time and Size with wander, Density both ways and in the dead zone with
Seed, Pitch up two octaves, each quality, delay mode, the synth, Freeze
saved and reloaded. Notes go into this document.

## Left to the builder

- How `stmlib::Random` is made per-instance (pointer or swapped state),
  as long as the reference test still compares to the bit.
- The exact block size handed to Beads' code at 44.1 or 96 kHz (Beads'
  own 12 frames at its own rate is the default).
- Whether the reverb buffer and the delay buffer share one allocation.
- The grain dash's minimum length and the waveform's column count in the
  window.
- The manual's wording, written from the source, with Beads' manual as a
  guide to what to cover.
