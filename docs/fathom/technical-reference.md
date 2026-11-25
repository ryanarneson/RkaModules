# Fathom 2.0.39: Technical Reference for QA

Module: **Fathom**, plugin `rka` (slug `rka`, module slug `Fathom`), VCV Rack 2 (minRackVersion 2.5.0).
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from that tree (`git archive 2e6e960`). The Fathom folder (`src/fathom/`) was last changed by `7a2ab51` (2026-11-21, in build 2.0.25); `res/fathom/`, `docs/fathom/` and Fathom's section of the specs are unchanged since the Instrument look (2026-11-15).
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It is a Zstandard-compressed tar. Its `plugin.json` is byte-identical to the commit's (version 2.0.39, 17 modules), and so are `res/fathom/Fathom.svg` and `Fathom-dark.svg`. `presets/` holds only `Fog`: **Fathom has no factory presets.** The four menu keys (`subOctave`, `freePitch`, `filterType`, `subThroughFilter`) appear as strings in `plugin.dll`. I did not run the package in Rack.
Document date: 2026-11-24.

## Build history

Fathom has no entry of its own under "Test builds" in `CHANGELOG.md`; its release note ("New module: **Fathom**") is under "2.0.0 (unreleased)". Its code is in every build since 2.0.1, the oldest build commit in this history. What `git log` shows for its folder:

| Date | Commit | In build | What changed |
|---|---|---|---|
| 2026-10-24 | `c55c9d8` … `bb73f55` | before 2.0.1 | Phase 1: TPT state-variable filter, 7-saw SuperSaw, per-saw Drift, drone voice, Orbit panel, module, menu |
| 2026-10-24 | `a905eba` … `981431d` | before 2.0.1 | Phase 2: halfband pair, compensated 2x ladder with four types, output stage with sub routing and drive makeup, flat filter section, manual |
| 2026-11-15 | `cadd96d`, `6d9b3c8`, `ea83404`, `f639f6d` | before 2.0.1 | The Instrument look: new panel, knobs, jacks; PITCH on a smaller dial |
| 2026-11-15 | `a46828c`, `514ce76`, `56289b0`, `5da7859` | before 2.0.1 | Per-sample logic moved into the Rack-free `FathomEngine`; `FastMath.hpp` (exp2, sin, tanh without libm); body skipped when L and R are unpatched; `test_engine` against the frozen pre-change engine. Cut the idle cost from 0.58 % to 0.02 % of a core (issue #8) |
| 2026-11-15 | `204ff8a`, `75d4985`, `b888141` | before 2.0.1 | `make bench` budgets and the libm and globals checks cover Fathom |
| 2026-11-20 | `b40ffb6` | 2.0.22 | Ladder and halfband made templates (`LadderCore.hpp`, `Float4.hpp`) so Narrows can share them |
| 2026-11-21 | `7a2ab51` | 2.0.25 | Two tests hold the shared ladder to the frozen copy: to the bit under strict floating point, to rounding under the plugin's flags |

- The CHANGELOG states that Fathom's output is unchanged by the 2.0.22 refactor (to the bit in the Mac and Linux builds, within rounding on any compiler). `git diff 122d85b 2e6e960` (2.0.2 to 2.0.39) over `src/fathom`, `res/fathom` and `docs/fathom` shows only that refactor (`LadderCore.hpp`, `Halfband.hpp`, `Ladder.hpp`) and the two new tests. The shared `widgets.hpp` and `gen_panels.py` also changed for other modules; Fathom's panel files did not.
- A listening-guide pass on 2.0.39 (`fathom-2.0.39-listening.html`) found two things that this document verifies: CUTOFF tops out at 20.48 kHz (the manual says 20 kHz), and the module is mono (issue #64). Both are confirmed (sections 1.1, 2.9 and 7.4).

---

## 0. How to read this document

- Every number comes from one of these sources, marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/fathom/*.hpp`, `Fathom.cpp`, `src/FastMath.hpp`, `src/sway/Waves.hpp`), cited as file:line where useful.
  - **[test]**: printed or asserted by the module's automated tests. I built all ten test programs in `src/fathom/` from the 2.0.39 tree with `tests.mk` (so with its flags: `-O2` for seven, and `-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd` for `test_engine`, `test_fast_math` and `test_ladder_fast`) on an Apple M-series Mac, and ran them. All ten passed. I then built the same ten with `-O1 -fsanitize=address,undefined -fno-sanitize-recover=all`, which `make test` does not do for Fathom, and all ten passed again.
  - **[meas]**: a measurement program I wrote and ran against the engine (`rka::FathomEngine`, or `Ladder` and `OutputStage` alone where stated), built with the plugin's flags, at 48 kHz unless stated. They are in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/fathom/meas/` (see the note at the end).
- **[issue #N]** points to a GitHub issue in the repository (section 9).
- **[unverified]** marks anything I could not check, mostly Rack host behaviour. I did not run Rack. I did not run `make libm-calls` (Linux only, issue #36) or `make smoke`.
- Voltages are Rack volts. The engine works at nominal ±1 and the module scales by 5 V. "Noon" means a knob at 0.5.
- The listening guide was used as a cross-check, not as a source. Where it disagrees with the code, the code wins; the disagreements are in section 1.2.
- A knob's value is 0..1 unless stated. CUTOFF's knob value is 0..10 (octaves above 20 Hz).

---

## 1. What Fathom is

Fathom is a 10 HP (50.8 mm) mono bass drone voice with stereo and sub outputs: a sine sub under a seven-saw supersaw body, slow per-saw drift so a held note keeps moving, and a Moog-style four-pole ladder filter with resonance and drive. One V/OCT input, five CV inputs, three outputs (L, R, SUB). Drawn as an echo sounder, with PITCH as the transducer. It is made for dark and space ambient, where one note is held for minutes. Specs: `docs/specs/2026-10-24-fathom-design.md` (phase 1, the voice), `docs/specs/2026-10-24-fathom-filter-design.md` (phase 2, the filter), `docs/specs/2026-11-15-instrument-look-design.md` (the panel, "Fathom: an echo sounder"). Manual: `docs/fathom/README.md`.

Design intent (from the specs): a drone you can hold on one note for minutes while it breathes; a sub you feel and a body you can thicken or thin; the filter growls when RES and DRIVE are both up (the stages compress against themselves) and is clean when they are down; the low end stays put as resonance rises; the sine of self-oscillation is quiet and in tune.

Goals QA can check:

- The sub is a pure sine at f0/2 (or f0/4), tracking V/OCT within 1 cent, and SUB OUT carries it alone at ±5 V.
- Drift is on by default and moves a held note without any patching; DRIFT 0 is steady.
- PITCH is ±2 octaves in semitones, 0 at C2 (65.41 Hz); the body's f0 is held between 8 Hz and 4 kHz.
- The ladder is 24 dB per octave in LP24, with a quiet self-oscillation at the top of RES that tracks CUTOFF at 1 V/octave.
- DRIVE changes the tone far more than the level.
- Under 1 % of a core at 48 kHz on Apple Silicon (spec target). The bench gives 0.6 % busy [meas, section 8.2].
- No aliasing grit at bass pitches; no click on a V/OCT step, a CUTOFF, RES or DRIVE move (sections 2.4, 5, 6.4).

Non-goals (do not file these as defects): polyphony (issue #64 is the open request), a filter position switch beyond the sub routing, comb mode, external audio in, an expander, wavetables, an effects section, a randomizer. Phase 1's "12 dB per octave low-pass" was replaced by the ladder in phase 2.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Test against the code's behaviour; the others are given for context. I compared the three specs, the manual and the code line by line.

| Topic | Spec / manual says | 2.0.39 does |
|---|---|---|
| **CUTOFF top** | manual "20 Hz – 20 kHz"; both specs "20 Hz … 20 kHz"; listening guide's range item says "about 20 kHz" | the knob spans 10 octaves from 20 Hz: **20 Hz to 20,480 Hz** (`Fathom.cpp:80`, `configParam(CUTOFF_PARAM, 0, 10, …, " Hz", 2, 20)`, 20·2^v). The tooltip reads up to 20480 Hz. At 44.1 kHz the ladder clamps its pole at 0.45·fs = 19,845 Hz, so the top 0.4 % of the dial sounds the same [code, meas]. The listening guide's own range sentence is correct (it says 20.48 kHz) |
| CUTOFF is "the ladder's pole frequency" | manual, spec | true, and it is not the −3 dB point: LP24 is −3 dB at 0.435 × CUTOFF (521 Hz at the default 1.2 kHz), LP12 at 0.645 ×, HP at 2.3 × [meas, section 5.2]. At the top of the dial the LP24 is −3 dB at about 10.2 kHz and −11.5 dB at 20 kHz, so "fully clockwise" is not flat to 20 kHz |
| **Polyphony** | spec: "Inputs read channel 0 only"; phase 1 non-goal "polyphony"; manual silent; listening guide "It is mono" | confirmed: channel 1 only of V/OCT and all five CVs (`inputs[i].getVoltage()` is channel 0); one voice; outputs are single-channel. A chord cable plays one note. Issue #64 asks for a voice per channel |
| **CV law** | manual: "**±5 V at full sweeps the knob's whole range**" (DETUNE, DRIFT, RES, DRIVE); the listening guide repeats it | the code adds `cv/10 × attenuverter` to the knob, then clamps to 0..1 (`FathomEngine::modulated`). ±5 V covers the whole range only from noon. At the defaults (DETUNE 25 %, DRIFT 25 %, RES 20 %, DRIVE 25 %) +5 V at full gives 75 %, 75 %, 70 %, 75 %, and −5 V clamps at 0. The spec's formula (`knob + atten·(cv/10 V)·(max − min)`) matches the code; only the prose is wrong. Fog had the same sentence (Fog issue #31, closed); Fathom's was not filed |
| CUTOFF CV | "1 V/oct at full clockwise" | exact: `octaves = knob + att × cv`, clamped to 0..10 octaves |
| **DRIFT 0** | spec: "DRIFT 0 freezes everything at zero offset"; manual "0 % is static" | static once the walks have decayed: after DRIFT 1 → 0 each saw's offset takes **6.0 s** to fall inside 0.01 cent and 0.01 dB, because the walks slew over 1 s [meas, section 2.8]. From a fresh start DRIFT 0 is exactly static |
| V/OCT smoothing | manual "smoothed over 5 ms" | a one-pole of 5 ms time constant on f0 in Hz: 63 % of the step at 5.0 ms, within 10 cents at 22–26 ms and within 1 cent at 34–37 ms for an octave step; 23 ms for a semitone step [meas, section 2.4] |
| Pitch clamp | manual "clamped to 8 Hz – 4 kHz"; spec "`f0` (the body's pitch) clamps … so the sub bottoms out at 4 Hz (2 Hz with the two-octave sub)" | the clamp is on the body's f0; the sub is f0/2 or f0/4, so SUB OUT runs from 4 Hz (2 Hz) to 2 kHz (1 kHz) [meas: 4.0001 Hz at −4 V, 1999.99 Hz at +6 V] |
| **Peak droop** | manual: a hot signal pulls the peak below CUTOFF "up to about an octave at the module's own levels, and it moves a little with DRIFT" | the peak sits at −0.2 octave for a 0.05-amplitude partial, −0.56 at 0.2 and −1.1 at 0.5 (RES 90 %, DRIVE 25 %) [meas, section 5.4]. In the real C2 drone the strongest bump is within −0.4 to +0.1 octave of CUTOFF. DRIVE moves it a lot (−0.56 to −1.42 octave at 0.2 amplitude going from DRIVE 25 % to 70 %). The listening guide says "a little with DRIVE", the manual says DRIFT; I did not measure DRIFT's effect |
| Self-oscillation level | phase 2 spec "about 0.1 at the ladder's output … roughly 1 V at the jack, 10–12 dB under the drone"; manual "a quiet sine" | 0.865 V peak at the jack at RES 100 %, DRIVE 25 % (11 dB under the default drone's RMS) [meas]; 1.68 V at DRIVE 0 and 0.18 V at DRIVE 100 % |
| Self-oscillation starts | spec "from about 99 %" | RES 0.9877 is k = 4.0 exactly; no tone at 0.985 or 0.9877; 0.38 V at 0.990, 0.67 V at 0.995, 0.86 V at 1 [meas] |
| Drive makeup | spec: "× 1/g_drive below unity drive and × g_drive^−0.25 above" (exponent "tunable by ear") | exponent **0.75** above unity (`MAKEUP_EXP`, `DroneVoice.hpp:168`); 1/g below. The L/R level moves within ±0.15 dB across DRIVE 0 to 100 % [meas] against the spec's "within about a dB" |
| DRIVE smoothing | spec: "k and g_drive with a 20 ms one-pole each" | DRIVE is smoothed in octaves of gain (a one-pole on log2 gain), so a step ramps geometrically; time constant 20 ms [code] |
| Panel layout | phase 1: "Orbit" panel with CUTOFF ring; phase 2: three columns under the orbit | both superseded by the Instrument look (`2026-11-15-instrument-look-design.md`, `FathomLayout.hpp`): positions in section 2.1. The shipped panel matches that spec |
| Pitch update | phase 1: "computed every 16 samples" | `exp2` is evaluated when PITCH or V/OCT changes (not on a 16-sample counter) and the 5 ms slew runs per sample; the body's high-pass coefficients are worked out once per 16-sample block while f0 moves [code] |
| File names | phase 1: `Voice.hpp`, `Svf.hpp` low-pass | the voice is `DroneVoice.hpp`; the `Svf` is used only as the body's high-pass (its LP and BP outputs are computed and unused) [code] |
| `plugin.json` | description "Deep bass drone voice: sub sine, 7-saw supersaw, drift, low-pass"; tags "Synth voice", "Oscillator" | the filter is a four-type ladder; no "Filter" and no "Polyphonic" tag. Docs only |
| Sub pitch on a tuner | manual: "The root itself is the sub" | confirmed by issue #77 (closed, works as designed) and by measurement: with SUB at 50 % the strongest partial in L is f0/2 (2.93 V), f0 is 0.65 V [meas, section 2.5] |
| libm | CLAUDE.md/TESTING.md: none per sample with the knobs still | true; libm runs only when a control moves (exp2 per block on a moving cutoff or drive, `tan` for the body's high-pass while f0 moves, `sin`/`cos` when SPREAD moves) and at rate changes [code]. Not run on Linux [issue #36] |

### 1.2 Where the listening guide is wrong or loose

Code wins. Noted for the author:

| Guide says | Found |
|---|---|
| CV items: "±5 V at full sweeps the knob's whole range" | only from noon (section 1.1) |
| "Sweep CUTOFF … 20 Hz at the left (the drone is mostly gone)" | at the default SUB (50 %) the sub joins after the filter and stays: L reads −0.7 dB against CUTOFF open (2.04 V against 2.20 V RMS). The drone is gone (−44 dB) only with SUB at 0 [meas] |
| "With RES and DRIVE both high … moves a little with DRIVE" | DRIVE moves the peak by about an octave (section 5.4); the manual says DRIFT |
| "DRIVE … the loudness stays fairly even"; spec "within about 2 dB" | measured within 0.15 dB of the default (−0.03 to +0.14 dB) for DRIVE 0 to 100 % on the default drone [meas] |
| "Turn SUB, DETUNE and DRIVE up together … the level stays inside ±5 V" | held settings reach 5.01 V (SUB 1, DETUNE 1, DRIVE 1) and up to 5.28 V in the worst held case I searched; a stepped CV can reach 7.2 V because the DC blocker follows the clipper (section 6.2) |
| "Change Rack's sample rate … at most a short settle" | the ladder is cleared at every rate change: the largest sample step in the 50 ms after is 0.34 V against 0.09–0.19 V before [meas]; a click is possible (section 8.3) |
| "Bypass the module. The outputs go silent." | no `configBypass` in `Fathom.cpp`, so Rack's default applies [unverified in Rack] |

Everything else I checked in the guide agrees with the code: the defaults, the 3.9 V default peak, the tooltip note names (C0 at −24, C4 at +24), 32.7 Hz sub at C2 and 16.35 Hz with two octaves, +6 V holding at 4 kHz and −4 V at 8 Hz, a 1 V/oct octave step, the ±6 cents and 3 dB drift figures, the quiet sine's level, and that the sine fades as DRIVE rises.

---

## 2. Panel, controls and ports

### 2.1 Layout

10 HP, 50.8 × 128.5 mm, title "FATHOM", subtitle "DRONE VOICE", footer "RKA · INSTRUMENTS". Two screws (top right, bottom left). The panel is generated (`tools/gen_panels.py`); the origin of every arc is PITCH. Positions in mm from `FathomLayout.hpp` [code]:

| Control | Widget | Centre (x, y) mm | Label |
|---|---|---|---|
| PITCH | `DialKnob`, 14 mm, numbered −2 … +2 octaves, a tick per semitone | 19.0, 33.0 | PITCH |
| SUB | `LimbKnob`, 8.6 mm | 43.73, 24.97 | SUB |
| SPREAD | `LimbKnob` | 44.23, 39.29 | SPREAD |
| DETUNE | `LimbKnob` | 37.06, 51.70 | DETUNE |
| DRIFT | `LimbKnob` | 24.41, 58.43 | DRIFT |
| V/OCT input | `BrassPort`, 8 mm | 8.42, 56.75 | V/OCT |
| RES | `LimbKnob` | 6.4, 78.28 | RES |
| CUTOFF | `FaceKnob`, 9.6 mm, unnumbered scale (a long tick per octave) | 25.4, 79.56 | CUTOFF |
| DRIVE | `LimbKnob` | 44.4, 72.55 | DRIVE |
| Attenuverters (`Atten`, 5.6 mm) RES, DRIFT, CUTOFF, DETUNE, DRIVE | | 6.4, 89.61 / 15.9, 90.92 / 25.4, 90.65 / 34.9, 88.78 / 44.4, 85.14 | none (the traces name them) |
| CV jacks RES, DRIFT, CUTOFF, DETUNE, DRIVE | | x 6.4, 15.9, 25.4, 34.9, 44.4 at y 101.0 | none |
| SUB output | `BrassPort` on an ivory tile | 6.4, 116.0 | SUB |
| L, R outputs | `BrassPort`, each on an ivory tile | 34.9, 116.0 / 44.4, 116.0 | L, R |

- All knobs sweep 270° (±0.75 π) [code, `widgets.hpp`]. A trace joins each CV jack to its attenuverter and on to its knob; DRIFT's and DETUNE's climb and elbow across to the arc.
- Order of the five CV columns, left to right: RES, DRIFT, CUTOFF, DETUNE, DRIVE.
- The faint line falling from PITCH is the sounding line; it is decoration.
- There are no lights and no switches on the panel. The menu (section 3) holds the four choices.

### 2.2 Parameter, input and output ids

Pinned by `static_assert` in `Fathom.cpp` against `FathomFrame` and must never change number (saved patches depend on them). Phase 2 ids were appended after phase 1's.

| Id | Param | Range | Default | Tooltip (label, unit) |
|---|---|---|---|---|
| 0 | PITCH | −24 … +24 st, snapped (menu can free it) | 0 | "Pitch": the note name (C0 … C4), or "%.2f st" with Free pitch |
| 1 | SUB | 0 … 1 | 0.5 | "Sub level", % (0 … 100) |
| 2 | DETUNE | 0 … 1 | 0.25 | "Detune", % |
| 3 | SPREAD | 0 … 1 | 0.6 | "Stereo spread", % |
| 4 | DRIFT | 0 … 1 | 0.25 | "Drift", % |
| 5 | CUTOFF | 0 … 10 (octaves above 20 Hz) | 5.907 = log2(1200/20) | "Cutoff", Hz, shown as 20 × 2^v (20 … 20480) |
| 6 | DETUNE attenuverter | −1 … +1 | 0 | "Detune CV amount", % (−100 … +100) |
| 7 | DRIFT attenuverter | −1 … +1 | 0 | "Drift CV amount", % |
| 8 | CUTOFF attenuverter | −1 … +1 | 0 | "Cutoff CV amount (1 V/oct at 100%)", % |
| 9 | RES | 0 … 1 | 0.2 | "Resonance", % |
| 10 | DRIVE | 0 … 1 | 0.25 | "Drive", % |
| 11 | RES attenuverter | −1 … +1 | 0 | "Resonance CV amount", % |
| 12 | DRIVE attenuverter | −1 … +1 | 0 | "Drive CV amount", % |

| Id | Input (tooltip) | | Id | Output (tooltip) |
|---|---|---|---|---|
| 0 | "1 V/oct pitch" | | 0 | "Left" |
| 1 | "Detune CV" | | 1 | "Right" |
| 2 | "Drift CV" | | 2 | "Sub (sine, before SUB and CUTOFF)" |
| 3 | "Cutoff CV" | | | |
| 4 | "Resonance CV" | | | |
| 5 | "Drive CV" | | | |

There is no CV for SUB, SPREAD or PITCH (V/OCT is the pitch CV). Lights: none. The tooltip text is as Rack formats it from these `configParam` calls [code]; the exact digits Rack shows were not checked [unverified].

### 2.3 Control summary: range, law, CV, smoothing

| Control | Law (knob → value) | CV law | Smoothing (all [code]) |
|---|---|---|---|
| PITCH + V/OCT | f0 = 65.4064 Hz × 2^((PITCH + 12 × V/OCT) / 12), clamped 8 … 4000 Hz | V/OCT 1 V/octave, no attenuverter, no range limit (±50 V simply clamps) | one-pole on f0 in Hz, τ 5 ms, per sample |
| SUB | linear gain 0 … 1 on a sub of nominal 0.6 | none | none: a step is a step |
| DETUNE | JP-8000 table, section 2.6 | `knob + cv/10 × att`, clamp 0..1, every sample | none: ratios and volumes change at once |
| SPREAD | pan of saw i = SPREAD × (i − 3)/3 | none | none: pan gains change at once |
| DRIFT | scales the walks' targets, section 2.8 | `knob + cv/10 × att`, clamp 0..1, every sample | the walks slew over 1 s (τ), targets drawn about every 0.5 s |
| CUTOFF | 20 × 2^v Hz | `v = clamp(knob + att × cv, 0, 10)`, read once per 16-sample block | one-pole on octaves, τ 20 ms, per block |
| RES | k = 4.1 × RES² | `knob + cv/10 × att`, clamp 0..1, once per block | one-pole on k, τ 20 ms, per block |
| DRIVE | gain = 2^(4 × DRIVE − 1) | `knob + cv/10 × att`, clamp 0..1, once per block | one-pole on log2 gain, τ 20 ms, per block |
| Filter type, Sub octave, Sub through filter | menu | none | none: instant |

Block = 16 samples (`BLOCK`, `DroneVoice.hpp:26`). So CUTOFF, RES and DRIVE CV are sampled at 3 kHz (48 kHz ÷ 16) and then smoothed; an audio-rate CV on them is aliased and filtered. DETUNE and DRIFT CV are read every sample. NaN or ±infinity on a CV, V/OCT or a parameter: section 8.4.

### 2.4 PITCH, V/OCT and the body pitch

[code, `FathomEngine::process`, `DroneVoice::process`]

- PITCH is snapped to semitones by default (Rack's `snapEnabled`; the menu's Free pitch turns it off, and turning it back on rounds the knob). The tooltip shows the note: `names[(n mod 12)] + (2 + floor(n/12))`, so −24 reads C0, −12 C1, 0 C2, +12 C3, +24 C4.
- f0 table (the body's fundamental; the sub is f0/2, or f0/4 with Sub octave "Two octaves down"):

| PITCH (V/OCT 0) | −24 | −12 | 0 | +12 | +24 |
|---|---|---|---|---|---|
| Note | C0 | C1 | C2 | C3 | C4 |
| f0, Hz | 16.352 | 32.703 | 65.406 | 130.813 | 261.626 |

| V/OCT, V (PITCH 0) | −4 | −3 | −1 | 0 | 1 | 2 | 5 | 6 |
|---|---|---|---|---|---|---|---|---|
| f0 before the clamp, Hz | 4.088 | 8.176 | 32.703 | 65.406 | 130.813 | 261.626 | 2093.0 | 4186.0 |
| f0 after the clamp, Hz | 8.000 | 8.176 | 32.703 | 65.406 | 130.813 | 261.626 | 2093.0 | 4000.0 |
| SUB OUT measured, Hz (one octave down) | 4.0001 | 4.0880 | 16.3519 | 32.7031 | 65.4056 | 130.8102 | 1046.487 | 1999.986 |

  Sub frequency error against f0/2 (or f0/4): within 0.03 cent at every row [meas]; two-octave sub measured 2.0000, 2.0440, 8.1759 and 16.3516 Hz at −4, −3, −1 and 0 V. The tests pin ±1 cent over −3 … +5 V and the clamp ends [test: `testSubTracks`, `testClampLow`, `testClampHigh`, `testSubOctaves`].
- **Slew.** `f0 += (target − f0) × (1 − exp(−dt/5 ms))` per sample [meas, f0 read from the engine]:

| V/OCT step | 63 % of the Hz step | within 10 cents | within 1 cent |
|---|---|---|---|
| 0 → 1 V | 5.0 ms | 22.3 ms | 33.8 ms |
| 1 → 0 V | 5.0 ms | 25.8 ms | 37.3 ms |
| 0 → +1 semitone | 5.0 ms | 11.4 ms | 22.9 ms |
| 0 → 5 V | 5.0 ms | 25.6 ms | 37.1 ms |

  The slew is in Hz, so a glide is exponential in Hz, not even in pitch. No click on a step: the largest sample step in L in the 100 ms after PITCH +1 st, PITCH +12 st or V/OCT +1 V is 0.14–0.18 V against 0.16 V steady [meas, section 2.13].
- In float the slew can stop up to about 0.03 Hz short of the target at 4 kHz (0.004 Hz at 440 Hz): under 0.02 cent [meas: f0 read back 439.996 for 440 and 3999.971 for 4000].
- **Start-up.** The engine's f0 starts at C2 whatever PITCH is (`FathomEngine::reset`, constructor), so a patch saved with another pitch glides in: from a fresh engine at PITCH +12 the first sample's f0 is 65.678 Hz and it reaches within 10 cents after 22 ms and 1 cent after 34 ms [meas]. Initialize does the same if PITCH is not at its default.
- V/OCT is unattenuated and has no range limit. The body clamp holds f0 inside 8 Hz … 4 kHz; the saws' own aliasing at 4 kHz is section 8.5.

### 2.5 SUB and SUB OUT

[code, `DroneVoice::advanceSub`, `SUB_GAIN`]

- The sub is `waveSin(phase)` (`fastSin2pi`, within 2.5e-7 of a sine), phase in double, advancing at f0/2 or f0/4. It does not drift in pitch. Its level drifts (section 2.8).
- In the L/R mix: `subMix = SUB × 0.6 × subDriftGain × sub`. SUB 0 gives exactly 0.
- **SUB OUT** = 5 V × the raw sine: exactly ±5.000 V, ignoring the SUB knob, the filter, the clip and the drift [test: `testSubOutIgnoresSubLevel`; meas: peak 5.000 V in every case]. It follows PITCH and V/OCT (through the slew) and the Sub octave menu. It keeps running with L and R unpatched (section 7.1).
- The sub joins **after** the ladder by default (so CUTOFF does not thin it) and goes through it with "Sub through filter" (then it also gets DRIVE's gain and makeup).
- **Tuner on L or R (issue #77, closed, works as designed).** L at C2, DRIFT 0, SPREAD 0, partial amplitudes at f0/2, f0, 2 f0, 3 f0 [meas]:

| SUB | Sub octave | f0/2 | f0 | 2 f0 | 3 f0 |
|---|---|---|---|---|---|
| 0 | either | 0.001 V | 0.759 V | 0.641 V | 0.460 V |
| 0.5 | one down | **2.925 V** | 0.651 V | 0.601 V | 0.452 V |
| 1 | one down | **4.705 V** | 0.501 V | 0.516 V | 0.423 V |
| 0.5 | two down (f0/4 = 16.35 Hz) | 0.035 V | 0.642 V | 0.545 V | 0.392 V; partial at f0/4: 2.880 V |

  With SUB up, the lowest and strongest partial of L is the sub, so a tuner on L reads the sub's note, one octave (or two) below PITCH; with SUB at 0 it reads f0 (the saws are high-passed at f0, so the fundamental is 3 dB down but present).
- The SUB knob is not smoothed: a step of 0.5 → 0 gives a one-sample jump of up to 2.5 V in L (section 2.13).

### 2.6 DETUNE: the seven saws

[code, `SuperSaw.hpp`, adapted from Befaco Oneiroi's `StereoSuperSaw.h` after Szabo (2010)]

```
d = clamp(DETUNE, 0, 1);  spread = 0.4 d
ratio = 1 + spread × {−0.11002313, −0.06288439, −0.01952356, 0, +0.01991221, +0.06216538, +0.10745242}
v = clamp(0.5 d, 0.005, 0.5)
side volume   = −0.73764 v² + 1.2841 v + 0.044372       (six outer saws)
centre volume = −0.55366 v + 0.99785
gain          = 0.3 × (1.4 − spread)
```

| DETUNE | Saw offsets in cents, outer left … outer right | Side volume | Centre volume | Gain |
|---|---|---|---|---|
| 0 | all 0 (seven coherent saws = one saw) | 0.051 | 0.995 | 0.420 |
| 0.25 (default) | −19.2, −10.9, −3.4, 0, +3.4, +10.7, +18.5 | 0.193 | 0.929 | 0.390 |
| 0.5 | −38.5, −21.9, −6.8, 0, +6.9, +21.4, +36.8 | 0.319 | 0.859 | 0.360 |
| 1 | −77.9, −44.1, −13.6, 0, +13.7, +42.5, +72.9 | 0.502 | 0.721 | 0.300 |

- Each saw is a polyBLEP saw (`sawBlep`, two-sample) with a double phase accumulator, all starting at phase 0. At DETUNE 0 the seven are one saw [test: `testZeroDetuneIsOneSaw`, within 1e-5].
- The centre saw gets quieter and the sides louder as DETUNE rises (above). The body then goes through a 2nd-order Butterworth high-pass at f0 (`Svf`, k = √2), per side, and × `BODY_GAIN` 0.8.
- DETUNE is not smoothed. Moving it by CV changes ratios and volumes at once; no click was found: the largest L step in the 100 ms after DETUNE 0.25 → 1 is 0.15 V against 0.16 V steady [meas].
- Tooltip "Detune", %.

### 2.7 SPREAD

[code, `SuperSaw::setPan`] Saw i (0 … 6) is panned to `pos = SPREAD × (i − 3)/3` with an equal-power pan, `a = (pos + 1) π/4`, L = cos a, R = sin a. At SPREAD 0 every saw is centred (0.707 each side, so L = R exactly [test: `testSpreadZeroIsMono`, < 1e-6]). At SPREAD 1 saw 0 is hard left and saw 6 hard right.

| Saw | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| L, R at SPREAD 0.6 (default) | 0.951, 0.309 | 0.891, 0.454 | 0.809, 0.588 | 0.707, 0.707 | 0.588, 0.809 | 0.454, 0.891 | 0.309, 0.951 |
| L, R at SPREAD 1 | 1, 0 | 0.966, 0.259 | 0.866, 0.5 | 0.707, 0.707 | 0.5, 0.866 | 0.259, 0.966 | 0, 1 |

- The sub stays centred (the same value on both sides).
- Mono sum with SPREAD: the sum stays within 1.5 dB of SPREAD 0's [test: `testSpreadKeepsMonoSum`, `testSpreadMonoSum`].
- Measured L/R correlation of the outputs, 10 s, defaults except as stated [meas]: 0.993 at the defaults, 1.000 at SPREAD 0, 0.982 at SPREAD 1, 0.949 with SUB 0, 0.972 at DETUNE 1. The sub dominates the default correlation.
- SPREAD has no CV and is not smoothed. The tooltip is "Stereo spread", %.

### 2.8 DRIFT

[code, `Drift.hpp`]

- Each of the seven saws and the sub owns a `DriftWalk` seeded 1, 2, … 7 and 100 (splitmix64). **The seeds are fixed, so every Fathom instance drifts identically**: two engines at the same settings gave 0 differing samples in 20 s [meas]. Two Fathoms patched together at the same pitch are phase-coherent and drift in lockstep.
- Every `0.5 s × (1 + 0.25 u)` (0.375 … 0.625 s, u uniform in ±1) a new target is drawn: pitch `6 cents × DRIFT × u1` (uniform in ±6 × DRIFT cents) and level `−3 dB × DRIFT × (0.5 + 0.5 u2)` (0 … −3 × DRIFT dB). Both slew toward the target with a 1 s one-pole.
- The sub walks in level only. Each saw's pitch ratio is `exp2(cents/1200)` and its gain `10^(dB/20)` (both via `FastMath`).
- Measured over 300 s, after 5 s [meas]: DRIFT 100 %: pitch −4.87 … +5.10 cents, level −2.65 … −0.41 dB; DRIFT 25 % (default): −1.22 … +1.28 cents, −0.66 … −0.10 dB. The tests pin the hard bounds ±6 cents and 0 … −3 dB [test: `testBounds`].
- **DRIFT 1 → 0** (after 20 s at 1): every saw is within 0.01 cent and 0.01 dB of rest after **6.00 s**. [meas] **DRIFT 0 → 1** (after 10 s at 0): the first saw passes 0.1 cent after 0.07 s in this run; this depends on where the 0.375–0.625 s timer is, so up to about 0.6 s plus the slew. The targets are drawn with the DRIFT value of that moment, so a change is felt at the next draw, not at once.
- Drift only runs while L or R is patched (section 7.1); on re-patching the walks resume where they stopped.
- Drift is not reset by Initialize (`DroneVoice::reset` does not touch `drift`) [code].
- DRIFT CV is read every sample. DRIFT steps are smooth (the walks slew): L steps after DRIFT 0.25 → 1 and → 0 are 0.14 V against 0.16 V steady [meas].
- Tooltip "Drift", %.

### 2.9 CUTOFF

[code, `Fathom.cpp:80`, `FathomEngine::process`, `OutputStage::process`]

```
octaves   = clamp( CUTOFF_knob + CUTOFF_att × CUTOFF_CV_volts , 0 , 10 )
cutoffHz  = 20 × 2^octaves                                  (20 … 20,480 Hz)
smoothed  : one-pole on octaves, τ 20 ms, once per 16-sample block
fc used   = clamp( 2^smoothed , 1 Hz , 0.45 × sampleRate )  (the ladder's pole frequency)
```

| Knob (octaves) | 0 | 2 | 4 | 5.907 (default) | 6 | 8 | 10 |
|---|---|---|---|---|---|---|---|
| Hz | 20 | 80 | 320 | 1200 | 1280 | 5120 | 20,480 |

- **The top is 20,480 Hz, not 20 kHz** (section 1.1). The ladder clamps its pole at 0.45 × fs: 21,600 Hz at 48 kHz (not reached), 19,845 Hz at 44.1 kHz (the top 0.4 % of the dial, knob 9.957 … 10, gives the same sound; the tooltip still reads up to 20480 Hz).
- The CUTOFF CV is added in octaves at 1 V/octave per 100 % of the attenuverter, then the sum is clamped to 0 … 10: CV cannot take the cutoff outside 20 … 20,480 Hz.
- CUTOFF is the **pole frequency** of the four identical one-pole stages, not the −3 dB point (section 5.2). At RES 0, LP24 is −3 dB at 0.435 × CUTOFF.
- Smoothed over 20 ms. The step 1.2 kHz → 20.48 kHz gave L steps of 1.5–1.7 V in both the first 100 ms and the 100–500 ms after, which is the new brighter waveform's own edge size, not a click [meas, section 2.13].
- Tooltip "Cutoff", unit " Hz", 20 × 2^v.

### 2.10 RES

[code, `OutputStage::targetK`, `Ladder::setCoefficients`] `k = 4.1 × RES²`, smoothed (one-pole on k, τ 20 ms, per block). In LP24 and LP12 the input is boosted by `comp = 1 + k` (`BASS_COMP` 1), to hold the passband at unity as resonance rises (the loop's DC gain is 1/(1 + k)); in BP and HP `comp = 1`.

| RES | 0 | 0.2 (default) | 0.5 | 0.9 | 0.9877 | 1 |
|---|---|---|---|---|---|---|
| k | 0 | 0.164 | 1.025 | 3.321 | 4.000 | 4.100 |

- k = 4 is the structure's exact self-oscillation point (RES 0.9877); k = 4.1 at the top is just past it. Section 5.3 has the measured behaviour.
- Level effect on the default drone: RES 0, 0.5, 0.9 and 1 against the default RES 0.2: −0.03, +0.06, +0.16 and +0.36 dB [meas]. The low end is held; only the self-oscillation adds at the top.
- Tooltip "Resonance", %.

### 2.11 DRIVE

[code, `OutputStage`, `DroneVoice.hpp:160-168`]

```
driveGain = 2^(4 × DRIVE − 1)          0.5x at 0, 1x at 25 %, 8x at 100 %   (smoothed in log2, τ 20 ms)
makeup    = 1/driveGain                 (driveGain < 1)
makeup    = driveGain^−0.75             (driveGain ≥ 1)
ladder in = driveGain × (body [+ sub])
ladder out × makeup
```

| DRIVE | 0 | 0.125 | 0.25 (default) | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|---|
| driveGain | 0.5 | 0.707 | 1 | 2 | 4 | 8 |
| makeup | 2 | 1.414 | 1 | 0.595 | 0.354 | 0.210 |
| gain × makeup | 1 | 1 | 1 | 1.19 | 1.41 | 1.68 |

- A driven ladder puts out a near-square wave of about 1.9 peak whatever the gain, so the makeup takes most of the gain back out (code comment).
- Measured L RMS on the default drone for DRIVE 0 … 100 %: 2.181, 2.181, 2.179, 2.215, 2.231, 2.198 V at RES 0.2 (spread 0.15 dB), 2.202 … 2.221 V at RES 0.6 [meas]. The test bounds are ±1 dB at DRIVE 0 and ±2 dB at DRIVE 100 % on a 100 Hz sine at 0.5 [test: `testDriveMakeup`].
- Third-harmonic energy rises by more than 20 dB from DRIVE 0 to 100 % on a 100 Hz sine, and the ladder's output peak never exceeds 2.0 [test: `testDriveAddsHarmonics`].
- The self-oscillation level follows DRIVE the other way (section 5.3).
- Tooltip "Drive", %.

### 2.12 Outputs

- **L, R**: single-channel. `out = 5 V × DC-block( tanh(2.2 × (ladder × makeup + sub)) )`. The clip is `fastTanh` (within 1.5e-7 of `tanh`) and the DC blocker is a one-pole high-pass at 2 Hz (`y = x − x1 + c y1`, `c = exp(−2π × 2/fs)`). Nominally ±5 V (section 6.2 for what the peaks really are).
- **SUB**: ±5.000 V sine, section 2.5.
- With L and R both unpatched the body, filter and drift are paused (section 7.1).

### 2.13 Which controls click

Each change made to a running default drone; the largest sample-to-sample step of L in the first 100 ms and in the next 100–500 ms, against the largest step in the second before (0.164 V) [meas]. A smooth change shows no more in the first 100 ms than in the 100–500 ms after (which is the new setting's own steady figure).

| Change | before | 0–100 ms | 100–500 ms | Click? |
|---|---|---|---|---|
| DETUNE 0.25 → 1 / → 0 | 0.164 | 0.149 / 0.160 | 0.150 / 0.159 | no |
| SPREAD 0.6 → 0 / → 1 | 0.164 | 0.147 / 0.141 | 0.175 / 0.182 | no |
| **SUB 0.5 → 1 / → 0** | 0.164 | **1.357 / 2.545** | 0.195 / 0.168 | **yes, a one-sample jump** |
| DRIFT 0.25 → 1 / → 0 | 0.164 | 0.142 / 0.142 | 0.181 / 0.181 | no |
| PITCH 0 → +1 st / +12 st | 0.164 | 0.142 / 0.183 | 0.182 / 0.181 | no |
| V/OCT 0 → 1 V | 0.164 | 0.183 | 0.181 | no |
| CUTOFF 1.2 kHz → 20.48 kHz | 0.164 | 1.507 | 1.719 | no (new waveform's edges) |
| CUTOFF 1.2 kHz → 20 Hz | 0.164 | 0.032 | 0.014 | no |
| RES 0.2 → 1 / → 0 | 0.164 | 0.413 / 0.133 | 0.409 / 0.165 | no |
| DRIVE 0.25 → 1 / → 0 | 0.164 | 0.118 / 0.148 | 0.125 / 0.194 | no |
| Filter type LP24 → LP12 / BP / HP | 0.164 | 0.257 / 0.451 / 1.865 | 0.316 / 0.440 / 1.862 | no (the new type's own steps) |
| Sub octave one → two down | 0.164 | 0.139 | 0.178 | no |
| **Sub through filter off → on** | 0.164 | **2.545** | 0.187 | **yes, a one-sample jump** |
| CUTOFF CV 0 → +5 V (att 1) | 0.164 | 1.507 | 1.719 | no (as CUTOFF) |
| DETUNE CV 0 → +5 V (att 1) | 0.164 | 0.190 | 0.182 | no |

A mouse drag on SUB moves in small steps per UI frame, so the click matters mainly for a mapped or automated SUB; the size of a step is the step in SUB × the sub's instantaneous value × 5 V × the clip.

---

## 3. The right-click menu and saved state

[code, `Fathom.cpp`] Below a separator at the end of Rack's standard menu:

| Item | Choices (menu text) | Default | JSON key | Stored value | On load |
|---|---|---|---|---|---|
| Sub octave | "One octave down", "Two octaves down" | one down | `"subOctave"` | integer 0..1 | `clamp(int(json_integer_value(v)), 0, 1)` |
| Free pitch (no semitone snap) | checkbox | off | `"freePitch"` | JSON boolean | `json_boolean_value`; on false the PITCH knob is rounded to a semitone |
| Filter type | "Low-pass 24 dB", "Low-pass 12 dB", "Band-pass", "High-pass" | Low-pass 24 dB | `"filterType"` | integer 0..3 | `clamp(int(json_integer_value(v)), 0, 3)` |
| Sub through filter | checkbox | off | `"subThroughFilter"` | JSON boolean | `json_boolean_value` |

A saved module's `"data"` is therefore `{"subOctave": 0, "freePitch": false, "filterType": 0, "subThroughFilter": false}`. The 13 params are saved by Rack as ordinary params (ids 0–12). Menu order: separator, Sub octave, Free pitch, separator, Filter type, Sub through filter.

JSON edge cases [code; none is tested: there is no golden saved-state image and no JSON test for Fathom]:

- A missing key leaves that setting as it was (a new module: the default). Whether Rack resets the module before loading a preset onto an existing instance is [unverified]; Fathom ships no presets, but a user's `.vcvm` saved before a key existed behaves this way.
- **The integer is narrowed to `int` before the clamp**, as Fog's was (Fog issue #12, closed by a fix in Fog; Fathom still has the old pattern, and no Fathom issue is filed). A hand-edited `"filterType": 4294967298` loads as Band-pass (2), not the clamped High-pass (3); `"subOctave": 4294967296` loads as one octave down (0), not two; a JSON real such as `2.0` and a string both read as 0. Values always end in range, so nothing can be corrupted.
- A non-boolean `"freePitch"` or `"subThroughFilter"` (for example the integer 1) reads as false.
- Ordinary out-of-range integers clamp (`"filterType": 9` loads High-pass, `-1` loads Low-pass 24 dB).
- On load, `freePitch` false rounds the PITCH knob. Rack loads params before `dataFromJson` [unverified here].
- **Initialize** (`onReset`) sets Sub octave one down, Filter type LP24, Sub through filter off, Free pitch off (rounding the knob), and resets the engine (`voice.reset(C2)`, ladders, DC blockers cleared; the drift walks are not reset). Rack resets the knobs and attenuverters to the defaults of section 2.2.

**Bypass** [code]: `Fathom.cpp` does not call `configBypass`, so Rack's default bypass applies (no input is routed to an output) and the outputs are expected to go silent [unverified in a live Rack]. `process()` is not called while bypassed, so the drift, the sub's phase and the filter freeze and resume where they stopped.

**Presets**: none ship (section on the package, above).

---

## 4. Signal flow

### 4.1 Block diagram

```
 V/OCT (ch 1) ─┐
 PITCH (snapped)├─► f0 = 65.4064 × 2^((PITCH + 12·V)/12) ─► clamp 8 … 4000 Hz ─► one-pole τ 5 ms (per sample) ─► f0 slewed
                                                                                          │
   DETUNE (+CV, per sample) ─► ratio[7], volume[7], gain ─────────┐                      ├──────────────────────────► SUB: sine at f0/2 or f0/4
   DRIFT  (+CV, per sample) ─► 7 walks + sub walk (τ 1 s) ─► per-saw pitch ratio, per-saw level, sub level
   SPREAD ─► pan gains                                            ▼
                                                  7 polyBLEP saws at f0 × ratio × drift ─► × volume × drift level × gain ─► pan ─► L, R
                                                                                          │
                                       2nd-order Butterworth high-pass at f0, per side (SVF, coefficients per 16-sample block) × 0.8
                                                                                          │ body L, R
                                  [Sub through filter: + SUB × 0.6 × sub × drift gain, both sides]
                                                                                          ▼
   CUTOFF, RES, DRIVE (+CV, once per 16 samples) ─► one-pole τ 20 ms ─► fc, k, driveGain ─► ladder coefficients
                                                                                          ▼
                       × driveGain ─► LADDER per side: 2x up (halfband) ─► 2 × core step ─► type mix ─► ladderLimit ─► 2x down ─► × makeup
                                                                                          ▼
                                       [Sub around the filter: + SUB × 0.6 × sub × drift gain, both sides]
                                                                                          ▼
                                                  tanh(2.2 ×) ─► DC block (2 Hz) ─► × 5 V ─► L, R outputs
   sub (raw sine, ±1) ─────────────────────────────────────────────────────────────────────► × 5 V ─► SUB output
```

L and R are processed by two ladders with the same coefficients and independent states. With L and R unpatched the body, drift, filter and clip are skipped (section 7.1).

### 4.2 Block constants

[code]

| Block | Constant |
|---|---|
| f0 clamp | 8 Hz … 4000 Hz (`F0_MIN`, `F0_MAX`); C2 = 65.4064 Hz (`FathomEngine::C2_HZ`) |
| Pitch slew | τ 5 ms, per sample (`PITCH_SLEW_SEC`) |
| Block | 16 samples (`BLOCK`): body high-pass coefficients (when f0 moves), the stage's cutoff, RES and DRIVE reads and smoothing |
| Body gain, sub gain | 0.8 (`BODY_GAIN`), 0.6 (`SUB_GAIN`) |
| Sub | `fastSin2pi`, phase in double |
| Body high-pass | `Svf`, TPT, k = √2 (Butterworth, −3 dB at f0), cutoff clamped to [1 Hz, 0.45 fs] |
| Drift | ±6 cents, 0 … −3 dB, interval 0.5 s × (1 ± 0.25), slew 1 s; seeds 1 … 7 and 100 |
| Ladder | four TPT one-poles with Mystran's per-stage tanh linearisation, Zavalishin's zero-delay loop solved in one pass; `g = tan34(π fc/(2 fs))` (degree (3,4) rational, 2.8e-5 relative), fc clamped to [1 Hz, 0.45 fs]; slope floor `tMin = 0.01`; loop input clamped to ±4 then `u·tanhXdX(u)` (4.2e-6 relative); LP24 = y3, LP12 = y1, BP = 4 y1 − 8 y2 + 4 y3, HP = u − 4 y0 + 6 y1 − 4 y2 + y3 (indices from 0); then `ladderLimit(v) = v·tanhXdX(v/2)` (= 2 tanh(v/2), a soft limit to ±2) |
| Ladder feedback | `k = 4.1 × RES²` (`LADDER_K_MAX` 4.1); noise `1e-6 × white` in the loop (splitmix64, seed 7) so self-oscillation starts from silence |
| Oversampling | 2x polyphase IIR halfband pair (`Halfband.hpp`): three allpass sections per branch, A = {0.0628224, 0.4243809, 0.7818615}, B = {0.2238073, 0.6165344, 0.9274736}; flat within 1e-7 dB below the base Nyquist and 78 dB down above it (header comment; [test: `test_halfband`: round trip within 0.1 dB, Nyquist content below −60 dB]) |
| Output | `CLIP_DRIVE` 2.2 into `fastTanh`; DC blocker 2 Hz; × 5 V |
| Smoothing | cutoff (octaves), k and drive (log2 gain): τ 20 ms per block (`CUTOFF_SLEW_SEC`, `RES_SLEW_SEC`) |

The 2x oversampling is fixed: always on, regardless of DRIVE or host rate. Memory: the engine holds no heap buffers (no vectors, no allocation in the sources of `src/fathom/*.hpp`) [code].

---

## 5. The filter

### 5.1 Types

| Type (menu) | Output mix | Notes |
|---|---|---|
| Low-pass 24 dB (default) | y3 (fourth pole) | input boosted by 1 + k |
| Low-pass 12 dB | y1 (second pole) | input boosted by 1 + k |
| Band-pass | 4 y1 − 8 y2 + 4 y3 (the Xpander's four-pole BP) | no boost; peak at CUTOFF |
| High-pass | u − 4 y0 + 6 y1 − 4 y2 + y3 | no boost; u is the post-feedback node |

Changing type mid-sound switches the mix at once with the states kept; after a switch the gain equals a fresh filter's within 1 dB [test: `testTypeSwitchMidSound`].

### 5.2 Measured response at RES 0

Small-signal (amplitude 0.01) gain of `Ladder` alone, fs 48 kHz, DRIVE unity [meas]:

| Type | CUTOFF | −3 dB at | Ratio to CUTOFF |
|---|---|---|---|
| LP24 | 100 Hz | 43.8 Hz | 0.438 |
| LP24 | 1200 Hz (default) | 521.5 Hz | 0.435 |
| LP24 | 5000 Hz | 2191 Hz | 0.438 |
| LP24 | 20,480 Hz | 10,189 Hz | 0.498 |
| LP12 | 100 / 1200 / 5000 / 20,480 Hz | 64.6 / 772.6 / 3235 / 14,478 Hz | 0.646 / 0.644 / 0.647 / 0.707 |
| HP | 100 / 1200 / 5000 Hz | 230 / 2728 / 9736 Hz | 2.30 / 2.27 / 1.95 |
| BP | 1200 Hz | peak at 1200 Hz, 0.00 dB | |

- At 44.1 kHz the LP24 at 1200 Hz is the same (−3 dB at 521.6 Hz); at 20,480 Hz it is −3 dB at 10,029 Hz (the clamp at 19,845 Hz).
- **LP24 with CUTOFF at the top of the dial** (20,480 Hz), gain at 1, 5, 10, 15, 18, 20 kHz: −0.03, −0.74, −2.93, −6.51, −9.33, −11.49 dB at 48 kHz; −0.03, −0.76, −3.02, −6.81, −9.86, −12.24 dB at 44.1 kHz [meas]. A fully open Fathom is not flat to 20 kHz.
- Slope and passband [test: `testLp24Shape`, `testLp12Shape`, `testHpAndBp`]: LP24 a decade below fc within 0.5 dB, two octaves above below −40 dB; LP12 two octaves above between −20 and −30 dB; HP at 100 passes 5 kHz within 1 dB and cuts 20 Hz by more than 40 dB; BP at 1 kHz is more than 20 dB above 100 Hz and 10 kHz.
- Low end held as resonance rises [test: `testSteadyLowEnd`]: a 40 Hz sine at RES 90 % within 1.5 dB of RES 0 in LP24 and LP12 (cutoff 1 kHz); HP's 5 kHz within 1.5 dB.
- Noise floor at RES 0 under 1e-4 [test: `testNoiseFloorAtZeroRes`].

### 5.3 Self-oscillation

`OutputStage` alone, no input, RES 1 (k 4.1), the peak in volts at the jack (5 × L) over the second half of 4 s [meas]:

| CUTOFF | fs | Oscillates at | Cents from CUTOFF | Peak |
|---|---|---|---|---|
| 55 Hz | 48 kHz | 55.15 Hz | +4.6 | 0.864 V |
| 220 Hz | 48 kHz | 218.72 Hz | −10.1 | 0.865 V |
| 880 Hz | 48 kHz | 875.14 Hz | −9.6 | 0.854 V |
| 3520 Hz | 48 kHz | 3504.8 Hz | −7.5 | 0.815 V |
| 7040 Hz | 48 kHz | 7019.3 Hz | −5.1 | 0.778 V |
| 14,080 Hz | 48 kHz | 14,061.8 Hz | −2.2 | 0.757 V |
| 20,480 Hz | 48 kHz | 20,460.1 Hz | −1.7 | 0.779 V |
| 220 / 3520 Hz | 44.1 kHz | 218.72 / 3505.3 Hz | −10.1 / −7.3 | 0.865 / 0.811 V |
| 20,480 Hz | 44.1 kHz | 19,825.9 Hz | **−56.2** | 0.783 V (the 0.45 fs clamp) |
| 220 / 3520 / 20,480 Hz | 96 kHz | 218.71 / 3502.0 / 20,439 Hz | −10.2 / −8.9 / −3.5 | 0.867 / 0.840 / 0.761 V |
| 220 / 3520 / 20,480 Hz | 192 kHz | 218.70 / 3500.6 / 20,405 Hz | −10.2 / −9.6 / −6.3 | 0.867 / 0.854 / 0.796 V |

- The tone sits a few cents flat (up to 10 cents at 220 Hz) and tracks CUTOFF within about 15 cents from 55 Hz to 20 kHz at 48 kHz; the octave step is within 1 % [test: `testSelfOscillation`: peak 0.05 … 0.5 asked, 96 kHz within 0.5 % of 48 kHz, octave within 1 %; the test's peak window is 0.05 … 0.5 in the ladder's own units, before the stage's ×2.2 clip and ×5 V; here the ladder peaks near 0.08].
- **Level against DRIVE** (CUTOFF 220 Hz, RES 1) [meas]: 1.679, 1.211, 0.865, 0.518, 0.308, 0.184 V peak at DRIVE 0, 0.125, 0.25, 0.5, 0.75, 1. Loudest with DRIVE down; the makeup scales the sine.
- **Against the default drone**: the sine (0.865 V peak, 0.61 V RMS) is 11.1 dB under the default drone's RMS (2.18 V).
- **Against RES** (CUTOFF 220 Hz, DRIVE 25 %, peak in the last 5 of 10 s): 0 V at RES 0.9, 0.97, 0.98, 0.985 and 0.9877; 0.381 V at 0.99; 0.674 V at 0.995; 0.865 V at 1. Onset is at k = 4, so only the last 1.2 % of the knob's travel oscillates. With a loud input the loop may swamp the tone; with a quiet one it is audible as a sine.
- The noise term 1e-6 lets the oscillation start from silence; I did not time the build-up.

### 5.4 Resonant peak against signal level (the growl)

The ladder's per-stage saturation pulls the peak below CUTOFF as the level rises.

Sine sweep through `Ladder` alone, LP24, CUTOFF 1000 Hz, 1/48-octave steps from 250 Hz [meas]. The input amplitude is after DRIVE's gain; the default body's own amplitude before the ladder is 0.575 peak, 0.105 RMS (nominal full scale 1). Peak position in Hz (octaves against CUTOFF):

| RES | DRIVE | 0.002 | 0.05 | 0.2 | 0.5 | 1.0 |
|---|---|---|---|---|---|---|
| 0.90 | 0.25 | 958 (−0.06) | 866 (−0.21) | 677 (−0.56) | 465 (−1.10) | 269 (−1.90) |
| 0.97 | 0.25 | 986 (−0.02) | 853 (−0.23) | 677 (−0.56) | 472 (−1.08) | 273 (−1.87) |
| 0.90 | 0.70 | 944 (−0.08) | 697 (−0.52) | 375 (−1.42) | 250 or lower | 250 or lower |

(At DRIVE 0.70 the last two cells hit the 250 Hz floor of the sweep; the true peak is lower.) The tests pin only the small-signal case: with RES 95 % and amplitude 0.001 the peak is within 5 % of 1 kHz [test: `testResonanceTuningSmallSignal`, 900 … 1100 Hz grid].

In the real drone (engine at PITCH 0 and −12, DRIFT 0, SUB 0, SPREAD 0, CUTOFF 1 kHz, LP24), the 1/6-octave band with the most gain against RES 0 / DRIVE 25 % [meas]:

| RES | DRIVE | Peak band, PITCH 0 (f0 65 Hz) | Gain | Peak band, PITCH −12 (f0 33 Hz) | Gain |
|---|---|---|---|---|---|
| 0.6 | 0.25 | 951 Hz | +13.7 dB | 755 Hz | +12.7 dB |
| 0.6 | 0.70 | 1068 Hz | +18.7 dB | 1068 Hz | +16.3 dB |
| 0.9 | 0.25 | 951 Hz | +27.1 dB | 951 Hz | +27.2 dB |
| 0.9 | 0.70 | 848 Hz | +24.6 dB | 951 Hz | +26.2 dB |
| 1.0 | 0.25 | 1068 Hz | +32.4 dB | 951 Hz | +35.8 dB |
| 1.0 | 1.0 | 1068 Hz | +23.2 dB | 1068 Hz | +26.8 dB |

The strongest bump sits within −0.41 … +0.09 octave of CUTOFF for the C2 and C1 drones. At PITCH +12 and +24 the harmonics are 131 and 262 Hz apart and the band method locks onto a harmonic (0.43 octave sharp to 1.09 octave high); do not read those as droop. In short: the droop is large for a single loud partial near the peak and small for the dense low drone; DRIVE moves it a lot.

### 5.5 Levels by type and routing

Default drone, L RMS, 48 kHz [meas]: LP24 2.179 V (peak 3.91), LP12 2.191 V (4.01), BP 2.056 V (−0.40 dB, 3.43), HP 2.041 V (−0.46 dB, 3.39). With CUTOFF at 20 Hz against 20.48 kHz: Sub around the filter (default) at SUB 50 %: −0.7 dB (2.036 against 2.200 V); at SUB 0: −44.3 dB (0.006 V); Sub through filter at SUB 50 %: −21.4 dB (0.186 V); at SUB 0: −44.3 dB [meas].

---

## 6. Level management

### 6.1 The default level

L, 10 s after 2 s of settling, DRIFT 25 %, 48 kHz [meas]: peak 3.91 V (R 3.88 V), RMS 2.203 V, DC mean 0.0001 V, SUB OUT peak 5.000 V. Spec: "defaults peak near 4 V" [test: `testModuleLevelAtDefaults` asks 3.5 … 5.0 V].

| Case (one change from the defaults) | pk L | pk R | rms L | corr L/R |
|---|---|---|---|---|
| defaults | 3.91 | 3.88 | 2.203 | 0.993 |
| SUB 0 | 3.75 | 3.75 | 0.988 | 0.949 |
| SUB 1 | 4.91 | 4.91 | 3.375 | 0.999 |
| SPREAD 0 | 3.94 | 3.94 | 2.204 | 1.000 |
| SPREAD 1 | 3.89 | 3.81 | 2.204 | 0.982 |
| DETUNE 0 | 3.93 | 3.96 | 2.291 | 1.000 |
| DETUNE 1 | 4.50 | 4.43 | 2.191 | 0.972 |
| DRIFT 0 | 4.19 | 4.15 | 2.260 | 0.993 |
| DRIFT 1 | 3.74 | 3.79 | 2.014 | 0.993 |
| SUB 1, DETUNE 1, DRIVE 1 | 5.01 | 5.00 | 3.324 | 0.990 |
| RES 1 (self-oscillation) | 4.54 | 4.52 | 2.291 | 0.977 |
| CUTOFF 20.48 kHz | 4.18 | 4.20 | 2.223 | 0.992 |
| CUTOFF 20 Hz (sub still there) | 2.85 | 2.85 | 2.046 | 1.000 |

### 6.2 What the peaks really are

- The clip is `tanh(2.2 × x)`, so the tanh stage never exceeds ±1 (±5 V); the DC blocker then follows it, which can push a peak past 5 V when a clipped waveform is asymmetric or a control steps.
- Held settings: the highest I found is 5.28 V (LP12, RES 0.5, DRIVE 1, SUB 1, DETUNE 1, PITCH −12, CUTOFF 10.2 kHz); LP24 5.27 V, HP 5.10 V, BP 4.97 V [meas].
- 300 random trials of 4 s, all knobs random, attenuverters random, every CV redrawn from ±10 V every 0.25 s, all menus, 48 and 44.1 kHz: **max |L, R| 7.23 V**, max |SUB| 5.000 V, no non-finite sample in 55.3 million [meas]. The worst case (LP12, Sub through filter, a +9.5 V CV on DETUNE and −7.8 V on RES) held steady is 4.36 V: the 7.23 V is a transient after a CV step. So "±5 V" is nominal: expect up to about 5.3 V held and about 7 V after a hard CV step.
- The ladder's own output is bounded by `ladderLimit` near ±1.9 (2 tanh(v/2)) before the makeup [code]; the stability test with ±8 square input at k 4.1 and cutoff steps 20 Hz ↔ 20 kHz every 100 ms for 5 s stays below 2.5 and finite [test: `testStabilityUnderSteps`]; a cutoff of 30 kHz at 44.1 kHz stays finite [test: `testCutoffClamp`].
- Mono compatibility: L/R correlation is 0.95 … 1.00 in every case above; the mono sum stays within 1.5 dB of SPREAD 0's [test]. I did not measure the mono-sum loss across the whole parameter space.

### 6.3 DC

The output stage has a DC blocker at 2 Hz after the clip; a constant 1.0 input reads under 0.01 at the output after 2 s [test: `testStageBlocksDc`]. The default drone's L mean is 0.0001–0.0004 V [meas]. The sub (a sine) and the saws are DC-free before the blocker; the blocker removes the offset the clip makes of an asymmetric waveform. SUB OUT is not DC-blocked (a pure sine).

### 6.4 Smoothness of the filter controls

CUTOFF, RES and DRIVE steps (0.02 s one-poles) produce no sample step above 1.5 × the steady maximum on a 100 Hz sine [test: `testCutoffIsSmoothed`, `testResAndDriveAreSmoothed`]. The section 2.13 table gives the full-drone figures.

---

## 7. Behaviour of the voice

### 7.1 Pause when L and R are unpatched

[code, `FathomEngine::process`; test `testUnpatchedBody`; issue #8 (closed) explains why]

- With neither OUT L nor OUT R patched, only `processSubOnly` runs: the pitch slew and the sub phase advance; the saws, drift and high-passes pause; L and R are exactly 0 and SUB OUT is 5 V × the sub. [meas: L, R exactly 0 for 1 s; the largest SUB step 0.021 V, no click]. The sub's own pitch and Sub octave setting keep working.
- When either is patched again: the output stage (ladders, DC blockers) is cleared, the body picks up where it paused, with Drift's walks and the saws' phases where they stopped. After re-patching L the largest step in the first 100 ms was 1.90 V against 1.48 V in a steady engine, peak 4.23 V [meas]. The test requires the first 10 ms within 3 dB of the reference, and the re-patched level within 1.5 dB [test: `testUnpatchedBody`: −0.56 dB at once, −1.05 dB later at 48 kHz; −0.25 and −0.15 dB at 96 kHz].
- Patching only R keeps the whole body running (both L and R are computed).
- Because the filter is idle while unpatched, an idle Fathom costs 0.02 % of a core (section 8.2).

### 7.2 Start-up and Initialize

The engine always starts at f0 = C2 and glides to the real pitch in about 35 ms (section 2.4). It also starts with all saw phases at 0, so every new Fathom begins coherent, and with Drift's fixed seeds each instance drifts the same way (section 2.8).

### 7.3 Sample-rate changes

Rack's rate is read every sample (`args.sampleRate`). On a change, `DroneVoice` recomputes its slew and the high-pass coefficients, and **`OutputStage` clears both ladders and the halfband states** and takes the new control values without glide [code]. Measured: the largest sample step in L in the 50 ms after a change was 0.34 V (48 → 96 kHz), 0.34 V (44.1 → 48) and 0.36 V (96 → 44.1) against 0.18, 0.19 and 0.09 V in the second before; the peak in the 50 ms after was 3.58 V [meas]. The drift walks, the sub's phase and the saws' phases continue. The test bound is 1.5 × the steady maximum on a 100 Hz sine at the stage alone [test: `testSampleRateChangeNoBurst`]; on the full drone the step is about 1.9–4 × the steady one: a possible tick at a rate change. Pitch is rate-independent: SUB OUT's pitch error is within 0.01 cent from 44.1 to 768 kHz [meas, section 8.3; test: `testSampleRateIndependence`, within 0.1 cent between 48 and 96 kHz].

### 7.4 Polyphony: mono only [issue #64]

- Only channel 1 (first channel) of V/OCT and of the DETUNE, DRIFT, CUTOFF, RES and DRIVE CVs is read (`inputs[i].getVoltage()`). Channels 2–16 do nothing. A chord on V/OCT plays one note: the first channel's.
- Outputs are single-channel; there is no CHANNELS control and no polyphonic mode. `plugin.json` has no "Polyphonic" tag.
- Issue #64 ("Polyphony: modules should be polyphonic where it makes sense", open) lists Fathom as the most obvious gain and the most costly: one voice per V/OCT channel, with the ladder's 2x oversampling making CPU scale with voices, and a cap to decide. Narrows (branch `narrows`) is polyphonic; the issue notes Fathom's ladder is now a template (`LadderCore.hpp`) a polyphonic Fathom could run four lanes at a time. Until that decision, mono is the intended behaviour. Do not file it again; reference #64.
- CPU consequence for QA: one Fathom is one voice's cost; a poly cable does not add cost.

---

## 8. CPU, sample rates and robustness

### 8.1 What costs CPU

[code] Per host sample, awake (an output patched): the pitch slew, seven polyBLEP saws and their drift (seven walks + sub walk, each with `fastExp2` and `fastDbToGain`), the sub sine, two high-pass SVFs, two 2x-oversampled ladders (two core steps each, six halfband sections each way), the clip and DC blocker. There is no inner-rate divider: the engine runs at the host rate, so **cost is proportional to the host sample rate** (section 8.2). Nothing is allocated in `process()` (no heap members in the headers); libm is called only when a control moves.

### 8.2 CPU figures

`bench` cases (`src/fathom/bench.inc`), each at 48 kHz:

- **idle**: nothing patched, knobs at the defaults (the sub only).
- **outputs**: every output patched, no inputs, defaults.
- **busy**: DETUNE, DRIFT, SPREAD at 100 %, all attenuverters at +0.5, a moving CV on every input (a one-semitone slow vibrato on V/OCT, ±5 V on the rest), every output patched.

Built with `make -f tests.mk build/bench/bench` and run as `bench fathom <case> <count> 4` (4 s per case), % of one core per instance, Apple M-series Mac [meas]. Two sets: the first three runs with the machine loaded (load average about 10, other work running), the last two lighter (about 4.4).

| Case | Budget, Apple arm64 | Budget, other (provisional) | TESTING.md ("was") | Loaded, 1 / 4 / 16 instances (range of 3 runs) | Lighter, 1 / 16 |
|---|---|---|---|---|---|
| idle | 0.1 | 0.3 | 0.02 (0.58) | 0.020–0.029 / 0.020–0.022 / 0.024–0.025 | 0.018–0.019 / 0.019–0.020 |
| outputs | 1.2 | 3.6 | 0.55 (0.58) | 0.721–0.734 / 0.696–0.765 / 0.714–0.719 | 0.573–0.615 / 0.593–0.601 |
| busy | 1.2 | 3.6 | 0.59 (0.59) | 0.713–0.760 / 0.732–0.748 / 0.739–0.754 | 0.619–0.622 / 0.619–0.621 |

- All within budget. With the machine lighter the figures agree with TESTING.md's (0.55, 0.59). The bench fails a case over its budget at 1 or 16 instances, or if the 16-instance cost exceeds 1.5 × the single plus 0.01 %: the cost per instance is flat at 1, 4 and 16 instances.
- The spec target is under 1 % of a core at 48 kHz; measured 0.6–0.76 % busy.
- The x86-64 and Windows budgets are three times the Mac's, provisional, **not measured on native hardware**. Issue #8 (closed) saw about 6 % per idle Fathom on Windows before the pause was added; there is no automated Windows budget. Record Rack's CPU meter for idle, outputs and busy on Windows.
- `make libm-calls` (Linux only) was not run [issue #36]; Fathom's libm cases are idle, outputs (both must make no libm call per sample) and outputs with knobs moving (reported) [code].
- `make engine-globals`: built and passed on the Mac (no writable global state) [test, run via tests.mk]; `bench --check` passed (15 module declarations complete).

### 8.3 Sample rates (44.1 kHz to 768 kHz)

[meas] SUB OUT pitch at C2 (32.7032 Hz expected), the default drone's L RMS and peak, and the engine cost per sample timed in-process (outputs patched, defaults, 2 s; the machine was moderately loaded):

| Host rate | Sub, Hz | Cents | L RMS | L peak | ns per sample | % of a core |
|---|---|---|---|---|---|---|
| 44.1 kHz | 32.7034 | +0.01 | 2.173 V | 3.91 V | 124 | 0.55 |
| 48 kHz | 32.7031 | −0.01 | 2.173 | 3.91 | 125 | 0.60 |
| 88.2 kHz | 32.7033 | 0.00 | 2.173 | 3.91 | 127 | 1.12 |
| 96 kHz | 32.7032 | 0.00 | 2.173 | 3.91 | 125 | 1.20 |
| 176.4 kHz | 32.7032 | 0.00 | 2.173 | 3.91 | 119 | 2.11 |
| 192 kHz | 32.7032 | 0.00 | 2.173 | 3.91 | 123 | 2.36 |
| 384 kHz | 32.7032 | 0.00 | 2.173 | 3.91 | 122 | 4.69 |
| 768 kHz | 32.7032 | 0.00 | 2.173 | 3.91 | 122 | 9.38 |

- Pitch and level are the same at every rate (the filter's only rate-dependent behaviour is the 0.45 fs clamp on the pole, section 2.9, and the smoothing constants, which are in seconds, not samples).
- CPU doubles with the rate: at 192 kHz about 2.4 % and at 768 kHz about 9.4 % of a core per instance. This differs from Fog, which caps its inner rate.
- The engine tests run the full engine at 44.1 and 96 kHz and switching between them every 0.5 s against the frozen reference: peak difference −106 to −107 dBFS [test: `test_engine`, "44.1/96 kHz switching", "busy, 96 kHz, LP12"].
- Aliasing at 44.1 and 48 kHz: section 8.5.

### 8.4 Hostile input and numerical safety

[meas, `m3_robust`; the engine only. Fathom has no hostile-input test in the repository (TESTING.md: "Sanitizers: No; hostile-input and sample-rate sweeps: Partly; random fuzz: No; allocation check: No")]

- 76 trials: NaN, +infinity, −infinity and 1e30 on each of the 13 params and 6 inputs, for 1 s, then 3 s clean. **No non-finite output sample and every case back within 3 dB of the baseline afterwards.** Worst finite |L, R| in these trials 4.75 V. All params and inputs NaN together for 3 s: finite. The same program, built with ASan and UBSan, ran clean.
- What a NaN does (the clamps send it to the top of the range, or to 0 for SPREAD and SUB) [meas, steady 2 s]:

| NaN on | Effect | L RMS / peak |
|---|---|---|
| PITCH param, or the V/OCT jack | body f0 held at 8 Hz (sub at 4 Hz) | 2.08 V / 4.86 V |
| CUTOFF param or CV jack | CUTOFF at the top (20.48 kHz) | 2.20 V / 4.14 V |
| RES param or CV | RES 100 % (self-oscillation) | 2.27 V / 4.48 V |
| DRIVE param or CV | DRIVE 100 % | 2.20 V / 4.02 V |
| DETUNE CV | DETUNE 100 % | 2.19 V / 4.32 V |
| DRIFT CV | DRIFT 100 % | 1.98 V / 3.38 V |
| SPREAD param | SPREAD 0 | 2.18 V / 3.94 V |
| SUB param | SUB 0 | 0.93 V / 3.35 V |

  A NaN on a CV jack drives its parameter to the maximum **even with its attenuverter at 0** (0 × NaN is NaN, which the clamp sends to 1). A ±infinity input does the same through the same arithmetic. These are out-of-spec cables; Rack normally never delivers NaN.
- V/OCT and the CVs have no range limit; ±50 V simply clamps.
- Seeded fuzz [meas]: 300 trials × 4 s (55.3 million samples), random knobs, attenuverters, CVs of ±10 V every 0.25 s, all menus, 48 and 44.1 kHz: no non-finite sample, max |L, R| 7.23 V, max |SUB| 5.000 V.
- Denormals: Fathom has no explicit flush (no `1e-20` guards, unlike Fog) [code]. It has no audio input and its saws and sub always drive the filters, so filter states are not left to decay toward zero; I did not measure for denormal stalls [unverified]. The bench's idle and busy times show none.

### 8.5 Aliasing (issue #67 covers measuring it; no test pins it)

TESTING.md rule 16 asks every sound-making module for an aliasing test with a bound; Fathom has none (issue #67, open, lists Fathom: "polyBLEP, ladder at 2x; untested"). The only related test is that the supersaw is smoother than a naive saw at 1 kHz [test: `testAliasingBelowNaive`]. I measured with `rkatest::AliasMeter` (the strongest component that is not a harmonic of f0), at 44.1 and 48 kHz, DETUNE 0, DRIFT 0, SPREAD 0, SUB 0, 65536 samples after 1 s [meas, `m3c_alias`]. f0 values avoid fs/f0 near an integer (else an alias lands on a harmonic and the meter cannot see it):

| f0 | fs | CUTOFF (RES, DRIVE) | Strongest harmonic | Strongest non-harmonic | dB re harmonic |
|---|---|---|---|---|---|
| 110 Hz | 48 k | 1200 (0.2, 25 %) | 1.360 V | 0.05 mV | −88.5 |
| 110 Hz | 48 k | 20,480 (0, 25 %) | 1.334 V | 0.43 mV at 23.8 kHz | −69.8 |
| 440 Hz | 48 k | 1200 | 1.257 V | 0.23 mV at 40 Hz | −74.8 |
| 440 Hz | 48 k | 20,480 | 1.348 V | 3.1 mV at 23.8 kHz | −52.8 |
| 440 Hz | 48 k | 20,480 (0.6, 100 %) | 1.583 V | 2.3 mV at 22.9 kHz | −56.7 |
| 1013 Hz | 48 k | 1200 | 0.609 V | 0.44 mV at 390 Hz | −62.9 |
| 1013 Hz | 48 k | 20,480 | 1.367 V | 7.7 mV at 23.7 kHz | −45.0 |
| 2027 Hz | 48 k | 1200 | 0.116 V | 0.41 mV at 648 Hz | −49.1 |
| 2027 Hz | 48 k | 20,480 | 1.382 V | 17.2 mV at 23.7 kHz | −38.1 |
| 2027 Hz | 48 k | 20,480 (0.6, 100 %) | 1.560 V | 20.6 mV at 7.46 kHz | −37.6 |
| 3511 Hz | 48 k | 1200 | 0.019 V | 0.20 mV at 1.15 kHz | −39.4 |
| 3511 Hz | 48 k | 20,480 | 1.368 V | 23.0 mV at 19.9 kHz | −35.5 |
| 110 Hz | 44.1 k | 20,480 | 1.334 V | 0.62 mV at 22.0 kHz | −66.7 |
| 440 Hz | 44.1 k | 20,480 | 1.349 V | 4.0 mV at 21.7 kHz | −50.7 |
| 1013 Hz | 44.1 k | 20,480 | 1.368 V | 10.3 mV at 21.8 kHz | −42.5 |
| 2027 Hz | 44.1 k | 20,480 | 1.383 V | 22.4 mV at 21.8 kHz | −35.8 |
| 2027 Hz | 44.1 k | 20,480 (0.6, 100 %) | 1.562 V | 34.4 mV at 21.8 kHz | −33.1 |
| 3511 Hz | 44.1 k | 20,480 | 1.368 V | 32.7 mV at 19.5 kHz | −32.4 |

- At the default 1.2 kHz cutoff every stray component is at or under about 0.5 mV (−80 dBFS against 5 V) at 110 Hz to 3.5 kHz; the aliases that exist land in the top octave (above 16 kHz) when the filter is open. At bass pitches (110 Hz) the strongest alias is 67–70 dB under the strongest harmonic with the filter fully open. Against the DSP-ALIASING.md rule "spend CPU by audibility", content in the top octave matters less; whether the open-filter figures at 2–3.5 kHz (−32 to −38 dB) are acceptable is for issue #67 to settle. DRIVE 100 % with RES 0.6 at 440 Hz gave −56.7 dB (48 kHz) and −52.9 dB (44.1 kHz); at f0 = 2027 Hz with the same settings the strongest stray moves to 7.46 kHz (48 kHz), −37.6 dB, which is not a top-octave component. At the default cutoff the 44.1 kHz rows (not tabled) read 0.05–0.48 mV.

---

## 9. Open issues

### 9.1 Filed issues: do not re-file

Issues about Fathom in `ryanarneson/RkaModules` at the time of writing (`gh issue list --state all --limit 200`, 2026-11-24).

| # | State | Title | QA impact |
|---|---|---|---|
| 64 | open | Polyphony: modules should be polyphonic where it makes sense | Fathom is mono: only channel 1 of V/OCT and the CVs, one voice. Do not file "poly V/OCT does not play a chord" |
| 74 | open | Fathom: test_supersaw's detune clamp check fails on Linux aarch64 (g++ 13) | A unit-test fragility (`setDetune(2)` against `setDetune(1)` compared exactly); passes here with the Mac's clang (section 10). Nothing audible: the clamp is applied before any arithmetic |
| 67 | open | Aliasing: measure every module that makes or bends sound, and fix what fails | Fathom's row: "polyBLEP, ladder at 2x; untested". Section 8.5 has measurements; add data there |
| 36 | open | Process: libm-calls check is skipped on macOS | `make libm-calls` is Linux-only; not run for this document |
| 77 | closed | Fathom: check the sub's pitch (not seen an octave below L on a tuner) | explains the tuner reading on L (section 2.5): works as designed; the manual might say so |
| 8 | closed | Fathom and Sway: measure and reduce CPU use | explains the pause when L and R are unpatched and the Rack-free engine (section 7.1) |

### 9.2 Found while preparing this document (not filed as issues)

For the author to file or dismiss. Each is [meas] or [code] as marked.

1. **Manual and guide: "±5 V at full sweeps the knob's whole range" holds only from noon** (section 1.1). The same sentence in Fog was issue #31 (closed).
2. **The manual says CUTOFF reaches 20 kHz; it reaches 20.48 kHz** (confirmed), and the manual's description of CUTOFF as the pole frequency leaves the −3 dB point (0.435 × CUTOFF for LP24, 2.3 × for HP) unstated.
3. **SUB and "Sub through filter" are not smoothed**: a step of SUB or a toggle gives a one-sample jump of up to 2.5 V (section 2.13). Only matters for mapped or automated SUB, and for the toggle.
4. **JSON load narrows integers before clamping** (`Fathom.cpp:dataFromJson`), the pattern Fog fixed in its issue #12 (Fog now uses `SavedState.hpp`). No Fathom test or issue.
5. **L/R can exceed ±5 V**: up to 5.3 V with held settings and 7.2 V after a hard CV step, because the DC blocker follows the clipper (section 6.2). The manual says "±5 V".
6. **DRIFT 1 → 0 takes 6 s to settle**; the spec says DRIFT 0 "freezes everything at zero offset" (section 2.8).
7. **Every instance drifts identically** (fixed seeds) and starts coherent; two Fathoms at the same pitch are phase-locked and drift in lockstep (section 2.8).
8. **A NaN or infinity on a CV jack drives the parameter to its maximum even with the attenuverter at 0** (section 8.4).
9. **Self-oscillation tracks CUTOFF only to about 15 cents (10 cents flat at 220 Hz)**, and at 44.1 kHz the top of the dial oscillates 56 cents flat because of the 0.45 fs clamp (section 5.3).
10. **The ladder is cleared on every sample-rate change** (section 7.3): a possible tick.
11. **Start-up glide**: a module added or a patch loaded at a pitch other than C2 glides in from C2 over about 35 ms (section 2.4).
12. **`plugin.json` description says "low-pass"**; the module has four filter types.
13. **The manual's droop claim** ("up to about an octave at the module's own levels, and it moves a little with DRIFT") does not match the measurements (section 5.4); the guide says DRIVE.
14. **Fathom has no hostile-input test, no sanitizer build, no seeded fuzz, no allocation test, no golden saved-state image, no aliasing test** (TESTING.md's table). I ran the ten existing tests under ASan and UBSan, and my own hostile/fuzz program under them; all clean.

---

## 10. What the automated suite already covers

All ten programs passed in my run (section 0). `test_fast_math`, `test_ladder_fast` and `test_engine` build with the plugin's floating-point flags, the rest with `-O2`. I also ran all ten with ASan and UBSan; all passed. Printed figures below are from my run.

| Program | Test | Guarantees | Tolerance (measured) |
|---|---|---|---|
| `test_supersaw` | detune table | ratios, volumes, gain at DETUNE 0 and 1 | ratios 1e-6, volumes 1e-5 |
| | detune clamps | `setDetune(−1)` = 0, `setDetune(2)` = 1, exactly | exact [issue #74 on Linux aarch64] |
| | zero detune is one saw | seven coherent saws equal one saw; SPREAD 0 is mono | 1e-5; 1e-6 |
| | spread mono sum | mono sum within 1.5 dB with SPREAD; side energy > −12 dB at SPREAD 1 | 1.5 dB |
| | aliasing below naive | the supersaw is smoother than a naive saw at 1 kHz | `rb < rn` |
| `test_drift` | zero amount | amount 0 stays 0 | exact |
| | bounds | cents within ±6, dB within −3 … 0, and moves | exact bounds; > 1 cent |
| | slewed | no per-sample step | cents < 1e-3, dB < 5e-4 |
| | walks differ | seeds 1 and 2 do not track | max diff > 0.5 |
| | back to zero; clamps; ratios | amount → 0 returns within 0.05 cent / 0.02 dB; amount 3 equals 1; pitch ratios and gains in range | |
| `test_svf` | shape | LP octave below fc within 0.5 dB, at fc −3 ± 0.5, two octaves above < −20 dB; HP mirrors; BP peak; sweep bounded; Nyquist clamp; 48 vs 96 kHz within 0.1 dB | as stated |
| `test_halfband` | round trip, Nyquist | 1 kHz round trip within 0.1 dB; alternating input < −60 dB | |
| `test_ladder` | LP24 and LP12 shape | section 5.2 | |
| | steady low end; HP and BP | section 5.2 | 1.5 dB; 1 dB, 40 dB, 20 dB |
| | self-oscillation | peak 0.05 … 0.5 in ladder units, 220 Hz within 5 %, 96 kHz within 0.5 %, octave within 1 % | |
| | noise floor at RES 0 | peak < 1e-4 | |
| | resonance tuning, small signal | peak within 900 … 1100 Hz grid at RES 95 %, amplitude 0.001 | |
| | drive adds harmonics | 8× has > 20 dB more third harmonic than 0.5×; peak ≤ 2.0 | |
| | stability under steps | cutoff 20 Hz ↔ 20 kHz and RES 0 ↔ 100 %, ±8 square, 5 s | finite, < 2.5 |
| | cutoff clamp | 30 kHz at 44.1 kHz finite | |
| | type switch mid-sound | after a switch within 1 dB of a fresh filter | |
| `test_ladder_shared` | to the bit | `Ladder` equals the frozen pre-template copy under strict floating point; four-wide equals four one-wide | 0 of 4·3·3·3·2·48000 samples differ; worst 0 (printed) |
| `test_ladder_fast` | to rounding | the same under the plugin's flags | 0 of 10,368,000 samples differ (printed); bound 1e-5 |
| `test_fast_math` | FastMath | exp2, exp2D, dB, sin, tanh against libm; NaN, infinity | fastExp2 7.83e-8, fastExp2D 2.19e-16, fastSin2pi 1.65e-7, fastTanh 1.34e-7 (printed) |
| `test_voice` | module level at defaults | peak 3.5 … 5.0 V | |
| | stage closed cutoff, DC, drive makeup, sub routing | CUTOFF 20 Hz passes 100 Hz < −20 dB; DC < 0.01; DRIVE ±1 dB / ±2 dB; sub around the filter within 1 dB, through a closed one < −15 dB | |
| | smoothing | CUTOFF, RES, DRIVE steps ≤ 1.5 × steady | |
| | sample-rate change | no burst, ≤ 1.5 × steady | |
| | sub tracking, clamps, octaves | within 1 cent over −3 … +5 V; floor 4 Hz; ceiling 2000 Hz; two-octave 27.5 Hz at 110 Hz | 1 cent |
| | SUB OUT ignores SUB; spread mono / mono sum; no click on pitch step; default level; rate independence | | |
| `test_engine` | optimised engine against the frozen pre-change engine (`testdata/ref/`): defaults, no drift, CVs, steps, drift, busy, rate switching, LP12 at 96 kHz; the unpatched body; two instances independent | every sample within −90 dBFS of the reference, RMS within −120 dBFS; unpatched body writes 0, SUB OUT matches, re-patched level within 1.5 dB; an instance does not depend on another | worst peak −95.5 dBFS (R, CVs, no drift), worst RMS −128.9 dBFS (busy, L) (printed) |

`test_engine` compares against the pre-optimisation code, not against an absolute behaviour: if both moved together the test would not notice.

### 10.1 Not covered by the automated suite

- Anything in Rack: the panel, the menu, the tooltip text, save and load through Rack, Initialize, bypass, polyphonic cables as Rack delivers them, the module browser, Rack's CPU meter, rate-change order.
- Windows and Linux CPU; the libm check (Linux only; not run here).
- Listening: character, the default settings, the drift amount, the makeup, the self-oscillation level, aliasing grit (the guide says these are first guesses).
- Hostile input, NaN, 1e30, the sample-rate extremes and a seeded fuzz of the module (there is no `test_hostile.cpp`); the JSON edge cases; a golden saved state; an allocation check.
- Aliasing against a bound (issue #67).
- The level against the sub's step (section 2.13), the ±5 V peak claim, the 20.48 kHz top, the −3 dB points.

---

## 11. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, all knobs at the defaults (PITCH 0 = C2, SUB 50 %, DETUNE 25 %, SPREAD 60 %, DRIFT 25 %, CUTOFF 1.2 kHz, RES 20 %, DRIVE 25 %), attenuverters 0, Sub octave one down, Free pitch off, LP24, Sub through filter off, L, R and SUB patched.

**Method notes.**
- For pitch, read SUB OUT (a clean sine): it is f0/2.
- For the filter, set SUB 0 (the sub joins after the filter and hides the cutoff otherwise) and DRIFT 0.
- Set DRIFT 0 for any steady-level or spectrum measurement, and wait 6 s after turning it down.
- For stereo, measure L and R separately and as a sum.

### 11.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Panel | add Fathom, look | the panel of section 2.1, every label present, the knobs turn 270° |
| PITCH tooltip | hover | "C2" at the default; the ends read "C0" and "C4"; a semitone per step |
| PITCH range | SUB OUT on a scope or counter, PITCH −24, 0, +24 | 8.18 Hz, 32.70 Hz, 130.81 Hz (sub = f0/2) |
| V/OCT | 1 V, 2 V, −1 V | sub 65.41, 130.81, 16.35 Hz (within 0.03 cent) |
| V/OCT clamp | +6 V, then −4 V | body at 4 kHz (not 4.19 kHz) and sub 2000 Hz; body 8 Hz and sub 4.00 Hz |
| V/OCT step | a sequencer step of +1 V | no click; f0 63 % in 5 ms, within 1 cent in about 34 ms |
| Free pitch | menu on, turn PITCH | fine steps, tooltip "x.xx st"; off again rounds the knob |
| Sub octave | menu "Two octaves down" | sub 16.35 Hz at C2 on L, R and SUB OUT |
| SUB | 0 → 100 % | the sine fades into L and R; SUB OUT unchanged at ±5.000 V; SUB 0 leaves L, R with the body only |
| DETUNE | 0 → 100 % | one clean saw at 0 (offsets 0); at 100 % the offsets of section 2.6 (−78 … +73 cents); the centre saw quieter |
| SPREAD | 0 → 100 % | 0: L = R exactly; 100 %: outer saws hard left and right; the sub stays centred; mono sum nearly constant |
| DRIFT | 0, 25 %, 100 %, hold a minute each | 0: steady; 25 %: ±1.5 cents, 0 … −0.75 dB; 100 %: ±6 cents, 0 … −3 dB; the sub drifts in level only |
| DRIFT to 0 | from 100 % to 0 | the movement dies away over about 6 s |
| CUTOFF range | sweep CUTOFF fully left to fully right with SUB 0 | 20 Hz: the drone is gone (−44 dB); fully right: tooltip reads 20480 Hz and the top of the range is open but −11.5 dB at 20 kHz |
| CUTOFF law | tooltip at 5.907 (default) | 1200 Hz; the frequency doubles for every 0.1 of the dial's travel (10 octaves over the whole dial) |
| CUTOFF CV | 1 V, attenuverter +100 % | cutoff up one octave; −100 % down one octave; ends clamp at 20 and 20,480 Hz |
| CUTOFF −3 dB | LP24, RES 0, a swept sine through (or a noise through a spectrum meter) at 1200 Hz | −3 dB at about 520 Hz (0.435 ×), not at 1.2 kHz |
| RES | 0 → 100 % | a peak at CUTOFF; the low end keeps its weight (within 0.4 dB of the default's RMS); self-oscillation from about 99 % |
| Self-oscillation | RES 100 %, SUB 0, CUTOFF well away from the drone's partials | a sine within 15 cents of CUTOFF (about 10 cents flat at 220 Hz) riding on the filtered drone; 0.86 V peak at DRIVE 25 % when measured alone (stage with no input); tracks a CUTOFF CV octave within 1 %. The saws cannot be muted, so the tone is added to the drone: RES 1 raises the default L RMS by 0.36 dB |
| Self-oscillation vs DRIVE | RES 100 %, DRIVE 0 / 50 / 100 % | 1.68, 0.52, 0.18 V peak (loudest with DRIVE down) |
| DRIVE | 0 → 100 % | tone rougher; L RMS within 0.2 dB of the default (the makeup); never above 2 dB |
| Growl | RES 60 %, DRIVE 70 %, sweep CUTOFF | the peak sits below CUTOFF (section 5.4); it moves with DRIVE |
| Filter type | the four choices | LP24 default; LP12 shallower; BP peaks at CUTOFF; HP −3 dB at about 2.3 × CUTOFF |
| Sub through filter | CUTOFF 20 Hz, SUB 50 % | off: the sub stays (L −0.7 dB re open); on: L falls about 21 dB |
| CV law | +5 V on DETUNE, DRIFT, RES, DRIVE, attenuverter +100 %, knobs at default | parameters reach 75 %, 75 %, 70 %, 75 % (not 100 %); −5 V clamps at 0; attenuverter 0 has no effect. Do not file as a bug; see section 1.1 |
| Poly on CV and V/OCT | 4-channel cable, a different voltage per channel | only channel 1 matters; channels 2–4 change nothing. Do not file; issue #64 |
| Outputs | scope L, R at the defaults | peaks near 3.9 V, no DC (mean under 1 mV); with SUB, DETUNE and DRIVE up, peaks reach about 5 V; a hard CV step can reach about 7 V |
| Unpatched body | unplug L and R, leave SUB | L, R silent; SUB OUT keeps following PITCH and V/OCT; re-plug: no stuck tone, a possible short tick |
| Initialize | after changing all menus and knobs | knobs to the defaults of section 2.2; Sub octave one down, LP24, Sub through filter off, Free pitch off |

### 11.2 Clicks

| Procedure | Expected |
|---|---|
| step V/OCT, PITCH, CUTOFF, RES, DRIVE, DETUNE, SPREAD, DRIFT in turn (mapping module or typed value) | no click (section 2.13) |
| step SUB 0 ↔ 100 % | a click, up to about 2.5 V in L: known (section 9.2 item 3) |
| toggle Sub through filter during a held note | a click, about 2.5 V: known |
| switch Filter type during a held note | no click beyond the new type's own edge |
| change Rack's sample rate while playing | the ladder is cleared: a short tick is possible; pitch unchanged |

### 11.3 Saved patches

| Procedure | Pass criteria |
|---|---|
| set every knob and all four menu choices away from their defaults, save, quit, reopen | everything restored; JSON `data` holds `subOctave`, `freePitch`, `filterType`, `subThroughFilter` |
| hand-edit JSON: missing keys, wrong types, out-of-range values | loads without crash; clamped (`filterType` 9 → High-pass, −1 → LP24); wrong type → 0 or false; missing key → current value. Large integers load a wrong but valid value (section 3) |
| save and load a module preset (.vcvm), duplicate the module | settings carried; instances independent |
| add a Fathom at PITCH other than 0, or load such a patch | a short glide from C2 (about 35 ms) is expected |
| two Fathoms, same settings, same V/OCT | identical output, phase-locked; drift in lockstep (fixed seeds); detune one by 1 cent and they beat |

### 11.4 Sample rates (44.1 kHz to 768 kHz)

| Procedure | Pass criteria |
|---|---|
| run at 44.1, 48, 88.2, 96, 176.4, 192, 384, 768 kHz | no crash, no silence; sub pitch the same at every rate (within 0.01 cent); L RMS 2.17 V and peak 3.9 V at the defaults |
| CPU against rate | grows in proportion to rate: about 0.6 % at 48 kHz, 2.4 % at 192 kHz, 9.4 % at 768 kHz of a core per instance (Apple M-series) |
| CUTOFF at the top at 44.1 kHz | the pole clamps at 19.8 kHz; self-oscillation about 56 cents flat |
| change rate while a note holds | pitch continues; a tick of up to about 0.35 V is possible |

### 11.5 Polyphony and CV at audio rate

| Procedure | Pass criteria |
|---|---|
| a polyphonic cable into V/OCT | only channel 1 plays (one note) [#64] |
| CUTOFF, RES or DRIVE CV at audio rate | read once per 16 samples (3 kHz at 48 kHz) and smoothed over 20 ms: only slow movement passes; no zipper |
| DETUNE or DRIFT CV at audio rate | read every sample; no click was found; DRIFT's effect still moves over seconds |
| ±10 V and larger on any CV | saturates at the ends of the range; no crash |

### 11.6 CPU

| Procedure | Pass criteria |
|---|---|
| one idle instance (L, R, SUB unpatched) | idle-level meter; Mac bench 0.02 % of a core |
| one instance with outputs patched, defaults | Mac bench 0.6–0.7 % (budget 1.2) |
| busy (all CVs moving, DETUNE, DRIFT, SPREAD at 100 %) | Mac bench 0.62–0.76 % (budget 1.2); spec target < 1 % |
| 16 instances | per-instance cost the same as one alone (±10 %) |
| Windows | no automated budget; record Rack's meter per instance for idle, outputs and busy at 48 kHz (issue #8 saw about 6 % per idle instance before the pause) |

### 11.7 Aliasing (section 8.5)

| Procedure | Pass criteria |
|---|---|
| DETUNE 0, DRIFT 0, SPREAD 0, SUB 0, CUTOFF at the top, V/OCT set for f0 = 2 kHz (use a value with fs/f0 not an integer, such as 2027 Hz), a spectrum meter | stray components at about −38 dB (48 kHz) and −36 dB (44.1 kHz) re the strongest harmonic, near the top of the spectrum; at 110 Hz −70 dB; no bound is set [#67]. Record on #67 |
| the same at the default CUTOFF | strays under about 0.5 mV |

---

## 12. Glossary

| Term | Meaning |
|---|---|
| Attenuverter | a −1 … +1 scale on a CV input; negative inverts |
| Block | 16 samples; the engine's rate for the filter controls and the body high-pass coefficients |
| Body | the seven saws (stereo), high-passed at f0; the sound without the sub |
| CUTOFF | the ladder's pole frequency, 20 … 20,480 Hz; LP24 is −3 dB at 0.435 × it |
| Detune | spread of the seven saws' frequencies, from one saw to the JP-8000's widest |
| Drift | per-saw random walks of pitch (±6 cents) and level (0 … −3 dB), slewed over 1 s |
| f0 | the body's fundamental after the V/OCT sum, slew and the 8 Hz … 4 kHz clamp |
| Halfband | the filter pair used to run the ladder at twice the host rate |
| k | the ladder's feedback amount, 4.1 × RES²; 4.0 is the self-oscillation point |
| Ladder | the four-pole transistor-ladder filter model |
| Makeup | the gain after the ladder that keeps DRIVE from changing the level: 1/g below unity drive, g^−0.75 above |
| One-pole | a first-order smoother; τ is its time constant; after τ it has covered 63 % of a step, after 5 τ about 99 % |
| Pole frequency | the frequency of each of the four identical one-pole stages |
| polyBLEP | a band-limited step correction that removes a saw's aliasing |
| Sub | the sine one (or two) octave(s) under the body; SUB OUT is the raw sine |
| Sub through filter | menu switch that sends the sub through the ladder |
| Supersaw | seven detuned saws mixed with the Roland JP-8000's curves |
| SVF | state-variable filter (here only the body's high-pass) |
| τ | time constant |

---

### Note on the measurement programs

- The test programs were built in a scratch copy of the exact tree (`git archive 2e6e960`) at `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/fathom/tree` (nothing was built in, edited or committed to the repository). Logs of the test builds and runs, the bench and the sanitizer runs are in `…/techref/fathom/logs/`.
- My measurement programs, outside the repository, compiled against that tree with the plugin's flags (`-std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; `build.sh` in the folder), all in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/fathom/meas/`:
  - `common.hpp`: the defaults as a `FathomFrame` and a small renderer.
  - `m1_voice.cpp`: sub pitch and clamps, V/OCT slew, levels at the jacks, drift bounds and decay, determinism.
  - `m2_filter.cpp`: the ladder's −3 dB points, the CUTOFF range, self-oscillation, droop against level, DRIVE and RES levels, type levels, sub routing.
  - `m2b_droop.cpp`: where the resonant peak sits in the real drone's spectrum.
  - `m3_robust.cpp`: NaN/infinity/1e30 on every input and param, the fuzz, sample rates and CPU per rate, rate changes, pause and resume.
  - `m3b_peak.cpp`: what makes L and R exceed 5 V.
  - `m3c_alias.cpp`: aliasing with `rkatest::AliasMeter`.
  - `m4_steps.cpp`: the click table (section 2.13).
  - `m5_start.cpp`: the start-up glide.
  - `m6_tuner.cpp`: partial amplitudes in L (issue #77).
- They drive `rka::FathomEngine` as `Fathom.cpp` does (one frame of params and port voltages per sample, channel 1 only, all outputs patched unless stated), at 48 kHz unless stated. They do not exercise Rack's ports, threading, tooltips or GUI. Anything marked [meas] should match Rack at 48 kHz; Rack-side effects (block size, the order of events on a patch load or a rate change, tooltips) are outside them.
- CPU figures are machine-dependent: the bench was run on a Mac that was also running other work.
