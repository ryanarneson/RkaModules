# Rigging 2.0.39: Technical Reference for QA

Module: **Rigging**, plugin `rka` (slug `rka`, module slug `Rigging`), VCV Rack 2 (minRackVersion 2.5.0). 14 HP. Six tuned strings.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from that tree.
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. Its `plugin.json` says 2.0.39. Its `res/rigging/Rigging.svg` and `Rigging-dark.svg` are byte-identical to the commit's.
Document date: 2026-11-24.
Rigging's own files were last changed in 2.0.17. Section "Build history" has the details. Rigging has no factory presets.

## Build history

Rigging has been in two test builds. Nothing in `src/rigging/`, `docs/rigging/`, `res/rigging/` or the design spec has changed since 2.0.17 (`git log 2e6e960 -- src/rigging docs/rigging res/rigging docs/specs/2026-11-19-rigging-design.md` ends at `d8a9991`).

| Build | Date | Commit | What changed |
|---|---|---|---|
| 2.0.16 | 2026-11-19 | `51e4b51` | First build for listening: six tuned strings, drone from any sound, struck by STRUM or by a new note at V/OCT. The module was designed, built and tested in one day (13 commits, `48e3d9d` design to `4a33300` manual). |
| 2.0.17 | 2026-11-19 | `5a15fa6` (code `d8a9991`) | Final review fixes: the pitch glide now ends exactly on its target; DC cannot pile up in a string or thump through a level knob (5 Hz DC block before each level); Ring changes ramp over 16 samples instead of stepping; a slewed note is one strike; Initialize cancels a rake in progress; typed values in the Ring, Spread and Chord tooltips; manual. |
| 2.0.18 to 2.0.39 | 2026-11-20 to 11-24 | | Other modules only. Shared files touched since: `src/DelayLine.hpp` (an added function, `fadeOutNewest`, for Wake; Rigging does not call it), `src/widgets.hpp` and `tools/gen_panels.py` (other modules' widgets and panels). `src/testing_alias.hpp` (the aliasing meter) was added on 2026-11-22. |

What the later history means for QA:

- Rigging was built before TESTING.md rule 16 (aliasing is measured) existed. It has **no aliasing test**; TESTING.md's table says "Issue #67". Section 8 gives my measurements.
- The open issues about Rigging (#53, #54, #55) were filed on 2026-11-20 from the final review and are unchanged.

---

## 0. How to read this document

- Every number comes from one of these sources, marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/rigging/*.hpp`, `src/rigging/Rigging.cpp`, `src/DelayLine.hpp`, `src/FastMath.hpp`), cited as file:line.
  - **[test]**: asserted by the module's automated tests. I rebuilt them from the 2.0.39 tree with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; the hostile test with ASan and UBSan at `-O1`) on an Apple M-series Mac. `test_chords`, `test_string`, `test_strike`, `test_engine` and `test_hostile` all passed. **The tests print nothing but their verdict**, so a [test] figure is the asserted tolerance, not a printed measurement.
  - **[meas]**: a small program I wrote and ran against the Rack-free engine (`rka::RiggingEngine`, or `rka::rigging::String` where stated), with the same flags, at 48 kHz unless stated. Section 14 lists them.
- **[issue #N]** is an open GitHub issue in `ryanarneson/RkaModules`.
- **[unverified]** is anything I could not check, mostly Rack host behaviour.
- Voltages are Rack volts. Noise is uniform white noise ("±5 V noise" = uniform in −5..+5 V, RMS 2.89 V). "Noon" means knob 0.5.
- "Fundamental T60" is the time for the string's fundamental to fall 60 dB, measured from the envelope between −10 dB and −30 dB, times 3.
- Things I ran: five test programs (above); `bench --check` (15 module declarations complete, including Rigging's); `make engine-globals` (the engine holds no writable global state: passed); `python3 tools/test_gen_panels.py` (115 tests, OK); `make bench` for Rigging. I did **not** run `make libm-calls` (Linux only, skipped on macOS [issue #36]), `make smoke`, the Rack plugin build, or anything in Rack.
- The listening list `rigging-2.0.39-listening.html` is not a source. Where it disagrees with the code, section 12 says so.

---

## 1. What Rigging is

Rigging is a 14 HP module of six tuned strings. It is a resonator that makes a tuned drone from any sound in the audio inputs, and a struck voice from a trigger, on the same strings in the same tuning. The picture is wind singing in a ship's rigging. The design intent (spec `docs/specs/2026-11-19-rigging-design.md`) is the user's: "definitely slow tuned drone, but also struck and decay as hits/stabs in the mix."

Each string is a Karplus-Strong waveguide: a delay line fed back through a damping low-pass, with its own loop gain set from Ring. The method follows Mutable Instruments Rings (read, not copied).

Design goals that QA can check:

- A steady, full tuned drone from noise or any audio, lasting as long as the input does and fading over up to a minute after it.
- A struck voice from a trigger alone: from a stab (all six at once) to a 1.5 s rake that tolls for tens of seconds.
- Seven tunings for the six strings (Unison, Octaves, Fifths, Minor, Sus, Cluster, Bell).
- A level knob per string.
- In tune: V/OCT at 1 V per octave, at every Damp and Ring setting.
- CV over Chord, Ring and Damp.
- Anti-goals: no runaway level (a loud input at the longest Ring must not blow up or clip harshly); no drone whose loudness swings as Ring is turned; no clicks or zipper noise when Chord, Tune, V/OCT, Ring or Damp change; no string out of tune with another; no DC at the outputs.

Non-goals (do not file these as defects): bars, bells and other materials ("Bell" is six harmonic strings at a bell's spacing); polyphony (one set of six strings; polyphonic CVs use channel 1) [issue #64 holds the design question]; a drone with nothing patched (Rigging has no sound of its own); strike strength from the trigger's voltage; attenuverters; CV for Spread, Mix, Tune or the levels; saving the strings' ringing with the patch.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Test against its behaviour; the spec's and the manual's versions are given for context. I have not filed any of these as issues.

| Topic | Spec / manual says | 2.0.39 does |
|---|---|---|
| Pitch glide time | spec "about 30 ms (to be set by ear)"; manual "over a few hundredths of a second" | a log-pitch one-pole of **8 ms** time constant [code `RiggingTuning.hpp:16`]. Octave jump, string 1: 63 % at 7.7 ms, 90 % at 18.4 ms, within 3 cents at 47.7 ms [meas]. The listening list knows this ("8 ms (the spec first suggested 30 ms)") |
| Note-strike threshold | spec "more than 0.4 semitone (33 mV) within a millisecond"; manual "more than about a third of a semitone" | 1/30 V (33.3 mV) per millisecond [code `:63`]. A one-sample step must exceed **34.0 mV (0.41 semitone)**: 34 mV down does not strike, 40 mV does. A ramp strikes above 33.3 V/s (1 V in 20 ms strikes, 1 V in 30 ms does not) [meas]. The manual's "a third of a semitone" would be 28 mV |
| Strike level | spec "one string alone peaks about ±2.5 V; all six at once about ±6 V, under the soft limit's knee where it can be"; manual "1.5 to 4 V; about 6 V" | 100 seeds, defaults: string 1 alone median 1.48 V (1.17 to 1.95), string 3 2.58 V, string 6 3.69 V (2.65 to 4.56). All six, Fifths: median 5.88 V, range 4.58 to **9.35 V**. The 8 V knee is passed in some strikes (section 7.3) [meas]. The manual's figures fit the medians, not the ranges |
| Highest strings and Ring | spec "60 s up to 1 kHz; at least 10 s up to 2 kHz"; manual "10 s at 2 kHz, about a second above 3.5 kHz" | better than promised at 2 kHz (fundamental T60 60.0 s at 48 kHz, 54.8 s at 44.1 kHz). Above 3.5 kHz it is **not monotonic and depends on the sample rate**: 3,520 Hz 2.1 s (48 kHz) or 1.3 s (44.1 kHz); 5 kHz 0.36 s or 0.69 s; 8 kHz 60 s (48 kHz, exact 6-sample period) or **0.03 s** (44.1 kHz); 12 kHz 60 s or under 0.02 s (below my measurement window). The cause is the Hermite read's loss at the fraction of the period (section 5.4). Reachable only through V/OCT [meas] |
| What the knobs reach | spec "about 2.1 kHz: C4 with Bell's top string" | the top reached by the knobs is 2,093 Hz, from **Octaves** string 6 at Tune +24. Bell's string 6 at Tune +24 is 1,569.8 Hz. The number is right and the chord named is wrong [meas, section 5.1] |
| Damp fully right | spec "settles almost at once to a soft, near-pure tone"; manual "no string rings for more than about two seconds" | fundamental T60 at Damp 1: 1.5 s at Ring 4 s, 0.69 s at Ring 1 s, **2.2 to 2.3 s at Ring 60 s** (65 Hz to 12 kHz), 1.8 s at 16 Hz. So "more than about two seconds" is exceeded by 0.3 s at Ring 60 s [meas] |
| Loudness across Ring | spec "within 6 dB over the whole of Ring, for white noise in, at the default Tune, Chord and Damp" | holds: 2.4 dB with 60 s windows over 3 noise seeds, 3.0 dB with 8 s windows. Not promised, and not met, at other Damp: **6.3 dB at Damp 0, 7.1 dB at Damp 1** [meas] |
| Chord CV hysteresis | spec "a little hysteresis" | a change needs the sum to pass **0.6** of a setting from the held one (`Chords.hpp:58`). Going up the change is at +0.6 setting (0.857 V) past a setting, going down at 0.4 (0.571 V): 0.29 V of hysteresis, and the up-going change points are not at the middle between settings [meas, section 2.3] |
| Bypass and NaN | spec and manual "NaN or infinite values on any input read as 0 V" | in processing, yes. **Bypass copies the raw input sum**, so a NaN at an input is a NaN at the output while bypassed [code `Rigging.cpp:219-226`; not run in Rack]. Rack's bypass is then the only path for a NaN to reach an output |
| Dry path at Mix < 1 | spec "keeps anything hotter under about ±10 V" (the strings) | the **dry** input passes at its own level, unclamped and unlimited, whatever its size (the strings hear at most ±24 V). Mix noon with a ±24 V input reads up to ±17 V [code `RiggingEngine.hpp:173`] |
| ±24 V ceiling on what the strings hear | manual only ("the strings hear an input only up to ±24 V"); spec silent | `INPUT_MAX = 24` [code `RiggingTuning.hpp:47`] |
| JSON `freePitch` missing | spec and manual silent | a missing key (or any value but boolean `true`) loads as **false** [code `Rigging.cpp:186-189`] |
| Aliasing | spec has no aliasing section (it predates `docs/DSP-ALIASING.md` by three days) | no anti-aliasing and no test. Section 8 |
| Drone loudness across Tune and Chord | spec silent | not constant: Tune −24 to +24 raises the drone 9.6 dB; Cluster is 7.6 dB under Fifths (section 7.1) [meas] |
| CPU | manual "about 0.15 % of a CPU core as a drone, 0.3 % with every CV moving" | matches my bench (0.15 % and 0.30 %) [meas] |

---

## 2. Panel, controls and ports

### 2.1 Layout

14 HP. From the top (positions in mm from `RiggingLayout.hpp`, generated, [code]):

| Control | Centre (x, y) mm |
|---|---|
| String levels 1 to 6 | (9.0, 33.0), (19.6, 39.5), (30.2, 43.8), (40.8, 46.6), (51.4, 48.2), (62.0, 48.8) |
| TUNE (dial) | 16.0, 62.5 |
| CHORD, RING, DAMP knobs | 38.0, 50.5, 63.0 at y 64.0 |
| V/OCT | 16.0, 83.0 |
| CHORD CV, RING CV, DAMP CV | 38.0, 50.5, 63.0 at y 83.0 |
| STRUM | 16.0, 102.0 |
| SPREAD knob, strike light, MIX knob | 38.0, 50.5, 63.0 at y 102.0 |
| IN L, IN R | 10.5, 116.0 and 24.0, 116.0 |
| OUT L, OUT R (on ivory tiles) | 50.5, 116.0 and 63.0, 116.0 |

The face has a title RIGGING, the subtitle SIX TUNED STRINGS, a masthead mark with six brass stays to the six level knobs, and the Tune dial lettered −2, −1, 0, +1, +2 (octaves). Each CV jack is joined to its knob by a trace and marked "CV". The panel preview (`Rigging.png`) shows the module at its defaults [code: `tools/gen_panels.py`].

### 2.2 Parameter, input, output and light ids

Ids follow the enums in `Rigging.cpp:13-46`. **Only the first level, Mix and the count are pinned** by a `static_assert` (`Rigging.cpp:192-193`, against the engine's frame); a renumbering of the middle ones would not fail the build. Saved patches depend on the numbers.

| Id | Param | Range | Default | Tooltip label | Display |
|---|---|---|---|---|---|
| 0 to 5 | String 1 to 6 level | 0..1 | 1 | "String N level" | percent (0..100 %) |
| 6 | Tune | −24..+24 semitones, snapped | 0 | "Tune (string 1)" | a note name, "C0" to "C4" (C2 at 0); with Free pitch, "%.2f st" |
| 7 | Chord | 0..6, snapped | 2 (Fifths) | "Chord" | the name |
| 8 | Ring | 0..1 | 0.57666 (4.00 s) | "Ring (time to fall silent)" | seconds: "%.2f s" under 10 s, "%.0f s" from 10 s |
| 9 | Damp | 0..1 | 0.33 | "Damp (how fast the brightness dies)" | percent |
| 10 | Spread | 0..1 | 0.342 (60.0 ms) | "Spread (string 1 to string 6 of a strike)" | "%.0f ms" under 1 s, "%.2f s" from 1 s |
| 11 | Mix | 0..1 | 1 | "Mix (input to strings)" | percent |

| Id | Input | Tooltip | | Id | Output |
|---|---|---|---|---|---|
| 0 | V/OCT | "1 V/oct pitch (STRUM unpatched: a new note strikes)" | | 0 | L ("Left") |
| 1 | Chord CV | "Chord CV" | | 1 | R ("Right") |
| 2 | Ring CV | "Ring CV" | | | |
| 3 | Damp CV | "Damp CV" | | **Light** | |
| 4 | STRUM | "Strum (trigger: strikes the strings)" | | 0 | Strike ("Strike") |
| 5 | IN L | "Left (alone: both sides)" | | | |
| 6 | IN R | "Right (alone: both sides)" | | | |

The tooltip texts are from `configParam`, `configInput` and the `ParamQuantity` subclasses [code `Rigging.cpp:56-160`]. How Rack lays them out is [unverified in Rack].

### 2.3 Controls, one by one

**Control rate.** The engine reads every knob, CV and menu setting **every 16 samples** (0.333 ms at 48 kHz) [code `RiggingEngine.hpp:213`]; each string runs its own 16-sample control tick as well (`String.hpp:143`). Only the audio inputs, STRUM and V/OCT (for note strikes) are read every sample. NaN or infinity on any CV, knob or input reads as 0 [code `finite`].

**Tune** [code `RiggingEngine.hpp:244-248`, `Chords.hpp:63-71`]

```
string 1 Hz = 65.40639 × 2^( Tune/12 + V/OCT )      (Tune rounded to a whole semitone unless Free pitch)
```

- −24 to +24 semitones; noon is C2 (65.406 Hz). String 1 at the ends: C0 16.352 Hz and C4 261.626 Hz [meas].
- Snapped to semitones in the engine as well as on the knob (`(int)(tune ± 0.5)`), unless Free pitch is on.
- No CV jack. Tooltip: the note of string 1 ("C0", "C#2"…), or "%.2f st" with Free pitch. A typed value uses Rack's default number parsing [unverified].

**V/OCT** adds 1 V per octave to all six strings. Input clamped to ±20 octaves inside `string1Hz`; NaN reads 0. First channel of a polyphonic cable. With STRUM unpatched it also strikes (section 6.2).

**Per-string frequency clamp.** Each string is clamped to **8 Hz at the bottom and the lower of 12 kHz and 0.4 × the sample rate at the top**, separately [code `Chords.hpp:74-79`, `String.hpp:52-59`]. At 44.1 kHz and above the top is 12 kHz. So a chord at a high V/OCT collapses: Fifths at V/OCT +5 V gives string 1 2,093 Hz and string 6 12,000 Hz; at +10 V all six sit at 12 kHz; at −10 V all six sit at 8 Hz [meas, 44.1 to 192 kHz identical]. 12 kHz needs 0.4 × rate ≥ 12 kHz, so rates under 30 kHz (not offered by Rack) would clamp lower.

**Glide.** Every change of the target pitch (Tune, V/OCT, Chord) is followed by the string in the log domain with an 8 ms time constant, updated every 16 samples; the delay length and gain ramp linearly across each 16 samples (section 5.5). The Tune knob's snapping is not smoothed (a semitone step is one glide).

**Chord** [code `Chords.hpp`]

- Seven settings, knob 0..6, snapped, default Fifths. Knob and CV add before rounding:

```
setting = clamp( knob + CV(V) × 0.7 , 0, 6 ),   changed only if |setting − held| > 0.6
```

- 10 V spans the seven settings; one setting per 1.43 V. ±5 V from the middle (Minor, 3) reaches both ends.
- Measured change points [meas], CV swept slowly in both directions:

| Knob | Going up, change to … at | Going down, change to … at |
|---|---|---|
| Fifths (2) | Octaves −2.00 V, Fifths −0.57, Minor +0.86, Sus +2.29, Cluster +3.71, Bell +5.14 | Cluster +4.86, Sus +3.43, Minor +2.00, Fifths +0.57, Octaves −0.86, Unison −2.29 |
| Minor (3) | Octaves −3.43, Fifths −2.00, Minor −0.57, Sus +0.86, Cluster +2.29, Bell +3.71 | Cluster +3.43, Sus +2.00, Minor +0.57, Fifths −0.86, Octaves −2.29, Unison −3.72 |

  At CV 0 the held setting is the knob's. A CV resting within 0.29 V of a change point does not chatter [test: `chord_cv_no_chatter`, `hysteresis`].
- The ratios are in the table below. The strings bend to the new pitches by the glide, so a Chord change is not a click.

| Chord | Semitones above string 1 (strings 1 to 6) | Cents added | Pitches at Tune 0, Hz [meas] |
|---|---|---|---|
| Unison | 0 0 0 0 0 0 | −8 −4 0 +3 +7 +11 | 65.105, 65.255, 65.406, 65.520, 65.671, 65.823 |
| Octaves | 0 12 12 24 24 36 | 0 −5 +5 −5 +5 0 | 65.406, 130.436, 131.191, 260.871, 262.382, 523.251 |
| Fifths | 0 7 12 19 24 31 | 0 | 65.406, 97.999, 130.813, 195.998, 261.626, 391.995 |
| Minor | 0 7 12 15 19 24 | 0 | 65.406, 97.999, 130.813, 155.563, 195.998, 261.626 |
| Sus | 0 5 7 12 17 19 | 0 | 65.406, 87.307, 97.999, 130.813, 174.614, 195.998 |
| Cluster | 0 1 2 3 4 5 | 0 | 65.406, 69.296, 73.416, 77.782, 82.407, 87.307 |
| Bell | ratios 1, 2, 2.4, 3, 4, 6 (not equal-tempered) | | 65.406, 130.813, 156.975, 196.219, 261.626, 392.438 |

  In Unison **string 1 itself sits 8 cents under** the nominal pitch (`Chords.hpp:20-21`). Bell's string 3 (2.4×) is 15.6 cents sharp of 15 equal-tempered semitones above string 1 (2.378×); it is a bell's tierce, as the spec says [code arithmetic]. At Tune −24, Fifths: 16.352, 24.500, 32.703, 48.999, 65.406, 97.999 Hz. At Tune +24, Fifths: 261.626 to 1,567.982 Hz; Bell 261.626 to 1,569.753 Hz [meas].

**String levels** (six knobs). Scale each string's output only; the string still rings and still takes its share of the input. Smoothed by a 5 ms one-pole (`SMOOTH_S`, coefficient 1 − e^(−1/(0.005 fs))) that lands exactly (within 1e-5). A string's DC is blocked (5 Hz one-pole, `OUT_DC_HZ`) before its level, so a level move never steps a held DC. No CV. A level at 0 is silent: every level at 0 gives exactly 0 V [test: `levels`].

**Ring** [code `RiggingEngine.hpp:182-185`, `:262-268`]

```
T60 (s) = 0.1 × 600^k        k = clamp( knob + RingCV(V) × 0.1 , 0, 1 )
```

| Knob | 0 | 0.25 | 0.5 | 0.57666 (default) | 0.75 | 1 |
|---|---|---|---|---|---|---|
| Ring | 0.100 s | 0.495 s | 2.449 s | 4.000 s | 12.12 s | 60.0 s |

- Ring CV adds 0.1 per volt: knob noon with −5 V gives 0.100 s, with +5 V gives 60.0 s [meas].
- The time shown is for the **fundamental at Damp 0**. Damp shortens it (below). It is the same for every string where a string can do it (section 5.3).
- Changes ramp: the gain and the delay length move linearly across 16 samples [test: `ring_change_is_smooth`, a 60 s to 0.1 s step on a ringing string moves less than twice the steady slope].

**Damp** [code `String.hpp:179-199`; `RiggingTuning.hpp:22-23`]

```
fundamental loss rate (nepers/s) = 3 × damp³       harmonic n loses about n² times that
```

- A one-pole low-pass in each string's loop, set per string from its pitch so that the fundamental loses exactly that rate; the delay is shortened by the filter's phase delay so Damp does not detune.
- Ring CV and Damp CV: 0.1 per volt on the knob, clamped 0..1. No smoothing of the coefficient: it is set at once at each control tick (a ramp exists for Ring's gain and the delay length only) — see the click measurement in section 9.3.
- Effect on the fundamental T60 at Ring 60 s: 60.0 s at Damp 0, 31.0 s at 0.33, 14.1 s at 0.5, 7.1 s at 0.66, 2.2 s at 1 [meas, 65 Hz to 12 kHz; section 5.3].

**Spread** [code `RiggingEngine.hpp:194-197`; `Strike.hpp:44-48`]

```
spread (s) = 1.5 × knob³          string k (0..5) is struck k/5 × spread after string 1
```

| Knob | 0 | 0.1 | 0.25 | 0.342 (default) | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|---|---|
| Spread | 0 | 1.5 ms | 23.4 ms | 60.0 ms | 187.5 ms | 633 ms | 1.500 s |

No CV. Read every 16 samples, so a strike takes the Spread of its trigger sample's tick.

**Mix** [code `RiggingEngine.hpp:276-282`]

```
out = dry × sin((1 − mix) × π/2)  +  wet × sin(mix × π/2)
```

- Equal power. Mix 0 is exactly the dry input (wet gain exactly 0); Mix 1 is exactly the strings (dry gain exactly 0). Measured dry gain at Mix 0, 0.25, 0.5, 0.75, 1: 1.0000, 0.9239 (−0.69 dB), 0.7071 (−3.01 dB), 0.3827 (−8.34 dB), 0 [meas; test: `mix`, noon −3 ± 0.3 dB].
- Both gains smoothed by the same 5 ms one-pole. No CV. Default fully right.

**Audio inputs** [code `RiggingEngine.hpp:139-153`]

- IN L and IN R are normalled both ways: either alone is a mono source feeding all six strings and both sides. With both, strings 1, 3, 5 hear L and strings 2, 4, 6 hear R.
- A polyphonic cable on an audio input is **summed** (`getVoltageSum`). V/OCT, the three CVs and STRUM use channel 1.
- What the strings hear is clamped to ±24 V, then passed through a 3 Hz high-pass (the input minus a 3 Hz one-pole), so DC in does not ring as DC. The dry path takes the input unclamped.
- NaN or infinity reads 0, per input and per channel sum.

**Outputs.** One channel each. Mono compatibility: string pan below.

**Strike light.** On for 0.1 s from every strike (STRUM edge or a note strike) [code `LIGHT_S`; test: `strum_edges`, on for 4,800 ± 16 samples]. The light is set with `setBrightnessSmooth(…, sampleTime)` in the module, so the on and off edges are softened [code `Rigging.cpp:215`; how it looks is unverified in Rack].

**String pan** [code `RiggingTuning.hpp:52`, `RiggingEngine.hpp:43-47`]. Equal-power pan, position p in −1..+1: left gain cos((p+1)π/4), right gain sin((p+1)π/4).

| String | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Position | 0 | −0.25 | +0.25 | −0.5 | +0.5 | −0.75 |
| L gain / R gain | 0.707 / 0.707 | 0.831 / 0.556 | 0.556 / 0.831 | 0.924 / 0.383 | 0.383 / 0.924 | 0.981 / 0.195 |
| L+R gain | 1.414 | 1.387 | 1.387 | 1.307 | 1.307 | 1.176 |

Measured per string by a strike with the other levels at 0: L/R power ratio 0.00, +3.50, −3.50, +7.66, −7.66, +14.03 dB [meas]. Strings 2, 4 and 6 are on the left, 3 and 5 on the right, so there are **three strings left and two right** plus the centre one: a Fifths noise drone reads **L 0.68 V RMS, R 0.52 V RMS (L 2.3 dB louder), L/R correlation 0.82** [meas]. Nothing cancels in L + R.

---

## 3. The right-click menu and saved state

[code `Rigging.cpp:180-189`, `:264-272`]

| Item | Choices | Default | JSON key | Stored value | On load |
|---|---|---|---|---|---|
| Free pitch (no semitone snap) | checkbox | off | `"freePitch"` | JSON boolean | `json_is_true`: only a boolean `true` is on; a missing key, `1`, a string or `false` is off |

A saved module's `"data"` object is `{"freePitch": false}`. The twelve knobs are saved by Rack as ordinary params (ids 0 to 11).

- **Turning Free pitch off rounds the Tune knob to the nearest semitone** (`setFreePitch`), so the note the tooltip shows is the note that sounds. Turning it on does nothing to the knob.
- Whether Rack loads params before `dataFromJson` is [unverified]. If it does not, a patch saved with Free pitch on would be rounded on load.
- **Initialize** (`onReset`): Free pitch off, then the engine's `reset()`: all six strings and the strike cleared (a rake in progress is cancelled [test: `reset_cancels_a_rake`]), the input high-pass, DC blockers and the strike light cleared, the pitch, levels and Mix snapped to the knobs (no glide). Rack resets the knobs.
- A **sample-rate change** retunes and clears the strings (and reallocates their delays) [code `onSampleRateChange`]. A rate outside 0 < rate ≤ 768 kHz is ignored and the last good rate kept [test: `extreme_rates`].
- **Nothing of the ringing is saved**: a reopened patch starts silent.
- The seed of the strike noise comes from `random::u32()` per instance, so two Riggings never strike identically [code `Rigging.cpp:136`].
- No golden saved-state image exists: JSON keys are not pinned by a test, and TESTING.md rule 5's checks (wrong type, missing field) are not made [test coverage: none].

**Bypass** [code `Rigging.cpp:219-226`]: IN L to L and IN R to R, with either alone to both, summed over polyphonic channels. The strike light is set to 0. `process()` is not called while bypassed, so the strings freeze and resume where they stopped when bypass is released [unverified in Rack]. The bypass does not sanitise NaN (section 1.1). A polyphonic input is summed to one channel, unlike Rack's default bypass [unverified in Rack].

**Typed values** [code `Rigging.cpp:72-134`]:

- Chord: a name, or the start of one, case-insensitive ("bell", "s" gives Sus); or a number 1 to 7. Anything else is ignored.
- Ring: seconds. "10" and "10 s" both give 10 s; 0.1 s or less gives the left end; more than 60 gives 60; zero or negative is ignored. The display of 10 s is "10 s" (not "10.0 s").
- Spread: milliseconds, or seconds if the text ends in "s" alone ("0.5 s"); "250" and "250 ms" are both a quarter of a second.

---

## 4. Signal flow

### 4.1 Block diagram

```
 IN L ──sum poly──┬──────────────────────────────────────────────── dry L ──────────────────────────┐
 IN R ──sum poly──┤  (either alone is both sides)                    dry R                          │
                  │                                                                                 │
                  └─► clamp ±24 V ─► 3 Hz high-pass (x − one-pole) ─► feed L, feed R                │
                                                          │ strings 1,3,5 take L;  2,4,6 take R     │
 STRUM rise ≥ 1 V (re-arm < 0.1 V)  ─┐                    │                                        │
 or, STRUM unpatched, a new note     ├─► STRIKE: per string one period of smoothed noise, string k   │
 at V/OCT (> 34 mV / ms)             ┘   delayed k/5 × Spread; a new trigger drops the old rake      │
                                                          ▼                                        │
  string k (k = 1..6)      in = feed × share_k  +  burst_k                                          │
     ┌─────────────────────────────────────────────────────────────────────────────┐                │
     │ delay line (Hermite read, length = period − 1 − low-pass delay)              │                │
     │   ─► damping one-pole (coefficient a, from Damp and pitch) ─► × loop gain g ─┼─► out_k        │
     │                                                                ▲             │                │
     │   write ◄── bound (identity to 12 V, tanh to 16 V) ◄── + in ◄──┘             │                │
     └─────────────────────────────────────────────────────────────────────────────┘                │
  out_k ─► DC block 5 Hz ─► × level_k (5 ms) ─► pan (equal power) ─► Σ L, Σ R                        │
  Σ ─► soft limit per side (identity to 8 V, tanh to 10 V) ─► × wet gain sin(mix π/2)                │
                                                                                      ▼              ▼
                                                         OUT L, R = dry × sin((1−mix) π/2) + wet ──► outputs

  Controls (every 16 samples): Tune, V/OCT, Chord ─► string pitch targets (8 ms glide) ; Ring, Damp ─► string
  gain, filter, share ; Spread ─► Strike ; levels and Mix ─► 5 ms one-poles.  Chord CV, Ring CV, Damp CV add to knobs.
```

All strings run all the time: there is no sleep, no mute and no gate [code; the spec's "keep running" choice]. The output of a string is taken **before** the input joins the loop, so the input is heard only after it has been round once (`String.hpp:110-116`).

### 4.2 Block constants

| Constant | Value | Source |
|---|---|---|
| Control interval | 16 samples | `RiggingEngine.hpp:213`, `String.hpp:143` |
| Strings | 6; chords 7 | `Chords.hpp:13` |
| Delay buffer | the next power of two above fs/8 + 12 samples, per string (8 Hz's period) | `String.hpp:46`, `DelayLine.hpp:29-40` |
| Loop bound | identity to 12 V, then 12 + 4·tanh((|x|−12)/4), ceiling 16 V; the read overshoot is allowed up to ×1.25 (20 V) | `RiggingTuning.hpp:27,30`, `String.hpp:117-122` |
| Output soft limit | identity to 8 V, then 8 + 2·tanh((|x|−8)/2), ceiling 10 V, per side, on the strings' sum only | `RiggingTuning.hpp:58`, `RiggingEngine.hpp:231-239` |
| Input clamp, input high-pass | ±24 V, 3 Hz | `:47`, `:40` |
| Output DC block | 5 Hz one-pole per string, before its level | `:58` |
| Strike burst | smoothed uniform noise (one-pole 0.5), peak 5.2 V (RMS about 1.7 V), one period long (8 samples at least) | `:35-36`, `Strike.hpp:28-33` |
| Pitch glide | 8 ms time constant, log domain | `:16` |
| Level and Mix smoothing | 5 ms one-pole | `:66` |
| Note strike | 1/30 V per 1 ms; no second strike until V/OCT has been still for 10 ms | `:63` |
| STRUM | rise to ≥ 1 V strikes; re-arms below 0.1 V | `RiggingEngine.hpp:93-102` |
| Light | 0.1 s | `:66` |
| Fast math | `fastTanh`, `fastSin2pi` (no libm per sample); libm only when a control moved | `FastMath.hpp` |

Delay memory per instance, six strings [code arithmetic]: 192 KiB at 44.1 and 48 kHz, 384 KiB at 88.2 and 96 kHz, 768 KiB at 176.4 and 192 kHz, 1.5 MiB at 352.8 and 384 kHz, 3 MiB at 705.6 and 768 kHz. Allocated only in `setSampleRate()` [test: `no_allocation_in_process`].

---

## 5. The strings

### 5.1 Pitch range in real units

| Quantity | Range | Source |
|---|---|---|
| String 1 from the knob | 16.352 Hz (C0, Tune −24) to 261.626 Hz (C4, +24); 65.406 Hz at noon | [meas] |
| All six strings from the knobs | 16.352 Hz (any chord at Tune −24) to 2,093.0 Hz (Octaves string 6 at +24) | [meas] |
| With V/OCT | clamped per string to 8 Hz .. 12 kHz | [meas] |
| Strings above 3.5 kHz | tune within the test bounds but ring short and unevenly (section 5.4) | [meas] |

### 5.2 Tuning

- Measured fundamental against the wanted pitch, Damp 0, by the phase between two windows: **within 0.05 cents** from 16 Hz to 2 kHz at 44.1, 88.2, 192 and 384 kHz [meas, `String`]. At Damp 1: −3.2 cents at 16.35 Hz, −1.0 at 30 Hz, −0.22 at 65 Hz, −0.02 at 220 Hz, under 0.02 above [meas].
- Tests: within 3 cents at 30, 65.4, 220, 880 and 2,000 Hz at Damp 0, 0.5 and 1 at 44.1, 48 and 96 kHz; within 3 cents at 3,520, 5,000 and 7,919 Hz at 44.1 and 48 kHz (Damp 0); within 10 cents at 16 and 20 Hz (Damp 0.33) [test: `tuning`]. Every chord string's engine-reported frequency is within 0.5 cents of the table, and the pitch found in the mixed output within 8 cents [test: `chord_pitches`]. V/OCT +1 V doubles it.
- The pitch does not depend on Ring or Damp: the filter's delay is taken off the line's length (`String.hpp:204`).

### 5.3 Ring and Damp: the fundamental's T60

The loop gain per trip is `exp(−6.9078/(Ring × f))` divided by what the Hermite read loses at the fundamental, and never more than a **60 s** Ring's gain at that pitch (so DC and anything below the fundamental die at least that fast, `String.hpp:222-226`). Damp adds `3 × damp³` nepers/s to the fundamental's loss.

Fundamental T60 of one string in seconds [meas, `String` driven by one period of noise, 48 kHz]:

| Ring | Pitch | Damp 0 | 0.33 | 0.5 | 0.66 | 1 |
|---|---|---|---|---|---|---|
| 60 s | 16.4 Hz | 60.6 | 31.2 | 14.7 | 7.3 | 1.8 |
| 60 s | 30 Hz | 60.0 | 31.0 | 14.0 | 7.0 | 2.0 |
| 60 s | 65.4 Hz | 60.1 | 31.0 | 14.2 | 7.1 | 2.3 |
| 60 s | 220 Hz | 59.9 | 31.0 | 14.1 | 7.1 | 2.2 |
| 60 s | 1 kHz | 60.0 | 31.0 | 14.1 | 7.1 | 2.2 |
| 60 s | 2 kHz | 60.0 | 31.0 | 14.1 | 7.1 | 2.2 |
| 4 s | 30 Hz | 4.00 | 4.00 | 3.00 | 2.50 | 1.50 |
| 4 s | 65.4 Hz | 4.13 | 3.67 | 3.21 | 2.75 | 1.38 |
| 4 s | 220 Hz | 3.96 | 3.75 | 3.27 | 2.66 | 1.50 |
| 4 s | 1 kHz | 4.02 | 3.78 | 3.30 | 2.67 | 1.47 |
| 1 s | 65.4 Hz | 0.92 | 0.92 | 0.92 | 0.92 | 0.69 |
| 1 s | 220 Hz to 3.5 kHz | 0.96 to 1.02 | 0.96 to 1.00 | 0.96 | 0.89 to 0.90 | 0.68 to 0.69 |

At Ring 1 s the 20 ms window of my measurement limits the resolution (about ±0.1 s). Ring 0.1 s is not tabulated because the window is longer than the ring; strings below 200 Hz cannot ring 0.1 s (20 periods of 65 Hz is 0.3 s). The tests promise: Damp 0 within 20 % of the tooltip for 30 Hz to 1 kHz from the larger of 20 periods and 0.1 s up to 60 s; at Damp 0.33 between 0.475 and 1.05 of the tooltip; 2 kHz at Ring 60 s above 10 s [test: `ring`].

### 5.4 The highest strings

The Hermite read loses gain at the fundamental that depends on the fractional part of the delay length. The gain makes it up while it has room under the 60 s ceiling; above about 3 kHz it cannot. T60 of the fundamental at Ring 60 s, Damp 0, by pitch and sample rate [meas]:

| Pitch | 48 kHz | 44.1 kHz |
|---|---|---|
| 2,000 Hz | 60.0 s | 54.8 s |
| 3,520 Hz | 2.1 s | 1.3 s |
| 5,000 Hz | 0.36 s | 0.69 s |
| 8,000 Hz | 60.1 s | 0.03 s |
| 12,000 Hz | 60.0 s | under 0.02 s (below the window) |

At 48 kHz, 8 and 12 kHz have exact 6- and 4-sample periods, so the fractional read is exact. The tests cover 3,520, 5,000 and 7,919 Hz for tuning and three higher rings (3,520 Hz at 1 s, 5,000 Hz at 0.5 s, 2,000 Hz at 8 s, within 10 % at 44.1 kHz) [test]. Only V/OCT reaches these pitches. In a six-string Fifths chord at V/OCT +3 to +6 V the mixed output still rings at 500 ms (1.4 to 2.4 V RMS in the mono sum) because the lower strings carry it; the exception is +6 V at 44.1 kHz (0.26 V), where string 1 is at 4,186 Hz and strings 4 to 6 are clamped at 12 kHz [meas, `m_high.cpp`].

### 5.5 Pitch glide and the read speed

A pitch change moves each string's delay length every sample. While the length shrinks the read pointer moves faster than the stored rate. Peak read speed in the first 16 samples of an octave-up jump (a Tune step of +12), Damp default [meas, `String` internals]:

| String pitch | 16.35 Hz | 65.4 Hz | 220 Hz | 440 Hz | 1 kHz | 2 kHz |
|---|---|---|---|---|---|---|
| Octave up (fastest read) | ×5.96 | ×2.26 | ×1.38 | ×1.19 | ×1.08 | ×1.04 |
| A fifth up | ×3.92 | ×1.74 | ×1.22 | ×1.11 | ×1.05 | ×1.03 |
| One semitone up | ×1.42 | ×1.11 | ×1.03 | ×1.02 | ×1.01 | ×1.00 |
| Octave down (slowest read) | −4.1 (backwards) | −0.30 (backwards) | ×0.61 | ×0.81 | ×0.91 | ×0.96 |

(44.1 and 48 kHz agree within 0.02.) At 65 Hz and below an octave-down glide reads **backwards** for a few milliseconds (it does not at 220 Hz). This is the intended bend of a waveguide's pitch, and the tests check only that it makes no click. A read above ×1 is a read faster than the stored rate, which aliases content near Nyquist (section 8); no test or measurement bounds it.

### 5.6 The input share, and the loudness law

Each string's input is multiplied by a "share" worked out from its Ring, Damp and pitch: 1 / √(sum over its harmonics below Nyquist of each one's resonant power), so noise in gives about the same drone at every Ring [code `String.hpp:230-252`; test: `input_share`, under 6 dB across 0.3 to 60 s at 65.4 Hz]. Struck sounds do not pass through it. Share at Ring 4 s, Damp 0.33 is about 0.012 at 65 Hz; at Ring 60 s, 0.0031; at Ring 0.1 s, 0.14 at 65 Hz and 0.079 at 1 kHz [meas].

- The share is read at the control tick and applied as a step (not ramped).
- **Issue #53.** At Damp exactly 0 the share is computed from a different formula and is discontinuous. Measured change when Damp moves from 0 to 0.001 [meas]:

| Ring | 8 Hz | 16.35 Hz | 30 Hz | 65.4 Hz | 220 Hz | 1 kHz |
|---|---|---|---|---|---|---|
| 0.1 s | −62.6 dB | −27.4 dB | −13.3 dB | −5.4 dB | +0.2 dB | +0.0 dB |
| 4 s | +0.11 dB | +0.02 | +0.01 | 0.00 | 0.00 | 0.00 |
| 60 s | +0.31 dB | +0.08 | +0.02 | +0.01 | 0.00 | 0.00 |

The issue calls the step small. At Ring 4 s and 60 s it is under 0.4 dB at every pitch, but at **Ring 0.1 s on strings under 100 Hz it is large**, up to −27 dB at C0 (Tune −24) and −5.4 dB at C2. The input at those settings is a thud with no clear pitch, so it matters little by ear; a noise drone at the shortest Ring and Damp at 0 is the place to hear it.

---

## 6. The strike

### 6.1 STRUM

- A rising edge to 1 V or more strikes (Schmitt 1 V up, 0.1 V down). A held gate strikes once; a level between 0.1 and 1 V neither strikes nor re-arms; NaN reads 0 [test: `strum_edges`].
- With a cable in STRUM, **only STRUM strikes**; V/OCT changes do not [test: `note_strikes`].
- Each string gets a burst of smoothed noise about one period of that string long (6,000 samples at 8 Hz and 48 kHz; 8 samples at least), added to the string's loop input beside the drone's input and **not scaled by the share**. String 1 first; string k (0..5) k/5 × Spread later. A new trigger during a rake starts a new rake from string 1 and drops the strikes still owed [test: `retrigger`]. A strike at audio rate (STRUM at up to 48 kHz / 8) keeps the output finite and within 10 V [test: `strum_at_audio_rate`].
- Every burst is different: no two of an instance's strikes are equal [test: `differs`], and two instances with different seeds differ.
- Mix 0 silences strikes (the wet gain is 0); the light still flashes.

### 6.2 Note strikes (STRUM unpatched)

[code `RiggingEngine.hpp:106-130`]

- A reference follows V/OCT by at most 1/30 V per millisecond (a glide stays with it). When V/OCT is more than 1/30 V ahead of the reference, a strike happens. Then no other strike until V/OCT has moved by less than that per millisecond for **10 ms**.
- Measured [meas, 48 kHz]:

| V/OCT change | Strikes |
|---|---|
| step 0.020, 0.030, 0.033, 0.034 V (0.24 to 0.41 semitone) | 0 |
| step 0.040 V (0.48 st), 0.05, 1/12 V (1 semitone), 1/6 V, 1 V | 1 |
| step −0.034 V / −1/12 V | 0 / 1 |
| ramp of 1 V over 5, 10, 20 ms (200, 100, 50 V/s) | 1 |
| ramp of 1 V over 30 ms (33.3 V/s) and slower (50 ms, 0.1 s, 0.5 s, 1 s, 5 s) | 0 |
| two notes 5 ms apart / 20 ms apart | 1 / 2 |
| a V/OCT already at 2 V when the module starts | 0 (the reference starts at V/OCT) |

- **Issue #54: a fast wide LFO strikes.** Sine on V/OCT, strikes in 10 s [meas]:

| Peak ± V | 0.5 Hz | 1 Hz | 2 Hz | 5 Hz | 10 Hz |
|---|---|---|---|---|---|
| 0.05 V, 0.1 V, 0.5 V | 0 | 0 | 0 | 0 | 0 |
| 1.0 V (peak slope 2πfA) | 0 | 0 | 0 | 0 (31 V/s) | **201** (63 V/s) |
| 2.0 V | 0 | 0 | 0 | **101** (63 V/s) | 1 (126 V/s) |

  The rule: strikes occur when the LFO's peak slope 2πfA exceeds 33.3 V/s and it stays below that slope for more than 10 ms near each peak: ±1 V at 10 Hz strikes at the rate of two per cycle; ±2 V at 10 Hz strikes once and then holds (it never rests). Workaround: patch anything into STRUM.
- The Tune knob and Chord never strike [test]. Plugging a cable carrying a voltage into V/OCT, or pulling it out, is a jump like any other [manual; code consistent].

---

## 7. Levels

### 7.1 The drone

White noise ±5 V (uniform), IN L, defaults (Fifths, Ring 4 s, Damp 0.33, Mix 1), 20 s settled, 8 s measured [meas]:

| | RMS L | RMS R | Peak | Notes |
|---|---|---|---|---|
| defaults | 0.68 V (0.71 V on another run: ±5 % between 8 s windows) | 0.52 V | 2.99 to 3.41 V | mono sum (L+R) RMS 1.14 V; L/R correlation 0.82 |

[test: `drone`: peaks between 2.2 and 4.5 V, steady within 2 dB between two 2 s windows.] The spec's "about ±3 V" holds.

**Against Ring** (RMS L in volts, 60 s windows, mean of 3 noise seeds):

| Ring | 0.1 s | 0.3 s | 1 s | 4 s | 15 s | 60 s | Spread |
|---|---|---|---|---|---|---|---|
| Damp 0.33 (default) | 0.645 | 0.736 | 0.752 | 0.720 | 0.668 | 0.573 | 2.4 dB |
| Damp 0 | 0.599 | 0.574 | 0.507 | 0.425 | 0.355 | 0.290 | 6.3 dB |
| Damp 1 | 0.578 | 0.658 | 0.627 | 0.520 | 0.401 | 0.289 | 7.1 dB |

**Against Damp** (Ring 4 s, 8 s windows, RMS L): 0.43 V at Damp 0, 0.62 at 0.1, 0.70 at 0.2, 0.71 at 0.33, 0.67 at 0.5, 0.61 at 0.66, 0.57 at 0.8, 0.51 at 1. The drone is quietest at Damp 0 and loudest near the default.

**Against Chord** (defaults, RMS L / R, peak): Unison 0.48 / 0.41 V, 2.0 V; Octaves 0.66 / 0.53, 3.2; Fifths 0.71 / 0.53, 3.4; Minor 0.54 / 0.47, 2.4; Sus 0.53 / 0.41, 2.3; **Cluster 0.30 / 0.25, 1.3**; Bell 0.72 / 0.53, 3.2. Cluster is 7.6 dB under Fifths [meas].

**Against Tune** (Fifths, RMS L / peak): −24: 0.35 V / 1.4 V; −12: 0.49 / 2.2; 0: 0.68 / 3.0; +12: 0.90 / 4.2; +24: **1.04 V / 5.2 V**. The drone rises 9.6 dB across the Tune range [meas]. The spec does not promise otherwise, but a QA tester will hear it when changing octave on a drone.

**Against the input level** (defaults, uniform ±V in, RMS L / peak): ±0.1 V 0.014 / 0.07; ±1 V 0.14 / 0.68; ±5 V 0.71 / 3.4; ±10 V 1.42 / 6.8; ±24 V 3.4 / 10.0; ±100 V 5.0 / 10.0 (the strings hear 24 V at most). Linear up to the soft limit [meas].

**Resonant tones are much louder than noise.** A sine on a string's harmonic (found by a search of V/OCT), Ring 60 s, Damp 0, six strings in Fifths: ±1 V at 1,013 Hz gives a 9.8 V peak; ±5 V gives 10.0 V flat-topped (fundamental 12.5 V) [meas; section 8]. A tuned input into a long Ring is hot: use ±1 V or less, or Ring short, if you do not want the limiter.

### 7.2 Ceiling

Worst cases [meas]: Ring 60 s, Damp 0, ±10 V and ±24 V noise for 60 s, chords Unison, Fifths and Bell: peaks 1.3 to 3.3 V at ±10 V and 3.0 to 7.9 V at ±24 V (the share keeps long Rings low). At the default Ring a ±24 V input reaches **10.00 V**. A ±10 V square at string 1's pitch for a minute stays within 10 V, and 10 s after it stops at Ring 60 s the output is still between 0.5 and 8 V; with Ring 1 s it is exactly 0 twenty seconds after [test: `tuned_square_at_60s`]. Fuzz: 10^6 samples of random knobs, CVs, triggers, patch states and rates stay finite and within 10 V [test: `seeded_fuzz`].

The wet is limited, **the dry is not**. At Mix below 1 the output is `dry × sin((1−mix)π/2) + wet`, so the dry input's size adds on top of up to 10 V.

### 7.3 Strikes

Strike peaks, Spread 0, Fifths, Ring 4 s, Damp 0.33, 100 seeds, max of |L| and |R| [meas]:

| | min | median | max |
|---|---|---|---|
| string 1 alone | 1.17 V | 1.48 V | 1.95 V |
| string 3 alone | 1.97 V | 2.58 V | 3.20 V |
| string 6 alone | 2.65 V | 3.69 V | 4.56 V |
| all six, Fifths | 4.58 V | 5.88 V | 9.35 V |
| all six, Unison | 3.24 V | 4.10 V | 6.01 V |
| all six, Bell | 4.64 V | 6.29 V | 9.08 V |

Single strings, 20 seeds: string 2 1.74 to 2.65 V; string 4 2.78 to 3.65 V; string 5 2.78 to 4.15 V. Higher strings strike louder; I did not trace why. [test: every single string between 1.2 and 4.5 V; all six between 4 and 9 V.]

All six at once, 20 seeds, min .. max V:

| Chord (Ring 4 s, Damp 0.33) | Unison 3.33 .. 4.92 | Octaves 4.98 .. 7.15 | Fifths 5.07 .. 9.35 | Minor 4.71 .. 6.47 | Sus 4.37 .. 6.71 | Cluster 3.54 .. 5.20 | Bell 5.09 .. 8.03 |
|---|---|---|---|---|---|---|---|

| Fifths, Ring / Damp | Damp 0 | Damp 0.33 | Damp 1 |
|---|---|---|---|
| Ring 0.1 s | 3.89 .. 5.52 | 2.98 .. 5.31 | 1.93 .. 3.32 |
| Ring 1 s | 6.95 .. 9.73 | 4.84 .. 9.01 | 2.42 .. 4.26 |
| Ring 4 s | 8.14 .. 9.85 | 5.07 .. 9.35 | 2.45 .. 4.37 |
| Ring 60 s | 8.74 .. 9.87 | 5.15 .. 9.44 | 2.46 .. 4.40 |

Tune −24: 2.47 .. 3.52 V; Tune +24: 6.32 .. 9.06 V. Forty strikes 20 ms apart (Spread 0): peak 9.63 V. A slow rake (Spread 1.5 s, Ring 60 s): 3.87 V, since the strings are each as loud as one alone.

- **The strike is as loud at its start at Ring 60 s as at Ring 1 s** [test: first 50 ms RMS within 3 dB]. First 50 ms of a six-string strike, L, RMS: 0.65 V at Ring 0.1 s (peak 4.0 V), 1.41 V at Ring 1 s (5.6 V), 1.62 V at Ring 60 s (6.2 V) [meas]. At Ring 0.1 s it is 8 dB lower, because the string is already dying inside the window.
- Strikes above the soft limit's 8 V knee are bent, not clipped: the peak never exceeds 10 V.

### 7.4 Content below 10 Hz (issue #55)

A 4th-order 10 Hz Butterworth low-pass on the left output of a ±5 V noise drone, 30 s [meas]:

| Chord, Tune | L RMS | under 10 Hz |
|---|---|---|
| Fifths, 0 | 0.72 V | 43.4 mV (−24.4 dB re L) |
| Fifths, −24 | 0.35 V | 22.0 mV (−24.1 dB) |
| Unison, 0 | 0.49 V | 26.6 mV (−25.3 dB) |
| Unison, −24 | 0.29 V | 25.5 mV (−21.2 dB) |

The issue says 35 to 40 mV; I measure 22 to 43 mV. 5 V of constant DC in for 30 s leaves a mean of 1e-6 V at the output [meas; test: `no_dc`, under 1 mV; noise on a 5 V offset under 20 mV].

### 7.5 Stereo

A mono input feeds both sides (one cable in either jack). The six strings spread by the pan table (section 2.3). A Fifths noise drone: L 0.68 V, R 0.52 V (2.3 dB apart), correlation 0.82; Unison 0.99 [meas]. L + R contains every string, within 1 dB of the sum of the sides for each string [test: `stereo`].

---

## 8. Aliasing (TESTING.md rule 16)

**Rule 16 applies to Rigging.** It makes sound, has nonlinearities (a bound in each string's feedback loop and a soft limit on the output), and reads delay lines at a varying rate during glides. **There is no `test_alias.cpp`, no bound in dB, and no oversampling or anti-aliasing in the module.** TESTING.md's table says "Issue #67" for Rigging, and #67 says "Rigging: string delay interpolation at high pitches; any saturation in the loop: not looked at." The design spec has no aliasing section. What follows is what I measured; none of it is pinned by a test.

By `docs/DSP-ALIASING.md`'s three kinds of source:

1. **Generated discontinuities.** None that is tonal. The strike burst is noise, smoothed by a one-pole of 0.5; its edges are inside noise.
2. **Nonlinearities.** Two, neither oversampled nor ADAA'd:
   - the loop bound in every string (`String.hpp:117-122`): identity below 12 V, tanh above. **Inside the feedback loop**, so what it makes goes round again;
   - the soft limit on the strings' sum (`RiggingEngine.hpp:231-239`): identity below 8 V, tanh above, outside the loop.
   Below their knees both are exactly linear, so quiet signals make no aliasing: a ±1 V sine at the string's pitch with Ring 4 s reads −145 to −150 dB re the tone.
3. **Variable-rate reads.** A fixed fractional Hermite delay at a fixed length makes no aliasing in steady state (it makes a gain and phase error that section 5.4 accounts for). A pitch change reads at a varying speed up to ×5.96 (section 5.5). **Not measured.** V/OCT is sampled every 16 samples (3 kHz at 48 kHz) and then smoothed by an 8 ms one-pole (corner 20 Hz), so audio-rate V/OCT is not an FM path; whether content above 1.5 kHz on V/OCT folds into the control band is not measured [unverified].

### 8.1 What I measured: a loud tone through the strings

Method [meas, `m_alias.cpp`, using `src/testing_alias.hpp` `AliasMeter`]:

- A sine at 1,013, 3,011, 6,007, 9,011 or 15,013 Hz (chosen to share no factor with 44,100 or 48,000, so a folded product cannot land on a multiple of the tone). 44.1 and 48 kHz.
- String 1 is tuned by a fine search of V/OCT (0.1 cent steps, at ±0.5 V) so the tone sits on one of its harmonics (the 8th, 25th, 50th, 75th or 125th). Damp 0, Mix 1.
- "single": string 1 audible only; "six": Fifths, all levels up. Ring 4 s or 60 s. Settled 25 s (90 s at Ring 60 s), then 1.5 s analysed (65,536-point FFT, Kaiser window).
- Reported: the strongest component that is not a multiple of the tone below Nyquist, in dB re the strongest multiple.

Results, worst of the tones, dB re the tone (lower is cleaner; DSP-ALIASING.md: 50 dB down is a reasonable target for a correction, 60 to 70 clean) [meas]:

| Input | single string | six strings |
|---|---|---|
| ±1 V | −141 to −150 dB (Ring 4 s); −127 to −141 (Ring 60 s) | −120 to −149 (Ring 4 s); Ring 60 s: **−78 to −83 at 1,013 Hz** (output peak 9.8 V: the limiter is engaged), −112 to −141 elsewhere |
| ±5 V | −93 to −149 | **−51.4 dB at 1,013 Hz, Ring 60 s** (peak 10.00 V); −70 to −71 at 1,013 Hz, Ring 4 s; −113 to −149 elsewhere (off the limiter) |
| ±24 V | **−39.0 dB** (6,007 Hz, 44.1 kHz, Ring 4 s), −44.1 (9,011 Hz, 44.1 kHz), −54 to −59 (3,011 Hz), −67.6 to −78 (1,013 Hz) | **−32.9 dB at 6,007 Hz, Ring 4 s, at both 44.1 and 48 kHz**; −43.9 at 1,013 Hz; −56.9 to −71.5 at 3,011 Hz |
| a tone half a harmonic off resonance | −94 dB at best (nothing near the knee) | |

Where the worst components fall: near Nyquist (18.8, 20.8, 22.7 kHz) and in the audio band (2,051 Hz at 44.1 kHz from the 7th harmonic of 6,007 Hz folded; 5,951 Hz at 48 kHz). Cases with a 9,011 or 15,013 Hz tone at 48 kHz sit under −112 dB; their output peaks stay under 4.3 V, below the limiter.

Where it comes from, in a scratch copy of the tuning header with a nonlinearity removed (compile-time override, nothing in the repository touched) [meas, `m_alias_mut.cpp`]:

| Case (24 V unless stated) | as built | output soft limit removed | loop bound removed |
|---|---|---|---|
| 6,007 Hz, six, Ring 4 s, 48 kHz | −32.9 dB | −55.8 dB | −29.4 dB |
| 6,007 Hz, six, Ring 4 s, 44.1 kHz | −32.9 | −45.7 | −25.9 |
| 1,013 Hz, six, Ring 4 s, 48 kHz | −43.9 | −81.1 | −44.4 |
| 1,013 Hz, six, Ring 60 s, ±5 V, 48 kHz | −51.5 | −99.2 | −42.6 |
| 6,007 Hz, single, Ring 4 s, 44.1 kHz | −39.0 | −41.9 | −31.8 |
| 9,011 Hz, single, Ring 4 s, 44.1 kHz | −44.1 | −47.9 | −41.0 |
| 3,011 Hz, single, Ring 4 s, 44.1 kHz | −54.0 | −60.3 | −44.9 |

So in the six-string cases the **output soft limit** makes most of the aliasing; in the single-string cases the **loop bound** does (removing it makes the loop run away, which is worse, so its row is not a clean attribution). There is no anti-aliasing to remove in the sense of rule 1; the removal rows show only where the aliasing comes from. No bound exists in the repository.

**What QA should take from this.**

- With ordinary material (noise, or tones under about ±1 V on a short Ring, or off a string's pitch) the module is clean: the nonlinearities are exactly linear below 8 V (output) and 12 V (loop).
- A hot, sustained, tuned tone is the case that aliases: a tone within about 0.5 Hz of a string harmonic at Ring 60 s, at ±5 V or more with several strings agreeing (Fifths and Octaves put harmonics of several strings on one pitch), reaches the 8 V knee. Expect −50 dB at ±5 V and −33 to −44 dB at ±24 V. Listening patch 7 (Fathom into IN L at the same V/OCT, Mix at noon) is the nearest real patch. A bound at "measured plus 3 dB" per DSP-ALIASING.md would be −30 dB for the worst case; that is not a clean figure, which is why this is a candidate for a fix under #67 (oversample the output limit, or make it ADAA).
- The glide's aliasing is unmeasured. Listen to octave jumps on low strings with Damp at 0 (bright content) for fizz at the start of the glide [unverified].

---

## 9. CPU, sample rates and robustness

### 9.1 CPU

`bench` cases [code `bench.inc`], each at 48 kHz, % of one core per instance:

- **idle**: defaults, nothing patched. The strings still run (a strike or a ringing string must not wait for a cable).
- **outputs**: noise ±5 V into IN L, knobs still.
- **busy**: noise on both sides, V/OCT, Chord CV, Ring CV and Damp CV all moving, STRUM at 20 Hz.

| Case | Budget, Apple arm64 | Budget, other machines (provisional) | TESTING.md measured | Mine, 1 instance [meas] | Mine, 16 instances [meas] |
|---|---|---|---|---|---|
| idle | 0.3 | 0.9 | 0.14 | 0.147, 0.150, 0.152 | 0.160, 0.160, 0.166 |
| outputs | 0.3 | 0.9 | 0.14 | 0.142, 0.146, 0.150 | 0.158, 0.160, 0.163 |
| busy | 0.6 | 1.8 | 0.30 | 0.300, 0.303, 0.305 | 0.331, 0.332, 0.343 |

Three runs, 4 s per case, Apple M-series Mac, with other work running (load average 6 to 7). An earlier run under load average 10 read 0.197, 0.191 and 0.392. The cost is flat with the number of instances (1.06 to 1.09 × at 16) and with the input: idle costs the same as a drone (30 ns a sample, 0.146 %), since nothing sleeps. It is fixed per sample, so it **scales with the sample rate**: about 0.3 % at 96 kHz and 0.6 % at 192 kHz idle [code arithmetic: 30.5 ns × rate]. The x86-64 and Windows budgets are three times the Mac's and have not been measured on native hardware; **Windows CPU is not covered by any automated budget**. Rack's CPU meter adds its own per-module overhead [unverified].

`make libm-calls` requires zero libm calls per sample in the idle and outputs cases (busy is reported only). It runs only on Linux and was not run here [issue #36]. libm is used only when a control moves (`controls()` and `String::control()` call `exp`, `pow`, `log2`, `atan2` and others) [code]; a moving CV on Chord, Ring, Damp or V/OCT therefore costs libm on the audio thread every 16 samples. That is why busy's libm case only reports.

### 9.2 Sample rates

- Accepted: any rate with 0 < rate ≤ 768 kHz; anything else (0, negative, NaN, 1e6, 768,001) is ignored and the last good rate kept [test: `extreme_rates`].
- The tests run 1 Hz to 768 kHz with finite outputs within 10 V, 44.1 → 768 → 48 kHz with a ringing string (string 1 within 10 cents of C2 afterwards), and the lowest string at the highest rate (8 Hz is 96,000 samples at 768 kHz) [test: `extreme_rates`, `rate_change_recovers`, `rate_change`].
- Tuning is the same at 44.1, 88.2, 192 and 384 kHz within 0.05 cents (Damp 0) [meas].
- Ring and Damp do not depend on the rate (section 5.3) except for strings above 3 kHz (section 5.4).
- A rate change clears the strings: the tail is cut, expected.

### 9.3 Hostile input and numerical safety

- **NaN, ±infinity, ±1e30** on every input, CV and knob, with mode switches and menus: finite outputs within 10 V, and the engine comes back (string 1 at 65.406 Hz afterwards) [test: `hostile_inputs`, `nan_inputs`].
- **Huge input.** The strings hear ±24 V at most (a 1e30 V input would otherwise sit in the high-pass for seconds); the dry path takes it as it is [code].
- **No allocation** in `process()` or `reset()` [test: `no_allocation_in_process`, a counting `operator new`].
- **Denormals.** A strike's tail passes through no subnormal and ends in exact silence; the last second costs less than 4× the first plus 0.05 s [test: `denormals`, 30 s after a 1 s strike the output is exactly 0, 0]. In-loop, values under 1e-20 are flushed.
- **Seeded fuzz** (10^6 samples; random knobs, CVs, triggers, patch states and rates): finite and within 10 V. A single seed; there is no seed sweep [test: `seeded_fuzz`].
- **Sanitizers**: only `test_hostile` is built with ASan and UBSan (TESTING.md: "Partly").
- **No writable global state** (`make engine-globals`, passed).
- **Click behaviour on a stepped Damp** is not covered by any test. Measured below.

**Damp step with an input playing [meas, `m_click*.cpp`].** Damp 1 → 0 in one step, 40 instants after a 20 s settle; the largest sample step of the left output in the next 100 ms (V) against the steady step at Damp 1:

| Input | steady step (Damp 1) | steady step (Damp 0) | after the step: min / median / max |
|---|---|---|---|
| sine ±5 V, 130.81 Hz | 0.20 | 0.07 | 0.74 / 2.24 / **3.93** |
| sine ±5 V, 311 Hz | 0.05 | 0.005 | 0.52 / 1.01 / 1.46 |
| sine ±5 V, 1,000 Hz | 0.07 | 0.02 | 0.99 / 1.37 / 1.51 |
| sine ±1 V, 130.81 Hz | 0.05 | 0.014 | 0.29 / 0.66 / 0.89 |
| noise ±5 V | 0.38 | 1.6 | 3.33 / 3.77 / 4.22 |
| none (a ringing string after a strike) | 0.000 | 0.000 | 0.000 (no click) |

Damp 0.33 → 0 gives 0.39 to 1.75 V on the same ±5 V 130.81 Hz sine (steady 0.07 to 0.38). Moved over 10, 50 or 200 ms (a hand turn) the step is 0.26 to 0.28 V on that sine, near the steady 0.2 V, and the 311 Hz sine gives 0.05 to 0.06 V against 0.05 V. **A stepped Damp (a CV step, a mapped control, a preset) with a tonal input playing is a click; a smooth turn is not.** The cause, in the code: the loop's low-pass coefficient `a_` is assigned at once at the control tick (`String.hpp:194,198`) while only the gain and the delay length ramp (`:163-167`); the input joins the loop after the filter, so opening the filter lets the raw input through in one sample. Not filed; the spec lists "clicks … when Chord, Tune, V/OCT, Ring or Damp change, by hand or by CV" as an anti-goal and `test_engine.cpp` steps Damp only on a struck string (no input). Ring, Chord, Tune, V/OCT, Mix and the levels stepped once under the same sine gave no step beyond 2.6 × the steady step and 0.62 V (Ring 0.2 ↔ 0.9: 0.16 V and 0.45 V; Tune 0 → +12: 0.62 V against 0.38 V) [meas].

---

## 10. Open issues

### 10.1 Filed issues about Rigging: do not re-file

| # | Title | QA impact |
|---|---|---|
| 53 | Rigging: drone loudness varies on very low strings at Damp exactly 0 | A step in the drone level between Damp 0 and just above, in `inputShare`. Under 0.4 dB at Ring 4 s and 60 s; large at Ring 0.1 s on strings under 100 Hz (section 5.6). |
| 54 | Rigging: a fast, wide LFO on V/OCT strikes the strings when STRUM is unpatched | A sine LFO whose peak slope 2πfA exceeds 33.3 V/s strikes (section 6.2). Patch a trigger into STRUM to turn note strikes off. |
| 55 | Rigging: about 35–40 mV RMS of content under 10 Hz in the output | Inaudible; 22 to 43 mV in my measurement (section 7.4). Shows on a scope. |
| 67 | Aliasing: measure every module that makes or bends sound, and fix what fails | Rigging is in its table ("not looked at"). Section 8 has the first measurements. |
| 64 | Polyphony: modules should be polyphonic where it makes sense | Rigging stays one set of six strings; a voice per V/OCT channel is mentioned as expensive. Not a defect. |
| 36 | Process: libm-calls check is skipped on macOS | Rigging's no-libm-per-sample claim was not run here. |

(#52 is the merged pull request that added the module.)

### 10.2 Found while preparing this document (not filed)

1. **Aliasing under a hot tuned tone**, down to −33 dB re the tone (section 8.1). A finding for #67.
2. **A stepped Damp clicks with an input playing** (up to 3.9 V against 0.2 V; section 9.3). Not covered by a test.
3. **Bypass passes a NaN input through** (section 1.1).
4. **Ring above 3 kHz depends on the pitch's fraction and the sample rate** (section 5.4). A string at 8 kHz at 44.1 kHz rings for 30 ms at Ring 60 s.
5. **The share's step at Damp 0 is much larger than "small" at Ring 0.1 s** (section 5.6, issue #53).
6. **The spec's "2.1 kHz: C4 with Bell's top string"** is 2,093 Hz from Octaves, not Bell (1,570 Hz).
7. **The listening list says "Type 10 into Ring's tooltip. Ring reads 10.0 s."** The code shows "10 s" (`Rigging.cpp:100`, "%.0f s" from 10 s up).

---

## 11. What the automated suite already covers

All of the following passed on the 2.0.39 tree in my run. All tests are Rack-free offline tests, at 48 kHz unless stated.

### 11.1 test_chords

| Test | Guarantees | Tolerance |
|---|---|---|
| table | the six semitone/cent rows and Bell's ratios, written out again in the test; names; out-of-range reads as the nearest | ratio within 1e-6 |
| cv_steps | knob 3 ± 5 V reaches both ends; one step is 10/7 V; clamped; NaN reads 0 V | exact |
| hysteresis | a dithering boundary changes at most once in 10,000 samples; a ramp visits 0..6 in order and back; 0.6 holds, 0.8 leaves | exact |
| pitch | Tune 0 is C2; +12 or +1 V is an octave; −24 and −1 V is 8.1758 Hz; clamp to 8 Hz and min(12 kHz, 0.4 fs); NaN, inf; huge V/OCT | 1e-5 relative |

### 11.2 test_string (one string)

| Test | Guarantees | Tolerance |
|---|---|---|
| tuning | 30, 65.4, 220, 880, 2000 Hz at 44.1/48/96 kHz, Damp 0/0.5/1; 3520, 5000, 7919 Hz at 44.1/48 kHz; 16 and 20 Hz | 3 cents; 3 cents; 10 cents |
| ring | fundamental T60 at Damp 0 for 30, 110, 440, 1000 Hz from the shortest (20 periods, 0.1 s) to 60 s; at Damp 0.33; three high strings at 44.1 kHz; 2 kHz at 60 s | within 20 %; 0.475 to 1.05 of Ring; within 10 %; over 10 s |
| damp | more Damp lowers the spectral centroid half a second after a strike and never lengthens the ring | centroid falls by 10 % per step (or is within 2 % of the fundamental) |
| glide | a step during an octave or a fifth jump does not exceed 1.3 to 2.2 × the steady step; pitch within 3 cents 100 ms on; the string reports the target | as stated |
| input_share | noise RMS across Ring 0.3 to 60 s at 65.4 Hz; the share falls as Ring rises | under 6 dB |
| bounded | a ±10 V square at the string's pitch for 30 s never exceeds 20 V; reaches the bound; rings 60 s afterwards; ends in exact silence | peak ≤ 16 × 1.25 V; 60 s within 20 % |
| rate_change | a rate change clears and retunes; 8 Hz at 768 kHz; clamps; NaN reads 0 | 3 cents |
| glide_ends | after a change the string stops gliding and reports exactly its target | exact |
| dc_dies_with_ring | DC in a string dies as fast as a 60 s Ring at worst | mean under 1 V after 600 strikes; later ≤ 0.4 × |
| ring_change_is_smooth | Ring 60 s → 0.1 s on a ringing string | step under 2 × the steady one |

### 11.3 test_strike

| Test | Guarantees |
|---|---|
| together | a never-triggered Strike is silent; Spread 0 starts all six within one sample |
| rake | Spread 1.5 s starts string k at k × 14,400 samples ± 2 |
| burst_length | burst length within 10 % of the period; an 8-sample minimum; peak ≤ 5.2 V |
| retrigger | a second trigger drops the old rake (strings 4 to 6 not struck by it); 10 s of triggers every 48 samples stay bounded; a rake after the last trigger ends |
| differs | the same seed gives the same burst, different seeds differ, two strikes of one instance differ; the burst is noise about zero |

### 11.4 test_engine (the engine, pitches and levels measured from the outputs alone)

| Test | Guarantees | Tolerance |
|---|---|---|
| chord_pitches | all seven chords × six strings: engine-reported pitch; pitch in the mixed output; Tune snapped and free (+40 cents) | 0.5 cents; 8 cents |
| drone | ±5 V noise: peaks; steadiness; level across Ring 0.3 to 60 s; silence with nothing patched | 2.2 to 4.5 V; 2 dB; 6 dB; exactly 0 |
| strike_levels | each string alone; all six; first 50 ms at Ring 1 s against 60 s | 1.2 to 4.5 V; 4 to 9 V; 3 dB |
| levels | a level at 0 removes its string's fundamental; a level raised mid-ring fades in without a step; all levels 0 is silent | 5e-3 × others; 3 × usual + 0.02 V; exactly 0 |
| input_routing | either jack alone is both sides; with both, strings 1, 3, 5 follow L and 2, 4, 6 follow R | 0.05 × |
| stereo | L + R holds every string within 1 dB; string 1 centred; alternating sides | 1 dB |
| mix | Mix fully dry passes the input to both outputs; Mix half is −3 dB | ±0.3 dB |
| cv | Ring CV ±5 V at Ring 0.5 reaches 60 s and 0.1 s; Damp CV the same centroid as the knob | 5 %; 2 % |
| chord_cv_no_chatter, chord change | no chatter on a boundary; a Chord change mid-ring is no click | ≤ 1 change; step under 2 × |
| note_strikes | a semitone strikes once; a 2 V edge 1 ms long once; a 1 V/s ramp and 0.2 semitone never; two notes 5 ms apart once; 20 ms apart twice; slews one strike; with STRUM patched none; Tune and Chord never | exact counts |
| strum_edges | 0.9 V no strike, 1 V strikes, a held gate once, 0.2 V does not re-arm, a second trigger strikes; light 4,800 ± 16 samples; NaN no strike | exact |
| clamps | V/OCT ±10 and beyond: strings within 8 Hz to 12 kHz and finite; back within 0.5 cents 0.2 s after | 0.5 cents |
| nan_inputs, no_dc | NaN and inf on every input, CV and knob; 5 V DC in; noise on a 5 V offset | finite within 10 V; < 1 mV; < 20 mV |
| reset_cancels_a_rake, no_thump_from_a_level | Initialize mid-rake gives silence; turning levels down or returning V/OCT in the silence after many strikes makes no thump | exactly 0; 0.02 V and 0.1 V |
| laws | Ring, Spread laws invert; defaults (Ring 4 s within 2 %, Spread 60 ms within 5 %, Fifths, Mix 1, Damp 0.33, Tune 0); typed Ring/Spread extremes | as stated |

### 11.5 test_hostile (ASan and UBSan, counting `operator new`)

| Test | Guarantees |
|---|---|
| extreme_rates | 1 Hz to 768 kHz, bad rates ignored, outputs finite within 10 V |
| rate_change_recovers | 44.1 → 768 → 48 kHz with a ringing string; pitch found by autocorrelation (an independent path) within 10 cents |
| hostile_inputs | NaN, ±inf and 1e30 on every input and knob; engine recovers |
| tuned_square_at_60s | ±10 V square at string 1's pitch for a minute at Ring 60 s; the output stays under 10 V; reaches the soft limit (peak > 8 V); decays as Ring says; Ring 1 s gives exact silence after 20 s |
| strum_at_audio_rate | STRUM at audio rate: finite, within 10 V, every pulse struck |
| no_allocation_in_process | zero allocations in `process()` and `reset()` |
| seeded_fuzz | 10^6 samples, one seed |
| denormals | the tail of a strike passes through no subnormal; ends in exact 0 within 30 s; runs at speed |

### 11.6 What the suite does not cover

- Anything in Rack: the panel, the tooltips, the light's look, the menu, Initialize ordering, bypass, polyphony as Rack delivers it, `make smoke` (not run here), save and load through Rack.
- **Aliasing** (section 8). No test, no bound.
- **A stepped Damp with an input playing** (section 9.3).
- The pitch glide's read speed and its artefacts (section 5.5).
- Windows and Linux builds, and CPU on x86-64 or Windows.
- No golden saved-state image; JSON edge cases (wrong type, missing key) are not tested.
- No seed sweep of the fuzz.
- Listening: the strings' tone, the strike's burst, Ring's and Damp's curves, the detunes, the string positions, the glide time, Spread's range (the listening list is the instrument; the numbers in `RiggingTuning.hpp` are first guesses "to be set by ear").
- The ranges of the knobs' tooltips and typed values as Rack shows them.

---

## 12. The listening list against the code

`rigging-2.0.39-listening.html` agrees with the code on every figure I checked, with these exceptions:

1. **"Type 10 into Ring's tooltip. Ring reads 10.0 s."** The code shows "10 s" at 10 s and above, and two decimals under 10 s ("4.00 s", "0.10 s") (`Rigging.cpp:100`). Code wins.
2. **"Damp fully right … a soft, almost pure tone that dies within about two seconds."** The fundamental's T60 is 2.2 to 2.3 s at Ring 60 s, 1.5 s at Ring 4 s and 0.7 s at Ring 1 s (section 5.3).
3. **Issue #53 item** ("the drone's level may change in a step at Damp exactly 0. Note whether you hear it."): at the default Ring of 4 s the step is under 0.4 dB at every pitch. To hear it, set Ring fully left and Tune left (section 5.6).
4. **Issue #55 item** ("about 35 to 40 mV RMS under 10 Hz"): I measure 22 to 43 mV (section 7.4).
5. The list's table says note strikes need "a jump of more than 1/30 V within 1 ms". True of a smooth change; a single-sample step must exceed 34.0 mV (section 1.1).

Everything else I checked (the chord pitches, 4 s / 0.1 s / 60 s Ring, the 60 ms Spread default, the 0.1 V / 1 V STRUM thresholds, 14 HP, the "C0, C2, C4" Tune tooltips, "3.27 st" in Free pitch, the 31 s ring at default Damp, 3 V and 6 V levels) matches.

---

## 13. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, all knobs at their defaults (Tune noon C2, Chord Fifths, Ring 4.00 s, Damp 33 %, Spread 60 ms, Mix fully right, levels 100 %), Free pitch off, nothing patched in STRUM.

**Method notes.**
- For a drone use uniform white noise at ±5 V in IN L (Hadal's Hiss is the natural source). Level measurements vary by about ±5 % between 8 s windows.
- For T60 use a strike with Spread 0 and one string left up, and a tuner or a spectrum scope at the fundamental. A one-period burst needs no special care.
- To judge tuning on a scope, use string 1 alone (other levels at 0).
- Set Mix fully right; the dry passes at its own level and unlimited.

### 13.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Tune | turn Tune fully left, noon, fully right; read the tooltip and a tuner on string 1 alone | C0 (16.35 Hz), C2 (65.41 Hz), C4 (261.63 Hz); one click per semitone; the strings bend, no click |
| Free pitch | tick it, turn Tune; untick | a smooth knob and "x.xx st" in the tooltip; unticking rounds the knob to a semitone |
| V/OCT | 1 V steps from a calibrated source, string 1 alone | 1 V per octave within 3 cents; clamps at 8 Hz and 12 kHz; no string above 12 kHz or below 8 Hz; at V/OCT +10 V all six at 12 kHz |
| Chord | all seven stops with a drone; a tuner on each string | pitches of the table in section 2.3 (within the tuner's accuracy); Unison string 1 is 8 cents flat; Bell 2.4× is not a semitone multiple |
| Chord CV | knob at Fifths; a slow −10 to +10 V ramp, and back | going up the changes are at −2.00, −0.57, +0.86, +2.29, +3.71, +5.14 V (to Octaves, Fifths, Minor, Sus, Cluster, Bell); coming down at +4.86, +3.43, +2.00, +0.57, −0.86, −2.29 V; 0.29 V of hysteresis |
| String levels | each to 0 in turn | that string leaves the mix; the others unchanged; every level at 0 is silent |
| Level raised mid-drone | bring string 4 up over a second | fades in already sounding, no click |
| Ring | tooltip at the left, default, right; strike string 1 alone; tuner / scope for the fundamental's decay | 0.10 s, 4.00 s, 60 s; T60 of the fundamental at Damp 0 within 20 % for 30 Hz to 2 kHz (above 20 periods); at the default Damp 60 s rings 31 s |
| Ring CV | knob noon, −5 V, 0, +5 V | 0.1 s, 2.45 s, 60 s |
| Damp | strike, Damp 0, default, 1 | bright for the whole ring; mellows; a soft near-pure tone dying in about 2 s (fundamental T60 1.5 s at Ring 4 s) |
| Damp step | ±5 V noise or a tuned sine in; Damp 1 → 0 by a CV step, then by a slow turn | **step: a click up to a few volts (section 9.3); slow turn: none.** Report it as a confirmation, not a new defect |
| Spread | tooltip at the left, default, right; scope a six-string strike | 0 ms, 60 ms, 1.50 s; at 0 all six start in the same sample; string 6 starts 1.5 s after string 1 at the right |
| Strike loudness | strike all six with Spread 0; Ring 1 s then 60 s | equally loud at the start (within 3 dB); peak median about 6 V, up to 9.4 V; never above 10 V |
| Mix | noise ±5 V in; Mix 0, 0.25, 0.5, 0.75, 1 with levels at 0 (dry only) | dry gain 1.00, 0.92, 0.71, 0.38, 0 |
| IN L / IN R | one cable in either, then two different sources | one jack alone feeds both sides; with both, strings 1, 3, 5 follow L and 2, 4, 6 follow R |
| Strike light | trigger STRUM, note strike, bypass | flashes about 0.1 s on every strike; dark in bypass |
| Typed values | "bell", "4" into Chord; "10" into Ring; "250" and "0.5 s" into Spread | Bell, Minor; reads "10 s"; 250 ms, 0.50 s |

### 13.2 Drone levels

| Procedure | Expected |
|---|---|
| ±5 V noise, defaults | a drone of about ±3 V peak (2.2 to 4.5 V in the test); L 0.7 V RMS, R 0.5 V RMS (L 2.3 dB louder) |
| Ring 0.1 s to 60 s on that drone | the level stays within 2.4 dB (3.0 dB on 8 s windows); the drone gets longer and smoother, not louder |
| Damp 0 and 1 across Ring | 6 to 7 dB spread: expected (the promise is at the default Damp) |
| Tune −24 to +24 | the drone rises about 9.6 dB: expected; Cluster 7.6 dB under Fifths |
| input ±1, ±5, ±10, ±24 V | linear: 0.14, 0.71, 1.42, 3.4 V RMS; peaks 0.7, 3.4, 6.8, 10.0 V |
| tuned ±1 V sine at Ring 60 s | about 10 V peak (the limiter): expected |

### 13.3 Strikes and note strikes

| Procedure | Expected |
|---|---|
| a trigger into STRUM, nothing in the audio inputs | each trigger rakes the strings low to high over Spread; they ring about 4 s (fundamental T60 3.7 s at the default Damp) |
| STRUM unpatched, stepped sequence on V/OCT (semitone steps) | one strike per new note; a slide to a note is one strike; the same note again is not a strike; a steady glide under 33 V/s only bends |
| STRUM unpatched, a V/OCT LFO ±1 V at 5 Hz, then 10 Hz | none, then about 20 strikes a second [issue #54] |
| STRUM patched, then change V/OCT | no strike |
| trigger again during a 1.5 s rake | a new rake from string 1; the strings the old one had not reached are not struck by it |
| two Riggings, same settings and trigger | never identical |

### 13.4 Glide

| Procedure | Expected |
|---|---|
| change Chord or turn Tune a semitone, a drone running | a smooth bend, about 50 ms to be in tune (63 % in 8 ms); no click |
| an octave jump on string 1 at Tune 0 and at Tune −24, Damp 0 | an audible swoop; fizz at the start would be the unmeasured glide aliasing (section 8) [unverified]; report it |

### 13.5 Saved patches and Initialize

| Procedure | Expected |
|---|---|
| set every knob and Free pitch, save, reopen | all restored; the strings start silent |
| edit the JSON: remove `freePitch`; set it to `1` | loads as off (anything but boolean true is off) |
| Initialize with a rake in progress | silence at once; Free pitch off; knobs defaults |
| duplicate the module | settings carry; the two instances strike differently |

### 13.6 Bypass

| Procedure | Expected |
|---|---|
| bypass with IN L only; with both; a polyphonic cable [unverified] | the input passes (IN L alone to both sides); the light dark; releasing resumes the strings where they stopped |

### 13.7 Sample rates

| Procedure | Expected |
|---|---|
| 44.1, 48, 96, 192, 384, 768 kHz | no crash, no silence; string 1 within 3 cents of C2; the 60 s Ring holds at 220 Hz at every rate |
| change the rate with a drone ringing | the strings go silent and ring again at the same pitches |
| V/OCT +4 to +6 V at 44.1 kHz | the highest strings ring very briefly (section 5.4): expected |

### 13.8 CPU

| Procedure | Expected |
|---|---|
| one instance, nothing patched | the idle level equals a drone: about 0.15 % of an M-series core at 48 kHz (budget 0.3) |
| with every CV moving and STRUM at 20 Hz | about 0.30 % (budget 0.6) |
| 16 instances | per-instance cost within 1.1 × one alone |
| Windows | no budget; record the Rack meter for one idle and one busy instance |

### 13.9 Aliasing and loud tuned tones

| Procedure | Expected |
|---|---|
| a ±5 V sine at 1,013 Hz into IN L; V/OCT fine-tuned so string 1 (126.6 Hz, the 8th harmonic) resonates; Ring 60 s, Damp 0, Fifths | the output holds about 10 V flat-topped; the spectrum shows a spur about 51 dB under the tone (16.7 kHz at 44.1 kHz, 22.7 kHz at 48 kHz) |
| the same at ±1 V | peak about 10 V; spurs 78 to 83 dB under |
| a ±24 V sine at 6,007 Hz, string 1 at 120.14 Hz (the 50th harmonic), Ring 4 s, six strings | spur about −33 dB (2,051 Hz at 44.1 kHz, 5,951 Hz at 48 kHz). Record the figure on #67 |

---

## 14. The measurement programs

All are outside the repository, in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/rigging/meas/`, compiled against a copy of the exact tree (`git archive 2e6e960`, at `.../rigging/tree/`) with the plugin's flags (`build.sh`). They drive `rka::RiggingEngine` as `Rigging.cpp` does: IN L only (unless stated), the knobs at their defaults, a seed per run. They do not exercise Rack's ports, threads or GUI.

| File | What it measures |
|---|---|
| `common.hpp` | the engine wrapper, windowed Fourier sum, RMS and peak helpers |
| `m_ranges.cpp` | pitch tables for all chords and Tune/V/OCT clamps at 44.1 to 192 kHz; glide time; Chord CV change points; note-strike detector (steps, ramps, LFOs); Mix law; Ring and Spread tooltip laws |
| `m_levels.cpp` | drone RMS and peak against Ring, Damp, Chord, Tune and input level; ceilings; strike peaks; content under 10 Hz; DC; per-string stereo gains |
| `m_misc.cpp` | issue #53 (the share's step at Damp 0); strike peaks over 100 seeds; drone level across Ring with 60 s windows and 3 noise seeds |
| `m_t60.cpp` | the fundamental's T60 of one string against pitch, Ring and Damp at 48 and 44.1 kHz |
| `m_tune.cpp` | tuning error at 44.1, 88.2, 192 and 384 kHz |
| `m_glide.cpp` | the delay's read speed during a pitch change (`String` internals, via `#define private public`) |
| `m_high.cpp` | the mixed output for V/OCT +3 to +6 V at 44.1, 48 and 96 kHz |
| `m_alias.cpp`, `m_alias_mut.cpp` | aliasing of a loud tuned tone with `AliasMeter` at 44.1 and 48 kHz; the same with the output limit or the loop bound removed (scratch copies of `RiggingTuning.hpp` in `mut/`) |
| `m_click.cpp`, `m_click2.cpp`, `m_click3.cpp`, `m_click4.cpp` | the largest sample step after a control step (every control; Damp in detail) |

Things I did not do: build or run the plugin in Rack; run `make smoke`, `make libm-calls` or `make` (the Rack build); load a patch; listen. The strings, their colour, the strike's character and the stereo picture are for the listening list.

---

## 15. Glossary

| Term | Meaning |
|---|---|
| Bound | the soft limiter inside each string's loop: identity to 12 V, ceiling 16 V |
| Burst | the noise a strike puts into a string, about one period long |
| Chord | one of seven tunings of the six strings |
| Damp | how fast the overtones die; a one-pole low-pass in each loop |
| Free pitch | menu item: Tune moves smoothly instead of in semitones |
| Fundamental T60 | time for a string's fundamental to fall 60 dB |
| Glide | the 8 ms (time constant) bend of a string to a new pitch |
| Hermite read | 4-point cubic interpolation for reading a delay line between samples |
| Karplus-Strong | a delay line fed back through a low-pass: a plucked string |
| Loop gain | the factor a string's sound is multiplied by on each trip round its loop, set by Ring |
| Note strike | a strike made by a new note at V/OCT when STRUM is unpatched |
| Rake | a strike spread over the six strings by Spread |
| Ring | the time for a string to fall 60 dB at Damp 0, 0.1 s to 60 s |
| Share | each string's input gain that keeps a noise drone's loudness steady across Ring |
| Soft limit | the limiter on the strings' sum: identity to 8 V, ceiling 10 V |
| Spread | the time from string 1's strike to string 6's, 0 to 1.5 s |
| String | one waveguide of the six |
| T60 | time for a decay to fall 60 dB |
