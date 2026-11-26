# Beacon 2.0.45: Technical Reference for QA

Module: **Beacon**, plugin `rka` (slug `rka`, module slug `Beacon`), VCV Rack 2 (minRackVersion 2.5.0). Tags in plugin.json: Envelope generator, Polyphonic, Slew limiter.
Build: **2.0.45 (this branch: commit `e162fa4` plus the uncommitted AD change and its version bump)** of the RkaModules repository, branch `beacon-ar-resume` (off `fix-waves`; neither merged to main). `e162fa4` is build 2.0.44 (plugin.json `"version": "2.0.44"`), which shipped the AR middle mode; the AD change that makes 2.0.45 is in the working tree (`src/beacon/Voice.hpp`, `EnvelopeView.hpp`, `BeaconEngine.hpp`, `Beacon.cpp`, the three test files, `tools/gen_panels.py` and the generated panels, the manual and the spec), not yet committed or bumped when I read it. Everything here was taken from that working tree; line numbers are from it.
Package on the share: none yet for 2.0.45. The share's `rka-2.0.44-win-x64.vcvplugin` (2026-11-25 15:59) is the AR build and does not behave as described here. Beacon has no factory presets.
Document date: 2026-11-25. Replaces the 2.0.44 edition of this reference (same day), which replaced the 2.0.39 reference (2026-11-24).
Spec: `docs/specs/2026-11-23-beacon-design.md` (the "Revised 2026-11-25" block, with its "Later the same day: the middle position is AD, not AR" entry, holds this build's decisions). Manual: `docs/beacon/README.md` (rewritten for ADSR / AD / HOLD). Listening guide (a cross-check, not a source): `docs/beacon/listening.html`, being revised for 2.0.45 alongside this document (uncommitted when I read it).

## Build history

Beacon was designed, built and listened to in one day, 2026-11-23, and reworked twice on 2026-11-25 (2.0.44, then 2.0.45). The test builds that changed it (CHANGELOG.md, `git log` for the module's paths):

| Build | Commit, time | What changed in Beacon |
|---|---|---|
| 2.0.34 | `4f8ac5a`, 11:50 | First test build: the engine, Gate / Trig / Hold, the stage gates and TRIG, the window, the three-position switch, OUT and INV. The first listening guide (`beacon-2.0.34-listening.html`) was for this build. |
| 2.0.35 | `42d9d5c`, 14:00 | Listening-stop and final-review fixes: the window draws the running stage from where it really started (the engine now publishes start and goal levels); Sustain at 0 V (span dim, dot at the end of the line); the stage lights hold from the stage's start and update every 16 samples; the switch steps by direction and never wraps (`Toggle3Step.hpp`, `test_switch`; in 2.0.34 it wrapped, so Trig to Gate passed through Hold); bypass and un-bypass reset every voice; tooltip times show three digits from 10 ms up (2.0.34 showed whole milliseconds from 10 ms, up to 5 % off); the Hold line scrolls by the frame clock (60 points a second, `HoldRing`); manual fixes. |
| 2.0.36 | `8e50405`, 14:52 | Issue #76: a stage that skipped others is drawn from where the last stage ended (the engine publishes the previous stage). Superseded in 2.0.44. |
| 2.0.37 | `f811166`, 15:20 | Issue #76: the running stage starts where the last stage was *cut off* (the engine also publishes how far that stage had got). #76 closed; PR #75 merged at 15:33. The widening was removed in 2.0.44; the engine still publishes the previous stage and its phase, but the window no longer reads them. |
| 2.0.38, 2.0.39, 2.0.40 | `6f1b81c`, `2e6e960`, `81bc24b` | Neap and Spring only. No Beacon change. The previous edition of this reference described 2.0.39. |
| 2.0.41 | `8ef841d`, 2026-11-25 08:29 | **Shape breakpoint** (`b66dbf8`, issue #83): a Shape move during a stage no longer slides the level; the rest of the stage becomes the new Shape's curve rescaled from the level and phase where it changed (Tides' rule), still ending at its set time. The engine publishes channel 1's breakpoint phase and level (`bpPhase`, `bpLevel`) and the window draws the bright line from it. New tests `shape_long_stage` (test_engine) and `live_breakpoint` (test_view). Also `54de7e7` (issue #84): `test_view` compares the dot with its line within 1e-6 instead of exactly, so it passes with the plugin's flags. |
| 2.0.42, 2.0.43 | `5004a95`, `1618d6e` | Rigging and Wake only. No Beacon change. |
| 2.0.44 | `f15953b` (15:37), `24cf724` and the bump `e162fa4` (15:57), 2026-11-25 | **ADSR / AR / HOLD rework** (the AR middle mode is superseded in 2.0.45, next row). The switch reads ADSR / AR / HOLD (param values 0, 1, 2 unchanged, so saved patches keep their position: a patch saved on Trig opens on AR). **AR is a strict one-shot**: a rising edge runs Attack to 10 V, then Release, whatever the gate does; Decay and Sustain never run (in Trig, a gate still high when Attack ended ran Decay and Sustain). **Attack resumes along its curve**: an Attack begun from a level in 0..10 V picks up its full 0-to-10 V curve at the point with that level and takes only the time left (from 6 V: 40 % of Attack at Shape noon, about 64 % at the default); Decay and Release still take their full time from wherever they start. **The window** draws what the mode will run (AR: only A and R) and every running stage over its own span only; the "skipped stages" widening of 2.0.36 and 2.0.37 is gone. Panel legend GATE / TRIG / HOLD became ADSR / AR / HOLD. New tests: test_voice `ar_mode`, `attack_resumes`; test_view `window_lines_up`, `ar_layout`, `live_span_own`; updated: test_voice `part_way`, `retrig`, `mode_switch`; test_engine `mode_switch`, `inv_exact`, `published`. Manual and spec rewritten for it. `24cf724`, in review: a switch to AR mid-Decay or in Sustain releases at once (it had carried on until the gate fell, stranding the window's dot at the A/R join). |
| 2.0.45 | working tree on `e162fa4`, plus the bump, 2026-11-25 | **The middle mode is AD, not AR** (the owner asked for an AD like Maths). The switch reads ADSR / AD / HOLD (`AD_MODE`, tooltip "AD (one-shot)"; param values unchanged). **AD is a one-shot**: a rising edge runs Attack to 10 V, then **Decay falls to 0 V over the Decay time** (Decay's goal is 0 V in AD), whatever the gate does; the gate's fall is ignored; Sustain and Release never run; only the A and D gates fire; TRIG gives A, D, idle. In 2.0.44's AR the fall was Release, on the R knob; patches saved on Trig (2.0.43 and earlier) or on AR (2.0.44) open in AD with the fall now on the **D** knob. Mode switches: to AD, a running Decay, Sustain or Release restarts as Decay to 0 V from its level over Decay's time; to ADSR, AD's falling Decay becomes Decay to Sustain if the gate is high, else Release; leaving Hold in AD with the gate low runs Decay (goal 0) to 0 V over Decay's time. The window in AD (`layoutEnvelope(params, true)`, field `ad`) has only A and D spans (`x[2] = x[3] = x[4] = 1`), Decay drawn from the top to 0 V, no S or R letter. Panel legend AR became AD. Tests: test_voice `ad_mode` (replaces `ar_mode`), `attack_resumes` (AD: a gate back during Decay), `mode_switch` (new AD checks, Hold to AD low); test_view `ad_layout` (replaces `ar_layout`), `window_lines_up` now also fails a running stage whose span is empty; test_engine `published` (A, D, IDLE), the AD completion check in `published_prev_stage`, the `mode_switch` bound. |

What a tester who knows 2.0.44 will see differently: the middle position reads AD and falls on the D knob, not R, ending at 0 V; Sustain and Release never run there; switching into AD turns any fall into a Decay to 0 V. What a tester who knows 2.0.39 will also see: the switch legend and tooltip (ADSR / AD / HOLD); AD ignores gate length; retriggers and gates back during Release reach the top sooner; fast triggers into a long Attack now climb to the top; the window in AD mode shows only A and D; a Release that cut Attack or Decay short is drawn in the R span only; a Shape turn mid-stage no longer moves the level. Section 12.9 compares the listening guide.

---

## 0. How to read this document

- Every number here comes from one of these sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.45 sources (`src/beacon/*.hpp`, `src/beacon/Beacon.cpp`, `src/Toggle3Step.hpp`, `src/widgets.hpp`, `src/FastMath.hpp`), cited as file:line where useful. Line numbers are from the 2.0.45 working tree.
  - **[test]**: asserted by the module's automated tests, which I rebuilt and ran. The tests print only "all checks passed", so a [test] figure is the asserted bound, not a measured value.
  - **[meas]**: a measurement program written against the Rack-free engine headers (`rka::BeaconEngine`, `rka::beacon::Voice`, the code the module runs), compiled with the plugin's flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`), run at 48 kHz unless stated, on an Apple M-series Mac (Apple clang 21). The programs are in section 14. Figures in parts of Beacon this rework did not touch (the time law, tooltips, Sustain follower, Hold, lights, TRIG width) were measured on 2.0.39 and still hold, because that code is unchanged; figures for resumed Attacks, the Shape breakpoint and the bench were measured on `f15953b` (2.0.44; that code is unchanged in 2.0.45); figures for AD mode, the stage trails and TRIG counts in both modes, mode switches and the window's AD layout were measured on the 2.0.45 working tree, where the ADSR trails and resumed-Attack times came out identical to 2.0.44's. Bench figures are [meas] from `make bench`'s binary (`build/bench/bench`).
  - **[calc]**: arithmetic on [code] constants that I did not run as a program.
- **[issue #N]**: a GitHub issue in `ryanarneson/RkaModules` (section 10). Two Beacon issues are open on GitHub, #83 and #84, both fixed on this branch (2.0.41) and waiting for the merge to main to close them. Closed #76 explains the window's history.
- **[unverified]**: anything I could not check against code or by measurement, mostly Rack host behaviour. Rack was not run.
- Voltages are Rack volts. "Knob" values are in [0, 1]; "noon" is 0.5.
- The tests and measurement programs do not exercise Rack's ports, threading, drawing or mouse. `Beacon.cpp` (the Rack module and its window) has no automated test beyond the default-panel screenshot of `make smoke` (not run by me).

---

## 1. What Beacon is

Beacon is a 10 HP polyphonic envelope generator, after the Instruo cèis: one three-way switch (ADSR / AD / HOLD) and the knob positions give ADSR, ADR, a gated AR, a one-shot AD (as a trigger into Maths) and a slew limiter. The design intent (spec "Intent") is cèis's outputs and its switching, in the Instrument look; the 2026-11-25 revision departs from cèis where cèis would surprise (spec "Revised 2026-11-25", judged on least astonishment against cèis and VCV's ADSR, then the owner's request for an AD like Maths): AD ignores gate length, and Attack resumes along its curve. Besides the envelope there is a gate for each stage and one trigger at every change of stage, so one envelope also drives events. A small window draws the envelope the knobs make and shows where it is.

Design goals that QA can check:

- ADSR, ADR (Sustain 0), a gated AR (ADSR mode, Decay at its minimum, Sustain full) and a one-shot AD (AD mode, any gate) from one switch and four knobs.
- Stage times from 1 ms to 60 s on the knob itself, each decade an equal share of the turn.
- **Exact Decay and Release times, wherever they start**: Release at 2 s from Sustain 5 V takes 2 s (the user's ruling, "option A"). Stages are timed, not rates for a full 10 V.
- **Attack resumes along its curve** (revised 2026-11-25, replacing option A for Attack): an Attack begun from a level L in 0..10 V takes Attack x (1 - p0), p0 the point of its full 0-to-10 V curve at L, as an RC envelope does. Before 2.0.44 "a retrigger from 8 V takes the full Attack time"; that rule is superseded.
- **AD is a one-shot**: any gate, from one sample to many times Attack, gives exactly Attack to 10 V, then Decay to 0 V over the Decay time.
- A gate per stage (A, D, S, R), a 1 ms trigger at every stage start, OUT and INV.
- A Hold mode that makes it a slew limiter.
- Polyphonic: one envelope per channel of GATE (Hold: of the Sustain CV).
- CV on all four stages. There is no CV on Shape.
- Anti-goals (a failure even if it works): a jump in the envelope when retriggered or when a gate ends early; a stage time that is not what the knob or tooltip says (an Attack begun part-way up taking only the time left on its curve is the 2026-11-25 design, not this failure); short times fiddly to set.

Non-goals (do not file these as defects): looping or cycling into an LFO; extra stages (hold, delay), multi-segment editing, drawable curves; Tides' spread outputs; a time range switch; audio-rate cycling; an attenuverter on the CVs; a CV on Shape; a window that is a ruler or a live scope in ADSR and AD; a fourth position for a separate AR (spec: not taken).

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. The three agree closely; the rows below are the places they do not, or where wording is looser than behaviour.

| Topic | Spec / manual / guide says | 2.0.44 does |
|---|---|---|
| A Shape move during a stage | spec "No jumps" and "Shape moved during a stage" (issue #83): a breakpoint, the level does not move | **Agrees since 2.0.41** (in 2.0.39 the level slid by up to 8.75 V). A Shape change starts the new Shape's curve from the level and phase where it changed. On a 10 s Attack, Shape 0 to 1 in one sample at phase 0.1 and 0.5: first step 1.9e-7 V and 3.0e-7 V, largest step 9.3e-5 V and 1.5e-4 V, Attack still exactly 480,000 samples [meas; test `shape_long_stage`]. While the 5 ms Shape smoothing settles each sample is a breakpoint, so the slope bends (up to 4 times the straight-line rate for what is left when turned toward right) but the level never steps. GitHub #83 is still open until the branch is merged. |
| Tooltip precision | spec "within 0.5 % of the time" | true from 10 ms up (0.497 % worst [meas]); below 10 ms the tooltip has one decimal and is up to 4.76 % off (spec and manual agree: "below 10 ms one decimal") |
| Stage length | spec "within 1 sample or 0.1 %" | exactly ceil(T x rate) samples, so up to 1 sample long: 1 ms at 44.1 kHz is 45 samples (1.02 ms); 10.0 ms at 48 kHz is 481 [meas, section 5.1] |
| Knob fully right | spec / manual "60 s" | stageSeconds(1) is 59.99997 s (0.5 ppm short, from a float constant); the voice holds any longer time to 60 s [code, meas]. Not visible |
| Defaults | manual "10 ms, 300 ms, 1 s" | 10.0015 ms, 299.91 ms, 1.0005 s (knobs 0.2093, 0.5184, 0.6279) [meas]. The tooltip reads "10.0 ms", "300 ms", "1.00 s" |
| Hold: Sustain knob | manual "the Sustain knob adds 0 to 10 V of offset" | correct, and the default knob is noon, so a Hold module with nothing patched sits at **5.000 V** with S high [meas]. The guide and manual say "turn S fully left" for a plain glide |
| Hold hysteresis | spec "about 1 mV" | settled within 1 mV, leaves settled only beyond 2 mV [code, test] |
| Hold TRIG | spec "TRIG fires on each change between them" | also fires once on entering Hold (the stage changes from the envelope's stage to the slew's) and once on leaving it [code; the `hold` test counts 4 pulses for A, S, D, S] |
| Window, ADSR and AD | spec "Polyphony: channel 1" | window, stage lights and published state are channel 1 only. A channel-1 gate that is low while other channels run shows an idle window [code] |
| ADSR to AD mid-Decay, Sustain or Release | spec and manual: a Decay, Sustain or Release running falls at once from its level to 0 V over Decay | as documented: on the switch sample a new Decay (goal 0 V) begins from the level, TRIG fires, and it takes Decay's full time: from 8.02 V mid-Decay, 5.00 V in Sustain and 2.35 V in Release (D 100 ms), each 4800 samples to 0 V, then idle [meas; test `mode_switch`]. A Release switched to AD therefore takes Decay's time, not what was left of Release |
| AD to ADSR mid-fall | spec and manual: "AD's falling Decay heads for Sustain while the gate is high, else Release takes over" | as documented: gate high, a new Decay toward the sustain level over Decay's full time; gate low, Release from the level over Release's time [meas; test `mode_switch`]. **Not stated anywhere:** when AD's fall is already below the sustain level, "heads for Sustain" means the Decay **rises**: from 2.97 V with S 5 V (or 8 V) and the gate high, the D gate is high while OUT climbs to 5 V (8 V) over Decay's 500 ms, then Sustain [meas]. No jump, and the window draws it (the bright line rises in the D span), but a rising Decay may surprise |
| AD to ADSR with the gate held | spec and manual: a gate held through AD's finished one-shot starts nothing until its next rising edge | as documented: the voice stays idle at 0 V with the gate high until the next rising edge [meas] |
| RETRIG in AD | manual: RETRIG "only while the gate is high", in ADSR and AD | as stated: a RETRIG edge in AD's Decay with the gate low does nothing; with the gate held high it restarts Attack [meas]. So with short triggers into GATE, RETRIG does nothing in AD |
| Old patches on Trig or AR | manual: "Patches saved before build 2.0.45 with the switch on Trig open in AD. The fall after Attack is now set by the D knob, not R" | true: the Mode value 1 is now AD [code]. The manual names only Trig; patches saved on 2.0.44's AR (the same value) open in AD too, with the same change of knob. A pulse arriving while the envelope is still running now resumes Attack at the level and reaches 10 V sooner than Trig did [code, meas section 5.2] |
| Switch stepping | spec: steps by direction, held at the ends | as specified; a click on the upper half moves up (toward ADSR). An "ADSR, HOLD, AD, ADSR" order is two down-clicks, then two up-clicks |
| Saved state | manual "Saved with the patch: the knobs and the switch" | also the push button: a shift-clicked (latched) button is a param at 1 and is saved with the patch, so the patch reloads with a gate held on every channel [code; Rack's save of it is [unverified]] |
| Bypass | spec "Rack's default (outputs silent)" | no `configBypass` routes, so outputs are Rack's default for a module with no routes (silent) [unverified in a live Rack]; bypass and un-bypass reset every voice [code] |
| Aliasing | spec: measure the fastest settings or record why not | TESTING.md records "not needed" (control signals; fastest segment is a 1 ms ramp), no bound measured; issue #67 (all modules' aliasing) lists other modules, not Beacon |
| `make test` flags | TESTING.md rule 9: plugin and tests share arithmetic flags | `make test` still builds Beacon's tests at `-O2` without fast-math (Beacon is not in `PLUGIN_MATH_TESTS`). Built with the plugin's flags, all five non-sanitizer programs now pass, `test_view` included (fixed in 2.0.41, `54de7e7`; GitHub #84 still open until the merge) [test, my run] |

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

The Mode switch is a 3.6 x 6.4 mm `Toggle3`: handle up at 0 (ADSR), middle at 1 (AD), down at 2 (Hold). Its legend, to the right of the switch at x 34.2 mm, reads ADSR, AD, HOLD from the top (y 41.1, 45.9, 50.7 mm; GATE, TRIG, HOLD before 2.0.44; ADSR, AR, HOLD in 2.0.44) [code, `tools/gen_panels.py`; the regenerated `res/beacon/Beacon.svg` in the working tree carries the three labels]. The push button is a 6.4 mm `CapButton`. The panel generator's checks pass (`tools/test_gen_panels.py`, 115 tests OK [test, rerun on the 2.0.45 tree]).

### 2.2 Parameter, input, output and light ids

`Beacon.cpp` pins the param ids to the engine frame's ids with `static_assert`. They must never change number (saved patches depend on them).

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 | Shape | 0..1 | 0.75 | "Shape (left: reverse, noon: linear, right: classic)", shown as -100..+100 %, "Linear" at exactly 0.5 |
| 1 | Mode | 0..2, snapped | 0 (ADSR) | "Mode": ADSR, AD (one-shot), Hold (Gate, Trig, Hold before 2.0.44; ADSR, AR (one-shot), Hold in 2.0.44; the values did not change) |
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

Input tooltips: "Attack CV (10 V: the knob's travel)" and likewise Decay and Release; "Sustain CV (Hold: the signal to slew; its channels set the count)"; "Gate (polyphonic: an envelope per channel)"; "Retrig (restarts Attack while the gate is high)". Output tooltips: "Stage trigger (1 ms at each stage start)", "Envelope", "Inverted envelope". The Mode param is read rounded: below 0.5 ADSR, 0.5 up to below 1.5 AD, 1.5 and up Hold, so a value of exactly 0.5 is AD [code, BeaconEngine.hpp:144]. In the code the modes are `GATE_MODE` (ADSR), `AD_MODE` (`TRIG_MODE` to 2.0.43, `AR_MODE` in 2.0.44) and `HOLD_MODE` (`Voice.hpp:36`). The Retrig tooltip still reads "restarts Attack while the gate is high"; the restarted Attack now resumes from the level (section 5.2).

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

- ADSR and AD: `sustain volts = 10 x clamp(knob + CV/10, 0, 1)`, that is `clamp(10 x knob + CV, 0, 10)`. The CV adds 1 V per volt. NaN keeps the last good level.
- The Sustain stage follows the sustain level through a **2 ms one-pole**, so a step on the knob or CV does not click but a 5 Hz LFO still moves the level (a tremolo). Measured for a 10 V to 0 V step: 63 % in 2.00 ms, 10 % left after 4.62 ms, 1 % left after 9.23 ms, within 1 mV after 18.4 ms, the same at 44.1, 48 and 96 kHz [meas]. A 5 Hz LFO of 4 V amplitude on Sustain reaches OUT within 0.5 dB [test: `sustain_follows`]. Audio-rate noise on Sustain keeps OUT inside 0..10 V [test].
- The smoothing applies in the Sustain stage only. **Decay's goal is the sustain level at the moment Decay began** (in AD mode, 0 V). If Sustain is moved during Decay, Decay still ends at the old level and the Sustain stage then glides to the new one: in a 100 ms Decay to 8 V with Sustain moved to 2 V half way, Decay ended at 8.000 V and OUT then read 7.94, 4.18, 2.80, 2.30 V at 2 ms steps [meas]. The window draws it the same way (spec).
- Sustain holds its level for ten minutes within 1e-6 V [test: `levels`].
- In AD mode the sustain level plays no part: Decay's goal is 0 V (`Voice::begin` takes it from the mode) and Sustain never runs. (A Sustain running when the switch moves from ADSR to AD becomes a Decay to 0 V, section 5.4.)
- In Hold the Sustain input means something else (section 5.3).

### 2.5 Mode switch

[code; test `test_switch`: `click_table`, `no_wrap`, `halves`, `drag`] Three positions: ADSR (0, up), AD (1), Hold (2, down). A click on the upper half moves one position up, on the lower half one down, held at the ends: from ADSR an upper click stays at ADSR, and AD to ADSR never passes through Hold. A vertical drag moves one position per 12 px of travel, down is down, held at the ends; after a whole step of drag, the release adds no click. One gesture is one undo step (`pushParamChange`, "move switch"). A double-click does not reset it (Rack's `Switch::onDoubleClick` overrides the reset) [unverified; read from the SDK header only]. The switch's decision logic is Rack-free and tested; the widget around it (`rka::Toggle3`, `widgets.hpp:287`) is not: a real mouse, undo history and drag feel are on QA's list (section 12).

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
- **Smoothing**: each voice follows the Shape knob through a 5 ms one-pole in double precision (coefficient 1 - e^(-1/(0.005 x rate))); it snaps to the knob on reset and for a channel that appears [code, `BeaconEngine.hpp:117`]. The window draws with the knob's value, not the smoothed one.
- **A Shape move mid-stage is a breakpoint** (since 2.0.41, issue #83; `Voice::runStage`): when the voice's Shape differs from the one the stage's curve was set with, the rest of the stage becomes the new Shape's curve from the present level and phase to the goal, over the time left: `level = bpLevel + (goal - bpLevel) x c((phase - bpPhase) / (1 - bpPhase))`. The level does not move and the stage still ends on time (section 1.1 for the measured steps). *Superseded:* in 2.0.39 the level was recomputed on the new curve from the stage's start, a shift of up to 0.875 of the stage's distance (Shape 0 to 1 at phase 0.5); those figures no longer apply.
- **An Attack that starts part-way uses the curve to find its starting phase** (since 2.0.44, section 5.1): the further right Shape is, the more of the climb is in the curve's slow tail, so a resumed Attack keeps more of its time. From 6 V it takes 12.0 % of Attack at Shape 0, 20.3 % at 0.25, 40.0 % at noon, 63.6 % at 0.75 and 79.5 % at 1 [meas].
- **Tooltip**: -100 % fully left, "Linear" at exactly 0.5, +100 % fully right; the default 0.75 reads +50 % [code: multiplier 200, offset -100]. A knob that stops beside noon reads a small percentage (the guide's 0.4 %). Typing "linear" sets noon [code, `ShapeQuantity`].
- Hold ignores Shape (section 5.3).

### 2.7 Push button

[code; test `push`] A gate held for as long as it is held, on every channel, ORed with GATE (`in.gate = gate || push`). If GATE is unpatched, it is the only gate, and the channel count is 1. Shift-click latches it down until the next click (`CapButton`, `holdPress`). It does nothing in Hold. A latched button is a param at 1; Rack saves it (section 3).

### 2.8 GATE and RETRIG

[code; test `schmitt`] Each channel has its own Schmitt trigger on GATE and on RETRIG: high at 1.0 V, low below 0.1 V (the plugin's thresholds, as Wake's clock). NaN reads low. A level between 0.1 V and 1.0 V keeps the last state: GATE 0.9 V from low stays low [test]; GATE 0.5 V after being high stays high, and 0.11 V from high stays high [test, meas]. An unpatched GATE reads 0 V through its Schmitt, so a cable plugged back in at 0.5 V is low, not a new edge [test].

RETRIG rising while the gate is high restarts Attack from the present level, without a new gate; since 2.0.44 that Attack resumes along its curve and takes only the time left (section 5.2). While the gate is low it does nothing (a RETRIG edge in Release is ignored) [test: `retrig`]. It restarts Attack in both ADSR and AD modes. In AD, with a trigger into GATE the gate is low again long before Attack ends, so RETRIG only acts there while a long gate is held (it then restarts Attack even during AD's Decay) [meas].

### 2.9 Outputs

[code]

- **OUT**: 0 to 10 V. In Hold, -10 to +10 V. Never outside +/-10 V [test: `ok()` in `test_hostile`, every output, every sample, every fuzz seed].
- **INV**: exactly -OUT, bit for bit, including -0 at 0 V [test: `inv_exact`]. A scope reads 0 V.
- **A, D, S, R**: 10 V while that stage runs, else 0 V. Every stage, even one that ends at once, holds its gate for at least one sample [test: Decay to Sustain 10 V shows its gate for exactly 1 sample].
- **TRIG**: 10 V for `round(0.001 x rate)` samples (at least 1) at the start of every stage, including the return to idle at the end of Release (in AD, at the end of Decay). Measured: 8, 22, 44, 48, 96, 192 and 768 samples at 8, 22.05, 44.1, 48, 96, 192 and 768 kHz, that is 1.0000 ms except 0.9977 ms at 22.05 and 44.1 kHz [meas]. A stage start while the pulse is high restarts the 1 ms (pulses merge): two starts 0.3 ms apart give one pulse 1.3 ms wide [test: `gates_and_trig`].
- TRIG pulses per gate, by mode [meas on the 2.0.45 tree, 48 kHz, A 10 ms, D 300 ms, S 5 V, R 100 ms, Shape 0.75, one gate from rest; tests `gates_and_trig`, `ad_mode`]:

| Into GATE | ADSR: stages, pulses | AD: stages, pulses |
|---|---|---|
| 1-sample or 1 ms trigger | A, R, idle; **2** (A's and R's starts are at most 1 ms apart, so the pulses merge into one 1 to 2 ms wide) | A, D, idle; **3** (Attack 480, Decay 14,400 samples) |
| gate of half the Attack (5 ms) | A, R, idle; 3 | A, D, idle; 3 |
| gate ending in Decay (20 ms) | A, D, R, idle; 4 | A, D, idle; 3 |
| gate held 1 s | A, D, S, R, idle; **5** | A, D, idle; **3** |
| gate held 1 s, Sustain 10 V | A, D (1 sample), S, R, idle; 4 (D and S merge) | A, D, idle; 3 (Sustain plays no part) |

  AD always gives exactly three pulses for a gate from rest, whatever its width, with Attack and Decay at their full times, the fall ending at exactly 0 V and the S and R gates never high [test: `ad_mode`, nine widths from 1 sample to 20 s]. With Decay under 1 ms the D and idle pulses merge (A 10 ms, D 1 ms, a 1 ms trigger: 2 pulses [meas]). Each further rising edge or RETRIG while the envelope runs adds a pulse (merged if within 1 ms of another stage start), and so does a mode switch that restarts a fall (section 5.4).
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
- **Saved state**: only Rack's params, ids 0 to 6 (Shape, Mode, Attack, Decay, Sustain, Release, Push). The JSON `data` object is absent. Mode keeps its values 0, 1, 2 across the rename, so a patch saved on Gate opens on ADSR and one saved on Trig (2.0.43 and earlier) or on AR (2.0.44) opens on AD, which ignores gate length and falls on the D knob where Trig's and AR's fall was on R [code]. The stage, phase and level of every voice are **not** saved: a reloaded patch starts every voice idle at 0 V (a gate still high then starts a new Attack from 0 V).
- The push button is a param and is saved: a patch saved with the button latched down reloads with it down [code; [unverified] in Rack]. The manual lists only "the knobs and the switch".
- **Initialize** (`onReset`): every voice idle at 0 V, gates low, lights out, Shape follower back to 0.75 and snapping to the knob. Rack resets the params to their defaults (Mode ADSR, Shape 0.75, A 10 ms, D 300 ms, S 5 V, R 1 s, button up).
- **Randomize**: Rack's. The Mode switch is a `configSwitch` and may be randomised; `configButton` params are not [unverified].
- **Bypass** (`onBypass`, `onUnBypass`): both reset every voice to idle at 0 V and put the lights out. Beacon configures no bypass routes, so its outputs are silent while bypassed (Rack's default for a module with none) [unverified in a live Rack]. On un-bypass nothing resumes and no Release runs; a gate still high starts a new Attack from 0 V [code, manual].
- **Sample-rate change** (`onSampleRateChange`): the engine keeps every voice's stage, phase and level; times stay in seconds; the TRIG pulse length is recomputed and trimmed if it was longer [code; tests `sample_rate_change`, `rate_change`, `rate_change_carries_the_stage`].
- No factory presets.

---

## 4. Signal flow

### 4.1 Block diagram

```
 CONTROL FRAME (once per sample, from Rack)
   MODE (rounded: <0.5 ADSR, <1.5 AD, else Hold) -> channel count n
        ADSR/AD:   n = GATE's channels (1 if unpatched), max 16
        Hold:      n = Sustain CV's channels (1 if unpatched), max 16
   channels that appear (n rising) start idle at 0 V; channels past n read 0 V

 PER CHANNEL c < n
   GATE[c]   --Schmitt 0.1/1 V--+
   PUSH button (>= 0.5) --------+--OR--> gate
   RETRIG[c] --Schmitt 0.1/1 V---------> retrig
   Attack, Decay, Release:  knob + CV[c]/10 --> 1 ms x 2^(15.87 k)   (clamped 1 ms .. 60 s)
   Sustain (ADSR, AD):      10 x clamp(knob + CV[c]/10, 0, 1)  V
   Sustain (Hold):          clamp(CV[c] + 10 x knob, -10, +10)  V     (the signal to slew)
   Shape:                   knob --5 ms one-pole (double)--> shape[c]
        |
        v
   VOICE (one per channel; Voice.hpp)
     edges: rise = gate & !prev; fall; retrig edge
     ADSR/AD:   [leaving Hold: gate high -> Attack; low -> Release (ADSR) or Decay to 0 V (AD)]
                [stage that finished last sample hands on]
                   (Attack done: AD, or ADSR with gate high -> Decay, else -> Release;
                    Decay done: AD -> idle; ADSR gate high -> Sustain, else -> Release)
                -> [switch ADSR->AD in Decay/Sustain/Release: Decay to 0 V from the level;
                    switch AD->ADSR in Decay: Decay to Sustain (gate high) or Release (low)]
                -> [edge may begin a stage: rise or RETRIG(gate high) -> Attack;
                    fall -> Release in ADSR (not from idle or Release); AD ignores the fall]
                   Decay's goal: the sustain level in ADSR, 0 V in AD
                   Attack begun at L in 0..10 V: phase = p0 with c(p0, shape) = L/10, curve from 0 V
                   (40-step bisection, Voice::curvePhase); other stages: phase 0, curve from L
                -> run the stage: phase += dt/T;  level = bp + (goal - bp) x c((phase - bpPhase)/(1 - bpPhase), shape)
                   (bp, bpPhase: the stage's start, or where Shape last changed)
     Hold:      slew toward the input at 10 V/Attack up, 10 V/Decay down; S/A/D by 1 mV / 2 mV hysteresis
        |
        +--> OUT[c]  (+/-10 V)                 --> INV[c] = -OUT[c]
        +--> stage gates A D S R [c]  (10 V while that stage runs)
        +--> TRIG[c]  (10 V for 1 ms from every stage start; restarts if re-entered)

 CHANNEL 1 ONLY
   stage, phase, level, mode, start, goal, breakpoint phase and level
        --relaxed atomics--> the window (UI thread; AD mode -> A and D layout)
   (previous stage and previous phase are still published, no longer read)
   stage gates --note() every sample, update() every 16--> four lights (40 ms hold)
```

While a voice is IDLE nothing runs but the gates (all low) and the TRIG countdown. There is no sleep logic; the cost is flat (section 9).

### 4.2 Block constants

| Constant | Value | Source |
|---|---|---|
| Time range | 1 ms to 60 s; `kLog2Span` 15.8726744 | Timing.hpp:13-14 |
| CV scale | 10 V = the knob's whole travel | Timing.hpp:28 |
| Default Shape, Sustain | 0.75, 0.5 (5 V) | BeaconEngine.hpp:38 |
| Default A, D, R knobs | 0.2093, 0.5184, 0.6279 | BeaconEngine.hpp:38 |
| Schmitt thresholds | high 1.0 V, low 0.1 V (NaN low) | BeaconEngine.hpp:245 |
| Shape smoothing | 5 ms one-pole, double | BeaconEngine.hpp:117 |
| Sustain smoothing | 2 ms one-pole, double | Voice.hpp:68 |
| Phase, step, level | double (a 60 s stage at 768 kHz is 46 M samples) | Voice.hpp:4 |
| End of stage | phase >= 1 - 0.001 x step counts as 1 | Voice.hpp:253 |
| TRIG length | round(0.001 x rate) samples, at least 1 | Voice.hpp:63 |
| Decay's goal | the sustain level in ADSR, 0 V in AD (from the mode, `mode_`) | Voice.hpp:198 |
| Resumed Attack | from L in [0, 10) V: phase p0 = the curve's point at L/10, 40 bisection steps on `curveRise` (no libm), at stage start only; L outside that range: phase 0, curve rescaled from L | Voice.hpp:209, 218 |
| Shape breakpoint | on any change of the voice's Shape mid-stage | Voice.hpp:242 |
| Hold slew rate | 10 V x dt / Attack (up), 10 V x dt / Decay (down) | Voice.hpp:272 |
| Hold hysteresis | settled within 1 mV; leaves beyond 2 mV | Voice.hpp:283 |
| Light hold, update | 0.04 s; every 16 samples | BeaconEngine.hpp:67-68 |
| Max channels | 16 | BeaconEngine.hpp:101 |
| Accepted sample rates | 1 Hz to 1e8 Hz; others ignored | BeaconEngine.hpp:112 |

**Stage end rule.** A phase within a thousandth of a sample of 1 counts as 1, so a time that is a whole number of samples (1 ms at 48 kHz, though 0.001f is 5e-8 long) takes that many samples, not one more [code, test `exact_times`].

**No libm per sample**: the per-sample code uses `fastExp2`, `fastExp2D` (set-up only) and polynomials; `std::log10` appears only in the window's UI-thread geometry [code]. The libm check (`make libm-calls`, Linux only) was not run by me. Beacon's busy case must show 0 libm calls there (TESTING.md).

---

## 5. The stage machine, modes and mode switching

### 5.1 Stages, times and levels

[code, `Voice::process`, `Voice::begin`; tests `exact_times`, `levels`, `part_way`, `attack_resumes`]

- Stages: IDLE, ATTACK, DECAY, SUSTAIN, RELEASE. Each sample, in this order: a stage that finished on the last sample hands on to the next; then edges and a mode change may begin a stage; then the stage runs.
- Attack rises from the present level to 10 V; Decay falls to the sustain level; Sustain follows the sustain level; Release falls to 0 V. Every stage starts from the present level, so the level never jumps.
- **Decay and Release take exactly their set time to reach their goal**, whatever the distance (Decay's goal is the sustain level in ADSR and 0 V in AD). Their curve is the stage's full curve rescaled between that start and the goal (Tides' breakpoint rule).
- **Attack resumes along its curve** (since 2.0.44). An Attack begun from a level L with 0 <= L < 10 V does not rescale: it starts at the phase p0 where its full 0-to-10 V curve, in the present Shape, reaches L, and runs the rest of that curve at the normal rate. So it takes **Attack x (1 - p0)** and its shape is exactly the drawn Attack from that point. From rest (L = 0) that is the full Attack, as before. An Attack begun outside that range (only on leaving Hold: a negative level) keeps the old rule, its full time along a rescaled curve (Hold at -4 V to ADSR with the gate high: 481 samples for a 10 ms Attack [meas; test `mode_switch`]). *Superseded:* "every stage takes its full time wherever it starts" (2.0.39) no longer holds for Attack.
- Decay with Sustain at 10 V, or a stage whose start equals its goal (an Attack begun at exactly 10 V), ends on its first sample.

  Resumed Attack, share of the Attack time taken, by start level and Shape [meas: the curve's own inverse, 60-step bisection; the voice's sample counts agree within 1 sample, e.g. from 6 V with A 100 ms at 48 kHz: 576, 976, 1920, 3054 and 3818 samples at Shapes 0, 0.25, 0.5, 0.75, 1]:

| Start level | Shape 0 (left) | 0.5 (noon) | 0.75 (default) | 1 (right) |
|---|---|---|---|---|
| 0 V (from rest) | 100 % | 100 % | 100 % | 100 % |
| 2 V | 33.1 % | 80.0 % | 91.1 % | 94.6 % |
| 5 V | 15.9 % | 50.0 % | 72.4 % | 84.1 % |
| 6 V | 12.0 % | 40.0 % | 63.6 % | 79.5 % |
| 8 V | 5.4 % | 20.0 % | 37.9 % | 66.9 % |
| 9 V | 2.6 % | 10.0 % | 19.8 % | 56.2 % |

  Examples [meas, 48 kHz]: RETRIG from Sustain 5 V with the default 10 ms Attack: 349 samples (7.3 ms; 2.0.39: 480). A retrigger at 8.0 V during Decay, Attack 50 ms: 912 samples (38 %; 2.0.39: 2400). Leaving Hold at 5 V into ADSR with the gate high, Attack 10 ms: 349 samples. The tests pin the time within 2 samples of Attack x (1 - p0) at five Shapes, for RETRIG from Sustain and for a gate back during the fall (Release in ADSR, Decay in AD; from 6.0 V in AD at Shape noon: 1921 samples of a 4800-sample Attack, 40.0 % [meas]), and the first sample on the curve just past p0 [test: `attack_resumes`, `part_way`, `retrig`].
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

### 5.2 ADSR and AD

[code; tests `gate_mode`, `ad_mode`, `attack_resumes`, `retrig`, `no_jumps`]

- **ADSR** (switch up, the default; `GATE_MODE`, the old Gate): a rising edge starts Attack. Attack and Decay run while the gate is high, then Sustain holds. The falling edge starts Release from wherever the envelope is, even mid-Attack. A trigger, or a gate shorter than Attack + Decay, goes straight to Release from where it got: no Decay or Sustain run. A 1 ms trigger into ADSR with a 50 ms Attack peaks at 0.488 V (the guide: "a faint blip") [meas]. With Attack at 1 ms and a 1 ms gate it reaches exactly 10 V where 1 ms is a whole number of samples (48, 96, 192, 768 kHz) [test: `gate_mode`]; at 44.1 kHz the gate is 44 samples and Attack 45, so the peak is about 9.9 V [calc].
- **AD** (switch middle; `AD_MODE`, since 2.0.45; a trigger into Maths): a strict one-shot. A rising edge starts Attack; **Attack always runs to 10 V, then Decay falls to 0 V over the Decay time, whatever the gate does**: the gate's fall is ignored at any point, and a gate still high when Attack ends is ignored too. Sustain and Release never run and their gates never go high; the Sustain and Release knobs and CVs do nothing. Decay ends exactly at 0 V, then idle. Tested at nine gate widths from 1 sample to 20 s (A 50 ms, D 30 ms, R 200 ms, 48 kHz): every one gives A, D, idle, Attack 2400 and Decay 1440 samples within 1, the fall from the top to exactly 0 V, peak exactly 10 V, S and R gates never high [test: `ad_mode`].
  *Superseded:* in Trig (2.0.34 to 2.0.43) a gate still high when Attack ended ran Decay and Sustain, so a long gate gave the full ADSR. In 2.0.44's AR the fall after Attack was **Release**, on the R knob, and the D and S gates never fired. Both are gone; `ar_mode` (which replaced `trig_mode`) was replaced by `ad_mode`.
- **A rising edge while the envelope runs** restarts Attack from the present level, in both modes, with no jump. Since 2.0.44 that Attack resumes along its curve (section 5.1), so it reaches 10 V in the time left on the curve, not a whole Attack. A rising edge during Attack is therefore seamless: the curve carries on from where it is (the A gate stays high and TRIG fires). In AD a rising edge during Decay resumes Attack from the level the same way. **RETRIG** does the same only while the gate is high.
- **Fast triggers into a long Attack**: 1 ms triggers every 50 ms into a 200 ms Attack (D and R 100 ms). In AD each trigger arrives during Attack and continues it, so OUT first reaches 10 V at sample 9599, Attack's 200 ms from the first trigger [meas; test `ad_mode`: within 2 samples of 9599]. In ADSR each trigger rises only for its 1 ms and then releases, so OUT never passes 0.174 V [meas]. In 2.0.39's Trig each trigger restarted a full Attack from the level, so with triggers faster than Attack the envelope only crept toward 10 V [code; the commit's account, not re-measured].
- ADR is ADSR with Sustain at 0 (S still goes high while the gate is high and OUT sits at 0 V); a gated AR (held at the top while the gate is high) is ADSR with Decay at its minimum and Sustain full; the one-shot AD is AD mode. There is no one-shot AR: the spec rejected a fourth position.
- **Consequence for a square-wave gate** (the manual's Sway example): in ADSR the pulse width against Attack and Decay decides the shape (a 1 % square at 1 Hz is high 10 ms, a blip against a 50 ms Attack; at 0.1 Hz it is high 100 ms, rises to the top and starts to decay); in AD every pulse gives the same envelope [code; manual table "Gates, pulses and triggers"; not run in Rack].

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
| ADSR or AD, to Hold | the slew starts from the present level (the first step is at most the slew's own) |
| Hold, to ADSR or AD, gate high | Attack starts from the present level. From 0..10 V it resumes along its curve (Hold at 5 V, Attack 10 ms: 349 samples [meas]); from a negative level it takes Attack's full time along a rescaled curve (-4 V: 481 samples [meas; test `mode_switch`: 480 within 1]). In AD the Attack then falls over Decay (4800 samples at D 100 ms [meas]) |
| Hold, to ADSR, gate low | Release runs from the present level to 0 V in Release's time; from a negative level it rises (-3 V to 0 V in Release's time [test]); from 0 V it ends at once (gate high for one sample) |
| Hold, to AD, gate low | AD's fall instead: **Decay (goal 0 V)** from the present level to 0 V in **Decay's** time; from 5 V or -4 V, 4800 samples at D 100 ms, rising from a negative level [meas; test `mode_switch`: -4 V, D 10 ms, 480 within 1]; from 0 V it ends at once. The D gate, not R, is high |
| ADSR to AD | an Attack carries on to 10 V and then falls over Decay to 0 V, whatever the gate (test `mode_switch`: ADSR to AD mid-Attack carries on, and a gate falling then is ignored). **A Decay, Sustain or Release running becomes a new Decay to 0 V** from its level on the switch sample, over Decay's full time, with a TRIG pulse; the D gate is then high: from 8.02 V mid-Decay, 5.00 V in Sustain, 2.35 V in Release, each 4800 samples at D 100 ms [meas; test `mode_switch` at 0.03, 0.3 and 0.45 s, within 2 samples] |
| AD to ADSR | an Attack carries on; the new mode applies from its end (gate high: Decay to Sustain; low: Release) or the next edge. AD to ADSR mid-Attack with the gate already low carries on to 10 V, then Release [test]. **AD's falling Decay** becomes, on the switch sample, a new Decay toward the sustain level (gate high) over Decay's full time, or Release from the level (gate low) over Release's time [meas; test `mode_switch`]. From below the sustain level that Decay rises (section 1.1). A gate held high through AD's whole one-shot and then switched to ADSR is no new edge: the voice stays idle at 0 V until the gate falls and rises again [meas; the `test_engine` `mode_switch` test lowers the gate for this reason] |
| Channel count changes with the switch | in Hold the count is the Sustain CV's: four gated channels with the CV unpatched collapse to 1 (channels 2 to 4 dropped, outputs 0 V). Back in ADSR or AD they appear as new channels: idle at 0 V, then, the gate being high, a fresh Attack from 0 V; channel 1 keeps its level [test: `hold_collapses_channels`] |

Switching through all three positions every 7 ms for 2 s with the gate held and a 3 Hz Sustain CV: every step within 1.5 x the active stage's own largest step + 0.1 mV (for Attack, the slope of the full 0-to-10 V curve, since a resumed Attack runs on it; for Decay in AD, the distance to 0 V); gates coherent; R low in Hold; then a fresh gate in ADSR settles in Sustain at 5 V [test: `test_engine` `mode_switch`]. Audio-rate switching: largest step at most 10 V/480 x 4 x 1.5 + 0.1 mV [test].

---

## 6. Stage gates and TRIG

Section 2.9 has the definitions; the checks QA can make:

- Each stage gate is high exactly while its stage runs; one gate high at a time per channel; at least one sample [test: `gates_and_trig`, at 44.1, 48 and 768 kHz].
- ADSR, a long gate: IDLE, A, D, S, R, IDLE; five TRIG pulses, each 1 ms [test]. Shorter gates and triggers: section 2.9's table.
- RETRIG during a held gate that has reached Sustain: the S gate falls, the A gate rises and TRIG fires; the Attack takes only the time left on its curve (section 5.1). A RETRIG during Attack restarts it: the A gate stays high, TRIG fires again, and the level carries on along the same curve. Two starts under 1 ms apart: one pulse.
- AD, any gate from rest (1 sample to 20 s): gates A then D, never S or R, three TRIG pulses (A, D, idle) [test: `ad_mode`].
- Hold: A, D, S gates as in section 5.3.
- The A gate rising is the "strike" (Rigging STRUM); TRIG into Narrows PING gives five pings for a long gate in ADSR, three in AD (the manual's patch).

---

## 7. Polyphony

[code; tests `poly_independent`, `mono_cv_to_all`, `poly_cv_per_channel`, `channel_count`, `push`, `hold_collapses_channels`, `channel_counts_change`]

- **Channel count**: ADSR and AD, GATE's channels (1 if GATE is unpatched, even with the button down); Hold, the Sustain CV's (1 if unpatched). At most 16. Every output carries that count (all seven outputs, set every sample, whether patched or not).
- **A polyphonic CV or RETRIG cable** gives each channel its own value; a **mono** cable applies to every channel; channels past a polyphonic cable's count read 0 V (no change for a time CV; no change for the Sustain CV in ADSR and AD; in Hold, 0 V plus the offset). Measured: GATE 4 channels, RETRIG 8, Attack CV 2 channels (channel 2 at -5 V) gave four channels with OUT 6.428, 7.178, 6.428, 6.428 V after 100 ms at Attack 245 ms: channel 2's shorter Attack is further along; channels 3 and 4 read 0 V and match channel 1 [meas]. A RETRIG cable wider than GATE affects only the first n channels.
- **Each channel equals a lone Voice fed the same input, bit for bit**, for 16 channels with different gates, mono CVs, polyphonic CVs, and a channel count that changes [test].
- **Channels that appear** (the count rising, on a patch or on a mode switch) start idle at 0 V; a gate that is already high then starts a fresh Attack from 0 V. Channels that go away are dropped (their outputs 0 V) and reset if they come back. A GATE cable changing 4 to 7 to 2 to 4 channels mid-run: surviving channels carry on, new ones start fresh [test].
- **The push button** gates every channel, with GATE patched or not (GATE 16 channels all low and the button down: channels 1 and 16 both at 6.428 V after 100 ms, so all 16 attack together [meas]).
- **Window, lights, published state**: channel 1 only.
- **Hold**: one slew per channel of the Sustain CV.
- A mono RETRIG with a polyphonic GATE applies to every channel.

---

## 8. The window

[code, `EnvelopeView.hpp`, `Beacon.cpp`; tests `test_view`] A 42.0 x 17.0 mm widget at (4.4, 19.0), drawn in vectors on the self-illuminated layer (layer 1) in the theme's cyan, so it stays visible when Rack's room lights are dimmed. It has no mouse handling. It is **not** a meter and not a ruler: it draws the shape the knobs make now, **as the mode will run it** (since 2.0.44: ADSR the four stages; since 2.0.45 AD only Attack and Decay, in 2.0.44 AR only Attack and Release), and where channel 1 is in it. No automated test draws it, and `make smoke` shows only the default panel, so every drawing statement below is [unverified] in Rack; the geometry behind it is tested offline.

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
- **AD mode** (since 2.0.45; `layoutEnvelope(params, true)`, field `ad`, chosen by the published mode): Sustain and Release get weight 0, so `x[2] = x[3] = x[4] = 1` and Attack and Decay share the whole width in the ratio of their ADSR weights; the Release knob and Sustain level do not change the picture. Decay is drawn falling from the top (10 V) to 0 V, not to the sustain level, and the end of the line (u = 1) is Decay's end at 0 V. The S and R strokes and letters are skipped (a zero-width span is not drawn) [code; test `ad_layout`: S and R spans empty, the A-to-D width ratio kept, up to the top, Decay from the top to 0, the AD dot on its outline]. Widths in mm [meas]:

| Times (A, D), AD mode | A | D |
|---|---|---|
| 10 ms, 300 ms (defaults) | 14.31 | 24.89 |
| both 1 ms (or both equal) | 19.60 | 19.60 |
| A 10 ms, D 1 ms | 26.13 | 13.07 |
| A 1 ms, D 60 s | 5.78 | 33.42 |
| A 60 s, D 1 ms | 33.42 | 5.78 |

  *Superseded:* 2.0.44's AR layout (A and R only, defaults A 13.07 mm, R 26.13 mm, Release from the top).

- The times are the knob plus **channel 1's CV** (a connected CV jack's channel 1; NaN reads 0). Sustain is the knob plus channel 1's Sustain CV / 10, clamped. So the window follows CV moves, and shows the shape of channel 1's envelope.
- **Heights**: Attack rises from 0 to 1 (10 V), Decay to the sustain fraction, Sustain flat, Release to 0 (in AD, Decay from 1 to 0 and no Release), each stage in its Shape curve (the knob's value, not the 5 ms smoothed one). Each stage is drawn from 48 segments (49 points).

### 8.2 What is drawn

| Element | Drawing |
|---|---|
| Baseline | faint line (alpha 0.18, 0.6 px) under the whole envelope |
| The knobs' envelope | each stage its own stroke, dim (alpha 0.38, 1.1 px). A running Sustain above 0 V is drawn full-bright (1.6 px) |
| The running Attack, Decay or Release | bright (1.6 px) over its own span, from its breakpoint level to its goal along the Shape curve (below). The knobs' shape stays dim as the reference |
| The dot | radius 0.75 mm, bright, on the bright line at the stage's phase; idle: radius 0.55 mm, dim, at the start (bottom left) |
| Letters A D S R | under their spans (AD mode: A and D only); the running stage's letter is bright, the rest dim |
| Sustain at 0 V | the Sustain span stays dim; while in Sustain the dot sits at the end of the line (bottom right) |
| Idle | the shape drawn, no stage lit, the dot at rest at the start |
| Module browser (no module) | the default envelope (10 ms, 300 ms, S 5 V, 1 s, Shape 0.75), idle |

- **Live span** (since 2.0.44): the bright stage is drawn over **its own span only**, whatever ran before it (`liveSpan(stage)` returns `x[stage-1]..x[stage]`) [code; test `live_span_own`]. What it starts from is the published breakpoint (`bpPhase`, `bpLevel`): normally the stage's start at phase 0; after a Shape move mid-stage, that phase of the way across the span at that level (since 2.0.41; test `live_breakpoint`).
  - **A resumed Attack** (a retrigger, a gate back during Release, leaving Hold at 0..10 V) publishes breakpoint level 0 V at phase 0, so its bright line is the full Attack curve from 0 V at the span's start to 10 V at its end, the same curve as the dim Attack when Shape is still, and the dot joins it at phase p0, at the level.
  - **A Release that cut Attack or Decay short** (ADSR, a gate falling early) is drawn in the R span, from the level it started at down to 0 V at the right edge. Example: defaults, a gate falling at 5 V half-way up Attack: the bright Release starts at 26.11 mm (the start of the R span) at half height [meas]. The dim R outline still falls from the sustain level, so the bright line can sit above or below it. (In AD no Release runs; a Decay begun mid-fall by a switch into AD is drawn in the D span from its level to 0 V, below the dim Decay outline.)
  - *Superseded:* the "skipped stages" widening of 2.0.36 and 2.0.37 (issue #76), where a Release that skipped stages was drawn from where the last stage was cut off, sweeping back across the A, D and S spans, is gone. Test `live_span_after_skip` was replaced by `live_span_own`. The engine still publishes `prevStage` and `prevPhase` (and `published_prev_stage` still tests them), but the window no longer reads them.
- **The window lines up with OUT** [test: `window_lines_up`]: a real Voice, both modes (ADSR and AD), four Shapes, Sustain 5 V and 0 V, six gate patterns (a 1-sample trigger, a gate shorter than Attack, one ending in Decay, a held gate with a RETRIG in Sustain, a square faster than Attack, triggers faster than Attack): on every non-idle sample the dot, computed from what the voice publishes, is within 2e-4 of the window height (2 mV) of OUT and inside its own stage's span, that span is not empty (new in 2.0.45: a running stage with no span fails), and a bright line kept from an earlier sample still predicts OUT while that stage runs on with Shape unchanged. Over 100,000 samples checked, none off.
- **No running stage lacks a span in AD**: a switch into AD turns a Decay, Sustain or Release into a Decay to 0 V on the same sample, and leaving Hold in AD runs Attack or Decay, so Sustain and Release never run there [code; `window_lines_up` now fails an empty span, though its patterns do not switch modes]. (2.0.44's first AR version could strand the dot at the A/R junction; `24cf724` fixed that by releasing at once.)
- **Start and goal** of the running stage are the levels latched when it began. Decay's goal is the sustain level at that moment, so a Sustain move during Decay leaves the bright Decay ending at the old level, as the voice does (section 2.4). Release after Hold from a negative level is drawn clamped at 0 V.
- **The dot is on the bright line by construction** (it uses the same function), so it cannot disagree with the line [test: `live_dot_on_line`, whatever level was published beside it]. Issue #76 reported the line and dot as misaligned; 2.0.37 closed it, and `window_lines_up` now checks the dot against OUT itself.
- **Hold**: instead of the envelope, a scrolling line of channel 1's output, +/-10 V filling the plot, 0 V a faint line, the newest point a dot, and the word "HOLD" at the top left. **160 points at 60 points a second of the UI frame clock = 2.67 s of history**, so it scrolls at the same speed at 60 and 144 Hz [test: `hold_ring_time`]. Entering Hold fills the line flat at the present level. A stall is held to one ring's worth.
- Torn reads: stage, phase, level, mode, goal and the breakpoint phase and level are separate relaxed atomics, so for one frame the dot can be a sample out of step with the line (clamped, drawn anyway), and on the frame the mode changes the layout can be one frame late. The published state is lock-free (`static_assert`, and a WARN in Rack's log at construction if not) [test, code].

---

## 9. CPU, sample rates and robustness

### 9.1 CPU

There is no silence sleep; the cost is per sample, flat across a voice's stages, and linear in rate. `bench` cases [code, `bench.inc`], 48 kHz:

- **idle**: one voice, nothing patched, defaults.
- **outputs**: one channel, a 2 Hz gate (a quarter second high every half second).
- **busy**: 16 channels, gates staggered a sixteenth of a period apart, RETRIG pulsing on every third channel, every CV moving, Shape 0.8.

| Case | Budget, Apple arm64 | Budget, other (provisional) | TESTING.md (builder's run) | 2.0.44, 1 instance [meas] | 2.0.44, 16 instances | 2.0.39, 1 instance (under load) |
|---|---|---|---|---|---|---|
| idle | 0.25 | 0.75 | 0.121 | 0.125 to 0.129 | 0.128 | 0.134 to 0.164 |
| outputs | 0.4 | 1.2 | 0.182 | 0.194 | 0.197 | 0.190 to 0.232 |
| busy (16 voices) | 2.05 | 6.15 | 1.013 | 1.023 to 1.034 | 1.039 | 1.09 to 1.37 |

Units: % of one core per instance at 48 kHz. 2.0.44: `build/bench/bench beacon <case> <1|16> 4`, rebuilt from `f15953b`, three runs of the single case, an Apple M-series Mac at load average about 2.7. The 2.0.39 column was taken at load average 8 to 10, so the drop is the machine, not the code: the resumed Attack adds a 40-step bisection only when an Attack begins, nothing per sample. The 16-instance cost per instance equals the single cost. All figures are inside the budgets. Per sample: idle 26 to 27 ns, outputs 40 ns, busy 213 to 215 ns for 16 voices (about 13 ns per voice). Scaling by rate [calc]: busy about 1.0 % at 48 kHz is about 2.1 % at 96 kHz and 4.1 % at 192 kHz; idle 0.13 % becomes 0.5 % at 192 kHz. **Windows CPU figures are not covered by any automated budget.** Rack's CPU meter includes Rack's own per-module overhead [unverified].

### 9.2 Sample rates

[code; tests `extreme_rates`, `rate_changes_mid_stage`, `rate_change_carries_the_stage`, `sample_rate_change`]

- Accepted: any rate with 1 Hz <= rate <= 1e8 Hz. Ignored, keeping the last good rate: zero, negative, NaN, infinity and above 1e8 [test: 0, -48000, NaN, inf, 1e30].
- A rate change keeps every voice's stage, phase and level. 48 kHz to 96 kHz in the middle of a 100 ms Attack: no step (at most 1.5 x the stage's own step + 0.1 mV) and the rest of Attack takes 50 ms (4800 samples) [test].
- All three modes stay finite and within +/-10 V at 1 Hz, 8 kHz, 44.1 kHz and 768 kHz with 16 channels of GATE and Sustain CV toggling and minimum times [test].
- Stage times are in seconds, so they do not change with the rate; they are quantised to whole samples (section 5.1). The TRIG pulse is 1 ms to the nearest sample.

### 9.3 Hostile input and numerical safety

- NaN, +/-infinity and +/-1e30 on every param and every port, one at a time and all together, with a gate running, in every mode: every output finite and within +/-10 V, INV exactly -OUT, gates and TRIG 0 or 10 V, nothing past the channel count [test: `bad_values_everywhere`]. After the bad values are gone an ordinary gate gives an ordinary envelope [test: `recovers_after_nan`]. MODE out of range (7 and -3) is handled [test: `mode_out_of_range`]; in the code a NaN Mode reads ADSR (the comparisons are false) [code].
- A NaN knob reads noon; a NaN time keeps the last good time; a NaN Shape keeps the last; a NaN sustain keeps the last; a NaN Hold input holds the level; NaN GATE and RETRIG read low [code].
- GATE and RETRIG toggling every sample, every other sample and at random on 16 channels; channel counts changing every block [test].
- Seeded fuzz: three seeds (2026, 1, 99), 10^6 samples each, of knobs, CVs, gates, modes, channel counts, rates (1 Hz to 768 kHz) and resets, 1 % of port values NaN or infinite [test].
- No allocation in `process()`, `setSampleRate()` or `reset()`: 0 allocations in 400,000 samples at 16 channels with mode, count and rate changing [test, counting operator new].
- No writable global state: `make engine-globals` passes on Beacon's declaration [test, my run].

---

## 10. Open issues

Checked with `gh issue list --state all --search Beacon`, 2026-11-25. No issue is filed against the 2.0.44 or 2.0.45 rework.

| # | State | Relevance |
|---|---|---|
| 83 | open on GitHub; **fixed on this branch** (`b66dbf8`, in 2.0.41) | "Beacon: moving Shape during a long stage slides the level by up to 8.75 V". Now a breakpoint (sections 1.1, 2.6). Closes when `fix-waves` (and this branch) reach main |
| 84 | open on GitHub; **fixed on this branch** (`54de7e7`, in 2.0.41) | "Beacon: test_view.cpp:178 compares floats exactly and fails with the plugin's flags". `test_view` passes with the plugin's flags (section 11) |
| 76 | closed 2026-11-23 | "Beacon: the window's Release line and the dot are still misaligned". Its fix (2.0.36, 2.0.37: a stage that skipped others drawn from where the last was cut off) is **superseded in 2.0.44**: every stage is drawn over its own span, and a Release that cut Attack short is drawn in the R span from its starting level. Retest the user's repro against the new rule (ADSR, A 4.64 s, D 300 ms, S 5 V, R 6 s, gate from a Sway square, 39.4 s cycle, PW 11.78 %, so the gate falls as Attack completes: expect Release drawn in the R span from about 10 V, the dot at OUT's level throughout) |
| 64 | open, tech debt | "Polyphony: modules should be polyphonic where it makes sense". The spec says it names Beacon; Beacon is polyphonic by design and needs nothing from it |
| 67 | open, tech debt | Aliasing measured for every module. Beacon is recorded "not needed" in TESTING.md and has no bound |

Found while preparing this document, **not filed** (the maintainer should decide; no issue was opened from this task):

1. The 2.0.39 edition's item 1 (`test_view` failing with the plugin's flags) and item 2 (the Shape slide) are fixed (#84, #83 above). `make test` still builds Beacon's tests without the plugin's fast-math flags (Beacon is not in `PLUGIN_MATH_TESTS`), so a regression of that kind would again show only in a plugin-flags build.
2. The manual does not say the latched push button is saved with the patch (section 3).
3. Switching AD to ADSR with the gate high while AD's fall is already below the sustain level gives a **rising Decay** (the D gate high while OUT climbs to the sustain level over Decay's time, section 1.1). The spec and manual say the fall "heads for Sustain", which is literally true, but neither says it can go up. The 2.0.44 edition's item 3 (a Decay or Sustain switched to AR) is moot: 2.0.45 turns them into AD's Decay.
4. The engine still publishes `prevStage` and `prevPhase` for channel 1 every sample, and `published_prev_stage` still tests them, but since 2.0.44 the window does not read them. Harmless; dead state.
5. A Release switched into AD restarts as a Decay over Decay's full time, so a long Release cut by the switch can end much sooner or later than it would have (by design in the spec; noted because the R knob stops mattering mid-stage). (The 2.0.44 edition's item 5, a stale `BeaconEngine.hpp` comment, is fixed: it reads "ADSR, AD, HOLD".)
6. The manual's old-patch note names only Trig; patches saved on 2.0.44's AR (Mode value 1 too) also open in AD with the fall moved from R to D. The 2.0.45 listening guide does mention AR. Also, from the 2.0.44 edition, still true of the code: the 2.0.44 listening guide's verdict row "SUSTAIN AT 0 V" (dropped from the 2.0.45 guide) said that after Decay to 0 V "the R light and R gate still run for the whole Release with nothing moving". In the code a Release that starts at its goal (0 V) ends on its first sample: with Sustain 0 V, the R gate is high for 1 sample [meas] (the R light still shows for its 40 ms minimum). A Sustain a hair above 0 V (knob not fully left, or a small CV) does give a full-length Release of a few millivolts, which matches the guide's description; at exactly 0 V it does not. Worth checking which case the owner saw before deciding.

---

## 11. What the automated suite covers

All of the following passed in my run on the 2.0.45 working tree (section 14). Six programs: `test_timing`, `test_voice`, `test_engine`, `test_view`, `test_switch` and `test_hostile` (built with ASan and UBSan, `-O1`). I built them twice: as `make test` does (`make -f tests.mk build/test/beacon/<test>`: `-O2`, `-Wall -Wextra`), where all six passed; and the five others with the plugin's flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -Wall -Wextra`), where all five passed with no warnings (in 2.0.39 `test_view` failed 4 checks here). Also passed: `tools/test_gen_panels.py` (115 tests). The bench (section 9.1) was run on `f15953b`; the AD change adds no per-sample work (a mode comparison per stage hand-on) and was not re-benched. Not rerun for this edition: `make bench-modules`, `make engine-globals` (both passed on 2.0.39; the rework adds no global and no module); `make libm-calls` (TESTING.md now runs it on macOS too, not run by me); `make smoke` (needs Rack); the full `make test` (every module). The tests print only "all checks passed".

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
| part_way | Release from 5 V at 2 s takes 2 s; a retrigger at 8 V in Decay takes the rest of Attack's curve, 2400 x (1 - p0) samples within 2 (912 at Shape 0.75 [meas]; 2.0.39 asserted the full 2400), and ends at 10 V; Decay to 10 V ends at once |
| gate_mode | a short gate releases from where Attack got, no Decay or Sustain; 1 ms gate with 1 ms Attack peaks at 10 V |
| ad_mode (2.0.45; replaces 2.0.44's ar_mode, which replaced trig_mode) | AD with gates of 1, 48, 1200, 2399, 2400, 2401, 4800, 24000 and 1,000,000 samples (A 50 ms, D 30 ms, R 200 ms): each gives exactly A, D, idle; Attack 2400 and Decay 1440 samples within 1; peak exactly 10 V, the fall from the top and ending at exactly 0 V; the S and R gates never high. 1 ms triggers every 50 ms into a 200 ms Attack reach 10 V at sample 9599 within 2 |
| attack_resumes (new) | at Shapes 0, 0.25, 0.5, 0.75, 1: RETRIG from Sustain at 6 V takes 4800 x (1 - p0) samples within 2 (p0 by an independent 60-step bisection on the curve), ends at 10 V, and its first sample is on the curve just past p0 within 2 mV; a gate back during the fall (Release in ADSR, Decay in AD, checked to be falling first) takes 4800 x (1 - p0) within 2 |
| retrig | RETRIG high restarts Attack from 5 V and takes 480 x (1 - p0) samples within 2 (349 [meas]; 2.0.39: 480) to 10 V; RETRIG low does nothing; a rising GATE while releasing restarts Attack within 0.25 V; RETRIG in AD mode with the gate high restarts Attack |
| no_jumps | retrigger mid-Attack, mid-Decay, mid-Release; gate ending in Attack and in Decay; Attack time swept 1 ms to 60 s and back; Shape swept mid-stage: every step at most 1.5 x the undisturbed stage's own largest step + 0.1 mV (at Shapes 0, 0.5, 0.75, 1) |
| sustain_follows | the 2 ms follower; the 5 Hz LFO within 0.5 dB; noise on Sustain stays in 0..10 V |
| gates_and_trig | gates coherent with the stage; IDLE A D S R IDLE; five TRIG pulses each 1 ms; merge of starts 0.3 ms apart; one-sample stage gate |
| hold | rise in A's time, fall in D's; negative input followed; clamp at +/-10 V; slow sine passes unchanged; hysteresis (1 mV, 2 mV); no chatter on dither; NaN holds; four TRIG pulses; mode entry and exit rules |
| mode_switch | ADSR to Hold; Hold to ADSR high from -4 V (Attack's full 480 samples: outside 0..10 V the old rule); Hold to AD low (AD's Decay rises from -4 V to 0 V in Decay's time, 480 within 1); ADSR to AD mid-Attack carries on and ignores the gate's fall; AD to ADSR with the gate already low carries on to 10 V, then Release; ADSR to AD in Decay, Sustain and Release (at 0.03, 0.3, 0.45 s): a Decay with goal 0 V from the level, taking Decay's 4800 samples within 2, then idle; AD to ADSR mid-fall with the gate high (Decay toward Sustain) and low (Release); audio-rate switching |
| nan | every field NaN, infinite or 1e30 in every mode: OUT finite and within +/-10 V |

### 11.3 test_engine (BeaconEngine.hpp)

| Test | Guarantees |
|---|---|
| poly_independent | 16 channels with different gates each equal a lone Voice bit for bit |
| mono_cv_to_all, poly_cv_per_channel | mono CVs and RETRIG apply to every channel; poly CVs per channel; missing channels read 0 V |
| channel_count | GATE 4 to 7 to 2 to 4 channels; new channels start at 0 V idle; the count follows GATE's, Hold's Sustain CV's, 1 unpatched, capped at 16 |
| hold_collapses_channels | ADSR to Hold to ADSR with four held channels (section 5.4) |
| push | the button gates every channel, GATE patched or not |
| schmitt | 0.1 V / 1 V per channel; unpatched GATE reads low; a replug at 0.5 V is no edge |
| mode_switch | every 7 ms for 2 s through all modes (Attack's step bound now the full 0-to-10 V curve's slope, as a resumed Attack runs on it); Decay's goal taken as 0 V in AD; then a fresh gate in ADSR settles in Sustain at 5 V (the gate is lowered first: a gate held through AD is no new edge); Hold to ADSR with the gate low from -3 V |
| inv_exact | INV = -OUT bit for bit, including negative OUT in Hold (Decay now 20 ms so Hold falls far enough to go negative in its stretch) |
| published, published_start_goal, published_prev_stage | `published`: Mode 1.2 (rounds to AD), the gate held half a second, the stage walks A, D, IDLE (2.0.44: A, R, IDLE in AR; 2.0.39: A D S R IDLE in Trig), phase 0..1, level equals OUT, mode AD; `published_prev_stage`'s one-shot check: AD's Attack completes into DECAY after ATTACK; start and goal latched; the previous stage and its phase (still published, no longer drawn), the user's #76 case (Decay cut off at 0.0043, Release from about 9.95 V) |
| stage_lights | one sample of Decay lights D for 40 ms; a longer stage goes out by the first update after it ends; reset puts all out; the hold covers a 30 Hz frame |
| rate_change | 48 to 96 kHz mid-Attack: no step, Attack ends at its time in seconds |
| shape_smoothing | Shape 0 to 1 mid-Attack on the default 10 ms Attack: largest step at most 1.5 x own + 0.1 mV; the settled Shape equals the target exactly |
| shape_long_stage (2.0.41) | 10 s Attack, Shape 0 to 1 at phase 0.1 and 0.5: largest step within 1.5 x the larger of the stage's own and the new curve's steepest from the breakpoint + 0.1 mV; Attack takes exactly its samples; after Shape settles OUT is Shape 1's curve from the published breakpoint within 0.1 mV; a new stage's breakpoint is its start; reset clears it |

The file also `static_assert`s that the published atomics are lock-free.

### 11.4 test_view (EnvelopeView.hpp)

`window_lines_up` (new; section 8.2: a real Voice through ADSR and AD, four Shapes, two Sustains, six gate patterns and a RETRIG; the dot at OUT's level within 2e-4 of the height and in its own non-empty span on every sample (the empty-span failure is new in 2.0.45), and a kept line still predicting OUT); widths grow with log time and stay inside the window; heights match the curve; the dot's position per stage and phase; `live_stage_curve` (start at p 0, goal at p 1, the voice's curve between); `live_dot_on_line` (passes with the plugin's flags since 2.0.41); `live_span_own` (new; replaces `live_span_after_skip`: every stage's span is its own; a Release from 9.7 V starts at the R span at that level and ends at the right edge at 0); `ad_layout` (2.0.45; replaces 2.0.44's `ar_layout`: no S or R span, `x[2] = x[3] = x[4] = 1`, the A-to-D width ratio kept, up to the top and Decay from the top to 0, the AD dot on its outline); `live_breakpoint` (2.0.41: the line and dot from a real voice's Shape breakpoint); `sustain_zero`; `hold_ring_time` (3 s at 60 Hz, at 144 Hz and at a jittery 30 to 50 Hz write the same number of points).

### 11.5 test_switch (Toggle3Step.hpp)

The click table (every value and half); no wrap (one click from AD to ADSR, never through Hold; clicking one half repeatedly walks to that end and stays); halves; vertical drag per 12 px, held at the ends, a drag back moves at once, no click on release after a whole step; NaN travel ignored.

### 11.6 test_hostile

Section 9.3.

### 11.7 Not covered by the automated suite

- **Everything in `Beacon.cpp` that needs Rack**: drawing of the window (layer, colours, strokes, glow, the font and letters), the Hold line's live use of Rack's frame time, the lights' brightness path, `ShapeQuantity` and `TimeQuantity` (the tooltip and typing as Rack calls them; the parse and format helpers are tested), bypass and un-bypass, the Initialize path, the module browser's default drawing.
- The `Toggle3` widget with a real mouse (press halves, drag, undo, the 12 px drag) and the push button's shift-click latch.
- Rack's save and load of the push button, Randomize, and polyphonic cables as Rack hands them (`getVoltages()` content past the channel count).
- The libm check (Linux only), a native x86-64 or Windows CPU budget, `make smoke` (not run by me).
- Aliasing, by design (no bound measured).
- The Rack host's order of events on a sample-rate change, and any block-size effect.
- How a resumed Attack sounds and looks in Rack (the window draws it as the dim Attack curve; only the geometry is tested).
- Mode switches into and out of AD as drawn in Rack (the window changes layout on the switch sample; `window_lines_up` does not switch modes).
- A rising Decay after AD to ADSR with the gate high from below the sustain level (section 1.1): tested by `mode_switch` only for its stage and goal, not for how it looks or sounds.
- Whether the user's patches (Fathom, Sluice, Narrows, Rigging, Fog) behave as the manual's "Patches to try".

---

## 12. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, all knobs at default (Shape +50 %, A 10 ms, D 300 ms, S 5 V, R 1 s), Mode ADSR, nothing patched except where stated. A scope is needed for times; the stage gates and TRIG are the easiest timing references.

**Method notes.** Time a stage with its stage gate on a scope rather than by eye. For Decay or Release from a level, expect the same time as from the top (they are timed). For an Attack from a level, expect **less** than the set time: Attack x (1 - p0), section 5.1's table (from 6 V: 40 % at noon, 64 % at the default Shape). For an Attack under 5 ms, use the A gate's width, not the OUT slope.

### 12.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Time tooltip | hover A, D, R at knob 0, default, noon, 1 | 1.0 ms, 10.0 ms / 300 ms / 1.00 s, 245 ms, 60.0 s (section 2.3) |
| Time law | gate held long, A at each of 1 ms, 10 ms, 100 ms, 1 s, 10 s, 60 s (type the values); time the A gate | within 1 sample at 48 kHz (exactly ceil(T x 48000)); same time at Shape left, noon and right |
| Typed times | type `250`, `250 ms`, `1.5 s`, `90 s`, `abc`, `0.5`, `1e3` into each | 250 ms; 250 ms; 1.50 s; 60 s; unchanged; 1.0 ms; 1.00 s |
| Time CV | +1 V, +2 V, -1 V into A with the knob at noon; time Attack | about 736 ms, 2.21 s, 81.5 ms (x3.005 per volt); +5 V and above 60 s; -5 V and below 1 ms; a CV on a mono cable also applies to all channels |
| Sustain | gate held long; turn S from 0 to full | OUT holds at 0 to 10 V; S at 0 is ADR (Release from where Decay ended); moving S during Sustain glides in about 10 ms with no click |
| Sustain CV | +/-5 V LFO on the Sustain CV in ADSR | the level follows (tremolo); the sum held to 0..10 V (never below 0 V). In AD it has no effect (Decay falls to 0 V whatever S is) |
| Shape | A 2 s, R 4 s; Shape left, noon, right (section 2.6) | Left: slow start, fast finish; decay holds then falls. Noon: straight. Right: fast, rounded Attack; fast-then-long-tail fall. The window's curve follows; the stage time is unchanged |
| Shape tooltip | hover | "Linear" only at exactly noon; the default reads +50 %; ends -100 % and +100 % |
| Shape move mid-stage | 10 s Attack, turn Shape fully across while it runs | no jump: the level carries on from where it is and bends into the new curve; the Attack still ends at 10 s; the window's bright line redraws from that point (sections 1.1, 2.6). (2.0.39 slid by up to 8.75 V: if that is seen, file it) |
| Mode switch | read the legend; hover; click the upper and lower halves, drag, double-click, undo | legend ADSR / AD / HOLD top to bottom; tooltip "Mode: ADSR", "Mode: AD (one-shot)", "Mode: Hold" [unverified: Rack's tooltip format]; steps one position by half, never wraps (AD to ADSR in one click, never via Hold); stops at the ends; drag 12 px per step; one undo step per gesture; double-click does not reset [unverified] |
| Push button | hold, release; shift-click | a gate on every channel while held; shift-click latches until the next click; does nothing in Hold |

### 12.2 Modes

| Procedure | Pass criteria |
|---|---|
| ADSR: long gate, then a gate shorter than Attack (A at 1 s, tap) | long: A, D, S, then R on release; short: Release from where Attack got, no D or S |
| ADSR: 1 ms trigger, A at 50 ms; A at 1 ms | a faint blip (about 0.5 V), then Release; with A at 1 ms a sharp pluck near 10 V (about 9.9 V at 44.1 kHz) |
| ADSR: gate widths against Attack (A 50 ms, D 200 ms; pulses of 1 ms, 30 ms, 150 ms, 1 s) | 1 ms: a blip; 30 ms: part way up, then Release; 150 ms: to the top, part way down Decay, then Release from there; 1 s: the full ADSR (the manual's table) |
| AD: a 1 ms trigger, a 100 ms pulse, a 2 s gate, A at 50 ms, D 300 ms | all three identical: up to 10 V in 50 ms, then down to 0 V in 300 ms; only the A and D lights and gates; S and R never fire; turning S or R changes nothing; TRIG three pulses each time (A, D, idle) |
| AD: hold the push button | one AD; holding longer changes nothing; release and press again for another |
| AD: a Sway square at 1 % width, 0.1 Hz and 1 Hz, A 50 ms | the same clean AD from every pulse (in 2.0.39's Trig, the 100 ms pulse ran on into Decay and Sustain) |
| AD: 1 ms triggers every 50 ms, A 200 ms, D 100 ms | climbs steadily and reaches 10 V 200 ms after the first trigger (sample 9599 at 48 kHz), then dips a little in Decay between triggers and catches the top again |
| Resumed Attack: ADSR, A 1 s, S 6 V, gate held into Sustain, then RETRIG; Shape at noon, default, fully right, fully left | back to 10 V in about 0.40 s, 0.64 s, 0.80 s, 0.12 s; no jump at the start; the A gate's width shows it |
| New gate during the fall (Release in ADSR, Decay in AD) | Attack restarts from the level: no jump, no drop to zero; it takes only the time left on its curve (section 5.1), not the full set time |
| RETRIG, gate high; RETRIG, gate low (both modes) | high: Attack restarts from the present level; low: nothing happens (so in AD with short triggers into GATE, RETRIG does nothing) |
| ADR, gated AR | ADSR, S at 0: A, D, then S gate high while the gate is high with OUT at 0, then R; ADSR, D at its minimum and S full: an AR held while the gate is |
| Hold: slow LFO then steps into the Sustain CV | OUT follows, rising at A's time per 10 V and falling at D's; A, D, S gates as in section 5.3; R low; the window shows the scrolling line |
| Hold with nothing patched, S at default | OUT 5.000 V, S gate high, one channel [meas]; S fully left: 0 V |
| Hold: -10 to +10 V step with A = 1 s | 2.00 s up; with D = 300 ms, 0.60 s down |
| Hold: button, GATE, RETRIG | nothing happens |
| Open a patch saved on 2.0.43 or earlier with the switch on Trig, and one saved on 2.0.44 with it on AR | both open on AD; the fall after Attack is now on the D knob (it was R), ends at 0 V, and a long gate no longer gives D and S (Trig) |

### 12.3 Mode switching while running

| Procedure | Pass criteria |
|---|---|
| ADSR to Hold to AD to ADSR (down, down, up, up) during a held gate | OUT never jumps; with the gate high, leaving Hold starts Attack from the level (resumed along its curve from 0..10 V) |
| Hold to ADSR, and Hold to AD, with the gate low | ADSR: Release from the level to 0 V in Release's time (R gate); AD: Decay from the level to 0 V in **Decay's** time (D gate) |
| Switch ADSR to AD mid-Attack, gate held | Attack carries on to 10 V, then falls over Decay to 0 V (no Sustain); the window redraws as A and D |
| Switch ADSR to AD mid-Decay, in Sustain, and mid-Release | each falls at once from its level to 0 V over Decay's full time, D gate high, one TRIG pulse at the switch; the window shows A and D, the dot on the bright Decay [test] |
| Switch AD to ADSR mid-fall, gate high; again with the gate low | high: a Decay toward the sustain level over Decay's time, then Sustain; low: Release from the level over Release's time |
| The same, gate high, with S above the level where AD's fall is (e.g. S 8 V, fall at 3 V) | the Decay **rises** to S over Decay's time with the D gate high, then Sustain; no jump. Record whether that surprises (section 1.1) |
| Switch AD to ADSR mid-Attack with the gate already low | Attack carries on to 10 V, then Release |
| AD with a gate held past the one-shot, then switch to ADSR | nothing starts until the gate falls and rises again (no new edge) |
| Four-channel gate into GATE; switch to Hold with the Sustain CV unpatched; back | one channel in Hold (outputs 2 to 4 at 0 V); back in ADSR three new channels start from 0 V |

### 12.4 Stage gates and TRIG

| Procedure | Pass criteria |
|---|---|
| scope A, D, S, R and TRIG on a long gate (ADSR) | each gate 10 V while its stage runs; five TRIG pulses of 1 ms (0.998 ms at 44.1 kHz) at the start of A, D, S, R and the return to idle |
| AD, any gate (1 ms to many seconds) | three TRIG pulses (A, D, idle); gates A then D; S and R never high; with D at 1 ms, two (D and idle merge) |
| ADSR, a 1 ms trigger with A 10 ms | two TRIG pulses in all: one about 2 ms wide (the A and R starts, 1 ms apart, merge), then the 1 ms idle pulse when Release ends |
| stages under 1 ms apart | one longer TRIG pulse |
| Decay with Sustain at 10 V | D gate high for one sample |
| TRIG into Narrows PING; A gate into Rigging STRUM (manual) | ADSR, long gate: five pings; AD: three; a strike as Attack starts |

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
| switch to AD | the window redraws with only A and D (defaults: A 14.3 mm, D 24.9 mm), lettered A and D, Decay falling from the top to the bottom right; back to ADSR: four stages again |
| AD, a trigger | A bright, then D bright from the top to 0 V; the dot rides each; no S or R anywhere |
| ADSR, gate falls half way up Attack | Release is drawn bright in the **R span** only, from that level at the R span's start down to 0 V at the right edge, the dot on it; nothing bright sweeps back across A, D or S (2.0.37's widening is gone) |
| retrigger from Sustain or Release | the bright Attack is the drawn Attack curve itself, from 0 V at the span's start; the dot joins it at the present level and rides it to the top |
| the dot against a scope of OUT, through short gates, pulses and retriggers in both modes | the dot's height always matches OUT's level; the dot never leaves the running stage's span |
| Sustain 0 V | the Sustain span dim; the dot at the end of the line during Sustain |
| CV on A, D, R, S | the window's widths and heights follow channel 1's CV |
| Hold | a scrolling line, about 2.7 s across, 0 V line faint, "HOLD" top left; the same speed at 60 and 144 Hz; entering Hold starts it flat at the level |
| room lights dimmed | the window stays visible (self-illuminated layer); dim strokes still show |
| Shape knob moved | the drawn curve follows at once; during a stage, the bright line redraws from the point where Shape changed |

### 12.7 Polyphony

| Procedure | Pass criteria |
|---|---|
| 4-voice gate cable into GATE | four envelopes; OUT, INV, the gates and TRIG each carry 4 channels |
| mono CV into A with a poly gate; then a poly CV | mono: the same time for every voice; poly: each its own |
| poly CV with fewer channels than the gate | the extra voices read 0 V on that CV |
| change the gate cable's channel count while voices play | voices that stay keep going; new ones start at rest |
| Hold with a poly V/OCT into the Sustain CV | one slew per channel; count follows the CV |
| 16 channels | no crash; CPU about 1.0 to 1.4 % of an M-series core, depending on load (section 9.1) |
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

I compared the 2.0.45 guide (`docs/beacon/listening.html`, "test build 2.0.45", uncommitted when read) with the 2.0.45 working tree. Everything in it that I checked matches. Notes:

- Its figures for a resumed Attack (from 6 V: 40 % at noon, about 64 % at the default, 80 % right, 12 % left; ADSR, A 1 s, S 6 V, RETRIG: about 0.4 s at noon, 0.8 s right, 0.12 s left) match the code [meas, section 5.1].
- "AD, A 50 ms, D 300 ms. A 1 ms trigger, a 100 ms pulse, a 2 s gate: all three up to 10 V in 50 ms, then down to 0 V in 300 ms. Only the A and D lights and gates; S and R never fire, and turning S or R changes nothing" matches (`ad_mode`; section 2.9).
- "Hold the push button in AD: one AD" matches: the button is a gate, and AD ignores its length.
- "ADSR with a long gate: five pings; AD: three (Attack, Decay, rest)" matches section 2.9's table.
- "Open a patch saved with the switch on Trig (or on AR in 2.0.44): it opens on AD; the fall is now on the D knob" matches [code]; the manual's note names only Trig (section 10, item 6).
- "Move the switch ADSR, HOLD, AD, ADSR while an envelope runs: the output never jumps. To AD: a Sustain or Release falls to 0 V over Decay. Back to ADSR with the gate high: the fall heads for Sustain" matches; it is two down-clicks then two up-clicks. It does not say that "heads for Sustain" can be a rise (section 1.1).
- "RETRIG while the gate is low: nothing happens (in both modes)" matches; the guide asks the owner whether AD should accept RETRIG without the gate high.
- The 2.0.44 guide's "SUSTAIN AT 0 V" verdict row is gone from this guide (section 10, item 6 keeps the finding).
- "Hold: unchanged in this build" is right; "S fully left" for a plain glide still matters, because the default S is 5 V, which in Hold is a 5 V offset (section 5.3).
- The guide refers to a retest report (`beacon-2.0.45-retest.html`) with scope traces; it was not on the share when I looked, and I did not use it.
- Neither the guide nor the manual says that the latched button is saved with the patch.

## 13. Glossary

| Term | Meaning |
|---|---|
| ADSR, ADR, AR, AD | Attack, Decay, Sustain, Release; ADR is Sustain 0; AR is an Attack held at the top while the gate is high, then a Release (on Beacon: ADSR with D minimum and S full); AD is an Attack and a Decay with no Sustain. On Beacon's switch, **ADSR** is the gated mode (was Gate) and **AD** the one-shot mode (replaced Trig; AR in 2.0.44) |
| Attack, Decay, Release | the timed stages, 1 ms to 60 s. Decay and Release take their set time from wherever they start; Attack from a level takes the time left on its curve |
| Breakpoint rule | Tides' rule: a stage starting part way rescales its curve between the present level and its goal. Beacon uses it for Decay (to Sustain in ADSR, to 0 V in AD), Release, an Attack from outside 0..10 V, and a Shape change mid-stage |
| Resumed Attack | since 2.0.44: an Attack begun from a level L in 0..10 V starts at the point p0 of its full curve at L and takes Attack x (1 - p0) |
| One-shot | AD mode: Attack to 10 V, then Decay to 0 V, whatever the gate does |
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

**2.0.45 (this edition).** In `/private/tmp/claude-501/-Users-rarneson/0921926a-3049-46b3-bf60-de6e15f66964/scratchpad/meas/`, built against the repo's `src/` with `c++ -std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -I<repo>/src -I<repo>/src/beacon`. Scratch only; not in the repo.

- `meas_ad.cpp` (2.0.45 working tree): stage trails and TRIG pulse counts for six gates in ADSR and AD, AD with a 1 ms Decay, fast triggers into a long Attack in both modes, RETRIG in AD with the gate low and high, a gate back during AD's Decay, Hold to AD at 5 V, -4 V and 0 V with the gate low and high, ADSR to AD mid-Decay, in Sustain and mid-Release, AD to ADSR mid-fall with the gate high and low (S 5 V and 8 V), AD to ADSR with the gate held past the one-shot, and the window's AD widths (sections 1.1, 2.9, 5.1, 5.2, 5.4, 8).

**2.0.44 (the previous edition, same day; the programs below still apply where noted).** Same folder, built against the working tree at `f15953b` with `c++ -std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -I<repo>/src -I<repo>/src/beacon`. Scratch only; not in the repo.

- `meas_ar.cpp` (2.0.44 only for its AR parts; the resumed-Attack and ADSR figures still hold): resumed Attack times by Shape and level (the voice's sample counts and the curve's inverse), the `part_way` and `retrig` cases, stage trails and TRIG pulse counts for six gates in ADSR and AR, fast triggers into a long Attack, the ADSR blip, RETRIG in AR, Hold to ADSR at 5 V and -4 V, ADSR to AR mid-Decay, AR to ADSR with the gate held, and the window's ADSR and AR widths (sections 2.6, 2.9, 5.1, 5.2, 5.4, 8).
- `meas_shape.cpp`: a 10 s Attack through `BeaconEngine`, Shape 0 to 1 at phase 0.1 and 0.5: first and largest steps and the Attack's length (sections 1.1, 2.6). Still applies (Shape code unchanged).
- `meas_s0.cpp`: the R gate's length for a Release from Sustain at 0 V (section 10, item 6).
- The six test programs with the plugin's flags are in `.../scratchpad/pf/`; the bench is the repo's `build/bench/bench`, rebuilt by `make -f tests.mk build/bench/bench`.

**2.0.39 (figures for unchanged code are kept from it).** All in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/beacon/meas/` (an earlier session's scratchpad; it may no longer exist), built against the extracted `2e6e960` tree (`.../beacon/tree/src`) with `c++ -std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -Itree/src`. Outputs are in `.../beacon/out/`.

- `meas_time.cpp`: the time law, tooltip text, typed values, CV law, curves and the window's widths (sections 2.3, 2.6, 8.1).
- `meas_voice.cpp`: stage lengths in samples at 44.1 to 192 kHz, edge latency, TRIG width at 8 to 768 kHz, the Sustain follower, Sustain moved during Decay, Hold slew times and the Hold S/A/D shares of a sine (sections 2.4, 2.9, 5).
- `meas_light.cpp`: the stage lights' on-time, a Shape move on a 10 s Attack (2.0.39's slide, superseded), Hold with nothing patched, and the channel-count and Schmitt rules through `BeaconEngine` (sections 2.6, 2.10, 5.3, 7).
- `meas_misc.cpp`: the tooltip's error from 10 ms up and below, and the largest level shift of a Shape move (2.0.39 only; superseded).
- `../view_ulp.cpp` and `../tv_copy.cpp` (a copy of `test_view.cpp` with the failing equality printed): the `test_view` fast-math finding (fixed in 2.0.41).
- Tests are built in `tree/build/test/beacon/` (as `make test` does) and `pf/` (the plugin's flags). The bench is `tree/build/bench/bench beacon <idle|outputs|busy> <1|4|16> <seconds>`.

The programs drive `rka::BeaconEngine` and `rka::beacon::Voice` as `Beacon.cpp` does (one frame per sample; for the lights, `BeaconLights` called as the module calls it: `note()` every sample, `update()` every 16 samples). They do not exercise Rack's ports, threading, drawing, mouse or the host's order of events. Anything marked [meas] should therefore match Rack at 48 kHz, but Rack-side effects are outside them.
