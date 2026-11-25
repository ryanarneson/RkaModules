# Squall 2.0.39: Technical Reference for QA

Module: **Squall**, plugin `rka` (slug `rka`, module slug `Squall`), VCV Rack 2 (minRackVersion 2.5.0). 8 HP. Tags in plugin.json: Random, Sample and hold, Clock modulator.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from that tree (`git archive 2e6e960`). Squall's files have not changed since build 2.0.33 (commit `63c28b1` is the last to touch `src/squall/` and `docs/squall/`; `git diff 051d217 2e6e960` shows no change in `src/squall`, `docs/squall` or `res/squall`).
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It is a zstd-compressed tar. It holds `plugin.json` with version 2.0.39 (byte-identical to the commit's) and `res/squall/Squall.svg` and `Squall-dark.svg` (byte-identical to the commit's). There are no factory presets for Squall.
Document date: 2026-11-24.

## Build history

Squall has been in four test builds (CHANGELOG.md, `git log -- src/squall docs/squall`):

| Build | Date | What changed for Squall |
|---|---|---|
| 2.0.12 (`26d79ac`) | 2026-11-18 | First build for listening: Weather, Gust, Memory, Clock, Freeze, Step, Swell, Gate, four ranges, loop length 4/8/16. Panel, manual, spec. |
| 2.0.13 (`b386e74`) | 2026-11-18 | Final-review fixes. Gust's excitation now lives in average gaps, so a clock being patched or unpatched adds no burst and Weather CV does not change the long-run rate. An infinite value on Clock or Freeze reads 0 V. Manual: Swell's pace at Stormy is "a fraction of a second". |
| 2.0.33 (`051d217`, PR #73) | 2026-11-23 | Three small fixes: **#47** the clock period is measured afresh after Freeze; **#48** Initialize no longer snaps Swell to the middle (it glides); **#49** saved gaps over 50 average gaps and gate lengths over 1.0 load as the default. |
| 2.0.34 to 2.0.39 | 2026-11-23 to 24 | No change to Squall (Beacon, Neap and Spring builds). |

The manual's sentence about skipped ticks at Stormy (section 1.1) was written for 2.0.12 and was not touched by the 2.0.13 rewrite of Gust's excitation.

---

## 0. How to read this document

- Every number here comes from one of these sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/squall/*.hpp`, `src/squall/Squall.cpp`, `src/FastMath.hpp`, `src/hadal/Random.hpp`), cited as file:line. "[code arithmetic]" is a number worked out by hand from such constants.
  - **[test]**: asserted by Squall's automated tests, which I rebuilt from the 2.0.39 tree with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; the hostile test with ASan and UBSan at `-O1`) and ran on an Apple M5 Pro Mac. All four test programs passed. The tests print only a pass line, so [test] figures are the bounds in the test source, not measured values.
  - **[meas]**: a short program I wrote against the engine (`rka::SquallEngine`, the code the module runs), compiled with the same flags. They are listed in the note at the end. Unless stated: 48 kHz, free-running, Memory 0, Gust 0.3 (the module's default), range ±5 V, Loop length 8.
- **[issue #N]** points to a GitHub issue in the RkaModules repository (section 8).
- **[unverified]** marks anything I could not check against code or by measurement. Mostly that is Rack host behaviour. I did not run Rack.
- Voltages are Rack volts. "Noon" is a knob value of 0.5. The knob value of every control is in [0, 1] (attenuverters in [−1, 1]).
- "Average gap" is the length of time the module treats as the typical time between events: 1 / rate(w) free-running, and the measured clock period / p(w) when clocked. Gaps, gate lengths and Swell's pace are all in average gaps, so they follow Weather.
- "Event" is one new or replayed step of Squall: Step takes a value, the gate rises, the light flashes.
- Squall draws its randomness from a PCG32 generator seeded per instance, so any single run is one sample of a random process. Counts and ratios below are measured over long runs. A single short run will differ by the amount the statistics allow (section 2.5 gives the spread).

---

## 1. What Squall is

Squall is an 8 HP random source of gates and CV, "the weather of a piece" (spec `docs/specs/2026-11-18-squall-design.md`). It makes **events** (gates and a stepped CV that jumps at each one) and **drifting CV** (a smooth follower of the stepped CV). All three outputs tell one story. One dial, **Weather**, sets how much happens, from one event in ten minutes to eight a second. **Gust** makes events come in bursts without changing the average. **Memory** replays the recent past. It runs free, or it locks to a clock.

Design goals that QA can check:

- Gate, Step and Swell tell one story: Swell follows Step.
- A range of pace of about 4800 to 1 on one dial: 1 event per 10 min to 8 per s.
- Gust changes the rhythm and never the average number of events.
- Memory from "never" to "an exact loop".
- Free-running, or events only on clock ticks.
- CV with an attenuverter on Weather, Gust and Memory.

Anti-goals (a failure even if the module otherwise works) and how they can be checked:

- **A pulse you can hear.** At Gust 0 the gaps are exponential; at any Gust the timing never settles into a beat, unless Memory locks a loop on purpose.
- **Gust that changes "how much".**
- **Swell that sounds like an LFO.** No regular period, no overshoot past its range, no corners.
- **A dead module.** At Calm there can be minutes of silence; the event light shows that Squall is alive.
- **Clicks where Squall drives a level.** Step jumps by design. Swell never jumps, including on a range change, Freeze, Initialize or a reload.

Non-goals (do not file these as defects): a Noise output, polyphony (a polyphonic CV uses channel 1; the question is open as issue #64), a Reset input, seeds or recallable sequences, scales or pitch quantising, chaos systems, a Slew knob, a Lull (standalone slew) module.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Test against the code's behaviour; the spec's and the manual's versions are given for context.

| Topic | Spec / manual / listening guide says | 2.0.39 does |
|---|---|---|
| **Stormy on a clock with Gust up** | Spec: "At Stormy every tick is an event anyway", and its Clocked test: "Stormy fires on every tick". Manual: "With a clock at Stormy and Gust up, the odd tick is skipped: a short lull." Listening guide: "As built, with Gust up, the odd tick is skipped even at Stormy (the manual says so)." | **Every tick fires at Stormy in steady state, at any Gust.** 400,000 ticks at Weather 1 gave 0 skips at Gust 0, 0.1 and 0.3, 1 at Gust 0.5 (tick 0) and 2 at Gust 1 (ticks 0 and 1) [meas]. Nothing alternates: a hit-skip-hit-skip pattern never occurred in any run. Skips happen only while Gust's excitation y is still low, that is in the first ticks after the module starts or after Weather rises from a lower setting: from a fresh start, 0 to 26 ticks at Gust 1, 0 to 2 at Gust 0.3 [meas, section 2.7]. The cause is in `Events::tick` (`Events.hpp:94`): chance = p·((1−n)+y), and y builds up to exactly n only after events. The test `clocked_chance_per_tick` checks "fired == ticks" only at Gust 0 [test]. The manual's sentence is wrong as a steady-state description and is true only as a start-up lull. Just below Stormy (Weather 0.9 to 0.999) ticks are skipped at 1−p(w) of the time, and Gust makes the skips run in clumps (section 2.7) |
| **Noon tooltip** | Manual: "Rack's tooltip shows the free-running rate ('1 per 9.0 s')". Manual table and spec table: "about one every 9 s" | The tooltip at noon reads **"1 per 8.7 s"**. The average gap at noon is 8.660 s (`rate(0.5) = 4800^0.5 / 600 = 0.11547 /s`), shown with `%.1f` [code `Squall.cpp:24`; meas]. The table's "about 9 s" is a fair rounding; the quoted tooltip text is not. The listening guide says 8.7 s and is right |
| **Initialize and Swell** | Spec and manual: silent about Initialize. Issue #48 (closed): "Initialize jumps Swell to the middle" | **Initialize does not reset Swell.** `SquallEngine::reset()` leaves Swell's state alone (`SquallEngine.hpp:100`), so Swell glides from where it was to Step's reset value (0 V in the ±5 V range), taking about 17.5 s for 95 % of the way at noon [meas, section 3.3]. This is the fix issue #48 asked for (commit `244b445`, build 2.0.33), not a bug. Initialize also leaves Gust's excitation, the clock's period measurement and the random stream alone (section 3.3) |
| Weather CV "changes the rate at once" | Spec: "no smoothing needed" | The three controls are recomputed **every 32 samples** (0.67 ms at 48 kHz, 0.04 ms at 768 kHz), not every sample (`SquallEngine.hpp:105-108`). No smoothing, but audio-rate CV is sampled at 1500 Hz at 48 kHz [code; test `weather_cv_audio_rate`] |
| CV law "±5 V sweeps the whole range" | Spec and manual: "With the attenuverter fully clockwise, ±5 V from noon sweeps the whole range" | True for Weather from noon. Gust's default is 0.3, so ±5 V reaches −0.2..0.8 (clamped to 0..0.8). Memory's default is 0, so +5 V reaches 0.5 and −5 V does nothing. The same limit as Fog's issue #31, here by the same formula (section 2.3) |
| Gate on retrigger | Spec: "drops for about 1 ms" | 1 ms (48 samples at 48 kHz [test: `gate_retrigger`]), **shorter when events are closer than 4 ms**: the drop is min(1 ms, a quarter of the average gap) (`SquallEngine.hpp:323`). A 1 kHz clock at Stormy gets 0.25 ms drops [meas] |
| **Every event is its own rising edge** (clocked) | Spec: "If an event comes while the gate is high, the gate drops for about 1 ms and rises again, so every event is a separate rising edge." Manual: "every event is its own rising edge". Spec test: "An event during a high gate gives a fresh rising edge" | **Not when the gate ends on the very sample of the next event.** With a clock, the gate is 0.25 / p(w) ticks long. At the dial words that is a whole number of ticks (16 at FAIR, **4 at CHANGE, the default**, 1 at RAIN), so an event exactly that many ticks after the last one finds the gate just ended: there is no drop, the two gates join, and the second event has no rising edge (Step still changes). Measured over 3000 events at 100 Hz, 48 kHz [meas, `m_merge`]: **4.0 % of events lose their edge at noon, 25.5 % at RAIN (0.75), 1.1 % at FAIR (0.25)**; none at 0.40, 0.49, 0.51, 0.60, 0.76 or 0.90. The result is the same at clock periods of 441, 480, 1000 and 4800 samples. A trigger input sees one trigger instead of two. The tests do not cover it [test `gate_retrigger` drops the gate only while it is still high; `fast_clock_gate` uses a 1 kHz clock at Stormy]. Free-running it needs a gate to end on exactly the sample of an event, which I did not see |
| Gate "held to 5 ms to 4 s" | Spec and manual | Free-running, the gate never reaches the 5 ms floor: the shortest is 31.25 ms at Stormy. Clocked, the floor applies (a 100 Hz clock at Stormy gives 5.00 ms gates [meas]). The 4 s ceiling binds for every Weather from fully left up to w = 0.428 (a quarter of a gap of 16 s) [code arithmetic] |
| First gate on a clock | Spec: "Until two edges have arrived, the gate is 10 ms." | Exactly 10.00 ms for events on the first edge only. The second edge already measures a period (one interval is enough), so an event on it gets the measured length [meas]. See also the merge case in section 2.9 |
| Replayed events and the clock | Spec: "A stored gap becomes whole ticks: round(gap ÷ p(w)), at least 1" | As the spec says. A loop made free-running and replayed on a clock at Stormy (p = 1) therefore plays at one tick per event whenever the stored gap is under 1.5 average gaps [code `SquallEngine.hpp:340`] |
| Event light | Spec: "about 100 ms" | 100.00 ms of engine time (4800 samples at 48 kHz) [meas]. The module then smooths it with `setBrightnessSmooth` (`Squall.cpp:189`). An event during the 100 ms re-arms it, so at Stormy (mean gap 125 ms) the light is on about 55 % of the time [code arithmetic]. How it looks is [unverified] |
| Swell "reaches a new value in about the average gap" | Manual, listening guide ("about 9 s at noon") | Half way after 0.70 average gaps (6.1 s at noon), 90 % after 1.62, **95 % after 1.98 (17.1 s at noon)**, 99 % after 2.77 [meas]. The spec's own figures (never faster than about half a gap, 5 % within two gaps) hold |
| Weather tooltip unit | Manual: "(1 per 9.0 s)" form only | Three forms: "1 per 10.0 min" (gap over 90 s: Weather up to 0.2238), "1 per 72.1 s", and "8.0 per s" (rate of 1 or more: Weather from 0.7547) [code `Squall.cpp:16-28`]. At RAIN (0.75) it reads "1 per 1.0 s" (the gap is 1.04 s), not "1.0 per s" |
| Bypass | Spec: "All outputs are 0 V and the Gate is low" | No bypass routes are configured, so Rack's bypass gives 0 V on every output [unverified in Rack]. `process()` is not called while bypassed: the engine stops, and resumes where it stopped [code; unverified in Rack] |
| Spec's file list | Spec lists `docs/future/` edits and the Hadal spec note | Not part of the shipped module; not checked |

The panel positions, the knob defaults, the four ranges, the loop lengths 4/8/16, the gate fraction, the weather law and the tick law all match between spec, manual and code.

---

## 2. Panel, controls and ports

### 2.1 Layout

8 HP, drawn as an aneroid barometer. Subtitle "RANDOM GATES & CV". Positions in mm from `SquallLayout.hpp` (generated from `tools/gen_panels.py`) [code]:

| Control | Centre (x, y) mm |
|---|---|
| Weather (hero knob, 18 mm face radius) | 20.32, 34.0 |
| Gust / Memory knobs | 7.6, 60.0 / 33.04, 60.0 |
| Attenuverters: Gust / Weather / Memory | 7.6 / 20.32 / 33.04, y 72.5 |
| CV jacks: Gust / Weather / Memory | 7.6 / 20.32 / 33.04, y 83.0 |
| CLOCK, event light, FREEZE | 7.6 / 20.32 / 33.04, y 102.0 |
| STEP, SWELL, GATE (on ivory tiles) | 7.6 / 20.32 / 33.04, y 116.0 |
| Screws | 33.02, 2.54 and 7.62, 125.96 |

- The barometer face has 24 ticks (a long one every 6) and the words CALM, FAIR, CHANGE, RAIN and STORMY at −135°, −67.5°, 0°, +67.5° and +135° (`gen_panels.py`). The knob sweeps ±135° (`widgets.hpp:162`: ±0.75 π), so each word sits at Weather 0, 0.25, 0.5, 0.75 and 1.
- Each CV is traced up through its attenuverter to its control; Weather's trace runs into the face.
- The row heights (83, 102, 116 mm) line up with Hadal, Ballast, Fog and Sway.
- The panel generator's 115 tests passed (`python3 tools/test_gen_panels.py`) [test].
- How it looks is [unverified]; I did not run Rack or `make smoke`.

### 2.2 Parameter, input, output and light ids

`Squall.cpp:37-63` [code]. The param ids are tied to the engine's `SquallFrame` by a `static_assert` (`Squall.cpp:169`). Saved patches store params by id, so these numbers must never change.

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 | Weather | 0..1 | 0.5 | "Weather (average events, free-running): 1 per 8.7 s" |
| 1 | Gust | 0..1 | 0.3 | "Gust (how events bunch up): 30%" (percent, value × 100) |
| 2 | Memory | 0..1 | 0 | "Memory (chance of replaying the past): 0%" |
| 3 | Weather CV amount | −1..1 | 0 | "Weather CV amount: 0%" (−100 to +100 %) |
| 4 | Gust CV amount | −1..1 | 0 | "Gust CV amount" |
| 5 | Memory CV amount | −1..1 | 0 | "Memory CV amount" |

| Id | Input | | Id | Output | | Id | Light |
|---|---|---|---|---|---|---|---|
| 0 | Weather CV | | 0 | Step ("a new value at each event") | | 0 | Event |
| 1 | Gust CV | | 1 | Swell ("a smooth drift through the same values") | | | |
| 2 | Memory CV | | 2 | Gate ("length follows Weather") | | | |
| 3 | Clock ("unpatched: Squall runs free") | | | | | | |
| 4 | Freeze ("high: hold still") | | | | | | |

The three CV jacks and the three knobs are in the same order (Weather, Gust, Memory) and the attenuverters follow the knobs; the panel puts Gust on the left and Memory on the right. The engine's `SquallFrame::cv[]` indexes are 0 Weather, 1 Gust, 2 Memory [code `SquallEngine.hpp:44-46`].

The Weather tooltip's value text comes from `WeatherQuantity::getDisplayValueString` and its unit is empty, so the text is the whole readout. Typing a value into the tooltip's field is [unverified]: the quantity has the default linear display mapping, so a number typed is the knob value in 0..1.

### 2.3 The knob and CV law (Weather, Gust, Memory)

[code `SquallEngine.hpp:264-273`]

```
effective = clamp( knob + (cv / 10 V) × attenuverter , 0, 1 )
```

- It is evaluated **every 32 samples**, and on a sample-rate change. Each control then acts at once; there is no slew.
- **A polyphonic CV uses channel 1 only** (`SquallPort::first`). An unpatched CV jack reads 0 V. NaN and ±infinity read 0 V [code; test `poly_cv_uses_channel_1`, `nan_cv_reads_zero`, hostile `hostile_inputs`].
- With the attenuverter at +1, +5 V adds 0.5 and −5 V subtracts 0.5. With a knob at 0.5 (Weather's default) ±5 V sweeps the whole range. With a knob at 0 or 1, a full 10 V is needed to cross the range. Defaults: Gust 0.3 gives −0.2..0.8 for ±5 V (clamped to 0..0.8); Memory 0 gives 0..0.5.
- There is no voltage limit on the jack; the sum is clamped, so ±50 V saturates [test `cv_law`].
- A NaN on a knob value reads as 0 (`clamp01`, `SquallEngine.hpp:235`). Rack does not produce that.

### 2.4 Weather: how much is happening

[code `Events.hpp:33-40`, `SquallTuning.hpp`; test `weather_rate_law`, `free_rate_at_each_word`, `clocked_chance_per_tick`]

```
free-running rate(w) = 4800^w / 600 s         (events per second, long-run average)
clocked chance p(w)  = 256^(w − 1)            (the chance of an event on each tick)
```

| Word | w | Rate | Average gap | Tooltip text [code] | Gate length (¼ gap, held to 5 ms .. 4 s) | Clocked p(w) | Clocked, one tick in |
|---|---|---|---|---|---|---|---|
| CALM | 0 | 0.001667 /s | 600.0 s | 1 per 10.0 min | 4.000 s (150 s before the limit) | 0.00391 | 256 |
| | 0.10 | 0.003890 /s | 257.1 s | 1 per 4.3 min | 4.000 s (64.3 s before the limit) | 0.00680 | 147 |
| FAIR | 0.25 | 0.013873 /s | 72.08 s | 1 per 72.1 s | 4.000 s (18.0 s before the limit) | 0.01562 | 64 |
| | 0.40 | 0.04947 /s | 20.21 s | 1 per 20.2 s | 4.000 s (5.05 s before the limit) | 0.03590 | 27.9 |
| CHANGE (noon, default) | 0.5 | 0.11547 /s | 8.660 s | **1 per 8.7 s** | 2.165 s | 0.06250 | 16 |
| | 0.60 | 0.2695 /s | 3.710 s | 1 per 3.7 s | 0.928 s | 0.1088 | 9.2 |
| RAIN | 0.75 | 0.9611 /s | 1.040 s | 1 per 1.0 s | 260.1 ms | 0.25000 | 4 |
| | 0.90 | 3.427 /s | 0.2918 s | 3.4 per s | 72.9 ms | 0.5743 | 1.74 |
| STORMY | 1 | 8.000 /s | 0.1250 s | 8.0 per s | 31.25 ms | 1.00000 | every tick |

All measured by `m_laws` from the engine's own functions, the tooltip text with the module's format strings [meas]. The manual's "CALM 1 in 256, FAIR 1 in 64, CHANGE 1 in 16, RAIN 1 in 4" are exact.

- Weather moves the rate geometrically: every 0.1 of knob multiplies the rate by 4800^0.1 = 2.33.
- Tooltip forms (`Squall.cpp:16-28`): rate of 1 /s or more reads "x.x per s" (from Weather 0.7547); a gap of 90 s or more reads "1 per x.x min" (up to Weather 0.2238); otherwise "1 per x.x s" [code arithmetic].
- **Measured free rates, 48 kHz, Gust 0** [meas, `m_rates`]: events against the law, 12 h at CALM and FAIR, 3000 events elsewhere:

| Word | Events | Measured rate / law |
|---|---|---|
| CALM (12 h) | 79 | 1.10 (expected ±11 %: Poisson) |
| FAIR (12 h) | 629 | 1.05 |
| CHANGE | 2953 | 0.984 |
| RAIN | 2955 | 0.985 |
| STORMY | 2994 | 1.001 |

  The test asserts within ±10 % over 2500 events at each word [test `free_rate_at_each_word`]. At Gust 0.3 the same runs gave 1.14, 1.06, 0.977, 0.977 and 1.001.
- **Event timing does not depend on the host sample rate.** The same seed gave 1195 events in 20 minutes at RAIN at every rate from 44.1 kHz to 768 kHz, and the event sequence is identical at 44.1 kHz and 768 kHz [meas, `m_sr`]. At CALM, 6 h at 48 kHz and at 768 kHz both gave 33 events (the law says 36, Poisson sd 6).
- Weather CV changes the rate at once (the next 32-sample refresh). Swell's pace follows through its average-gap coefficient, which moves smoothly (section 2.10).

### 2.5 Gust: how events bunch up

[code `Events.hpp`, `SquallTuning.hpp:10-11`]

Gust g runs 0..1. Gust is a self-exciting (Hawkes) process:

```
n          = g × 0.9                    (branching ratio; N_MAX = 0.9)
background = rate(w) × (1 − n)
tau        = 0.35 × average gap         (K = 0.35)
each event adds a bump of area n (in average gaps) that decays with tau
```

- The long-run rate stays rate(w) whatever Gust is. The mean number of events in a burst (a cluster) is 1 / (1 − n): 1.00 at Gust 0, 1.37 at the default 0.3, 1.82 at 0.5, 10 at 1.
- tau at the words [code arithmetic]: CALM 210 s, FAIR 25.2 s, CHANGE 3.03 s, RAIN 0.364 s, STORMY 43.75 ms. "A burst at Calm lasts minutes; at Stormy, a moment" holds.
- At Gust 0 the events are a Poisson process: independent, memoryless, no pulse.

Measured gap statistics, 20,000 events, Weather noon, 1 kHz engine rate, Memory 0 [meas, `m_gust`] (Weather 1.0 gives the same figures within 0.05, because the process runs in average-gap time):

| Gust | n | CV of gaps (1.0 for Poisson) | Fano factor, events per window of 10 average gaps | Longest gap, in average gaps | Mean events in a short-gap burst (gap < ¼ average) |
|---|---|---|---|---|---|
| 0 | 0 | 0.995 | 1.01 | 10.9 | 2.27 |
| 0.3 (default) | 0.27 | 1.239 | 1.85 | 14.5 | 2.62 |
| 0.5 | 0.45 | 1.489 | 3.16 | 18.9 | 2.99 |
| 0.7 | 0.63 | 1.902 | 6.73 | 27.7 | 3.65 |
| 1 | 0.9 | 3.971 | 62.9 | 99.6 | 8.37 |

  The long-run limit of the Fano factor is 1/(1−n)²: 1.88, 3.31, 7.3 and 100 at Gust 0.3, 0.5, 0.7 and 1 [code arithmetic]; the measured values match until the 10-gap window cuts off the longest bursts. The test bounds are: CV within 0.1 of 1 at Gust 0, above 1.5 at Gust 1 [test `gust_clusters`]; gap autocorrelation under 0.05 in magnitude at lags 1 to 10 and a Kolmogorov-Smirnov fit to the exponential at Gust 0 [test `no_pulse_at_gust_0`].
- **Gust does not change the amount.** The test asserts the long-run rate within 12 % of the law at Gust 0, 0.5 and 1 (40,000 events) [test `gust_keeps_the_amount`]. Measured, rate / law: Gust 0.3: 0.997 (six runs of 60,000 average gaps at 1 kHz, range 0.991 to 1.004) and 0.997 at 48 kHz at Weather 1; **Gust 1: about 0.97 on average, but one run of 60,000 events ranges from 0.91 to 1.20** (0.935 to 1.016 at noon; 0.912 to 1.197 at RAIN at 8 kHz and 48 kHz, mean 0.979; 0.969 at Stormy at 48 kHz) [meas, `m_gust_rate`, `m_gr2`]. At a 1 kHz engine rate and Stormy, Gust 1 gave 0.755 (0.73 to 0.79 over six runs): that is the engine's rate, not Squall's behaviour, since a gap is then 125 samples; at Rack's rates (44.1 kHz and up) Stormy has over 5000 samples per gap and gave 0.969. At Gust 1 the clusters are so large that the count in a window of N events has a standard error of about sqrt(Fano/N) = sqrt(100/N): ±10 % over 1000 events. Judge Gust 1 over hours, not minutes. Whether the small deficit (about 3 %) is real is not settled by my runs.
- **A fresh Squall starts with no excitation (y = 0)**, so its first events are slower than the long-run average. Mean gap of the first event, in average gaps: Gust 0: 0.98; Gust 0.3: 1.35; Gust 1: 9.84, then 3.98, 3.27, 2.63, 2.41, 2.51, 2.19, 2.07 for events 2 to 8 [meas, `m_first`]. So a patch loaded or a module just added at Gust 1 is quiet for the first few average gaps (at CHANGE, about 85 s for the first event on average). The excitation is not saved with the patch (section 3).

### 2.6 Memory: whether the weather repeats

[code `SquallEngine.hpp:300-351`, `Memory.hpp`]

Memory m runs 0..1. The module keeps the last 16 events (`Memory::CAP`), each as {gap, value, gate length}, with gap and length in average gaps and the value in 0..1.

- At every event (new or replayed) the engine decides what the **next** one will be (`decideNext`): with probability m, **a replay** of the event L − 1 back from the newest (so the loop has period L) once at least L events are stored; otherwise a new event whose timing comes from Gust's process.
- A replay waits the stored gap (free-running: in average gaps at the current rate; clocked: round(gap / p(w)) ticks, at least 1) and then takes the stored value and gate length. Gust's process cannot fire during that wait (the wait counts as `gustDuringReplay` in the engine's debug counters and the process still decays) [code; test `replay_owns_timing`].
- Replayed events excite Gust like new ones, and every event goes into the history.
- The test draws with `rng.unit() < m`, so m = 1 always replays and m = 0 never does.
- **Loop length** (menu) is 4, 8 (default) or 16. The history keeps all 16 whatever the loop length; changing the length while locked changes which stored event is replayed, and the loop continues with the new period [test `loop_length_change_while_locked`].
- Weather scales a locked loop: stored gaps are in average gaps, so turning Weather up speeds the loop and keeps its shape [test `locked_loop_scales_with_weather`].

Measured over 20,000 events, Weather 0.9, Gust 0.3, 1 kHz [meas, `m_memory`]:

| Loop length | Memory | Replays / events | Events equal to the one L back | First replay is event number |
|---|---|---|---|---|
| 4 | 0 | 0 | 0 | none |
| 4 | 0.3 | 0.295 | 0.295 | 8 |
| 4 | 0.7 | 0.700 | 0.700 | 5 |
| 4 | 1 | 1.000 | 1.000 | 5 |
| 8 | 0.3 / 0.7 / 1 | 0.295 / 0.700 / 1.000 | same | 9 / 9 / 9 |
| 16 | 0.3 / 0.7 / 1 | 0.295 / 0.700 / 0.999 | same | 18 / 17 / 17 |

- The share of replays is m. At Memory 1, the first replay is event L + 1 (the 5th, 9th or 17th), and from then on the output repeats exactly with L events: a loop of 4 at 1 kHz repeated the gaps 0.124, 0.089, 0.073, 0.044 s with no drift over 24 events [meas].
- Around 0.7 the loop wears away: after locking at Memory 1 and dropping to 0.7, the number of events in each block of 8 that matched the event 8 before was 5, 6, 4, 7, 7, 6, 7, 7, 6, 5 (the expected 5.6, with 30 % of events new) [meas].
- **A locked loop's rate is the luck of the stored loop.** Memory 1, loop 8, Weather 0.9, 40 seeds: rate / law of the locked loop had minimum 0.50, median 1.11 and maximum 5.04 [meas, `m_memory`]. The spec says "give or take the luck of the stored loop"; the spread is wide because eight stored gaps are few. Expect a locked loop to be faster or slower than Weather's word by a factor of two or more.
- Memory is saved with the patch. Patching or unpatching the clock keeps the history and any pending replay (`switchMode` converts the remaining wait) [test `history_survives_clock_change`, `clock_unpatch_mid_replay`].

### 2.7 CLOCK

[code `SquallEngine.hpp:113-153`]

- A Schmitt trigger: rising at 1 V, falling below 0.1 V (`schmitt`). A clock whose high level is 0.99 V never triggers; 1.00 V does [meas, `m_freeze`]. Clock reads channel 1 (`getVoltage()`). NaN and ±infinity read 0 V.
- **Patched = clocked.** `isConnected()` of the CLOCK jack decides it, not the signal. With a cable patched and no edges, no events happen (10 s at Stormy: 0 events) [meas]. Unpatched, Squall runs free.
- Events land **on the sample of the clock's rising edge**. The gate and Step change on that sample [code]. At Weather w each edge fires with chance p(w), Gust adding its bumps.
- **Patching a cable whose signal is already high gives a rising edge on the first sample**: at Stormy that is an event at once [meas]. (`switchMode` clears the Schmitt state, so a high clock reads as a new edge.)
- The period is measured as the sample count between the last two edges, in seconds, and replaced at **every** edge: it is the last interval, not an average. A jittery clock therefore gives a gate length and a Swell pace that follow the last interval [code `SquallEngine.hpp:139-141`].
- Until two edges have arrived, the average gap for Swell is the free-running one (1 / rate(w)), and a gate is 10 ms [code; test `clocked_first_gate_10ms`].
- Steady-state share of ticks that fire, 200,000 ticks at 100 Hz, Memory 0 [meas, `m_clock`]:

| Weather | p(w) | Gust 0 | Gust 0.3 | Gust 1 |
|---|---|---|---|---|
| 0 (2,000,000 ticks) | 0.00391 | 0.00393 | 0.00397 | 0.00389 |
| 0.25 | 0.01562 | 0.01502 | 0.01494 | 0.01586 |
| 0.5 | 0.06250 | 0.06202 | 0.06156 | 0.06664 |
| 0.75 | 0.25000 | 0.25044 | 0.25106 | 0.24946 |
| 1 | 1.00000 | 1.00000 | 1.00000 | 0.99995 |

  The test asserts within 10 % at Gust 0 over 400,000 ticks [test `clocked_chance_per_tick`]. The Gust 1 column at 0.5 is +6.6 %; with clusters this size one run has several percent of noise (section 2.5).
- **Stormy and the skips** [meas, `m_stormy`]:
  - A fresh engine at Weather 1, clock running, ticks 1 to 60 (1 = event): Gust 0.3, seeds 1 to 4: all ticks fire except the first two on seed 3. Gust 1, seeds 1 to 4: 1, 26, 7 and 1 skipped ticks at the start, then every tick fires. So the lull at the start is up to 26 ticks at Gust 1.
  - Weather 0.5 → 1 with the clock running: Gust 0.3: 0 to 2 skips; Gust 1: 0 to 5 skips, then every tick.
  - Steady state at Weather 1: 0 of 400,000 ticks skipped at Gust 0.3, 2 at Gust 1; all of them were ticks 0 and 1 after the start (`m_skippos`).
  - Close to Stormy, with the share equal to p(w) in every case: Weather 0.99, Gust 0 / 0.3 / 1: 5.4 % of ticks skipped, and of those the fraction followed by another skip was 5.2 %, 29.1 %, 89.5 %. Weather 0.9: 42.6 %, 42.8 %, 42.0 % skipped, followed by another skip 42.5 %, 55.9 %, 92.7 %. At Gust 1 the skips come in long runs, never in an alternating pattern.
  - After a 2 s Freeze at Stormy, Gust 1, all 30 following ticks fired (Gust's excitation does not decay while frozen).
- **Clock ticks while Freeze is high are ignored** (section 2.8).
- A sample-rate change does not rescale `sinceEdge_`, so the first period measured after a rate change mixes sample counts from two rates [code `SquallEngine.hpp:140`; not measured]. Rack changes the rate rarely; the next edge corrects it.

### 2.8 FREEZE

[code `SquallEngine.hpp:113-134`; `Swell.hpp:51-58`]

- A Schmitt trigger at 1 V and 0.1 V on channel 1. Freeze at 0.99 V does nothing; once high, it holds down to 0.1 V (10 V then 0.5 V stayed frozen; 0.09 V released) [meas, `m_freeze`].
- While high: no events, clock edges are ignored, Step holds, the **count to the next event pauses** (free-running time is paused, Gust's excitation stops decaying). A frozen Squall at Stormy for 30 s had 0 events and Step moved 0.000 V [meas].
- **A gate already high ends at its normal time**: a gate high at the moment Freeze rose stayed high 31.2 ms more (the Stormy length is 31.25 ms) [meas]. The event light also runs out its 100 ms.
- **Swell brakes over about 20 ms and holds.** Stage 1 is pulled to the output by a one-pole that reaches 1 % in 20 ms (`BRAKE_S`, `SquallTuning.hpp:16`). Measured at Stormy, Swell moving at 8 to 15 V/s [meas, `m_swell`]: slope under 10 % after 9.2 ms and under 1 % after 18.5 ms; Swell coasted 0.033 to 0.058 V past where Freeze rose; the largest change of slope in one sample was 0.5 % of the slope (a smooth curve, no corner). The test requires the held value to drift under 0.01 V [test `swell_brakes`]. At noon the coast is about 0.002 V.
- On release Swell starts from rest: the first sample's change was 0 V [meas]. The wait carries on where it stopped: 0.799 of an average gap left at release gave the next event 99.9 ms later (0.799 × 125 ms) [meas].
- After a release the period measurement is discarded: the clock's period is measured afresh from the next two ticks, and gates keep the period from before Freeze until then [code; test `freeze_discards_period_measurement`; issue #47, closed].
- Freeze has no effect on Squall's outputs other than those above: Step and Gate do not go to 0 V.

### 2.9 GATE

[code `SquallEngine.hpp:313-325`, `Gate.hpp`; test `gate_length`, `gate_retrigger`, `gate_follows_weather`]

- 0 V or 10 V.
- **Length** = 0.25 × the average gap, held to 5 ms .. 4 s. A replayed event uses its stored length (a fraction of the average gap at the time, scaled by the current average gap).
  - Free-running: 0.25 / rate(w). Measured over 40 gates each [meas, `m_gate`]: the longest gate was 4000.0 ms at CALM and FAIR, 2165.1 ms at CHANGE, 260.1 ms at RAIN and 31.2 ms at STORMY (the law); the means were 4000, 4000, 2039, 244.9 and 29.4 ms, because a later event can cut a gate short (the shortest were 556.8, 66.9 and 8.0 ms).
  - Clocked: 0.25 × the measured period / p(w). 4 Hz clock at Stormy: 62.50 ms; at RAIN (p = 0.25): 250 ms, or 500 and 750 ms where events on consecutive ticks joined (next bullet); 2 Hz clock at CHANGE: 2000 ms, or 1500 ms where a later event cut it [meas].
  - The first gate on a clock (before a period is known) is 10.00 ms.
- **Gate high time is shorter when the next event comes before the gate ends.** At RAIN, Gust 0, the mean high time over 1195 gates in 20 minutes was 230.9 ms, against the 260.1 ms length: an event within 260 ms of the last has a 21 % chance at Gust 0 [meas, `m_sr`; code arithmetic].
- **Retrigger.** An event while the gate is high drops it for min(1 ms, ¼ average gap), then raises it for the new length, so every event is its own rising edge. The test measures 48 samples low at 48 kHz [test `gate_retrigger`]. A 1 kHz clock at Stormy gives gates of 0.75 ms high and 0.25 ms low [meas].
- **Merge case (clocked).** When the previous gate ends on exactly the sample of the next event there is no drop, and the two gates join into one longer gate with a single rising edge. A clocked gate is 0.25 / p(w) ticks long, a whole number of ticks at the dial words, so it happens at the default noon (events exactly 4 ticks apart), at RAIN (consecutive ticks) and at FAIR (16 ticks apart). Measured losses of rising edges: 4.0 % of events at noon, 25.5 % at RAIN, 1.1 % at FAIR, 0 % at 0.40, 0.49, 0.51, 0.60, 0.76, 0.90 [meas, `m_merge`; 4 Hz clock at RAIN gave gates of 500 and 750 ms]. The first gate on a clock (10 ms) merges the same way with a 10 ms clock period (a 100 Hz clock gave one 15 ms pulse at tick 1) [meas]. See section 8.2.
- Initialize cuts a high gate at once (section 3.3).

### 2.10 STEP and SWELL

**STEP** [code `SquallEngine.hpp:170`, `239-246`]

- A new value in 0..1 at each new event (`rng_.unit()`, 24 bits, [0, 1)), held until the next. A replayed event brings back its stored value. Mapped to volts by the Range menu:

| Range (menu) | Step / Swell span | Mapping |
|---|---|---|
| ±5 V (default) | −5 to +5 V | v01 × 10 − 5 |
| 0 to 10 V | 0 to 10 V | v01 × 10 |
| ±2 V | −2 to +2 V | v01 × 4 − 2 |
| ±1 V | −1 to +1 V | v01 × 2 − 1 |

- **Values are uniform.** 200,000 events at each range: the extremes were exactly −5.0000/5.0000, 0/10, −2/2 and −1/1 V; the means −0.004, 4.996, −0.002 and −0.001; the ten equal tenths held 19,797 to 20,281 events each (expected 20,000 ± 140) [meas, `m_steps`]. The test checks the spread and the four ranges [test `step_values_uniform`, `ranges`].
- Step changes only at events [test `step_changes_only_at_events`]. A range change moves it at once (see below). Step jumps are as big as the whole range: the largest in 10 minutes at Stormy was 9.93 V [meas].
- A fresh module's Step is 0 V in ±5, ±2 and ±1 V, and 5 V in 0 to 10 V (the mid value 0.5) [meas].

**SWELL** [code `Swell.hpp`]

- Two cascaded one-poles at omega = 2.4 / average gap (`SWELL_OMEGA`), each with coefficient 1 − exp(−omega / fs): a critically damped second order. State in doubles. The target is Step's voltage.
- **Step response, in average gaps** [meas, `m_swell`, identical at gaps of 1, 8.66 and 72.08 s]: 50 % at 0.70, 90 % at 1.62, 95 % at 1.98, 99 % at 2.77; overshoot 0; the steepest slope 0.884 of the step per gap. In seconds, for a full-range change (±5 V → ∓5 V is 10 V):

| Word | Average gap | 50 % | 95 % |
|---|---|---|---|
| CALM | 600 s | 7.0 min | 19.8 min |
| FAIR | 72.08 s | 50.5 s | 142.5 s |
| CHANGE | 8.66 s | 6.1 s | 17.1 s |
| RAIN | 1.04 s | 0.73 s | 2.06 s |
| STORMY | 0.125 s | 87 ms | 0.25 s |

  [code arithmetic from the measured ratios; 142.5 s and 17.1 s measured].
- Swell stays inside Step's range: 2 h at noon: Swell −4.962 to +4.821 V, Step −4.989 to +4.979 V [meas]. No overshoot [test `swell_no_overshoot`: under 1e-6]; no corner: the change of slope per sample stays under 1e-6 V/sample² [test `swell_no_corner`]. The biggest per-sample Swell change in 10 minutes at Stormy was 0.00144 V (69 V/s) [meas].
- The pace follows Weather and the average gap, which changes every 32 samples and only changes coefficients, never the state, so Swell does not jump when Weather or the clock's period moves [code; test `swell_pace_follows_gap`].
- In a burst Swell does not chase each Step: it integrates through them. In a lull it arrives and rests.
- **Range change**: Step moves at once, Swell glides. Swell was −0.788 V and changed by 0.00001 V on the next sample; 120 s later at noon it had moved from −0.788 V to 8.759 V (0 to 10 V), 1.504 V (±2 V) or 0.752 V (±1 V) for Step at 8.978, 1.591 and 0.796 V [meas].
- **Initialize and reload**: see sections 3.2 and 3.3.
- **Event light**: 100.00 ms per event, re-armed by another event; the module smooths the brightness with `setBrightnessSmooth(…, sampleTime)` (`Squall.cpp:189`). Drawn as `JewelLight` in the house palette. The light colour and look are [unverified].

---

## 3. The right-click menu and saved state

### 3.1 Menu

[code `Squall.cpp:224-240`]

| Item | Choices (menu text) | Default | Stored as |
|---|---|---|---|
| Range (Step and Swell) | ±5 V, 0 to 10 V, ±2 V, ±1 V | ±5 V | `"range"`, integer 0..3 |
| Loop length | 4 events, 8 events, 16 events (the submenu's title shows the current length) | 8 events | `"loopLength"`, integer 4, 8 or 16 |

No other menu items. The Range menu is an index submenu (`createIndexPtrSubmenuItem`); Loop length is a check-item submenu. Menu appearance is [unverified].

### 3.2 Saved state (JSON)

[code `Squall.cpp:104-166`] A saved module's `"data"` object looks like:

```
{"range": 0, "loopLength": 8,
 "history": [[gap, value, length], ... up to 16 entries ...],
 "step": 0.5,
 "swell": [stage1, out]}
```

Rack saves the six param values (ids 0 to 5) itself.

| Key | What | Valid when | Else |
|---|---|---|---|
| `range` | menu index | JSON **integer** 0..3 | 0 (±5 V): a real such as `1.0`, a string, or a number out of range loads as 0. It is not clamped |
| `loopLength` | menu | JSON integer exactly 4 or 16 | 8: anything else (including 5, 12, 100) loads as 8; it is not rounded or clamped |
| `history` | up to 16 events, oldest first | a JSON array of at most 16 entries | **the whole array is ignored** (empty history) if it is not an array or has more than 16 entries. Step and Swell are still read |
| each entry | `[gap, value, length]` | an array of exactly 3 numbers | a malformed entry becomes the default event (gap 1, value 0.5, length 0.25) and keeps its place; a non-number field reads as −1 and then as the field's default |
| `gap` | in average gaps | 0 < gap ≤ 50 | default 1.0 (`SAVED_GAP_MAX`; issue #49) |
| `value` | 0..1 | 0 ≤ value ≤ 1 | default 0.5 |
| `length` | gate, in average gaps | 0 < length ≤ 1.0 (`SAVED_LENGTH_MAX` = 4 × 0.25) | default 0.25 |
| `step` | Step's 0..1 value | 0..1 | 0.5 |
| `swell` | `[stage1, out]` in volts | an array of exactly 2 numbers, both in −10..10 | **Swell starts at Step's value** (a missing key does the same) |

- All numbers are written as JSON reals (floats widened to doubles), which Jansson reads back exactly.
- The engine validates every field [test `state_rejects_bad`, `state_caps_gaps_and_lengths`, `state_round_trip`]. The module's own JSON reading (`dataFromJson`) is not exercised by any automated test: the tests drive `SquallEngine::setState` with a `SquallState`, so the key names, the integer checks and the array-size checks above are [code], not [test].
- **What is not saved** [code]: Gust's excitation (a loaded patch starts with y = 0: its first events are slow, section 2.5), a pending replay, the wait until the next event, the gate and the light, the random stream (a reloaded patch makes different weather), the clock's period and the Schmitt states.
- **On load**, the engine drops any pending replay, clears its gap accumulator, restarts the wait and decides the next event afresh (`setState`, `SquallEngine.hpp:218-221`). With Memory up and a full history, the first event can be a replay at once.
- A fresh module (no data) has Step 0.5 and Swell 0 V. In the default ±5 V range, Swell starts at 0 V, equal to Step. Rack's missing-key behaviour on a preset or paste onto an existing instance (whether the module is reset first) is [unverified]; with a missing `"range"` or `"loopLength"` Squall loads the default, not "what it was".
- The saved Swell values are volts of the saved range; a hand-edit that changes `range` without `swell` makes Swell glide to the new range.

### 3.3 Initialize

`Squall::onReset` (`Squall.cpp:98-102`) runs `engine.reset()` and sets `range = 0` and `loopLength = 8`. Rack's default reset of the knobs (Weather 0.5, Gust 0.3, Memory 0, attenuverters 0) also happens: the module overrides the deprecated no-argument `onReset()`, which Rack's default `onReset(e)` calls after resetting the params [unverified in Rack: not in the SDK headers].

| Item | After Initialize [code; meas, `m_init`, `m_init2`] |
|---|---|
| Weather, Gust, Memory, attenuverters | 0.5, 0.3, 0, 0 (Rack) |
| Range, Loop length | ±5 V, 8 events |
| Memory history | cleared (`memory_.clear()`) |
| Step | 0.5 (0 V in the ±5 V range, which Initialize also sets) |
| **Swell** | **not reset: it keeps its state and glides to Step's new value** (issue #48). With Swell at 7.0 V and Step 0 V after Initialize: no jump (largest sample-to-sample step 0.00006 V), and within 5 % of its distance to Step after 17.5, 17.6 and 17.7 s in three runs (about two average gaps at noon) |
| Gate | cut low at once (a gate that was high reads 0 V on the next sample) |
| Pending replay, wait to next event, gap accumulator | cleared; a new wait starts |
| Event light | not reset: it runs out its 100 ms |
| **Gust's excitation** | **not reset.** Right after a burst at RAIN, the mean wait to the next event after Initialize was 0.73 average gaps at Gust 0.3 and 0.58 at Gust 1, against 1.37 and 10.0 for a fresh module [meas, `m_init2`] |
| Clock edge count, period, Schmitt states, Freeze state | not reset |
| Random stream | not reseeded |
| Swell's stage 1 and output | kept |

So after Initialize Step jumps to 0 V at once while Swell takes about 17 s (CHANGE) to settle on it. A tester who initializes a module whose Swell was at, say, +4 V sees Swell fall towards 0 V over those 17 s, not an instant drop.

### 3.4 Bypass, Randomize and other host behaviour

- **Bypass**: `configBypass` is not called, so Rack's bypass gives no routed outputs and all three read 0 V [code; unverified in Rack]. The engine does not run while bypassed, so Gust, the wait, Swell and any gate in progress freeze and resume where they were on un-bypass [unverified in Rack]. The event light stays as it was [unverified].
- **Randomize** is not overridden: Rack's default randomizes the six params (Weather, Gust, Memory and the three attenuverters) uniformly in their ranges. Range and Loop length are not touched [unverified in Rack].
- **Sample-rate change**: `onSampleRateChange` calls `setSampleRate`; a rate outside (0, 768000] is ignored (the last good one kept) [test `bad_rate_ignored`, `extreme_rates`, `rate_change_recovers`]. Gate and light lengths are held in samples, so a gate or flash in progress at the moment of a change lasts a different time; the next event is normal [code].
- **Two instances**: each is seeded with `random::u32()` at construction, so two Squalls make different weather [code; test `seeds_differ_and_repeat` for the engine].
- **Polyphony**: CV uses channel 1; CLOCK and FREEZE read channel 1; the outputs are mono [code; the channel count Rack gives a mono output is [unverified]].

---

## 4. Signal flow

### 4.1 Block diagram

```
 Weather knob  + att × CV/10 ─┐
 Gust knob     + att × CV/10 ─┼─ every 32 samples ─► w, g, m (clamped 0..1)
 Memory knob   + att × CV/10 ─┘
        w ─► rate(w) = 4800^w/600 s⁻¹  ;  p(w) = 256^(w−1)  ;  average gap
        g ─► n = 0.9 g

 FREEZE ─► Schmitt (1 V up, 0.1 V down) ─► frozen ───────────────────────────────┐
 CLOCK  ─► patched? ─► Schmitt (1 V up, 0.1 V down) ─► edge                       │
                                                                                  ▼
   unless frozen:                                                       Swell: brake
   ┌───────────── free-running ─────────────┐ ┌────────── clocked ───────────┐
   │ adv = rate/fs per sample               │ │ on each edge only            │
   │ pending replay? count it down          │ │ pending replay? count ticks  │
   │   (Gust decays, cannot fire)           │ │   (Gust decays, cannot fire) │
   │ else Events::stepFree():               │ │ else Events::tick():         │
   │   Λ += adv·((1−n)+y); fire at Λ ≥ E    │ │   chance = clamp(p·((1−n)+y))│
   └──────────────────┬─────────────────────┘ └──────────────┬───────────────┘
                      └────────────► fire() ◄─────────────────┘
        fire(): new event: gap = elapsed average gaps, value = unit(), length = 0.25
                replay: the stored event
                ─► step01 = value ─► map(range) ─► STEP (V)
                ─► gate: ¼ average gap (5 ms..4 s), retrigger low min(1 ms, ¼ gap) ─► GATE 0/10 V
                ─► light 100 ms
                ─► Events::excite()  (y += bump)
                ─► Memory.push(event)
                ─► decideNext(): replay with chance m (if ≥ L stored), else new wait

 STEP (V) ─► Swell: two one-poles, omega = 2.4 / average gap ─► SWELL (V)
             (frozen: stage 1 pulled to the output, 1 % in 20 ms)
```

### 4.2 Block constants

| Constant | Value | Source |
|---|---|---|
| Control refresh | every 32 samples | `SquallEngine.hpp:107` |
| Weather law | 4800^w / 600 s⁻¹ (`12.2288…` = log2 4800) | `Events.hpp:34` |
| Tick law | 2^(8(w−1)) | `Events.hpp:39` |
| N_MAX | 0.9 | `SquallTuning.hpp:10` |
| K (Gust's tau, in average gaps) | 0.35 | `SquallTuning.hpp:11` |
| SWELL_OMEGA | 2.4 (per average gap) | `SquallTuning.hpp:15` |
| BRAKE_S | 0.02 s (to 1 %) | `SquallTuning.hpp:16`, `Swell.hpp:79` |
| GATE_FRACTION | 0.25 | `SquallTuning.hpp:20` |
| GATE_MIN_S, GATE_MAX_S | 0.005 s, 4 s | `SquallTuning.hpp:21-22` |
| RETRIGGER_S | 0.001 s | `SquallTuning.hpp:23` |
| FIRST_CLOCK_GATE_S | 0.01 s | `SquallTuning.hpp:24` |
| SAVED_GAP_MAX | 50 average gaps | `SquallTuning.hpp:29` |
| SAVED_LENGTH_MAX | 1.0 (4 × GATE_FRACTION) | `SquallTuning.hpp:30` |
| LIGHT_S | 0.1 s | `SquallTuning.hpp:33` |
| Memory capacity | 16 events | `Memory.hpp:17` |
| Loop lengths | 4, 8, 16 (others round up to the next) | `Memory.hpp:20-22` |
| Schmitt levels | rise ≥ 1 V, fall < 0.1 V | `SquallEngine.hpp:253-262` |
| Sample-rate limits | 0 < fs ≤ 768000 | `SquallEngine.hpp:81` |
| Random source | PCG32 (`hadal::Random`); Events has its own, seeded from the engine seed × 2654435761 + 0x9E3779B9 | `SquallEngine.hpp:74-75` |

Free-running precision: the wait is counted in a double. Λ and y are doubles, because at 768 kHz and CALM a step adds about 1e-8 per sample, which a float would lose [code `Events.hpp:18-21`]. A simulated 10 min at 768 kHz of FAIR gives 3 to 16 events in the test; I measured 33 events in 6 h at CALM at 768 kHz [test `long_gaps_at_calm`; meas].

### 4.3 Memory and state

All fixed-size: the history (16 × 3 floats), a few doubles and counters; no allocation after construction [test hostile `no_allocation_in_process`]. A Squall instance has no delay line, no buffer and no wake or sleep logic.

---

## 5. How an event is timed (free-running and clocked)

[code `Events.hpp`, `SquallEngine.hpp:103-167`]

- **Free-running.** Each sample advances `adv = rate(w) / fs` average gaps. The expected events that sample are `adv × ((1 − n) + y)`. They are added to an accumulator that fires when it reaches an exponential draw `E = −ln u`. After an event the accumulator restarts from 0 with a new draw (`restartWait`). `y` decays by `d = exp(−adv / K)` per sample and each event adds `n (1 − d) / adv`.
- **Clocked.** On each edge the chance is `clamp(p(w) × ((1 − n) + y), 0, 1)`; `y` decays by `exp(−p / K)` per edge and each event adds `n (1 − d) / p`. Non-edge samples do nothing. `y` is in average gaps in both modes, so it carries across patching or unpatching the clock without a burst (2.0.13).
- **Elapsed time**: `gapAcc_` adds `rate/fs` per sample free-running and `p(w)` per edge clocked, so a stored gap is in average gaps in both modes.
- **Average gap for Swell and the gate**: free: 1 / rate(w); clocked: period / p(w), with the period the last interval (and the free value until a period is known).
- **Weather changes** while a wait is under way: `rate_` and `adv` change at the next refresh and the accumulator keeps its value, so the next event comes after the remaining expected count, not a fresh draw.
- **Replays** (`decideNext`): `pend_ = memory_.next()`; `replayLeft_ = gap` (average gaps) free, `ticksLeft_ = round(gap / p)` (≥ 1) clocked. Switching mode with a replay pending converts the remainder.

---

## 6. CPU, sample rates and robustness

### 6.1 CPU

`make bench` figures for Squall, one instance, 48 kHz, on an Apple M5 Pro Mac (the machine was busy with other work; figures vary by a few thousandths) [meas: `build/bench/bench squall <case> <count> 4`, two runs each]:

| Case | % of one core per instance, 1 instance | 16 instances | Budget (Apple arm64 / elsewhere) |
|---|---|---|---|
| idle (nothing patched, defaults) | 0.029–0.030 | 0.029 | 0.05 / 0.15 |
| outputs (same work as idle) | 0.032–0.034 | 0.029–0.030 | 0.05 / 0.15 |
| busy (Stormy, Gust 1, Memory 0.7, all CVs moving at full attenuverter, clocked at 100 Hz) | 0.038–0.039 | 0.033 | 0.1 / 0.3 |

- About 6 ns per sample idle, 8 ns busy. The per-instance cost does not grow from 1 to 16 instances (x1.00 to x1.06 in the full bench run).
- TESTING.md records 0.023, 0.024 and 0.029 for the same cases; the manual says "about 0.03 %". Today's figures are about 25 % higher, within the budgets and within the machine's noise.
- The full `make bench` run failed on Narrows `busy` (5.57 % against 4.90 %), not on Squall, while other agents were running builds [meas]. Squall's three cases printed no OVER BUDGET line. `bench --check`: all 15 declarations complete [test].
- The spec says "Squall should cost less than Hadal" (Hadal's budget is 0.1, 0.45, 0.45). It does.
- `make engine-globals` passed (no writable global state) [test]. `make libm-calls` is skipped on macOS (Linux only) [issue #36]; `bench.inc` declares the idle and outputs cases as knobs-still libm cases (0 calls required) and busy as reported only. The code uses `FastMath` (`fastExp2`, `fastExp2D`, `fastLog2`) in the event path; the module's tooltip uses `snprintf` only in the GUI thread.
- x86-64 and Windows CPU: not measured [unverified]. The elsewhere budgets are three times the Mac's, provisional.

### 6.2 Sample rates

- Accepted range: 0 < fs ≤ 768000. A bad rate (0, negative, NaN, infinity, above 768000) is ignored and the last good one kept [test `bad_rate_ignored`; meas: after those, Stormy for 100 s at 48 kHz gave 799 events against 800].
- **Event timing is rate-independent**: 1195 events in 20 min at RAIN at 44.1, 48, 88.2, 96, 192, 384 and 768 kHz, and 3542 events in 1 h at RAIN at 1 kHz and at 8 kHz (law 3460) [meas, `m_sr`].
- The mean gate at RAIN was 230.9 ms at every rate; Swell's response is rate-independent (the coefficient is 1 − exp(−omega/fs), stable at any rate) [test `swell_stable_any_rate`: 1 Hz with a 0.125 s gap and 768 kHz with a 60 s gap stay finite and in range].
- The hostile test runs the engine at 1, 10, 1000, 44,100, 192,000 and 768,000 Hz [test `extreme_rates`] and 44.1 → 768 → 48 kHz [test `rate_change_recovers`: 8 events/s within 10 % at Stormy afterwards].
- Long gaps: a 10 min gap at 768 kHz is 4.6 × 10^8 samples; the counters are doubles and 64-bit integers [code; test `long_gaps_at_calm`].

### 6.3 Hostile input and numerical safety

[test hostile, built with ASan and UBSan at `-O1`: all passed]

- NaN, +∞ and −∞ on every CV, Clock and Freeze, patched and unpatched: outputs stay finite and in range [test `hostile_inputs`]. An infinite Clock or Freeze reads 0 V [test `inf_clock_freeze_read_zero`].
- No allocation in `process()`, `getState` or `setState` over 400,000 samples with Range, Loop length, clock patching and Freeze all switching [test `no_allocation_in_process`].
- Seeded fuzz: 1,000,000 samples with random knobs, CVs (±12 V), channel counts 0 to 2, Clock, Freeze, Range, Loop length (0 to 19) and rates (22.05 to 192 kHz) kept every output finite and in range [test `seeded_fuzz`].
- Gust's excitation is never subnormal at 768 kHz and Stormy (the fastest decay), free-running and clocked [test `denormals`].
- A frame with a Loop length outside 4/8/16 rounds up to the next (`Memory::setLength`): the module only ever sends 4, 8 or 16.

---

## 7. Interaction with other modules

The manual's patches use Hadal, Fog, Sluice, Ballast, Fathom, Pan and Beacon; the listening guide adds Rigging, Narrows and Sway. Nothing in Squall depends on another module, and it has no expander. Voltages follow the house standard: Gate 0/10 V; clock and Freeze detected at 1 V and 0.1 V. Step and Swell are bipolar CV by default (±5 V) and unipolar with the 0 to 10 V range. Squall's stepped CV can be used as audio (issue #67: not looked at).

---

## 8. Open issues

### 8.1 Filed issues that touch Squall

| Issue | State | Subject |
|---|---|---|
| #64 | open | Polyphony: modules should be polyphonic where it makes sense. Squall's entry: "One stream of events; CVs use channel 1". Likely later: several independent event streams from one module. Not a defect |
| #67 | open | Aliasing: measure every module that makes or bends sound. Squall's entry: "Squall's stepped CV used as audio ... not looked at; expected to need a note, not a fix" |
| #36 | open | Process: the libm-calls check is skipped on macOS (Squall's libm check runs on Linux and CI only) |
| #47 | closed 2026-11-23 | Clock period measured across a Freeze was skewed; fixed in 2.0.33 (section 2.8) |
| #48 | closed | Initialize jumped Swell to the middle; fixed in 2.0.33: Swell glides (sections 1.1 and 3.3) |
| #49 | closed | Saved gaps and lengths were capped at 1e6 average gaps; now 50 and 1.0 (section 3.2) |

I found no open issue that is specifically about Squall.

### 8.2 Found while preparing this document (not filed)

I did not file these; they are for the owner to decide.

1. **The manual's sentence about skipped ticks at Stormy is wrong** in steady state, and the listening guide repeats it (section 1.1). At exactly Stormy every tick fires; a start-up lull of up to about 26 ticks occurs at Gust 1. The manual's tooltip example ("1 per 9.0 s") is wrong for the code ("1 per 8.7 s").
2. **Initialize leaves Gust's excitation, the clock period and Schmitt states, and the random stream** (section 3.3). The behaviour change from issue #48 (Swell glides) is intended; the other items are not mentioned anywhere.
3. **Patching a clock cable that is already high gives an immediate tick** (section 2.7).
4. **A sample-rate change mixes sample counts into the next period measurement** and leaves gate and light lengths in samples (sections 2.7 and 3.4). Not measured.
5. **The module's JSON reading is untested**, and strict: `range` and `loopLength` accept only JSON integers (a real `1.0` loads as the default), and a history with more than 16 entries is dropped whole (section 3.2).
6. **A locked loop's rate varies widely** from Weather's word (0.5 to 5 over 40 seeds); the manual describes Memory 100 % as "the last events loop exactly" without saying that the loop's speed is whatever those eight gaps happened to be.
7. **Gust 1's long-run rate averages about 0.97 of the law** over my runs, with a one-run spread of 0.91 to 1.20 (section 2.5). The test's tolerance is 12 %. I did not establish whether the 3 % is real.
8. **Clocked gates join, and the second event loses its rising edge**, whenever the gate's length is a whole number of ticks and the next event arrives exactly then: 4.0 % of events at the default noon, 25.5 % at RAIN, 1.1 % at FAIR (section 2.9; table in section 1.1). It contradicts the spec's and the manual's "every event is its own rising edge". It looks like a bug: the retrigger drop only happens if the gate is still high on the event's sample, and a gate of exactly n samples has just run out. Not filed.

### 8.3 Known limits that are not defects

- Minutes of silence at CALM and FAIR are by design; the light shows Squall is alive.
- At Gust 1 the clusters are large (10 events on average) and the long-run average takes hours to settle at the slow words.
- A fresh Squall at Gust above 0 is slower than average for its first few average gaps.
- The 5 ms gate floor is unreachable free-running.
- Step is uniform, so the same value repeats only by chance (or by Memory).

---

## 9. What the automated suite covers

Four test programs, all passed. They ran in 3.7 s, 0.35 s, 1.3 s and 0.3 s of CPU time. There are no tests of `Squall.cpp` itself.

### 9.1 test_events (`Events.hpp`)

`fast_log2_accuracy` (error under 1e-3); `weather_rate_law` (rate and p(w) within 1e-4 of the formulas at the five words; rate(1) = 8 within 1e-3); `free_rate_at_each_word` (within 10 %, 2500 events); `gust_keeps_the_amount` (within 12 % at Gust 0, 0.5, 1; 40,000 events); `gust_clusters` (CV 1 ± 0.1 at Gust 0, above 1.5 at Gust 1); `no_pulse_at_gust_0` (autocorrelation, Kolmogorov-Smirnov); `clocked_chance_per_tick` (within 10 %; **every tick fires at Stormy at Gust 0 only**); `long_gaps_at_calm` (10 min at FAIR at 768 kHz: 3 to 16 events); `seeds_repeat_and_differ`; `bad_rate_ignored`.

### 9.2 test_parts (`Memory.hpp`, `Gate.hpp`, `Swell.hpp`)

Memory: `memory_loop_period` (period L for L = 4, 8, 16), `memory_before_full` (no replay until L events), `loop_length_change_while_locked`, `memory_wraps` (16 stored, oldest first). Gate: `gate_length` (4800 samples for 0.1 s at 48 kHz), `gate_retrigger` (48 low, 2400 high). Swell: `swell_settles` (under 2.5 V at half a gap, within 0.25 V of 5 V at two gaps), `swell_no_overshoot` (1e-6), `swell_no_corner` (1e-6 V/sample²), `swell_pace_follows_gap` (5 %), `swell_brakes` (slope under 1 % at 60 ms, change of slope under 20 % of the moving slope, holds within 0.01 V, restarts from rest within 5 %), `swell_stable_any_rate`.

### 9.3 test_engine (`SquallEngine.hpp`)

`cv_law`, `poly_cv_uses_channel_1`, `nan_cv_reads_zero`; `step_changes_only_at_events`, `step_values_uniform`, `ranges`, `gate_follows_weather`, `light_on_each_event`; `memory_zero_no_repeat`, `memory_one_exact_loop`, `memory_share`, `replay_owns_timing`, `locked_loop_scales_with_weather`; `clocked_events_on_edges_only`, `clocked_gate_uses_tick_period`, `clocked_first_gate_10ms`, `clock_unpatch_mid_replay`, `history_survives_clock_change`, `fast_clock_gate`; `freeze_holds`, `freeze_resumes`, `freeze_discards_period_measurement` (gate after Freeze keeps the old period: 25 ms ± 1); `reset_swell_glides` (Initialize: Step 0 V, Swell within 0.1 V of before, no event at Calm in 2 s, Swell's stage 1 moves towards 0); `weather_cv_audio_rate`; `state_round_trip`, `state_rejects_bad`, `state_caps_gaps_and_lengths`; `bad_rate_ignored`, `seeds_differ_and_repeat`; `weather_cv_gusty` and `clock_switch_gusty` (Gust's rate unchanged under Weather CV, no burst on a clock switch); `inf_clock_freeze_read_zero`.

### 9.4 test_hostile (ASan and UBSan, counting `operator new`)

`extreme_rates`, `rate_change_recovers`, `hostile_inputs`, `no_allocation_in_process`, `seeded_fuzz`, `denormals` (section 6.3).

### 9.5 Not covered by the automated suite

- Anything in Rack itself: the panel drawing, the tooltip text, the menu, save and load through Rack, bypass, Randomize, Rack's reset of knobs, polyphony as Rack delivers it, and Rack's CPU meter. Squall's own `dataToJson` and `dataFromJson`. The automated smoke test (`make smoke`) loads the plugin; I did not run it.
- Listening: whether Gust's n_max (0.9) and K (0.35), Swell's pace (omega 2.4), noon as a default and the four ranges are right. They were set as first guesses and are the subject of the listening guide.
- The full-length behaviour of Gust 1 over hours; the loop-rate spread (section 2.6) has no test.
- Windows and Linux CPU; the libm check on macOS [issue #36].
- A golden saved-state file: no JSON image is pinned by a test.
- The aliasing of Step used as audio [issue #67].

---

## 10. Suggested QA test areas and expected results

Defaults unless stated: 48 kHz, Weather noon, Gust 0.3, Memory 0, attenuverters 0, nothing patched, Range ±5 V, Loop length 8. Because Squall is random, state the run length: counts over a Poisson window carry a standard deviation of the square root of the count (more at higher Gust; section 2.5).

| Area | Procedure | Pass criteria |
|---|---|---|
| Tooltips | hover Weather at each word | CALM "1 per 10.0 min"; FAIR "1 per 72.1 s"; CHANGE "1 per 8.7 s"; RAIN "1 per 1.0 s"; STORMY "8.0 per s"; Gust reads 30 %, Memory 0 %, the attenuverters 0 % |
| Defaults | add a module | Weather noon, Gust 30 %, Memory 0, attenuverters 0, Range ±5 V, Loop length 8; STEP and SWELL read 0 V; light off; the first event comes on average later than at Gust 0 (about 1.35 average gaps = 12 s) |
| Weather rate | Gust 0; a scope or counter on GATE; count events | per hour: CALM 6 ± 2.5, FAIR 50 ± 7, CHANGE 416 ± 20; per minute RAIN 58 ± 8, STORMY 480 ± 22 (one standard deviation at Gust 0; at the default Gust 0.3 about 1.4 times wider) |
| **Event light** | watch the light at CALM | flashes for about 0.1 s at every gate, and only then; at STORMY it is nearly steady |
| Gate length | GATE on a scope, Gust 0 | CALM up to Weather 0.428: 4.0 s (the longest); CHANGE 2.17 s (shorter if the next event comes first); RAIN 0.26 s; STORMY 31 ms |
| Retrigger | Stormy, any Gust, free-running | each gate rises separately: a drop of about 1 ms between gates that overlap |
| **Clocked gate merge** | a steady clock, Weather exactly at noon (then RAIN 0.75, FAIR 0.25), Gust 0; count gate rising edges against Step changes over a few thousand events | **known: about 4 % of events (noon), 25 % (RAIN) and 1 % (FAIR) have no rising edge** (gates join); at 0.49 or 0.51 none lose theirs. Record on a new issue |
| Gust shape | CHANGE; scope GATE; Gust 0 vs 1 | Gust 0: irregular events, no beat. Gust 1: bursts of several events with long lulls (the longest about 100 average gaps), the same count over many hours (±10 % per 1000 events at Gust 1) |
| Gust never changes the amount | count events over an equal long time at Gust 0 and 1 | within a few percent over thousands of events; do not judge from 50 events |
| STEP | Step on a scope or meter; record values | one new value per gate; values spread evenly across the range |
| Range | switch the menu while running | Step moves at once to the new mapping; Swell glides without a jump; ranges ±5, 0 to 10, ±2, ±1 V; values never outside |
| **SWELL shape** | scope Swell and Step together | Swell follows Step with a lag of about 0.7 average gaps to half, 2 to 95 %; no corner, no overshoot, never outside the range; in a burst, smoothed |
| SWELL pace | Weather at CALM / CHANGE / STORMY | the lag scales with the average gap (7 min / 6 s / 90 ms to half way) |
| Clocked | patch a 2 Hz square into CLOCK, Weather RAIN, Gust 0 | events only on rising edges (on the same sample); about one in four ticks; 10 ms gate on the very first event, then 0.25 × 0.5 s / 0.25 = 0.5 s; events on consecutive ticks join into 1 s, 1.5 s gates (the merge row below) |
| **Clock at Stormy** | clock 4 Hz, Weather 1, Gust 0 and 1 | **every tick fires**; at Gust 1 the first few ticks after adding the module or raising Weather may be skipped (up to about 26 at a fresh start). The manual says otherwise |
| Clock level | a 0.5 V square into CLOCK | no ticks; 1 V or more ticks |
| Unpatching the clock | pull the cable | free-running starts at once; the stored pattern carries on |
| Freeze | Weather STORMY, Freeze high | no events, Step holds, Swell stops within 20 ms (coast under 0.06 V), a gate already high ends normally; release: continues; Freeze at 0.5 V keeps holding until it falls under 0.1 V |
| Freeze with a clock | Freeze high for several ticks, then release | ticks during Freeze are ignored; the gate length stays the old period's until two new ticks have arrived |
| Memory 100 % | Loop 4, Weather RAIN, Gust 0; Memory 100 % | after the first four new events a four-event pattern repeats exactly, including its gaps and gate lengths; the first repeated event is the fifth |
| Memory 70 % | the same at 70 % | about 70 % of events replay; the pattern recurs and wears away |
| Memory loops 8 and 16 | menu | the pattern period changes to 8 or 16 events, the first replay after the 8th and 16th events |
| Weather moves a loop | Memory 100 % loop locked; turn Weather up | the loop speeds up and keeps its shape |
| Attenuverters | Weather CV: +5 V, attenuverter ±1, Weather at noon | Weather goes to 1 (Stormy) or 0 (Calm); attenuverter 0: no effect |
| Gust and Memory CV | +5 V at attenuverter +1 | Gust 0.3 → 0.8; Memory 0 → 0.5 (the defaults' limit; not the whole range) |
| Poly CV | a 4-channel cable on each CV | only channel 1 acts |
| Hostile CV | ±10 V and ±50 V; a NaN producer | saturates; no crash, outputs finite and in range |
| **Initialize** | Range 0 to 10 V, Memory up, a locked loop, Swell far from the middle; right-click Initialize | Range ±5 V, Loop 8, Memory 0, Weather noon, Gust 30 %; Step 0 V at once; Gate drops at once; history cleared; **Swell does not jump and glides to 0 V over about 17 s** (issue #48 closed) |
| Save and load | a locked loop, Range 0 to 10, Loop 4; save, reload | knobs, Range and Loop restored; history restored (the loop repeats after the first replay); Step and Swell resume where they were, with no jump |
| Corrupt state | hand-edit the saved `"data"`: a gap 1e5, a history of 20, `"range": 7`, a missing `"swell"` | gap loads as 1.0 (default); history of 20 is dropped; range 0; Swell starts at Step; no crash |
| Two instances | add two Squalls with the same settings | different weather; a reloaded patch makes different weather (the random stream is not saved) |
| Bypass | bypass while running | all outputs 0 V, gate low [unverified in Rack]; un-bypass: carries on |
| Sample rate | change Rack's rate from 44.1 to 192 kHz while running | the event rate does not change; a gate in progress at the change may be longer or shorter; no crash |
| CPU | `make bench` | idle and outputs under 0.05 % of a core, busy under 0.1 % (measured 0.03–0.04 %); no growth from 1 to 16 instances |
| Panel | `make smoke` screenshot | barometer face with CALM, FAIR, CHANGE, RAIN, STORMY; three columns; CLOCK, light, FREEZE; STEP, SWELL, GATE on ivory tiles [unverified: I did not run it] |

---

## 11. Glossary

| Term | Meaning |
|---|---|
| Attenuverter | a −1..+1 scale on a CV input; negative inverts |
| Average gap | the typical time between events: 1 / rate(w) free-running, period / p(w) clocked; the unit in which gaps, gate lengths and Swell's pace are stored |
| Branching ratio n | Gust × 0.9: the mean number of events one event brings with it. A burst has 1 / (1 − n) events on average |
| Bump | the rise in the event rate that each event adds; it decays with tau |
| Calm, Fair, Change, Rain, Stormy | the words on the Weather face: Weather 0, 0.25, 0.5, 0.75, 1 |
| Cluster, burst | a run of events close together, made by Gust |
| Event | one step of Squall: a new or replayed Step value, gate and light |
| Excitation (y) | Gust's current extra rate, in average gaps; zero on a fresh module and after a load; not reset by Initialize |
| Fano factor | the variance of the count of events in a window divided by its mean: 1 for Poisson, 1/(1−n)² for Gust's long-run limit |
| Gust | the control that bunches events into bursts without changing the average rate |
| Hawkes process | a self-exciting point process: each event raises the chance of another soon after |
| Loop length | 4, 8 or 16: the number of events back that a replay copies; the period of a locked loop |
| Memory | the chance that the next event is a replay of the one L back; also the stored history of 16 events |
| p(w) | the chance of an event on each clock tick, 256^(w − 1) |
| Poisson process | events that are independent and memoryless; Gust 0 |
| Replay | an event that copies a stored event's gap, value and gate length |
| Retrigger | the 1 ms drop (or less) that gives each event its own rising edge |
| Schmitt trigger | a comparator with two thresholds: rises at 1 V, falls below 0.1 V |
| Step | the output that jumps to a new value at each event |
| Swell | the output that follows Step through two cascaded one-poles; smooth, no overshoot |
| tau | the decay time of Gust's bump: 0.35 average gaps |
| Tick | a rising edge on CLOCK |
| Weather | the control that sets how much happens: 1 event per 10 min to 8 per s |

---

### Note on the measurement programs

- All programs are short C++ files outside the repository, in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/squall/meas/`, compiled against a copy of the 2.0.39 tree (`git archive 2e6e960`, in `.../squall/tree/`) with `-std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -I tree/src -I tree/src/squall`. They drive `rka::SquallEngine` as `Squall.cpp` does: params in a `SquallFrame`, one sample per call, 48 kHz unless stated. Several use a lower engine rate (1 kHz or 8 kHz) to run many events quickly; section 6.2 shows the event timing does not depend on the rate.
  - `common.hpp`: the shared rig. `m_laws`: rates, gaps, tooltip text, p(w), gate length. `m_rates`: free rate against the law at five words and three Gust values. `m_gust`: gap statistics against Gust. `m_gust_rate`, `m_gr2`: long-run rate at Gust 0.3 and 1. `m_first`: the start-up gaps. `m_clock`: chance per tick and the Stormy skip pattern. `m_stormy`: where Stormy's skips occur. `m_gate`: gate lengths, retrigger, event light. `m_swell`: Swell's step response, range change, Freeze brake. `m_freeze`: Freeze and Clock thresholds, held state. `m_memory`, `m_loop`: Memory's share, loop exactness and rate luck. `m_steps`: Step's distribution per range. `m_sr`: sample-rate sweep. `m_init`, `m_init2`: Initialize.
- The tests were built with `make -f tests.mk MODULES=squall build/test/squall/<name>` from the copied tree (`RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64`), and `make -f tests.mk … build/bench/bench`, `engine-globals` and `python3 tools/test_gen_panels.py`. Logs are in `.../squall/logs/`.
- They do not exercise Rack's ports, threading, GUI or JSON. Anything marked [meas] should match Rack at 48 kHz for the engine's behaviour; Rack-side effects (the tooltip, the menu, the save format through Rack, Initialize and Randomize order, bypass, light smoothing) are outside them and tagged [unverified].
- My seeds were fixed in each program, so these runs are repeatable, but another seed will give different single-run figures within the spreads given.
- The listening guide `squall-2.0.39-listening.html` was used as a cross-check only. Where it disagrees with the code, the code wins; the one disagreement is the Stormy tick claim (section 1.1). Its other figures (8.7 s at noon, 2.2 s gate at noon, 4 s at Calm and Fair, 30 ms at Stormy) agree with the code to rounding, and its "Swell reaches a new value in about one average gap" is looser than the 0.7 / 1.98-gap figures above.
