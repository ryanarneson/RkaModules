# Beacon 2.0.39: Technical Reference for QA

Module: **Beacon**, plugin `rka` (slug `rka`, module slug `Beacon`), VCV Rack 2 (minRackVersion 2.5.0). Tags in plugin.json: Envelope generator, Polyphonic, Slew limiter.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Beacon itself was merged to main in pull request #75 (commit `5cac7d3`, 2026-11-23); this build contains it. Nothing under `src/beacon/`, `docs/beacon/`, `res/beacon/`, `src/Toggle3Step.hpp` or `src/widgets.hpp` changed between 2.0.37 (`f811166`) and 2.0.39 (`git diff f811166 2e6e960` on those paths is empty), so everything here also holds for 2.0.37 and 2.0.38. Everything was taken from the tree of `2e6e960`.
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. A Zstandard-compressed tar. Its `plugin.json` is byte-identical to the commit's (version 2.0.39) and `res/beacon/Beacon.svg` and `Beacon-dark.svg` are byte-identical to the commit's. It holds no `presets/Beacon` (Beacon has no factory presets; only `presets/Fog` ships) and no `docs/` folder. I did not inspect `plugin.dll`.
Document date: 2026-11-24.
Spec: `docs/specs/2026-11-23-beacon-design.md`. Manual: `docs/beacon/README.md`. Listening guide (a cross-check, not a source): `beacon-2.0.34-listening.html`, for test build 2.0.34.

## Build history

Beacon was designed, built and listened to in one day, 2026-11-23. Four test builds carry it (CHANGELOG.md, `git log` for the module's paths):

| Build | Commit, time | What changed in Beacon |
|---|---|---|
| 2.0.34 | `4f8ac5a`, 11:50 | First test build: the engine, Gate / Trig / Hold, the stage gates and TRIG, the window, the three-position switch, OUT and INV. The listening guide is for this build. |
| 2.0.35 | `42d9d5c`, 14:00 | Listening-stop and final-review fixes: the window draws the running stage from where it really started (the engine now publishes start and goal levels); Sustain at 0 V (span dim, dot at the end of the line); the stage lights hold from the stage's start and update every 16 samples; the switch steps by direction and never wraps (`Toggle3Step.hpp`, `test_switch`; in 2.0.34 it wrapped, so Trig to Gate passed through Hold); bypass and un-bypass reset every voice; tooltip times show three digits from 10 ms up (2.0.34 showed whole milliseconds from 10 ms, up to 5 % off); the Hold line scrolls by the frame clock (60 points a second, `HoldRing`); manual fixes. |
| 2.0.36 | `8e50405`, 14:52 | Issue #76: a stage that skipped others is drawn from where the last stage ended (the engine publishes the previous stage). |
| 2.0.37 | `f811166`, 15:20 | Issue #76: the running stage starts where the last stage was *cut off* (the engine also publishes how far that stage had got). #76 closed; PR #75 merged at 15:33. |
| 2.0.38, 2.0.39 | `6f1b81c`, `2e6e960` | Neap and Spring only. No Beacon change. |

What the listening guide (2.0.34) predates, and what a tester will therefore see differently from its wording: the window's bright line now starts where the previous stage was cut off; the switch cannot wrap; the tooltip digits; the light hold; bypass resets. Section 12.9 marks where the guide and the code differ.

---

## 0. How to read this document

- Every number here comes from one of these sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/beacon/*.hpp`, `src/beacon/Beacon.cpp`, `src/Toggle3Step.hpp`, `src/widgets.hpp`, `src/FastMath.hpp`), cited as file:line where useful. Line numbers are from `2e6e960`.
  - **[test]**: asserted by the module's automated tests, which I rebuilt and ran. The tests print only "all checks passed", so a [test] figure is the asserted bound, not a measured value.
  - **[meas]**: a measurement program I wrote against the Rack-free engine headers (`rka::BeaconEngine`, `rka::beacon::Voice`, the code the module runs), compiled with the plugin's flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`), run at 48 kHz unless stated, on an Apple M-series Mac (Apple clang 21). The programs are in section 14. Bench figures are [meas] from `make bench`'s binary (`build/bench/bench`).
  - **[calc]**: arithmetic on [code] constants that I did not run as a program.
- **[issue #N]**: a GitHub issue in `ryanarneson/RkaModules` (section 10). **No open issue is filed against Beacon.** Closed #76 explains the present window drawing.
- **[unverified]**: anything I could not check against code or by measurement, mostly Rack host behaviour. Rack was not run.
- Voltages are Rack volts. "Knob" values are in [0, 1]; "noon" is 0.5.
- The tests and measurement programs do not exercise Rack's ports, threading, drawing or mouse. `Beacon.cpp` (the Rack module and its window) has no automated test beyond the default-panel screenshot of `make smoke` (not run by me).

---

## 1. What Beacon is

Beacon is a 10 HP polyphonic envelope generator, after the Instruo cèis: one three-way switch (Gate / Trig / Hold) and the knob positions give ADSR, ADR, AR, AD and a slew limiter. The design intent (spec "Intent") is cèis's outputs and its switching, in the Instrument look. Besides the envelope there is a gate for each stage and one trigger at every change of stage, so one envelope also drives events. A small window draws the envelope the knobs make and shows where it is.

Design goals that QA can check:

- ADSR, ADR (Sustain 0), AR (Decay at its minimum, Sustain full) and AD (Trig mode with a trigger) from one switch and four knobs.
- Stage times from 1 ms to 60 s on the knob itself, each decade an equal share of the turn.
- **Exact stage times, wherever a stage starts**: Release at 2 s from Sustain 5 V takes 2 s; a retrigger from 8 V takes the full Attack time to reach 10 V (the user's ruling, "option A"). Stages are timed, not rates for a full 10 V.
- A gate per stage (A, D, S, R), a 1 ms trigger at every stage start, OUT and INV.
- A Hold mode that makes it a slew limiter.
- Polyphonic: one envelope per channel of GATE (Hold: of the Sustain CV).
- CV on all four stages. There is no CV on Shape.
- Anti-goals (a failure even if it works): a jump in the envelope when retriggered or when a gate ends early; a stage time that is not what the knob or tooltip says; short times fiddly to set.

Non-goals (do not file these as defects): looping or cycling into an LFO; extra stages (hold, delay), multi-segment editing, drawable curves; Tides' spread outputs; a time range switch; audio-rate cycling; an attenuverter on the CVs; a CV on Shape; a window that is a ruler or a live scope in Gate and Trig.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. The three agree closely; the rows below are the places they do not, or where wording is looser than behaviour.

| Topic | Spec / manual / guide says | 2.0.39 does |
|---|---|---|
| A Shape move during a stage | spec "No jumps": a knob or CV moved mid-stage gives "no sample-to-sample step beyond the stage's own slope" | The level is a function of phase and Shape, so a Shape move moves the level. It is smoothed (5 ms one-pole) but on a long stage the step is far above the stage's own slope. On a 10 s Attack at phase 0.1, Shape 0 to 1 gave a first per-sample step of 0.0083 V against the stage's own 8e-5 V; the level slides by up to 8.75 V (Shape 0 to 1, at phase 0.5) in about 5 to 25 ms [meas, section 2.6]. `shape_smoothing` pins it only on the default 10 ms Attack [test]. No click, but a fast slide. Shape has no CV, so only a hand turn, MIDI mapping or automation causes it. Not filed. |
| Tooltip precision | spec "within 0.5 % of the time" | true from 10 ms up (0.497 % worst [meas]); below 10 ms the tooltip has one decimal and is up to 4.76 % off (spec and manual agree: "below 10 ms one decimal") |
| Stage length | spec "within 1 sample or 0.1 %" | exactly ceil(T x rate) samples, so up to 1 sample long: 1 ms at 44.1 kHz is 45 samples (1.02 ms); 10.0 ms at 48 kHz is 481 [meas, section 5.1] |
| Knob fully right | spec / manual "60 s" | stageSeconds(1) is 59.99997 s (0.5 ppm short, from a float constant); the voice holds any longer time to 60 s [code, meas]. Not visible |
| Defaults | manual "10 ms, 300 ms, 1 s" | 10.0015 ms, 299.91 ms, 1.0005 s (knobs 0.2093, 0.5184, 0.6279) [meas]. The tooltip reads "10.0 ms", "300 ms", "1.00 s" |
| Hold: Sustain knob | manual "the Sustain knob adds 0 to 10 V of offset" | correct, and the default knob is noon, so a Hold module with nothing patched sits at **5.000 V** with S high [meas]. The guide and manual say "turn S fully left" for a plain glide |
| Hold hysteresis | spec "about 1 mV" | settled within 1 mV, leaves settled only beyond 2 mV [code, test] |
| Hold TRIG | spec "TRIG fires on each change between them" | also fires once on entering Hold (the stage changes from the envelope's stage to the slew's) and once on leaving it [code; the `hold` test counts 4 pulses for A, S, D, S] |
| Window, Gate and Trig | spec "Polyphony: channel 1" | window, stage lights and published state are channel 1 only. A channel-1 gate that is low while other channels run shows an idle window [code] |
| Switch stepping | spec: steps by direction, held at the ends | as specified; a click on the upper half moves up (toward Gate). The guide predates this; its "GATE, HOLD, TRIG, GATE" order is reached by two down-clicks, then two up-clicks |
| Saved state | manual "Saved with the patch: the knobs and the switch" | also the push button: a shift-clicked (latched) button is a param at 1 and is saved with the patch, so the patch reloads with a gate held on every channel [code; Rack's save of it is [unverified]] |
| Bypass | spec "Rack's default (outputs silent)" | no `configBypass` routes, so outputs are Rack's default for a module with no routes (silent) [unverified in a live Rack]; bypass and un-bypass reset every voice [code] |
| Aliasing | spec: measure the fastest settings or record why not | TESTING.md records "not needed" (control signals; fastest segment is a 1 ms ramp), no bound measured; issue #67 (all modules' aliasing) lists other modules, not Beacon |
| `make test` flags | TESTING.md rule 9: plugin and tests share arithmetic flags | `make test` builds Beacon's tests at `-O2` without fast-math. With the plugin's flags, `test_view` fails 4 checks (section 10, item 1) |

---

## 2. Panel, controls and ports

### 2.1 Layout

10 HP (50.8 mm). Top to bottom: the window; Shape and the three-way switch; the four stage knobs, each with a light at its shoulder; four CV inputs; four stage-gate outputs; GATE and RETRIG; the push button; TRIG, OUT and INV. Positions in mm from `BeaconLayout.hpp` (generated by `tools/gen_panels.py`) [code]:

| Control | Centre (x, y) mm |
|---|---|
| Window | rectangle at (4.4, 19.0), 42.0 x 17.0 |
| Shape knob / Mode switch | 13.8, 45.5 / 30.0, 45.5 |
| Attack, Decay, Sustain, Release knobs | 8.0 / 19.6 / 31.2 / 42.8, y 61.0 |
| Stage lights (A, D, S, R) | 13.5 / 25.1 / 36.7 / 48.3, y 56.0 |
| Attack, Decay, Sustain, Release CV inputs | same x as the knobs, y 76.5 |
| A, D, S, R gate outputs | same x as the knobs, y 92.0 |
| GATE input, RETRIG input | 8.0, 105.5 and 19.6, 105.5 |
| Push button | 8.0, 115.0 |
| TRIG, OUT, INV outputs | 19.6 / 31.2 / 42.8, y 118.0 |
| Screws | 43.18, 2.54 and 7.62, 125.96 |

The Mode switch is a 3.6 x 6.4 mm `Toggle3`: handle up at 0 (Gate), middle at 1 (Trig), down at 2 (Hold). The push button is a 6.4 mm `CapButton`. The panel generator's checks pass (`tools/test_gen_panels.py`, 115 tests OK [test]).

### 2.2 Parameter, input, output and light ids

`Beacon.cpp` pins the param ids to the engine frame's ids with `static_assert`. They must never change number (saved patches depend on them).

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 | Shape | 0..1 | 0.75 | "Shape (left: reverse, noon: linear, right: classic)", shown as -100..+100 %, "Linear" at exactly 0.5 |
| 1 | Mode | 0..2, snapped | 0 (Gate) | "Mode": Gate, Trig, Hold |
| 2 | Attack | 0..1 | 0.2093 | "Attack (Hold: time to rise 10 V)", shown as ms or s |
| 3 | Decay | 0..1 | 0.5184 | "Decay (Hold: time to fall 10 V)", ms or s |
| 4 | Sustain | 0..1 | 0.5 | "Sustain (Hold: offset)", shown as 0..10 V |
| 5 | Release | 0..1 | 0.6279 | "Release", ms or s |
| 6 | Push | button 0..1 | 0 | "Push (a gate on every channel while held)" |

| Id | Input | | Id | Output | | Id | Light |
|---|---|---|---|---|---|---|---|
| 0 | Attack CV | | 0 | Attack stage gate | | 0 | Attack |
| 1 | Decay CV | | 1 | Decay stage gate | | 1 | Decay |
| 2 | Sustain CV | | 2 | Sustain stage gate | | 2 | Sustain |
| 3 | Release CV | | 3 | Release stage gate | | 3 | Release |
| 4 | Gate | | 4 | Stage trigger | | | |
| 5 | Retrig | | 5 | Envelope (OUT) | | | |
| | | | 6 | Inverted envelope (INV) | | | |

Input tooltips: "Attack CV (10 V: the knob's travel)" and likewise Decay and Release; "Sustain CV (Hold: the signal to slew; its channels set the count)"; "Gate (polyphonic: an envelope per channel)"; "Retrig (restarts Attack while the gate is high)". Output tooltips: "Stage trigger (1 ms at each stage start)", "Envelope", "Inverted envelope". The Mode param is read rounded: below 0.5 Gate, 0.5 up to below 1.5 Trig, 1.5 and up Hold, so a value of exactly 0.5 is Trig [code, BeaconEngine.hpp:138].

### 2.3 The time law: Attack, Decay, Release

[code, `Timing.hpp`]

```
seconds = 0.001 x 2^(k x 15.8726744)        k = clamp(knob + CV/10, 0, 1)
```

- 15.8726744 is log2(60000) as a float (true value 15.8726749). The law is an exponential from 1 ms at k = 0 to 60 s at k = 1. Each decade is 0.209 of the travel, which is why the Attack default 0.2093 is 10 ms.
- `fastExp2` is within 1.25e-6 relative of 0.001 x 60000^k over 100,001 knob points [meas].
- A NaN knob reads noon. NaN CV volts read 0 V. The sum is clamped to [0, 1], so the time is held between 1 ms and 60 s. The Voice clamps again, to [1 ms, 60 s], and a NaN time keeps the last good time [code, `Voice::pickTime`].
- Knobs and CVs are read every sample, with no smoothing. A time change on a running stage changes the rate at once, from where it is, without moving the level [test: `no_jumps`, Attack swept 1 ms to 60 s and back mid-stage, step at most 1.5 x the 1 ms Attack's own largest step + 0.1 mV].

Knob to time [meas]:

| Knob | Seconds | Tooltip |
|---|---|---|
| 0 | 0.00100000 | 1.0 ms |
| 0.1 | 0.0030048 | 3.0 ms |
| 0.2093 (Attack default) | 0.0100015 | 10.0 ms |
| 0.25 | 0.0156508 | 15.7 ms |
| 0.5 (noon) | 0.244949 | 245 ms |
| 0.5184 (Decay default) | 0.299912 | 300 ms |
| 0.6279 (Release default) | 1.00046 | 1.00 s |
| 0.75 | 3.83366 | 3.83 s |
| 0.9 | 19.9681 | 20.0 s |
| 1 | 59.99997 | 60.0 s |

**Tooltip text** [code, `formatSeconds`; test `typed`]: below 99.95 ms one decimal ("1.0 ms" to "99.9 ms"); 100 to 999 ms whole ms; 1.00 to 9.99 s two decimals; from 10 s one decimal ("10.0 s" to "60.0 s"). From 10 ms up the text is within 0.497 % of the real time; below 10 ms up to 4.76 % [meas, 200,001 knob points].

**Typed values** [code, `parseSeconds`; test `typed`; meas of each case]: the text is milliseconds, or seconds if it ends in "s" or "S" alone. Spaces around are ignored. The time is converted to a knob with `stageKnob` (a clamp to [0, 1]).

| Typed | Result |
|---|---|
| `250`, `250 ms` | 250 ms (knob 0.501855) |
| `1.5 s`, `1.5s` | 1.50 s |
| `5 S`, `5 MS` | 5 s, 5 ms |
| `90 s`, `60000`, `inf` | held to 60 s |
| `0.5`, `-5`, `0` | held to 1 ms (the tooltip then reads "1.0 ms") |
| `1e3` | 1 s (read as 1000 ms) |
| `0x10` | 16 ms (hex is accepted) |
| `abc`, `nan`, `1.5 sec`, `1,5 s`, empty | rejected: the value does not change |

A typed value round-trips to within 1.07e-6 relative (seconds to knob to seconds) [meas]. What the tooltip then shows is rounded as above.

**CV law** [code, `stageSecondsCv`]: the CV adds CV/10 to the knob, so **10 V is the knob's whole travel and 1 V multiplies the time by 60000^0.1 = 3.005** [meas, from noon]:

| CV | -10 V | -5 V | -1 V | 0 V | +1 V | +2 V | +5 V | +10 V |
|---|---|---|---|---|---|---|---|---|
| Time at noon knob | 1 ms | 1 ms | 81.5 ms | 244.9 ms | 736 ms | 2.21 s | 60 s | 60 s |

There is no attenuverter. +/-50 V simply saturates. A polyphonic CV gives each channel its own value, a mono CV applies to every channel, and channels past a polyphonic cable's count read 0 V (section 7).

### 2.4 Sustain

[code, `BeaconEngine::process`, `Voice::runStage`]

- Gate and Trig: `sustain volts = 10 x clamp(knob + CV/10, 0, 1)`, that is `clamp(10 x knob + CV, 0, 10)`. The CV adds 1 V per volt. NaN keeps the last good level.
- The Sustain stage follows the sustain level through a **2 ms one-pole**, so a step on the knob or CV does not click but a 5 Hz LFO still moves the level (a tremolo). Measured for a 10 V to 0 V step: 63 % in 2.00 ms, 10 % left after 4.62 ms, 1 % left after 9.23 ms, within 1 mV after 18.4 ms, the same at 44.1, 48 and 96 kHz [meas]. A 5 Hz LFO of 4 V amplitude on Sustain reaches OUT within 0.5 dB [test: `sustain_follows`]. Audio-rate noise on Sustain keeps OUT inside 0..10 V [test].
- The smoothing applies in the Sustain stage only. **Decay's goal is the sustain level at the moment Decay began.** If Sustain is moved during Decay, Decay still ends at the old level and the Sustain stage then glides to the new one: in a 100 ms Decay to 8 V with Sustain moved to 2 V half way, Decay ended at 8.000 V and OUT then read 7.94, 4.18, 2.80, 2.30 V at 2 ms steps [meas]. The window draws it the same way (spec).
- Sustain holds its level for ten minutes within 1e-6 V [test: `levels`].
- In Hold the Sustain input means something else (section 5.3).

### 2.5 Mode switch

[code; test `test_switch`: `click_table`, `no_wrap`, `halves`, `drag`] Three positions: Gate (0, up), Trig (1), Hold (2, down). A click on the upper half moves one position up, on the lower half one down, held at the ends: from Gate an upper click stays at Gate. A vertical drag moves one position per 12 px of travel, down is down, held at the ends; after a whole step of drag, the release adds no click. One gesture is one undo step (`pushParamChange`, "move switch"). A double-click does not reset it (Rack's `Switch::onDoubleClick` overrides the reset) [unverified; read from the SDK header only]. The switch's decision logic is Rack-free and tested; the widget around it (`rka::Toggle3`, `widgets.hpp:287`) is not: a real mouse, undo history and drag feel are on QA's list (section 12).

### 2.6 Shape

[code, `Curve.hpp`] One knob for every stage. The curve maps a stage's phase p (0 to 1, linear in time) to the fraction c of the distance from the stage's start to its goal that has been covered. Rising and falling stages use the same function.

```
classic(p) = 1 - (1 - p)^4          (right)
reverse(p) = p^4                    (left)
a = |2 x Shape - 1|
c(p)  = p + a x (curve(p) - p)      curve = classic for Shape >= 0.5, reverse below
```

Noon is exactly linear. Both ends are exact (c(0) = 0, c(1) = 1 for every Shape) [test: `curve_ends`, `curve_ends_odd`]. Values of c [meas]:

| Shape | p = 0.1 | 0.25 | 0.5 | 0.75 | 0.9 | p where c = 50 % | p where c = 90 % |
|---|---|---|---|---|---|---|---|
| 0 (left) | 0.0001 | 0.0039 | 0.0625 | 0.3164 | 0.6561 | 0.841 | 0.974 |
| 0.25 | 0.0501 | 0.1270 | 0.2812 | 0.5332 | 0.7780 | 0.725 | 0.958 |
| 0.5 (noon) | 0.1000 | 0.2500 | 0.5000 | 0.7500 | 0.9000 | 0.500 | 0.900 |
| 0.75 (default) | 0.2220 | 0.4668 | 0.7188 | 0.8730 | 0.9499 | 0.276 | 0.802 |
| 1 (right) | 0.3439 | 0.6836 | 0.9375 | 0.9961 | 0.9999 | 0.159 | 0.438 |

- Classic: initial slope 4 times the average, final slope 0 (a rounded arrival). Reverse: initial slope 0, final slope 4. These are quartic polynomials, not RC exponentials. The spec lets the builder choose how the curve is computed.
- **For a falling stage the same c is the fraction of the fall covered**: Decay and Release at Shape right drop fast and have a long tail; at Shape left they hold near the top and fall late.
- Examples: default Attack 10 ms at Shape 0.75 reaches 5 V after 2.76 ms and 9 V after 8.0 ms [calc]. A 245 ms Release from 10 V at Shape right is at 5 V after 39 ms [calc].
- **Smoothing**: each voice follows the Shape knob through a 5 ms one-pole in double precision (coefficient 1 - e^(-1/(0.005 x rate))); it snaps to the knob on reset and for a channel that appears [code, `BeaconEngine.hpp:113`]. The window draws with the knob's value, not the smoothed one.
- **A Shape move mid-stage moves the level** (section 1.1). Largest level shift for a one-sample Shape change, as a fraction of the stage's distance [meas]: 0 to 1: 0.875 (at phase 0.5); noon to 1: 0.472; noon to 0: 0.472; 0.75 to noon: 0.236; 0.75 to 0: 0.663. A level shift is spread over the 5 ms one-pole (99 % in 23 ms [calc]).
- **Tooltip**: -100 % fully left, "Linear" at exactly 0.5, +100 % fully right; the default 0.75 reads +50 % [code: multiplier 200, offset -100]. A knob that stops beside noon reads a small percentage (the guide's 0.4 %). Typing "linear" sets noon [code, `ShapeQuantity`].
- Hold ignores Shape (section 5.3).

### 2.7 Push button

[code; test `push`] A gate held for as long as it is held, on every channel, ORed with GATE (`in.gate = gate || push`). If GATE is unpatched, it is the only gate, and the channel count is 1. Shift-click latches it down until the next click (`CapButton`, `holdPress`). It does nothing in Hold. A latched button is a param at 1; Rack saves it (section 3).

### 2.8 GATE and RETRIG

[code; test `schmitt`] Each channel has its own Schmitt trigger on GATE and on RETRIG: high at 1.0 V, low below 0.1 V (the plugin's thresholds, as Wake's clock). NaN reads low. A level between 0.1 V and 1.0 V keeps the last state: GATE 0.9 V from low stays low [test]; GATE 0.5 V after being high stays high, and 0.11 V from high stays high [test, meas]. An unpatched GATE reads 0 V through its Schmitt, so a cable plugged back in at 0.5 V is low, not a new edge [test].

RETRIG rising while the gate is high restarts Attack from the present level, without a new gate. While the gate is low it does nothing (a RETRIG edge in Release is ignored) [test: `retrig`]. It restarts Attack in both Gate and Trig modes.

### 2.9 Outputs

[code]

- **OUT**: 0 to 10 V. In Hold, -10 to +10 V. Never outside +/-10 V [test: `ok()` in `test_hostile`, every output, every sample, every fuzz seed].
- **INV**: exactly -OUT, bit for bit, including -0 at 0 V [test: `inv_exact`]. A scope reads 0 V.
- **A, D, S, R**: 10 V while that stage runs, else 0 V. Every stage, even one that ends at once, holds its gate for at least one sample [test: Decay to Sustain 10 V shows its gate for exactly 1 sample].
- **TRIG**: 10 V for `round(0.001 x rate)` samples (at least 1) at the start of every stage, including the return to idle at the end of Release. Measured: 8, 22, 44, 48, 96, 192 and 768 samples at 8, 22.05, 44.1, 48, 96, 192 and 768 kHz, that is 1.0000 ms except 0.9977 ms at 22.05 and 44.1 kHz [meas]. A stage start while the pulse is high restarts the 1 ms (pulses merge): two starts 0.3 ms apart give one pulse 1.3 ms wide [test: `gates_and_trig`].
- A long gate gives five TRIG pulses (start of A, D, S, R and the return to idle); a Trig-mode trigger gives three (A, R, idle) [test, meas].
- Every output carries the channel count (section 7).

### 2.10 The stage lights

[code, `BeaconLights`; test `stage_lights`; meas] The four lights follow **channel 1's** stage gates. `note()` runs every sample; `update()` every 16 samples (the module's clock divider; 0.33 ms at 48 kHz). The module sets brightness 1 or 0; any fade is Rack's [unverified].

- A stage lit even for one sample shows for **at least 40 ms** (`HOLD_SECONDS` 0.04, over a 30 Hz UI frame), counted from the stage's first update. A longer stage's light goes out at the first update after the stage ends.
- Measured on-time, A = D = R = T, 48 kHz, long gate [meas]: T 1 ms, 10 ms, 30 ms: 40.3 ms; 50 ms: 50.3 ms; 100 ms: 100.3 ms; 500 ms: 500.3 ms. A one-sample stage (Decay to Sustain 10 V): 40.3 ms. So stages under 40 ms overlap: the A and D lights are lit together for short stages. The Sustain light follows the S gate (the gate stayed in Sustain 200 ms, the light 200.3 ms).
- Bypass and un-bypass put all four lights out [code].

---

## 3. The right-click menu and saved state

[code; `Beacon.cpp` has no custom menu items and no `dataToJson` / `dataFromJson`]

- The right-click menu is Rack's standard one (Plugin, Preset, Initialize, Randomize, Disconnect cables, Duplicate, Bypass, Delete). Beacon adds nothing.
- **Saved state**: only Rack's params, ids 0 to 6 (Shape, Mode, Attack, Decay, Sustain, Release, Push). The JSON `data` object is absent. The stage, phase and level of every voice are **not** saved: a reloaded patch starts every voice idle at 0 V (a gate still high then starts a new Attack from 0 V).
- The push button is a param and is saved: a patch saved with the button latched down reloads with it down [code; [unverified] in Rack]. The manual lists only "the knobs and the switch".
- **Initialize** (`onReset`): every voice idle at 0 V, gates low, lights out, Shape follower back to 0.75 and snapping to the knob. Rack resets the params to their defaults (Mode Gate, Shape 0.75, A 10 ms, D 300 ms, S 5 V, R 1 s, button up).
- **Randomize**: Rack's. The Mode switch is a `configSwitch` and may be randomised; `configButton` params are not [unverified].
- **Bypass** (`onBypass`, `onUnBypass`): both reset every voice to idle at 0 V and put the lights out. Beacon configures no bypass routes, so its outputs are silent while bypassed (Rack's default for a module with none) [unverified in a live Rack]. On un-bypass nothing resumes and no Release runs; a gate still high starts a new Attack from 0 V [code, manual].
- **Sample-rate change** (`onSampleRateChange`): the engine keeps every voice's stage, phase and level; times stay in seconds; the TRIG pulse length is recomputed and trimmed if it was longer [code; tests `sample_rate_change`, `rate_change`, `rate_change_carries_the_stage`].
- No factory presets.

---

## 4. Signal flow

### 4.1 Block diagram

```
 CONTROL FRAME (once per sample, from Rack)
   MODE (rounded: <0.5 Gate, <1.5 Trig, else Hold) -> channel count n
        Gate/Trig: n = GATE's channels (1 if unpatched), max 16
        Hold:      n = Sustain CV's channels (1 if unpatched), max 16
   channels that appear (n rising) start idle at 0 V; channels past n read 0 V

 PER CHANNEL c < n
   GATE[c]   --Schmitt 0.1/1 V--+
   PUSH button (>= 0.5) --------+--OR--> gate
   RETRIG[c] --Schmitt 0.1/1 V---------> retrig
   Attack, Decay, Release:  knob + CV[c]/10 --> 1 ms x 2^(15.87 k)   (clamped 1 ms .. 60 s)
   Sustain (Gate, Trig):    10 x clamp(knob + CV[c]/10, 0, 1)  V
   Sustain (Hold):          clamp(CV[c] + 10 x knob, -10, +10)  V     (the signal to slew)
   Shape:                   knob --5 ms one-pole (double)--> shape[c]
        |
        v
   VOICE (one per channel; Voice.hpp)
     edges: rise = gate & !prev; fall; retrig edge
     Gate/Trig: [stage that finished last sample hands on] -> [edge may begin a stage]
                -> run the stage: phase += dt/T;  level = start + (goal - start) x c(phase, shape)
     Hold:      slew toward the input at 10 V/Attack up, 10 V/Decay down; S/A/D by 1 mV / 2 mV hysteresis
        |
        +--> OUT[c]  (+/-10 V)                 --> INV[c] = -OUT[c]
        +--> stage gates A D S R [c]  (10 V while that stage runs)
        +--> TRIG[c]  (10 V for 1 ms from every stage start; restarts if re-entered)

 CHANNEL 1 ONLY
   stage, phase, level, mode, start, goal, previous stage, previous phase
        --relaxed atomics--> the window (UI thread)
   stage gates --note() every sample, update() every 16--> four lights (40 ms hold)
```

While a voice is IDLE nothing runs but the gates (all low) and the TRIG countdown. There is no sleep logic; the cost is flat (section 9).

### 4.2 Block constants

| Constant | Value | Source |
|---|---|---|
| Time range | 1 ms to 60 s; `kLog2Span` 15.8726744 | Timing.hpp:13 |
| CV scale | 10 V = the knob's whole travel | Timing.hpp:28 |
| Default Shape, Sustain | 0.75, 0.5 (5 V) | BeaconEngine.hpp:37 |
| Default A, D, R knobs | 0.2093, 0.5184, 0.6279 | BeaconEngine.hpp:37 |
| Schmitt thresholds | high 1.0 V, low 0.1 V (NaN low) | BeaconEngine.hpp:235 |
| Shape smoothing | 5 ms one-pole, double | BeaconEngine.hpp:113 |
| Sustain smoothing | 2 ms one-pole, double | Voice.hpp:51 |
| Phase, step, level | double (a 60 s stage at 768 kHz is 46 M samples) | Voice.hpp:4 |
| End of stage | phase >= 1 - 0.001 x step counts as 1 | Voice.hpp:180 |
| TRIG length | round(0.001 x rate) samples, at least 1 | Voice.hpp:46 |
| Hold slew rate | 10 V x dt / Attack (up), 10 V x dt / Decay (down) | Voice.hpp:199 |
| Hold hysteresis | settled within 1 mV; leaves beyond 2 mV | Voice.hpp:208 |
| Light hold, update | 0.04 s; every 16 samples | BeaconEngine.hpp:63 |
| Max channels | 16 | BeaconEngine.hpp:97 |
| Accepted sample rates | 1 Hz to 1e8 Hz; others ignored | BeaconEngine.hpp:108 |

**Stage end rule.** A phase within a thousandth of a sample of 1 counts as 1, so a time that is a whole number of samples (1 ms at 48 kHz, though 0.001f is 5e-8 long) takes that many samples, not one more [code, test `exact_times`].

**No libm per sample**: the per-sample code uses `fastExp2`, `fastExp2D` (set-up only) and polynomials; `std::log10` appears only in the window's UI-thread geometry [code]. The libm check (`make libm-calls`, Linux only) was not run by me. Beacon's busy case must show 0 libm calls there (TESTING.md).

---

## 5. The stage machine, modes and mode switching

### 5.1 Stages, times and levels

[code, `Voice::process`; tests `exact_times`, `levels`, `part_way`]

- Stages: IDLE, ATTACK, DECAY, SUSTAIN, RELEASE. Each sample, in this order: a stage that finished on the last sample hands on to the next; then edges and a mode change may begin a stage; then the stage runs.
- Attack rises from the present level to 10 V; Decay falls to the sustain level; Sustain follows the sustain level; Release falls to 0 V. **A stage always starts from the present level and takes exactly its set time to reach its goal**, whatever the distance. The curve is the stage's full curve rescaled between that start and the goal (Tides' breakpoint rule). Decay with Sustain at 10 V, or a stage whose start equals its goal, ends on its first sample.
- Stages end exactly on their goal: Attack at 10 V, Decay at the sustain level, Release at 0 V, bit for bit, at every Shape [test: `levels`].
- **Stage length is ceil(T x rate) samples** [meas]. Stage lengths for A = D = R at Shape 0.75, with the want (T x rate) in brackets where it is not a whole number:

| Rate | T = 1 ms | T = 10.0015 ms | T = 24.5 ms | T = 245 ms | T = 1 s |
|---|---|---|---|---|---|
| 44.1 kHz | 45 (44.1) | 442 (441.07) | 1081 (1080.45) | 10805 (10804.5) | 44100 |
| 48 kHz | 48 | 481 (480.07) | 1176 | 11760 | 48000 |
| 96 kHz | 96 | 961 (960.14) | 2352 | 23520 | 96000 |
| 192 kHz | 192 | 1921 (1920.29) | 4704 | 47040 | 192000 |

  The tests accept within 1 sample at 22.05 kHz (1 ms), 44.1, 48, 96 and 768 kHz with T 1 ms, 10 ms and 1 s, 60 s at 48 kHz, at Shapes 0, 0.5 and 1; and 60 s at 768 kHz at Shape 1 (46 M samples a stage, phase never backwards) [test].
- Edge latency [meas, 48 kHz]: the rising GATE sample already starts Attack (stage ATTACK, TRIG and the A gate high, OUT 0.052 V for a 10 ms Attack at Shape 0.75). The falling GATE sample already starts Release. There is no gap between stages: Attack lasts samples 0 to 479, Decay starts at 480, Sustain at 960 (A = D = 10 ms).
- At rates under 1 kHz a 1 ms stage is under one sample and takes one sample [code]. Not a Rack rate.

### 5.2 Gate and Trig

[code; tests `gate_mode`, `trig_mode`, `retrig`, `no_jumps`]

- **Gate** (switch up, the default): a rising edge starts Attack. Attack and Decay run while the gate is high, then Sustain holds. The falling edge starts Release from wherever the envelope is, even mid-Attack. A trigger, or a gate shorter than Attack + Decay, goes straight to Release from where it got: no Decay or Sustain run. A 1 ms trigger into Gate with a 50 ms Attack reaches about 0.5 V (the guide: "a faint blip") [calc: phase 0.02 at Shape 0.75]. With Attack at 1 ms and a 1 ms gate it reaches exactly 10 V where 1 ms is a whole number of samples (48, 96, 192, 768 kHz) [test: `gate_mode`]; at 44.1 kHz the gate is 44 samples and Attack 45, so the peak is about 9.9 V [calc].
- **Trig**: a rising edge starts Attack, and **Attack always runs to the end**; a gate that falls during Attack is ignored until Attack ends. If the gate is still high after Attack, Decay and Sustain follow; if it is low, Release starts from 10 V. A trigger gives AR; a long gate gives the full ADSR. A gate that falls during Decay still releases at once. A Trig-mode Attack of 50 ms is within 1 sample of 2400 at 48 kHz [test].
- **A rising edge while the envelope runs** restarts Attack from the present level, in both modes, with no jump (the level is continuous; the slope changes). **RETRIG** does the same only while the gate is high.
- ADR is Sustain at 0 (S still goes high while the gate is high and OUT sits at 0 V); AR is Decay at its minimum with Sustain full.
- **Consequence for a square-wave gate** (the manual's Sway example): in Trig, a gate shorter than Attack falls during Attack, so Decay and Sustain never run; with a 4 s Attack and a 5.7 s gate they do [code; not run in Rack].

### 5.3 Hold (the slew limiter)

[code, `Voice::runHold`; test `hold`]

- The envelope stops. The **Sustain CV** is the signal to slew, +/-10 V; the **Sustain knob adds 0 to 10 V of offset** (`CV + 10 x knob`, held to +/-10 V). With the CV unpatched, OUT slews to 10 V x knob: **at the default knob (noon) OUT sits at 5.000 V with S high** [meas].
- OUT rises at **10 V per Attack time** and falls at **10 V per Decay time**. A -10 to +10 V step takes twice the knob's time [meas, 48 kHz, every combination of A 1 ms, 10 ms, 245 ms, 1 s with D 10 ms and 300 ms: 2 x A up, 2 x D down, exactly]. Release and Shape are unused. The Attack and Decay CVs act per channel as in the envelope modes.
- Stage gates: **A** while rising (limited upward), **D** while falling, **S** when OUT has caught the input; R is low. **TRIG** fires on each change among A, D and S [test: 4 pulses for A, S, D, S], and once on entering and once on leaving Hold [code].
- Settled means within 1 mV; it leaves settled only beyond 2 mV (hysteresis). A +/-1.5 mV dither around the output on every sample gives no gate change [test].
- **When a signal reads as S**: a sine of amplitude V passes unslewed when its steepest slope 2 pi f V is within the slew rate. With A = D = 100 ms (100 V/s) and a 5 V sine the limit is 3.18 Hz [calc]. Measured S, A and D shares in the second half of a 20 s run [meas]:

| Sine frequency | S | A | D | TRIG pulses in 20 s |
|---|---|---|---|---|
| 0.5 to 3.1 Hz | 100.0 % | 0 | 0 | 1 (the entry) |
| 3.3 Hz | 76.3 % | 11.8 % | 11.8 % | 265 |
| 4 Hz | 38.1 % | 30.9 % | 31.0 % | 321 |
| 8 Hz | 0.1 % | 50.0 % | 50.0 % | 321 |

  A slow sine the slew can follow passes unchanged to the bit [test].
- A NaN input holds the level, then the slew carries on from it [test]. +/-10 V steps every sample keep OUT within +/-10 V with exactly one of A, D, S high [test].
- GATE, RETRIG and the push button do nothing in Hold, but their edge state is kept, so a gate that is high when Hold ends does not give a second edge.

### 5.4 Mode switching while running

[code; tests `mode_switch` in `test_voice` and `test_engine`] Each voice has one level shared by the envelope and the slew, so the switch itself never moves OUT.

| From, to | What happens |
|---|---|
| Gate or Trig, to Hold | the slew starts from the present level (the first step is at most the slew's own) |
| Hold, to Gate or Trig, gate high | Attack starts from the present level and takes Attack's time |
| Hold, to Gate or Trig, gate low | Release runs from the present level to 0 V in Release's time; from a negative level it rises (-3 V to 0 V in Release's time [test]); from 0 V it ends at once (gate high for one sample) |
| Gate to Trig, or back | the running stage carries on; the new mode applies from the next edge. Gate to Trig mid-Attack with the gate already low carries on to 10 V, then Release |
| Channel count changes with the switch | in Hold the count is the Sustain CV's: four gated channels with the CV unpatched collapse to 1 (channels 2 to 4 dropped, outputs 0 V). Back in Gate they appear as new channels: idle at 0 V, then, the gate being high, a fresh Attack from 0 V; channel 1 keeps its level [test: `hold_collapses_channels`] |

Switching through all three positions every 7 ms for 2 s with the gate held and a 3 Hz Sustain CV: every step within 1.5 x the active stage's own largest step + 0.1 mV; gates coherent; R low in Hold [test]. Audio-rate switching: largest step at most 10 V/480 x 4 x 1.5 + 0.1 mV [test].

---

## 6. Stage gates and TRIG

Section 2.9 has the definitions; the checks QA can make:

- Each stage gate is high exactly while its stage runs; one gate high at a time per channel; at least one sample [test: `gates_and_trig`, at 44.1, 48 and 768 kHz].
- A long gate: IDLE, A, D, S, R, IDLE; five TRIG pulses, each 1 ms [test].
- RETRIG during a held gate that has reached Sustain: the S gate falls, the A gate rises and TRIG fires. A RETRIG during Attack restarts it: the A gate stays high and TRIG fires again. Two starts under 1 ms apart: one pulse.
- Trig mode with a trigger: gates A then R, no D or S, three TRIG pulses.
- Hold: A, D, S gates as in section 5.3.
- The A gate rising is the "strike" (Rigging STRUM); TRIG into Narrows PING gives five pings for a long gate (the manual's patch).

---

## 7. Polyphony

[code; tests `poly_independent`, `mono_cv_to_all`, `poly_cv_per_channel`, `channel_count`, `push`, `hold_collapses_channels`, `channel_counts_change`]

- **Channel count**: Gate and Trig, GATE's channels (1 if GATE is unpatched, even with the button down); Hold, the Sustain CV's (1 if unpatched). At most 16. Every output carries that count (all seven outputs, set every sample, whether patched or not).
- **A polyphonic CV or RETRIG cable** gives each channel its own value; a **mono** cable applies to every channel; channels past a polyphonic cable's count read 0 V (no change for a time CV; no change for the Sustain CV in Gate and Trig; in Hold, 0 V plus the offset). Measured: GATE 4 channels, RETRIG 8, Attack CV 2 channels (channel 2 at -5 V) gave four channels with OUT 6.428, 7.178, 6.428, 6.428 V after 100 ms at Attack 245 ms: channel 2's shorter Attack is further along; channels 3 and 4 read 0 V and match channel 1 [meas]. A RETRIG cable wider than GATE affects only the first n channels.
- **Each channel equals a lone Voice fed the same input, bit for bit**, for 16 channels with different gates, mono CVs, polyphonic CVs, and a channel count that changes [test].
- **Channels that appear** (the count rising, on a patch or on a mode switch) start idle at 0 V; a gate that is already high then starts a fresh Attack from 0 V. Channels that go away are dropped (their outputs 0 V) and reset if they come back. A GATE cable changing 4 to 7 to 2 to 4 channels mid-run: surviving channels carry on, new ones start fresh [test].
- **The push button** gates every channel, with GATE patched or not (GATE 16 channels all low and the button down: channels 1 and 16 both at 6.428 V after 100 ms, so all 16 attack together [meas]).
- **Window, lights, published state**: channel 1 only.
- **Hold**: one slew per channel of the Sustain CV.
- A mono RETRIG with a polyphonic GATE applies to every channel.

---

## 8. The window

[code, `EnvelopeView.hpp`, `Beacon.cpp`; tests `test_view`] A 42.0 x 17.0 mm widget at (4.4, 19.0), drawn in vectors on the self-illuminated layer (layer 1) in the theme's cyan, so it stays visible when Rack's room lights are dimmed. It has no mouse handling. It is **not** a meter and not a ruler: it draws the shape the knobs make now and where channel 1 is in it. No automated test draws it, and `make smoke` shows only the default panel, so every drawing statement below is [unverified] in Rack; the geometry behind it is tested offline.

### 8.1 Geometry

- Margins: 0.8 mm padding plus 0.6 mm inside it, so the plot is **39.2 mm wide**. The letters (A D S R) sit in a 2.7 mm band at the bottom; the plot is 11.9 mm high (from 1.6 mm to 13.5 mm down the window).
- **Time axis, not to scale.** Each stage's width weight is `1 + log10(T / 1 ms)` for A, D and R (1.000 at 1 ms, 2.000 at 10 ms, 4.000 at 1 s, 5.778 at 60 s); Sustain has a fixed weight of 2.5. Widths are the weights over their sum. Widths in mm [meas]:

| Times (A, D, R) | A | D | S | R |
|---|---|---|---|---|
| 10 ms, 300 ms, 1 s (defaults) | 6.55 | 11.38 | 8.18 | 13.09 |
| all 1 ms | 7.13 | 7.13 | 17.82 | 7.13 |
| all 60 s | 11.42 | 11.42 | 4.94 | 11.42 |
| A, D 1 ms, R 60 s | 3.81 | 3.81 | 9.53 | 22.04 |
| A, D 60 s, R 1 ms | 15.04 | 15.04 | 6.51 | 2.60 |
| all 245 ms (noon) | 10.49 | 10.49 | 7.74 | 10.49 |

  The narrowest a stage can be is 2.60 mm (a 1 ms stage beside 60 s ones). Every stage stays visible and inside the window at every knob setting [test: `widths`].
- The times are the knob plus **channel 1's CV** (a connected CV jack's channel 1; NaN reads 0). Sustain is the knob plus channel 1's Sustain CV / 10, clamped. So the window follows CV moves, and shows the shape of channel 1's envelope.
- **Heights**: Attack rises from 0 to 1 (10 V), Decay to the sustain fraction, Sustain flat, Release to 0, each stage in its Shape curve (the knob's value, not the 5 ms smoothed one). Each stage is drawn from 48 segments (49 points).

### 8.2 What is drawn

| Element | Drawing |
|---|---|
| Baseline | faint line (alpha 0.18, 0.6 px) under the whole envelope |
| The knobs' envelope | each stage its own stroke, dim (alpha 0.38, 1.1 px). A running Sustain above 0 V is drawn full-bright (1.6 px) |
| The running Attack, Decay or Release | bright (1.6 px) from the level where it really started to its goal, along the Shape curve, over its live span (below). The knobs' shape stays dim as the reference |
| The dot | radius 0.75 mm, bright, on the bright line at the stage's phase; idle: radius 0.55 mm, dim, at the start (bottom left) |
| Letters A D S R | under their spans; the running stage's letter is bright, the rest dim |
| Sustain at 0 V | the Sustain span stays dim; while in Sustain the dot sits at the end of the line (bottom right) |
| Idle | the shape drawn, no stage lit, the dot at rest at the start |
| Module browser (no module) | the default envelope (10 ms, 300 ms, S 5 V, 1 s, Shape 0.75), idle |

- **Live span** (issue #76, builds 2.0.36 and 2.0.37): the bright stage ends where its own span ends. If it skipped earlier stages (Release straight after Attack or Decay), it starts where the last stage was cut off: that stage's phase across its span (Attack or Decay), or the end of the Sustain span. A retrigger's Attack, and a stage from idle, stay on their own span. Examples: a gate falling half-way up Attack draws Release from mid-Attack span down to 0 V at the right edge; a gate falling 1.3 ms into a 300 ms Decay draws Release from just after the end of Attack [test: `live_span_after_skip`, the user's case, phase 0.0043]. Leaving Hold counts as from idle.
- **Start and goal** of the running stage are the levels latched when it began. Decay's goal is the sustain level at that moment, so a Sustain move during Decay leaves the bright Decay ending at the old level, as the voice does (section 2.4). Release after Hold from a negative level is drawn clamped at 0 V.
- **The dot is on the bright line by construction** (it uses the same function), so it cannot disagree with the line [test: `live_dot_on_line`, whatever level was published beside it]. Issue #76 reported the line and dot as misaligned; 2.0.37 closed it.
- **Hold**: instead of the envelope, a scrolling line of channel 1's output, +/-10 V filling the plot, 0 V a faint line, the newest point a dot, and the word "HOLD" at the top left. **160 points at 60 points a second of the UI frame clock = 2.67 s of history**, so it scrolls at the same speed at 60 and 144 Hz [test: `hold_ring_time`]. Entering Hold fills the line flat at the present level. A stall is held to one ring's worth.
- Torn reads: stage, phase, start, goal, previous stage and previous phase are separate relaxed atomics, so for one frame the dot can be a sample out of step with the line (clamped, drawn anyway). The published state is lock-free (`static_assert`, and a WARN in Rack's log at construction if not) [test, code].

---

## 9. CPU, sample rates and robustness

### 9.1 CPU

There is no silence sleep; the cost is per sample, flat across a voice's stages, and linear in rate. `bench` cases [code, `bench.inc`], 48 kHz:

- **idle**: one voice, nothing patched, defaults.
- **outputs**: one channel, a 2 Hz gate (a quarter second high every half second).
- **busy**: 16 channels, gates staggered a sixteenth of a period apart, RETRIG pulsing on every third channel, every CV moving, Shape 0.8.

| Case | Budget, Apple arm64 | Budget, other (provisional) | TESTING.md (builder's run) | Mine, 1 instance [meas] | Mine, 16 instances |
|---|---|---|---|---|---|
| idle | 0.25 | 0.75 | 0.121 | 0.134 to 0.164 | 0.143 to 0.172 |
| outputs | 0.4 | 1.2 | 0.182 | 0.190 to 0.232 | 0.202 to 0.233 |
| busy (16 voices) | 2.05 | 6.15 | 1.013 | 1.09 to 1.37 | 1.11 to 1.38 |

Units: % of one core per instance at 48 kHz. Mine: `build/bench/bench beacon <case> <1|16> 4..5`, several runs, an Apple M-series Mac **under load** (load average 8 to 10 from other work), so the figures are above the builder's. The 16-instance cost per instance equals the single cost. All figures are inside the budgets. Per sample: idle 28 to 34 ns, outputs 40 to 48 ns, busy 227 to 285 ns for 16 voices (14 to 18 ns per voice). Scaling by rate [calc]: busy about 1.1 % at 48 kHz is about 2.2 % at 96 kHz and 4.4 % at 192 kHz; idle 0.14 % becomes 0.55 % at 192 kHz. **Windows CPU figures are not covered by any automated budget.** Rack's CPU meter includes Rack's own per-module overhead [unverified].

### 9.2 Sample rates

[code; tests `extreme_rates`, `rate_changes_mid_stage`, `rate_change_carries_the_stage`, `sample_rate_change`]

- Accepted: any rate with 1 Hz <= rate <= 1e8 Hz. Ignored, keeping the last good rate: zero, negative, NaN, infinity and above 1e8 [test: 0, -48000, NaN, inf, 1e30].
- A rate change keeps every voice's stage, phase and level. 48 kHz to 96 kHz in the middle of a 100 ms Attack: no step (at most 1.5 x the stage's own step + 0.1 mV) and the rest of Attack takes 50 ms (4800 samples) [test].
- All three modes stay finite and within +/-10 V at 1 Hz, 8 kHz, 44.1 kHz and 768 kHz with 16 channels of GATE and Sustain CV toggling and minimum times [test].
- Stage times are in seconds, so they do not change with the rate; they are quantised to whole samples (section 5.1). The TRIG pulse is 1 ms to the nearest sample.

### 9.3 Hostile input and numerical safety

- NaN, +/-infinity and +/-1e30 on every param and every port, one at a time and all together, with a gate running, in every mode: every output finite and within +/-10 V, INV exactly -OUT, gates and TRIG 0 or 10 V, nothing past the channel count [test: `bad_values_everywhere`]. After the bad values are gone an ordinary gate gives an ordinary envelope [test: `recovers_after_nan`]. MODE out of range (7 and -3) is handled [test: `mode_out_of_range`]; in the code a NaN Mode reads Gate (the comparisons are false) [code].
- A NaN knob reads noon; a NaN time keeps the last good time; a NaN Shape keeps the last; a NaN sustain keeps the last; a NaN Hold input holds the level; NaN GATE and RETRIG read low [code].
- GATE and RETRIG toggling every sample, every other sample and at random on 16 channels; channel counts changing every block [test].
- Seeded fuzz: three seeds (2026, 1, 99), 10^6 samples each, of knobs, CVs, gates, modes, channel counts, rates (1 Hz to 768 kHz) and resets, 1 % of port values NaN or infinite [test].
- No allocation in `process()`, `setSampleRate()` or `reset()`: 0 allocations in 400,000 samples at 16 channels with mode, count and rate changing [test, counting operator new].
- No writable global state: `make engine-globals` passes on Beacon's declaration [test, my run].

---

## 10. Open issues

No open issue is filed against Beacon (checked with `gh issue list --state all --limit 200`, 2026-11-24).

| # | State | Relevance |
|---|---|---|
| 76 | closed 2026-11-23 | "Beacon: the window's Release line and the dot are still misaligned". Explains the present drawing (section 8.2): the bright running stage now starts where the previous stage was cut off. Fixed in 2.0.36 and 2.0.37. Retest against the user's repro (Gate, A 4.64 s, D 300 ms, S 5 V, R 6 s, gate from a Sway square, 39.4 s cycle, PW 11.78 %, so the gate falls as Attack completes) |
| 64 | open, tech debt | "Polyphony: modules should be polyphonic where it makes sense". The spec says it names Beacon; Beacon is polyphonic by design and needs nothing from it |
| 67 | open, tech debt | Aliasing measured for every module. Beacon is recorded "not needed" in TESTING.md and has no bound |

Found while preparing this document, **not filed** (the maintainer should decide; no issue was opened from this task):

1. `test_view.cpp:178` compares `dot()` and `livePoint()` with exact equality, and fails 4 of its checks (p 0.4, 0.5, 0.65, 0.95 of the 21 steps) when built with the plugin's flags (`-O3 -funsafe-math-optimizations`; also `-O2 -funsafe-math-optimizations`). The two differ by one or two float ulps (3e-8 to 6e-8 of the window width, depending on inlining) [meas]. `make test` builds at `-O2` without fast-math and passes. It is a fragile test, not a drawing defect. TESTING.md rule 9 says the plugin and its tests should share arithmetic flags.
2. A Shape move during a long stage slides the level by up to 8.75 V in tens of ms (section 1.1); the test pins only the 10 ms Attack.
3. The manual does not say the latched push button is saved with the patch (section 3).

---

## 11. What the automated suite covers

All of the following passed in my run (section 14). Six programs: `test_timing`, `test_voice`, `test_engine`, `test_view`, `test_switch` and `test_hostile` (built with ASan and UBSan, `-O1`). I built them twice: as `make test` does (`-O2`, `-Wall -Wextra`), where all six passed; and with the plugin's flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; hostile `-O1` with the sanitizers), where five passed and `test_view` failed 4 checks (section 10, item 1). Also passed: `make bench-modules` (15 module declarations complete), `make engine-globals`, `tools/test_gen_panels.py` (115 tests). `make libm-calls` skipped (Linux only); `make smoke` not run (needs Rack); the full `make test` (every module) not run.

### 11.1 test_timing (Timing.hpp, Curve.hpp)

| Test | Guarantees |
|---|---|
| time_law | knob 0 is 1 ms, 1 is 60 s, noon 244.9 ms; the three defaults within 0.5 % of 10 ms, 300 ms, 1 s; out-of-range knobs clamp; NaN knob reads noon; knob to seconds to knob within 1e-4 |
| cv | +10 V from 0 is 60 s; -10 V from 1 is 1 ms; 5 V is half the travel; 1 V a tenth; NaN volts ignored; the sum clamps |
| typed | parsing of the forms in section 2.3 (and rejections); the text of every 0.01 knob step reads back within one display digit, and from 10 ms up within 0.5 %; fixed strings ("1.0 ms", "1.00 s", "60.0 s", "300 ms"); a short buffer is not overrun |
| curve_ends, curve_ends_odd | both ends exact at every Shape including odd and out-of-range values; stays within 0..1 |
| curve_linear_noon | noon linear within 1e-6; just either side of noon already bent |
| curve_bends | classic rise at p 0.25 above 0.6; reverse rise below 0.01; reverse fall late; monotonic; matches the formula to 1e-6 |

### 11.2 test_voice (Voice.hpp)

| Test | Guarantees |
|---|---|
| exact_times | each stage within 1 sample of T x rate at 22.05 (1 ms), 44.1, 48, 96, 768 kHz, T 1 ms, 10 ms, 1 s, 60 s at 48 kHz, Shapes 0, 0.5, 1; phase never backwards or past 1; 60 s at 768 kHz at Shape 1 |
| sample_rate_change | stage, phase and level kept; the rest takes the right time; bad rates ignored |
| levels | A ends at exactly 10 V, D at exactly the sustain level, R at exactly 0 V, at five Shapes; Sustain holds within 1e-6 V for ten minutes |
| part_way | Release from 5 V at 2 s takes 2 s; retrigger at 8 V reaches 10 V in Attack's time; Decay to 10 V ends at once |
| gate_mode | a short gate releases from where Attack got, no Decay or Sustain; 1 ms gate with 1 ms Attack peaks at 10 V |
| trig_mode | a trigger gives A, R, idle with a full Attack (within 1 sample of 2400) and a 10 V peak; a long gate gives the full ADSR; a gate ending in Decay releases |
| retrig | RETRIG high restarts Attack from 5 V and takes 480 samples; RETRIG low does nothing; a rising GATE while releasing restarts Attack within 0.25 V; RETRIG in Trig mode |
| no_jumps | retrigger mid-Attack, mid-Decay, mid-Release; gate ending in Attack and in Decay; Attack time swept 1 ms to 60 s and back; Shape swept mid-stage: every step at most 1.5 x the undisturbed stage's own largest step + 0.1 mV (at Shapes 0, 0.5, 0.75, 1) |
| sustain_follows | the 2 ms follower; the 5 Hz LFO within 0.5 dB; noise on Sustain stays in 0..10 V |
| gates_and_trig | gates coherent with the stage; IDLE A D S R IDLE; five TRIG pulses each 1 ms; merge of starts 0.3 ms apart; one-sample stage gate |
| hold | rise in A's time, fall in D's; negative input followed; clamp at +/-10 V; slow sine passes unchanged; hysteresis (1 mV, 2 mV); no chatter on dither; NaN holds; four TRIG pulses; mode entry and exit rules |
| mode_switch | Gate to Hold, Hold to Gate/Trig high and low, Gate to Trig and back, audio-rate switching |
| nan | every field NaN, infinite or 1e30 in every mode: OUT finite and within +/-10 V |

### 11.3 test_engine (BeaconEngine.hpp)

| Test | Guarantees |
|---|---|
| poly_independent | 16 channels with different gates each equal a lone Voice bit for bit |
| mono_cv_to_all, poly_cv_per_channel | mono CVs and RETRIG apply to every channel; poly CVs per channel; missing channels read 0 V |
| channel_count | GATE 4 to 7 to 2 to 4 channels; new channels start at 0 V idle; the count follows GATE's, Hold's Sustain CV's, 1 unpatched, capped at 16 |
| hold_collapses_channels | Gate to Hold to Gate with four held channels (section 5.4) |
| push | the button gates every channel, GATE patched or not |
| schmitt | 0.1 V / 1 V per channel; unpatched GATE reads low; a replug at 0.5 V is no edge |
| mode_switch | every 7 ms for 2 s through all modes; Hold to Gate with the gate low from -3 V |
| inv_exact | INV = -OUT bit for bit, including negative OUT in Hold |
| published, published_start_goal, published_prev_stage | channel 1's stage walk A D S R IDLE, phase 0..1, level equals OUT, mode, start and goal latched, the user's #76 case (Decay cut off at 0.0043, Release from about 9.95 V) |
| stage_lights | one sample of Decay lights D for 40 ms; a longer stage goes out by the first update after it ends; reset puts all out; the hold covers a 30 Hz frame |
| rate_change | 48 to 96 kHz mid-Attack: no step, Attack ends at its time in seconds |
| shape_smoothing | Shape 0 to 1 mid-Attack on the default 10 ms Attack: largest step at most 1.5 x own + 0.1 mV; the settled Shape equals the target exactly |

The file also `static_assert`s that the published atomics are lock-free.

### 11.4 test_view (EnvelopeView.hpp)

Widths grow with log time and stay inside the window; heights match the curve; the dot's position per stage and phase; `live_stage_curve` (start at p 0, goal at p 1, the voice's curve between); `live_dot_on_line` (exact equality; **fails 4 checks with the plugin's flags**); `live_span_after_skip` (#76); `sustain_zero`; `hold_ring_time` (3 s at 60 Hz, at 144 Hz and at a jittery 30 to 50 Hz write the same number of points).

### 11.5 test_switch (Toggle3Step.hpp)

The click table (every value and half); no wrap (one click from Trig to Gate, never through Hold; clicking one half repeatedly walks to that end and stays); halves; vertical drag per 12 px, held at the ends, a drag back moves at once, no click on release after a whole step; NaN travel ignored.

### 11.6 test_hostile

Section 9.3.

### 11.7 Not covered by the automated suite

- **Everything in `Beacon.cpp` that needs Rack**: drawing of the window (layer, colours, strokes, glow, the font and letters), the Hold line's live use of Rack's frame time, the lights' brightness path, `ShapeQuantity` and `TimeQuantity` (the tooltip and typing as Rack calls them; the parse and format helpers are tested), bypass and un-bypass, the Initialize path, the module browser's default drawing.
- The `Toggle3` widget with a real mouse (press halves, drag, undo, the 12 px drag) and the push button's shift-click latch.
- Rack's save and load of the push button, Randomize, and polyphonic cables as Rack hands them (`getVoltages()` content past the channel count).
- The libm check (Linux only), a native x86-64 or Windows CPU budget, `make smoke` (not run by me).
- Aliasing, by design (no bound measured).
- The Rack host's order of events on a sample-rate change, and any block-size effect.
- Shape moves mid-stage on a long stage (section 1.1).
- Whether the user's patches (Fathom, Sluice, Narrows, Rigging, Fog) behave as the manual's "Patches to try".

---

## 12. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, all knobs at default (Shape +50 %, A 10 ms, D 300 ms, S 5 V, R 1 s), Mode Gate, nothing patched except where stated. A scope is needed for times; the stage gates and TRIG are the easiest timing references.

**Method notes.** Time a stage with its stage gate on a scope rather than by eye. For Release from a level, expect the same time as from 10 V (stages are timed). For an Attack under 5 ms, use the A gate's width, not the OUT slope.

### 12.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Time tooltip | hover A, D, R at knob 0, default, noon, 1 | 1.0 ms, 10.0 ms / 300 ms / 1.00 s, 245 ms, 60.0 s (section 2.3) |
| Time law | gate held long, A at each of 1 ms, 10 ms, 100 ms, 1 s, 10 s, 60 s (type the values); time the A gate | within 1 sample at 48 kHz (exactly ceil(T x 48000)); same time at Shape left, noon and right |
| Typed times | type `250`, `250 ms`, `1.5 s`, `90 s`, `abc`, `0.5`, `1e3` into each | 250 ms; 250 ms; 1.50 s; 60 s; unchanged; 1.0 ms; 1.00 s |
| Time CV | +1 V, +2 V, -1 V into A with the knob at noon; time Attack | about 736 ms, 2.21 s, 81.5 ms (x3.005 per volt); +5 V and above 60 s; -5 V and below 1 ms; a CV on a mono cable also applies to all channels |
| Sustain | gate held long; turn S from 0 to full | OUT holds at 0 to 10 V; S at 0 is ADR (Release from where Decay ended); moving S during Sustain glides in about 10 ms with no click |
| Sustain CV | +/-5 V LFO on the Sustain CV in Gate or Trig | the level follows (tremolo); the sum held to 0..10 V (never below 0 V) |
| Shape | A 2 s, R 4 s; Shape left, noon, right (section 2.6) | Left: slow start, fast finish; decay holds then falls. Noon: straight. Right: fast, rounded Attack; fast-then-long-tail fall. The window's curve follows; the stage time is unchanged |
| Shape tooltip | hover | "Linear" only at exactly noon; the default reads +50 %; ends -100 % and +100 % |
| Shape move mid-stage | 10 s Attack, turn Shape fully across while it runs | the level slides by up to about 8.75 V within tens of ms; no click; record the figure (section 1.1) |
| Mode switch | click the upper and lower halves, drag, double-click, undo | steps one position by half, never wraps (Trig to Gate in one click, never via Hold); stops at the ends; drag 12 px per step; one undo step per gesture; double-click does not reset [unverified] |
| Push button | hold, release; shift-click | a gate on every channel while held; shift-click latches until the next click; does nothing in Hold |

### 12.2 Modes

| Procedure | Pass criteria |
|---|---|
| Gate: long gate, then a gate shorter than Attack (A at 1 s, tap) | long: A, D, S, then R on release; short: Release from where Attack got, no D or S |
| Gate: 1 ms trigger, A at 50 ms; A at 1 ms | a faint blip (about 0.5 V), then Release; with A at 1 ms a sharp pluck near 10 V (about 9.9 V at 44.1 kHz) |
| Trig: the same trigger, A at 50 ms | Attack runs to the top, then Release (AR); lights A then R |
| Trig: a long gate | full ADSR |
| Trig with a gate shorter than Attack, then longer | shorter: no Decay or Sustain; longer: both run (the manual's Sway example: a 4 s Attack with a 5.7 s gate gives D and S; a 6 s Attack does not) |
| New gate while releasing, both modes | Attack restarts from the level: no jump, no drop to zero; the Attack takes the full set time |
| RETRIG, gate high; RETRIG, gate low | high: Attack restarts from the present level; low: nothing happens |
| ADR, AR | S at 0: A, D, then S gate high while the gate is high with OUT at 0, then R; D at its minimum and S full: AR |
| Hold: slow LFO then steps into the Sustain CV | OUT follows, rising at A's time per 10 V and falling at D's; A, D, S gates as in section 5.3; R low; the window shows the scrolling line |
| Hold with nothing patched, S at default | OUT 5.000 V, S gate high, one channel [meas]; S fully left: 0 V |
| Hold: -10 to +10 V step with A = 1 s | 2.00 s up; with D = 300 ms, 0.60 s down |
| Hold: button, GATE, RETRIG | nothing happens |

### 12.3 Mode switching while running

| Procedure | Pass criteria |
|---|---|
| Gate to Hold to Trig to Gate (down, down, up, up) during a held gate | OUT never jumps; Hold to Gate with the gate low releases from where it was to 0 V in Release's time; with the gate high, Attack from the level |
| Switch Gate to Trig mid-Attack with the gate already low | Attack carries on to 10 V, then Release |
| Four-channel gate into GATE; switch to Hold with the Sustain CV unpatched; back | one channel in Hold (outputs 2 to 4 at 0 V); back in Gate three new channels start from 0 V |

### 12.4 Stage gates and TRIG

| Procedure | Pass criteria |
|---|---|
| scope A, D, S, R and TRIG on a long gate | each gate 10 V while its stage runs; five TRIG pulses of 1 ms (0.998 ms at 44.1 kHz) at the start of A, D, S, R and the return to idle |
| Trig-mode trigger | three TRIG pulses; gates A then R |
| stages under 1 ms apart | one longer TRIG pulse |
| Decay with Sustain at 10 V | D gate high for one sample |
| TRIG into Narrows PING; A gate into Rigging STRUM (manual) | five pings; a strike as Attack starts |

### 12.5 Lights

| Procedure | Pass criteria |
|---|---|
| gate with A, D, R at 5 ms | each light visible for about 40 ms; A and D lights overlap |
| A, D, R at 500 ms | each light on for its stage (about 500 ms) |
| poly gate, channel 1 low, channel 2 high | lights stay out (channel 1 only) |
| bypass during a stage; un-bypass | lights out; nothing resumes; a gate still high starts Attack from 0 V |

### 12.6 The window

| Procedure | Pass criteria |
|---|---|
| module browser | default envelope drawn, idle, no crash |
| defaults | A short, D longer, flat S, R longest; the letters A D S R under, all dim; the dot at the bottom left |
| turn A from 1 ms to 60 s with D and R at 1 ms | A grows from 7.1 mm to 22.0 mm of the 39.2 mm plot; every stage stays visible |
| gate held | the running stage and its letter bright; the dot rides the bright line; Sustain bright while it runs |
| gate falls half way up Attack | Release is drawn from that level at that point in the Attack span down to 0 V at the right edge, the dot on it [issue #76] |
| retrigger from Release | Attack drawn over its own span from that level up to 10 V |
| Sustain 0 V | the Sustain span dim; the dot at the end of the line during Sustain |
| CV on A, D, R, S | the window's widths and heights follow channel 1's CV |
| Hold | a scrolling line, about 2.7 s across, 0 V line faint, "HOLD" top left; the same speed at 60 and 144 Hz; entering Hold starts it flat at the level |
| room lights dimmed | the window stays visible (self-illuminated layer); dim strokes still show |
| Shape knob moved | the drawn curve follows at once |

### 12.7 Polyphony

| Procedure | Pass criteria |
|---|---|
| 4-voice gate cable into GATE | four envelopes; OUT, INV, the gates and TRIG each carry 4 channels |
| mono CV into A with a poly gate; then a poly CV | mono: the same time for every voice; poly: each its own |
| poly CV with fewer channels than the gate | the extra voices read 0 V on that CV |
| change the gate cable's channel count while voices play | voices that stay keep going; new ones start at rest |
| Hold with a poly V/OCT into the Sustain CV | one slew per channel; count follows the CV |
| 16 channels | no crash; CPU about 1.1 to 1.4 % of an M-series core (section 9.1) |
| window and lights with voice 1 idle, voice 2 running | window idle, lights out |

### 12.8 Patch and host

| Procedure | Pass criteria |
|---|---|
| set every knob and the switch, save, quit, reopen | everything comes back; the envelope starts idle at 0 V |
| latch the button, save, reopen | the button's state comes back [unverified]; a gate on every channel |
| Initialize | defaults back; every envelope at rest at 0 V |
| change the sample rate (44.1, 48, 96 kHz) while a long envelope runs | it carries on where it was; times stay the same in seconds |
| bypass | outputs fall silent [unverified]; un-bypass starts clean |
| duplicate the module | copy carries all settings; instances independent |
| 44.1 kHz | stage lengths ceil(T x 44100): 1 ms is 45 samples; TRIG 0.998 ms |
| Randomize | the Mode switch may change [unverified] |

### 12.9 Cross-check of the listening guide

I compared `beacon-2.0.34-listening.html` with the 2.0.39 code. Nothing in it is wrong about behaviour. Notes:

- The guide was written for 2.0.34. Since then: the switch no longer wraps; tooltip digits changed (10 to 99 ms now show three digits); the window draws the running stage from where the last stage was cut off; the light hold counts from the stage's start; bypass resets. Its window items ("the bright line", "dot rides the line") describe 2.0.34's drawing, which #76 corrected.
- "Hold: turn S fully left (no offset)" is right, and the reason matters: the default S is 5 V, which in Hold is an offset of 5 V (section 5.3).
- "Move the switch GATE, HOLD, TRIG, GATE": with the 2.0.35 switch this is two down-clicks then two up-clicks, and works.
- "Shape tooltip: reads 'Linear' only at exactly noon" matches the code; a snap zone is not implemented.
- "A CV into each time's jack: adds to the knob, 10 V for the whole travel": correct; 1 V is a factor of 3.005 in time.
- "Change the gate cable's channel count... voices that stay keep going; new ones start at rest": correct.
- The guide does not say that a mid-stage Shape move slides the level (section 1.1), or that the latched button is saved.

---

## 13. Glossary

| Term | Meaning |
|---|---|
| ADSR, ADR, AR, AD | Attack, Decay, Sustain, Release; ADR is Sustain 0; AR and AD are an Attack and a Release (or Decay) with no Sustain |
| Attack, Decay, Release | the timed stages; each takes its set time, 1 ms to 60 s |
| Breakpoint rule | Tides' rule: a stage starting part way rescales its curve between the present level and its goal |
| Channel | one voice of a polyphonic cable; at most 16 |
| Classic, reverse | Shape's curves: 1 - (1 - p)^4 (right) and p^4 (left) |
| Gate (stage gate) | the A, D, S or R output: 10 V while that stage runs |
| Hold | the third mode: a slew limiter on the Sustain CV |
| Hysteresis | Hold's settled test: within 1 mV to enter, beyond 2 mV to leave |
| Idle | no stage running; OUT at 0 V |
| Phase | a stage's progress, 0 to 1, linear in time |
| Schmitt trigger | a threshold with two levels (here 1 V up, 0.1 V down) |
| Shape | the one curve knob for every stage |
| Slew | to move toward a target at a limited rate |
| TRIG | a 1 ms, 10 V pulse at the start of every stage |
| Window | the display that draws the envelope with a dot riding it |

---

## 14. The measurement programs

All in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/beacon/meas/`, built against the extracted `2e6e960` tree (`.../beacon/tree/src`) with `c++ -std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -Itree/src`. Outputs are in `.../beacon/out/`.

- `meas_time.cpp`: the time law, tooltip text, typed values, CV law, curves and the window's widths (sections 2.3, 2.6, 8.1).
- `meas_voice.cpp`: stage lengths in samples at 44.1 to 192 kHz, edge latency, TRIG width at 8 to 768 kHz, the Sustain follower, Sustain moved during Decay, Hold slew times and the Hold S/A/D shares of a sine (sections 2.4, 2.9, 5).
- `meas_light.cpp`: the stage lights' on-time, a Shape move on a 10 s Attack, Hold with nothing patched, and the channel-count and Schmitt rules through `BeaconEngine` (sections 2.6, 2.10, 5.3, 7).
- `meas_misc.cpp`: the tooltip's error from 10 ms up and below, and the largest level shift of a Shape move.
- `../view_ulp.cpp` and `../tv_copy.cpp` (a copy of `test_view.cpp` with the failing equality printed): the `test_view` fast-math finding.
- Tests are built in `tree/build/test/beacon/` (as `make test` does) and `pf/` (the plugin's flags). The bench is `tree/build/bench/bench beacon <idle|outputs|busy> <1|4|16> <seconds>`.

The programs drive `rka::BeaconEngine` and `rka::beacon::Voice` as `Beacon.cpp` does (one frame per sample; for the lights, `BeaconLights` called as the module calls it: `note()` every sample, `update()` every 16 samples). They do not exercise Rack's ports, threading, drawing, mouse or the host's order of events. Anything marked [meas] should therefore match Rack at 48 kHz, but Rack-side effects are outside them.
