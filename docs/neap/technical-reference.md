# Neap 2.0.39: Technical Reference for QA (with Spring, its expander)

Modules: **Neap** (8 HP) and **Spring** (6 HP, Neap's expander), plugin `rka` (slug `rka`; module slugs `Neap` and `Spring`), VCV Rack 2 (minRackVersion 2.5.0).
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository, branch `neap` (plugin.json `"version": "2.0.39"`). Not yet merged to main. Everything here was taken from that tree.
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It is a zstd-compressed tar. It holds `plugin.json` (version 2.0.39), `plugin.dll` and the four panel files `res/neap/Neap.svg`, `Neap-dark.svg`, `res/spring/Spring.svg`, `Spring-dark.svg`, which are byte-identical to the commit's. Neither module has factory presets. I did not run the DLL.
Document date: 2026-11-24.
Listening guide: `/Volumes/share1/VCVRACK/neap-2.0.39-listening.html`.

**Status: new and unruled.** Neap and Spring first appeared in test build 2.0.38 (this build adds one menu item). They are still waiting on the user's listening rulings. The RATE range and default, the step table, the tracking spread, Drift, the Weight curve, TIDE's gain, MARK's default, the default wave and Spring's output set are all first guesses (spec, "Listening stop"; the listening guide says the same). Report impressions of these against the listening guide, not as defects.

## Build history

Only these two test builds contain the modules. Both are dated 2026-11-24 (`git log` for `src/neap`, `src/spring`, `res/neap`, `res/spring`; CHANGELOG.md "Test builds").

| Build | Commit | What it holds |
|---|---|---|
| 2.0.38 | `6f1b81c` | "Neap and Spring: first test build": the design, mockups and plan (`b1d32b9`), panels (`ccbcd39`), tables and rate law (`1aec47f`), the eight cycles (`ebcaa83`), tide and HIGH (`6aef1dd`), the engine (`569f114`), Spring's core (`403eb8a`), hostile tests and bench (`4d9121f`), TIDE's glide frozen on a bad sample time (`cb4523d`), the two modules (`f2e68c3`), Level changed from a checkbox to a two-option menu (`57318a0`). |
| 2.0.39 | `2e6e960` | "Neap: Add Spring expander menu item" (`19de92b`): a right-click item on Neap that places a Spring directly to its right. Nothing else changed in `src/neap`, `src/spring` or the panels. |

The saved-state keys did not change between the two builds (the Level change was inside 2.0.38's development: the key was already `"level"`, an integer 0 or 1).

---

## 0. How to read this document

- Every number comes from one of the sources below, marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources, cited as file:line (`src/neap/*`, `src/spring/*`).
  - **[test]**: printed or asserted by the module's automated tests. I built them from the 2.0.39 tree with the repository's own rules (`make -f tests.mk`, `-Werror`; the hostile tests with ASan and UBSan at `-O1`) and again, for the non-sanitizer ones, with the plugin's flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`), on an Apple M-series Mac. All eight test programs passed (seven at first; `neap/test_state` was added after the whole-branch review); the six without sanitizers are built at the plugin's flags by `make test` (section 9).
  - **[meas]**: a small program I wrote against the Rack-free engine (`rka::NeapEngine`, `neap::Cycles`, `neap::Tide`, `rka::SpringCore`, the code the modules run), built with the plugin's flags. Sampling rates are stated where they matter. Paths are in the note at the end.
- **[issue #N]**: an open GitHub issue in `ryanarneson/RkaModules`. There are none against Neap or Spring (section 8).
- **[unverified]**: anything I could not check, mostly Rack host behaviour. I did not run Rack. I did compile `Neap.cpp` and `Spring.cpp` against the Rack SDK 2.6.6 (both compile without warnings).
- Voltages are Rack volts. "Noon" is a knob at 0.5 (Weight noon is 0, RATE noon is 0.5). Neap's cycle indices are 1 to 8 in this document (the code is 0-based).
- "x" is a cycle's value in ±1; its output in volts is 5·x (Level ±5 V) or 5·x + 5 (Level 0–10 V).
- The spec is `docs/specs/2026-11-24-neap-design.md`, the plan `docs/plans/2026-11-24-neap.md`.

---

## 1. What Neap and Spring are

**Neap** is a bank of eight very slow triangle (or sine) cycles, each about an octave below the one before but on uneven steps (ratios 2.16, 1.93, 2.25, 1.93, 1.93, 2.23, 2.17), driven by one RATE knob. Besides the eight outputs it has **TIDE** (the weighted sum of the eight, tilted by WEIGHT, soft-limited to about ±5 V), **HIGH** (a gate while TIDE is above MARK), **ALL** (the eight cycles on one polyphonic cable) and a **RESET** input. It follows Instruō øchd, whose rates the spec measured, and Kelvin's tide-predicting machine, which supplies TIDE and HIGH. The name: a neap tide is the small one; Spring is the big one.

**Spring** is a 6 HP expander placed directly to Neap's right. It makes sixteen outputs from Neap's eight shaped cycles: four folds, four min/max outputs, four triggers (with a fall-through chain for unpatched jacks) and four 4-bit step codes. It has no knobs, no inputs and one light (LINK).

Design goals that QA can check (spec, "Goals" and "Anti-goals"):

- Eight cycles about an octave apart, from one Rate. Cycle 1 from 1.7 min per cycle to 25 ms; cycle 8 from 4.8 h to 4.2 s (section 2.3).
- No two cycles may lock into a repeating relationship at any RATE setting. The rule: every pair with a frequency ratio of 16 or less stays at least 3 % from every whole ratio, and pairs at 6 or less also from every half ratio (section 2.6).
- No jump on any output when Rate, Weight, either CV or the Wave setting changes. Reset is the only jump.
- TIDE should neither sit near the middle (so HIGH never fires) nor clip flat at the rails.
- Spring's outputs should be in step with Neap's own.

Non-goals (do not file these as defects): audio rate; a height or phase knob per cycle; per-cycle rate knobs; clock sync; polyphonic voices (more than one Neap on a cable); other wave shapes; Spring using TIDE, WEIGHT or Level (the listening guide asks the user whether it should).

### 1.1 Where the spec, the manual and the shipped code differ

**There is no manual yet.** `docs/neap/README.md` does not exist in this tree; the manual is plan Task 9, still pending. `README.md`'s module table has no Neap or Spring row, `docs/future/neap.md` still says "Status: idea, 2026-11-15. Not designed", and CHANGELOG.md's "2.0.0 (unreleased)" list has no Neap entry. So the comparison below is against the spec and the plan only. When the manual arrives, compare it with this table.

The code is what ships. Test against it; the spec's version is given for context.

| Topic | Spec / plan says | 2.0.39 does |
|---|---|---|
| Where Spring's code lives | spec: Spring's code in `src/neap/` | plan states the deliberate change: `src/spring/` (panels load from `res/<folder>/`), as Pan sits beside Ballast. Spring includes `neap/NeapExpander.hpp` |
| TIDE gain | spec: "tune the gain by ear so that at noon Weight the tide spends … roughly a quarter" of its time beyond ±2.5 V (a target, not a test) | gain 2.0, the plan's starting value; **18.8 % at Weight noon** over 24 simulated hours [test info line; meas 18.78 % at 24 h, 18.70 % at 168 h] (section 2.12). The code comment says "to be tuned by ear". The listening guide asks the user |
| TIDE's peak | "stays within ±5 V and never clips flat" | the peak is **4.92 V**, not 5: even with all eight cycles at +1, raw = 2 and the limiter gives 0.984 (section 2.12). No test pins the figure |
| MARK in 0–10 V Level | spec: "In 0–10 V mode the mark follows (0 to 10 V)". Plan: "the mark is MARK + 5 V" | HIGH compares TIDE before the +5 V offset with the MARK knob value (−5 to +5 V). This is the same comparison as MARK + 5 V against the TIDE jack, so HIGH fires at the same moments in both Levels [test `levels`; meas]. The MARK knob and its tooltip keep reading −5 to +5 V in 0–10 V Level; they do not shift to 0–10 V (section 2.13) |
| MARK's ends | MARK runs −5 V to +5 V | the ends do nothing useful: at **MARK −5 V HIGH is high for 100 % of the time and at +5 V it never fires** (0 %), because TIDE peaks at ±4.92 V and the gate needs ±0.05 V beyond the mark [meas] (section 2.13) |
| WEIGHT CV | spec: "±5 V sweeps the knob's full range, added to the knob" | adds 0.2 per volt, then clamps to ±1. ±5 V covers the whole range only from Weight noon (the same wording issue as Fog's #31) |
| Weight glide | "glide over about 50 ms" | a one-pole with a **50 ms time constant**: 63 % in 50 ms, settled within 1e-4 in about 0.46 s at 48 kHz [test: 63 % at 2,400 samples] |
| Tracking spread | spec: slopes "about 0.15 % per octave or less (about 1 % across the knob)" | table of 0.0014 octave per octave at most (cycle 2); cycle 8's spread vs the unspread law is −0.29 % at knob 0 and +0.29 % at knob 1, so 0.58 % across the knob [meas]. Cycle 2's is 1.2 % across it [code arithmetic] |
| No-locking margin | spec: "passes with a 3.3 % margin at noon" | 3.325 % at noon [meas]. **Worst over the whole clamped range: 3.04 %**, pair 6–8 at the bottom clamp [meas, 160,001-point scan]. The test grid has 81 points and finds the same worst value (0.0304). The margin with the largest Drift offsets added is about 2.0 % [meas]. The rule is checked without Drift |
| Pairs beyond 16:1 | spec: "too far apart to be heard locking" | several come very close to whole ratios: 4–8 is **18.005** at the bottom clamp (0.03 % from 18:1), 1–6 reaches 34.978 (0.06 % from 35:1), 2–7 crosses **exactly 36:1** at e1 = −5.42 (RATE about 0.105 with no CV), 2–6 reaches 16.068 (0.4 % from 16:1), 1–5 reaches 18.063 (0.35 % from 18:1) [meas]. They are outside the rule by its own definition. This is a listening question (section 2.6) |
| Drift | spec: "a slow random wander on each cycle's rate, at most ±0.5 %, changing over minutes" | a new random target every 60 to 123 s per cycle, reached by a cosine glide over that interval; at most ±0.5 % [test]; extent seen over 2 h: −0.493 % to +0.500 % [meas]. **Deterministic**: every fresh module starts from the same seeds (1000 + i), so two Neaps drift identically unless Drift is off or they were loaded at different moments [code] |
| Reset | "puts all eight cycles at the start of their cycle, 0 V rising" | yes (the first sample after the edge reads +0.0003 to +0.0005 V for cycles 1 to 3 at noon [meas]). **Not in the spec: a Reset also fires a Spring trigger on every even cycle that was below 0 V**, because the jump to 0+ looks like a rising zero crossing (section 6.5) |
| Spring link | spec: "Spring sees itself unlinked"; plan: "no `fromNeap` for 64 of its own `process()` calls" | 63 further process calls after Neap's last call still read as linked, the 64th reads unlinked. At 48 kHz that is 1.33 ms. The outputs hold their last values for those 63 calls (section 6.2) |
| Spring cascade | "unpatched … passes its triggers on to the next patched one in the order 2 → 4 → 6 → 8 → 2" | as stated; the full table of 16 patch masks is in section 6.4 [meas] |
| Add Spring expander | spec: Spring placed to the right. Plan Task 8 does not mention the menu item (added in 2.0.39) | an item at the end of Neap's right-click menu; greyed out while a Spring is directly to the right (section 4) |
| Aliasing | spec: the builder measures against a bound, or records in TESTING.md why none is needed | TESTING.md records "Not needed": cycle 1 tops out at 160 Hz; scratch measurement about −86 dB re the fundamental (Neap) and −74 dB (Spring's F) at 44.1 kHz; no bound is pinned and no test measures it (section 7.4) |
| Documentation | plan Task 9: manual, README row, changelog, roadmap, `docs/future/neap.md` marked built | none done yet (see top of this section) |

---

## 2. Neap: panel, controls and ports

### 2.1 Layout

8 HP (40.64 mm). Title NEAP, subtitle TIDE PREDICTOR [code: `tools/gen_panels.py`]. Top to bottom: the RATE dial on an ivory face numbered 0 to 12 in steps of 2, with its attenuverter and RATE CV jack to the right (joined by a trace); a brass rope from the left edge zigzagging over the eight pulley lights; WEIGHT with WEIGHT CV beside it, and MARK; three columns of output tiles; the bottom row RESET, TIDE, HIGH. Positions in mm from `NeapLayout.hpp` [code]:

| Control | Centre (x, y) mm | Widget |
|---|---|---|
| RATE knob | 14.4, 33.0 | `DialKnob` (dial face, radius 12.8, 48 ticks, a long one every 8) |
| RATE attenuverter | 33.6, 27.0 | `Atten` |
| RATE CV jack | 33.6, 39.0 | `BrassPort` |
| WEIGHT knob / WEIGHT CV jack | 8.0, 66.0 / 20.3, 66.0 | `LimbKnob` / `BrassPort` |
| MARK knob | 32.6, 66.0 | `LimbKnob` |
| Outputs 1 2 3 | 7.6, 20.3, 33.0 at y 79.0 | `BrassPort` |
| Outputs 4 5 6 | same x at y 92.0 | |
| Outputs 7 8 ALL | same x at y 105.0 | |
| RESET input / TIDE / HIGH | 7.6, 20.3, 33.0 at y 118.0 | |
| Pulley lights P1 to P8 | x = 5.0 + 4.37·i (5.0, 9.37, … 35.59), y alternating 49.6 and 54.6 (P1 at 49.6) | `JewelLight` |
| HIGH light | 36.1, 113.1 (on the HIGH tile) | `JewelLight` |
| Screws | 33.02, 2.54 and 7.62, 125.96 | |

Small "CV" notes sit under WEIGHT CV (20.3, 71.6) and RATE CV (33.6, 44.6). The default-panel preview shows RATE and WEIGHT at noon and MARK at its default turn. There are light and dark panel files.

### 2.2 Parameter, input, output and light ids

Ids follow the enum order in `Neap.cpp:15-24` [code]. Unlike Fog, no `static_assert` pins them and no test checks them; saved patches depend on them, so QA's patch-load test (section 11.9) is the check.

| Id | Param | Range | Default | Display |
|---|---|---|---|---|
| 0 | RATE | 0..1 | 0.5 | text "1: &lt;period&gt; · 8: &lt;period&gt;", no unit (section 2.3) |
| 1 | RATE CV amount (attenuverter) | −1..+1 | 0 | % (−100..+100) |
| 2 | WEIGHT | −1..+1 | 0 | % (−100..+100); name "Weight (fast ← → slow)" |
| 3 | MARK | −5..+5 | 2 | V; name "Mark (HIGH threshold)" |

| Id | Input | | Id | Output |
|---|---|---|---|---|
| 0 | RATE CV ("Rate CV (1 V/oct)") | | 0..7 | Cycle 1 .. Cycle 8 |
| 1 | WEIGHT CV ("Weight CV (5 V: the knob's travel)") | | 8 | ALL ("All cycles (polyphonic, 8 channels)") |
| 2 | RESET ("Reset (all cycles to phase 0)") | | 9 | TIDE ("Tide (the weighted sum)") |
| | | | 10 | HIGH ("High (gate: Tide above Mark)") |

Lights: ids 0..7 are P1..P8 ("Cycle n"), id 8 is HIGH.

- Each CV input reads **channel 1 only** (`Input::getVoltage()`); an unpatched jack reads 0 V [code: `Neap.cpp:98-100`]. RESET is the same.
- Neap has no audio and no sleep: it always runs.

### 2.3 RATE

[code, `NeapRate.hpp:41-63`]

```
e1 = log2(40) − 12 × (1 − RATE) + att × CVvolts      (cycle 1's exponent, in octaves; f1 = 2^e1 Hz)
e1 clamped to [log2(40) − 14, log2(40) + 2] = [−8.678, +7.322]
```

- The knob spans 12 octaves, 1/12 of its travel per octave. Fully right, cycle 1 runs at 40 Hz; noon is 0.625 Hz; fully left 40/4096 = 9.766 mHz.
- NaN reads as noon (knob), 0 (attenuverter, CV). The knob is clamped to 0..1 and the attenuverter to ±1; the CV is clamped to ±100 V **before** the multiply, so a huge CV with the attenuverter at 0 is exactly 0 [code, test `cv_one_volt_per_octave`].
- RATE is not smoothed. Moving it never makes a step in an output, because the phases integrate the frequency.
- The tooltip shows the periods of cycle 1 and cycle 8, with the spread, the attenuverter and the RATE CV as they are now (not Drift) [code: `RateQuantity`, `rateTooltip`]. Format: "ms" below 1 s ("25.0 ms", "104 ms"), "s" below 60 s, "min" below 60 min, "h" beyond; one decimal ("1.6 s", "4.5 min", "4.8 h"); a value that would print as a full unit rolls to the next ("1.0 s", "1.0 min") [test `tooltip_text`].

Periods with no CV, spread included, Drift off [meas, from `rateTooltip` and `cycleHz`; the three rows marked [test] are also pinned by `tooltip_text`]:

| RATE | Tooltip | Cycle 1 | Cycle 2 | Cycle 3 | Cycle 4 | Cycle 5 | Cycle 6 | Cycle 7 | Cycle 8 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 1: 1.7 min · 8: 4.8 h [test] | 1.7 min | 3.7 min | 7.1 min | 16.1 min | 30.9 min | 59.7 min | 2.2 h | 4.8 h |
| 0.25 | 1: 12.8 s · 8: 36.1 min | 12.8 s | 27.7 s | 53.5 s | 2.0 min | 3.9 min | 7.5 min | 16.6 min | 36.1 min |
| 0.5 (noon) | 1: 1.6 s · 8: 4.5 min [test] | 1.6 s | 3.5 s | 6.7 s | 15.0 s | 29.0 s | 55.9 s | 2.1 min | 4.5 min |
| 0.75 | 1: 200 ms · 8: 33.8 s | 200 ms | 431 ms | 832 ms | 1.9 s | 3.6 s | 7.0 s | 15.6 s | 33.8 s |
| 1 | 1: 25.0 ms · 8: 4.2 s [test] | 25.0 ms | 53.7 ms | 104 ms | 234 ms | 452 ms | 873 ms | 1.9 s | 4.2 s |
| 1, att 1, CV +10 V (top clamp) | 1: 6.3 ms · 8: 1.1 s | 6.3 ms (160 Hz) | 13.4 ms | 25.9 ms | 58.3 ms | 113 ms | 218 ms | 485 ms | 1.1 s |
| 0, att 1, CV −10 V (bottom clamp) | 1: 6.8 min · 8: 19.3 h | 6.8 min | 14.9 min | 28.6 min | 1.1 h | 2.1 h | 4.0 h | 8.9 h | 19.3 h |

Noon frequencies, Hz [meas]: 0.625000, 0.289352, 0.149923, 0.066633, 0.034525, 0.017888, 0.008022, 0.003697. Cycle 1 to cycle 8 at noon is 169.07 : 1 (the product of the steps); across the clamped range the ratio runs from 168.42 (top) to 169.73 (bottom) because of the spread [meas]. Frequency accuracy: each cycle within 0.01 % of the law at 44.1, 48 and 96 kHz [test `frequency_by_crossings`: 1e-4 relative]; the slowest setting at 768 kHz over 50 million samples within 1e-6 [test `slowest_long_run`].

Spec figures that hold: "cycle 1 at 40 Hz and cycle 8 every 4.2 s" at the top; "cycle 1 every 1.6 s and cycle 8 every 4.5 minutes" at noon; "cycle 8 every 4.8 hours" at the bottom (the spec's "cycle 1 every 100 s" is 102.4 s, 1.7 min).

### 2.4 RATE CV and the attenuverter

- Added to the knob in octaves: `att × CVvolts`. Attenuverter fully right is +1 V per octave, fully left −1 V per octave, 0 is off.
- The total is clamped two octaves beyond each end of the knob's range, so cycle 1 can reach 160 Hz and 2.44 mHz (period 409.6 s) [test `cv_one_volt_per_octave`: 160 Hz and 40/16384 Hz within 1e-6].
- It never stalls or runs backwards: a negative CV only slows it, down to the clamp. (øchd stalls on negative CV.)
- NaN and infinities give a finite rate: NaN CV is the knob alone, ±inf CV is the matching clamp [test].
- Exactly 1 V per octave only when the attenuverter is at ±1 and only for cycle 1, which has zero spread. Cycles 2 to 8 follow with their own small slopes (section 2.7).
- The cycles' base frequencies are recomputed (eight `fastExp2D`) only on a sample where e1 changed, not every sample [code: `Cycles.hpp:44-49`].

### 2.5 The eight cycles: step table

[code, `NeapRate.hpp:24-31`]

| Cycle i → i+1 | 1→2 | 2→3 | 3→4 | 4→5 | 5→6 | 6→7 | 7→8 |
|---|---|---|---|---|---|---|---|
| Ratio (steps) | 2.16 | 1.93 | 2.25 | 1.93 | 1.93 | 2.23 | 2.17 |

`kOffsets` (L_i, the log2 of the product of the steps before cycle i, precomputed so there is no libm per sample): 0, 1.1110, 2.0596, 3.2296, 4.1782, 5.1268, 6.2838, 7.4015. The test checks each against the log2 of the step products to 1e-5 and that cycle i is faster than i+1 at 81 settings across the range [test `spread_zero_at_noon_and_for_cycle_1`, `order_kept`].

Cycle i's frequency:

```
f_i = 2^( e1 − L_i + s_i × (e1 − e_noon) ) × (1 + d_i)        e_noon = log2(40) − 6 = −0.678
```

`d_i` is the Drift of section 2.8 (0 when Drift is off). `s_i` is the spread of section 2.7.

### 2.6 The no-locking rule

[code, `NeapRate.hpp:65-88`; test `no_locking`]

For every pair (i < j) with r = f_i / f_j ≤ 16, the relative distance |r − n| / r to every integer n (and, for r ≤ 6, every half-integer) must be at least 3 %.

Measured over the whole clamped range of e1 (−8.678 to +7.322) with a 160,001-point scan [meas `m_lock`]:

| Quantity | Value |
|---|---|
| Worst margin over the range | **0.03039** (3.04 %), pair 6–8 at the bottom clamp (r = 4.853 against the half-ratio 5; ratios 4.826 to 4.853 across the range) |
| Margin at noon | 0.03325 (the spec's "3.3 %") |
| Test grid (81 points) | worst 0.0304 [test prints `no_locking: worst margin over the range 0.0304`] |
| Slack over the 3 % limit | 0.04 percentage points, so any retune of a step or a slope that costs 1.3 % of the margin would fail the test |
| With Drift at its extremes (ratio × 0.99005 to 1.00995) | 0.0202 [meas]. The rule does not cover Drift |

Pairs inside the rule, worst margin by pair (all at an end of the range) [meas]: 1–2 6.7 %, 1–3 3.5 %, 1–4 3.6 %, 2–3 3.5 %, 2–4 3.3 %, 2–5 4.0 %, 3–4 11.0 %, 3–5 3.2 %, 3–6 4.1 %, 4–5 3.3 %, 4–6 5.7 %, 4–7 3.6 %, 5–6 3.5 %, 5–7 4.3 %, 5–8 3.5 %, 6–7 10.0 %, 6–8 3.04 %, 7–8 7.8 %.

**Pairs just beyond the rule's 16:1 cutoff** (never within it at any RATE) [meas `m_lock2`], closest approach to a whole ratio:

| Pair | Ratio range | Closest to an integer |
|---|---|---|
| 4–8 | 18.005 to 18.045 | 0.03 % from 18:1 (at the bottom clamp); 0.14 % at noon |
| 1–6 | 34.900 to 34.978 | 0.06 % from 35:1 |
| 2–7 | 35.951 to 36.191 | **0.000 %**: exactly 36:1 at e1 = −5.42 (RATE about 0.105 with no CV) |
| 2–6 | 16.068 to 16.283 | 0.42 % from 16:1 (1.1 % at noon) |
| 1–5 | 18.063 to 18.143 | 0.35 % from 18:1 (0.57 % at noon) |
| 3–7 | 18.659 to 18.721 | 1.5 % from 19:1 |

These are allowed by the spec (it calls them too far apart to hear locking). Drift's ±0.5 % per cycle moves each of them by up to 1 %, so they pass through their whole ratios during Drift. Whether any is audible is for the listening stop (guide item STEP TABLE: "Does any pair still seem to fall into step by ear or on a scope?").

### 2.7 Tracking spread

[code, `NeapRate.hpp:27`] Each cycle's exponent includes `s_i × (e1 − e_noon)`, so the ratios between cycles change a little with RATE, like øchd's analogue converters. Cycle 1 has s = 0 (the reference); RATE's periods and RATE CV's 1 V/oct are exact for it.

| Cycle | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| s_i | 0 | 0.0014 | 0.0011 | 0.0009 | 0.0004 | 0.0002 | 0.0008 | 0.0007 |

- At noon the spread is exactly zero for all eight [test].
- Cycle 8's frequency against the spread-free law: ×0.99709 at knob 0 and ×1.00292 at knob 1 [meas]. Cycle 2 moves 1.2 % across the knob [code arithmetic].
- The order of cycles never changes [test `order_kept`].

### 2.8 Drift

[code, `Cycles.hpp:68-79`; menu "Drift (slow random wander of each rate)", checkbox, default on]

- Each cycle has its own seeded random source (`RandomGen`, seed 1000 + i, first target drawn at construction) and a drift phasor at 1 / (60 + 9 i) Hz: periods 60, 69, 78, 87, 96, 105, 114, 123 s for cycles 1 to 8.
- At each wrap of that phasor a new random target in [−1, 1] is drawn; the value moves from the previous target to the new one by a cosine glide over the whole period (glide 1), so the rate wanders continuously. `d_i = 0.005 × value`.
- Cycle frequency = base × (1 + d_i): at most ±0.5 % [test `drift_bounded_and_moving`: within 0.005 + 1e-6, and each cycle's range over 30 min wider than 0.2 %; and matches an independent reference built from the plan's numbers within 2e-6].
- Extent over 2 simulated hours at noon, per cycle (percent, low / high) [meas]: 1: −0.475/+0.487; 2: −0.476/+0.497; 3: −0.493/+0.493; 4: −0.489/+0.485; 5: −0.483/+0.500; 6: −0.480/+0.496; 7: −0.474/+0.488; 8: −0.450/+0.486.
- The drift state advances only while Drift is on [test `drift_off_leaves_drift_state`]. Drift off is exact: with Drift off, two runs from a Reset are bit-identical [test; meas: a Reset at 100 s and one at 37 s give 0 differing samples over the next 120 s]. **With Drift on, a Reset does not make two runs repeat** (the drift state is kept): the same test gave differences on all 5,760,000 samples [meas].
- Fresh modules are deterministic (same seeds): two new Neaps with Drift on start with identical drift. A patch load starts every cycle at phase 0 (phases are not saved) and drift from the beginning of its sequence [code].
- The tooltip does not include Drift.

### 2.9 Wave

[code, `Cycles.hpp:35-39, 59-66, 86-92`; menu "Wave": Triangle (default), Sine]

- Triangle of phase p: 4p for p < 0.25, 2 − 4p up to 0.75, then 4p − 4. Zero rising at p = 0, +1 at 0.25, 0 at 0.5, −1 at 0.75. Sine is `fastSin2pi(p)` (maximum error 1.6e-7 over a cycle, 0.8 µV at ±5 V [meas]).
- Both start at 0 rising, so Spring's rising-zero-crossing triggers fall at the same moment in either wave.
- The two differ by at most 0.2105 of full scale, 1.05 V at ±5 V, at the same phase [meas].
- A change of Wave is a **10 ms linear crossfade** (480 samples at 48 kHz; mid-fade a new change reverses it) [test `crossfade_takes_ten_ms`: each step within 2e-4 of the expected blend]. The output may bend but does not jump: every sample step stays within the cycle's own slope plus the crossfade's, over many phases and toggles every 37 to 900 samples [test `wave_crossfade_no_jump`]. Toggling every 2 ms stays inside ±1 [test].
- A Reset finishes the crossfade at once [test `reset_puts_all_at_zero_rising`]; at phase 0 both shapes are 0, so there is no extra step.
- Wave is not saved in the cycles; it is the menu's `"wave"` key (section 4).

### 2.10 Level

[code, `NeapEngine.hpp:65-72`; menu "Level": ±5 V (default), 0–10 V]

- ±5 V: cycles and TIDE are 5·x and TIDE. 0–10 V: 5·x + 5 and TIDE + 5. This applies to C1..C8, ALL and TIDE together.
- **HIGH is 10 V or 0 V whichever Level** [test `levels`]. The comparison is made in the ±5 V domain (section 2.13).
- Level changes only the offset: no step but the 5 V one when the menu is changed (the offset is applied at once, not crossfaded) [code; test `toggles` in `test_hostile` checks that Level changes only the offset].
- Measured, 20 s at RATE 0.9: every cycle's output spans −5.000 to +5.000 V (±5 V triangle) and 0.000 to 10.000 V (0–10 V sine) [meas].
- **Spring never sees Level**: it gets the ±5 V shaped values (section 6.1).

### 2.11 WEIGHT

[code, `NeapEngine.hpp:56-60`, `Tide.hpp:36-61`]

```
w_target = clamp( WEIGHT + 0.2 × WEIGHT_CV_volts , −1, +1 )        (NaN CV reads 0, NaN knob reads 0)
w        = one-pole glide of w_target, time constant 50 ms
weight of cycle j (1..8) = 2^( w × (j − 4.5) )
```

- WEIGHT CV: 5 V adds 1.0 (the knob's whole travel from noon); the tooltip says "5 V: the knob's travel". From a knob at the far left, +5 V reaches only noon.
- Weight (fast ← → slow): fully left (−1) favours cycle 1, fully right (+1) favours cycle 8; each step of cycles changes the weight by a factor 2^|w|.
- Share of the total weight per cycle, percent [code arithmetic]:

| Weight | C1 | C2 | C3 | C4 | C5 | C6 | C7 | C8 |
|---|---|---|---|---|---|---|---|---|
| −1 | 50.2 | 25.1 | 12.5 | 6.3 | 3.1 | 1.6 | 0.8 | 0.4 |
| −0.5 | 31.2 | 22.1 | 15.6 | 11.0 | 7.8 | 5.5 | 3.9 | 2.8 |
| 0 | 12.5 | 12.5 | 12.5 | 12.5 | 12.5 | 12.5 | 12.5 | 12.5 |
| +0.5 | 2.8 | 3.9 | 5.5 | 7.8 | 11.0 | 15.6 | 22.1 | 31.2 |
| +1 | 0.4 | 0.8 | 1.6 | 3.1 | 6.3 | 12.5 | 25.1 | 50.2 |

- The glide ends exactly on its target at 8, 48 and 768 kHz, from either side (the plain one-pole would stall about 7e-5 short at 48 kHz and 1e-3 at 768 kHz; the code snaps within 1e-4) [test `weight_glide`]. A bad sample time freezes the glide where it is [test].
- During a glide TIDE moves less than 0.01 V per sample with the cycles held [test].
- Weight at the end of its travel: TIDE follows cycle 1 (Weight −1) or cycle 8 (Weight +1) most closely, with a correlation margin of at least 0.3 over the other end [test `weight_leads`].

### 2.12 TIDE

[code, `Tide.hpp:11-24, 62-66`]

```
raw  = 2.0 × Σ(weight_j × x_j) / Σ(weight_j)          (kTideGain = 2.0)
TIDE = 5 V × limit(raw)
limit(x) = x (27 + x²) / (27 + 9 x²)      for |x| < 3 ;   ±1 beyond ; NaN gives 0
```

- Slope 1 at 0, odd, monotone, continuous at ±3 [test `limit_shape`]. With the gain of 2, TIDE near zero is twice the weighted mean of the cycles' output volts.
- **The peak is 4.9206 V** (all eight cycles at +1: raw 2, limit 0.9841) [code arithmetic]. The limiter never reaches ±5 V in use, so it never clips flat. Over 24 h at noon the measured extremes are +4.74/−4.71 V at Weight 0 and up to +4.88/−4.90 V at Weight −1 [meas].
- A NaN cycle gives 0 V, never NaN [test]. |TIDE| ≤ 5 V for 24 simulated hours at every Weight in {−1, −0.5, 0, 0.5, 1} at noon [test `bounded_24h`, at 100 Hz sampling].

**How far TIDE swings (the "beyond ±2.5 V" figure).** Simulated with the engine's own `Cycles` and `Tide`, Drift off, RATE noon [meas `m_tide`]:

| Weight | Beyond ±2.5 V, 24 h | RMS | Beyond +2.5 V / below −2.5 V |
|---|---|---|---|
| −1 | 46.91 % | 2.66 V | 23.45 % / 23.46 % |
| −0.5 | 30.45 % | 2.17 V | 15.22 % / 15.23 % |
| **0 (noon)** | **18.78 %** | 1.82 V | 9.46 % / 9.32 % |
| +0.5 | 30.25 % | 2.17 V | 15.31 % / 14.94 % |
| +1 | 45.73 % | 2.66 V | 22.62 % / 23.11 % |

- The test prints the same figures (46.9, 30.4, 18.8, 30.2, 45.7 for Weight −1 to +1) as an information line, not a gate.
- At Weight 0 the figure is **18.8 % at 24 h and 18.7 % at 168 h** (18.70 %; Drift on, 24 h: 18.64 %, 168 h: 18.68 %). Sampling at 1 kHz instead of 100 Hz gives the same 18.78 %. It does not depend on RATE: 18.69 % at RATE 0.75 and RATE 1 (cycle 1 at 40 Hz); 18.65 % at RATE 0.25; 18.45 % at RATE 0 (24 h is only 1.3 periods of cycle 8 there, so that one is not converged).
- The first 10 minutes after a Reset at noon read 18.86 % with a peak of +4.51 V: the "spring tide" after a Reset (all cycles starting together) does not show as a swell.
- The spec's target was "roughly a quarter" (25 %); the listening guide says "against an aim of about 25 %" and asks whether the gain should go up.
- In 0–10 V Level the same distribution reads beyond 2.5 V and 7.5 V on the TIDE jack (TIDE + 5 V).

### 2.13 HIGH and MARK

[code, `Tide.hpp:82-94`, `NeapEngine.hpp:62-63`]

- HIGH goes to 10 V when TIDE (the ±5 V value, before Level) rises above MARK + 0.05 V, and to 0 V when it falls below MARK − 0.05 V; between, it keeps its state. NaN TIDE or MARK keep it. HIGH is never anything but 0 V or 10 V [test].
- MARK: −5 to +5 V, default +2 V, no CV, not smoothed. The tooltip reads in volts and does not change with Level.
- **In 0–10 V Level, MARK compares against TIDE before the +5 V offset** (the comparison is TIDE against MARK in the ±5 V domain). That is the same as comparing the TIDE jack with MARK + 5 V. [meas, 3 h at RATE noon, MARK +2 V, both Levels run in step]: 0 samples where HIGH differs between the two Levels; duty 15.60 % in both; the TIDE jack at the rising edge averages **2.052 V in ±5 V Level and 7.052 V in 0–10 V Level**; the lowest TIDE jack while HIGH is 1.950 V (6.950 V), the highest while low 2.050 V (7.050 V). The test `levels` asserts the same equivalence at marks −3, 0 and +2.
- Consequence for QA: in 0–10 V Level the MARK knob still reads 2 V at its default, but the threshold you see on the TIDE jack is 7 V. The listening guide asks "in 0–10 V, should MARK follow the offset?"; in effect it already does (HIGH fires at the same moments), only the displayed knob value does not move.
- The light on the HIGH tile is lit (1.0) while HIGH is high, else 0 [code: `Neap.cpp:125`].

**HIGH's duty at the default MARK +2 V** [meas `m_tide`, RATE noon, Drift off, Weight 0]: **15.56 % over 24 h** (15.39 % over 168 h; 15.4 % with Drift on; 15.60 % in the 3 h run of the Level check). It rises about **617 times an hour** at noon (mean time high about 0.91 s; the number scales with RATE: 78 per hour at RATE 0.25, 4,998 at 0.75). The duty barely moves with RATE (15.3 % to 15.6 %).

HIGH's duty by MARK and Weight, 24 h at noon, Drift off [meas]:

| MARK | Weight −1 | Weight 0 | Weight +1 |
|---|---|---|---|
| −5 V | 100.00 % | 100.00 % | 100.00 % |
| −4 V | 94.23 % | 99.60 % | 93.79 % |
| −2 V | 70.82 % | 84.64 % | 70.46 % |
| 0 V | 50.00 % | 49.88 % | 50.42 % |
| **+2 V** | **29.17 %** | **15.56 %** | **28.67 %** |
| +4 V | 5.77 % | 0.36 % | 6.25 % |
| +5 V | 0.00 % | 0.00 % | 0.00 % |

**The knob's ends do nothing.** TIDE's range is ±4.92 V and HIGH needs ±0.05 V beyond MARK. So at MARK −5 V (and below about −4.87 V) HIGH goes high on the first sample and can never fall, and at MARK +5 V (and above about +4.87 V) it can never rise [code arithmetic; meas 100.00 % and 0.00 %]. Not mentioned in the spec.

### 2.14 RESET

[code, `NeapEngine.hpp:47-49`; test `reset_edges`]

- A Schmitt trigger on the RESET jack: rises through **1 V**, re-arms below **0.1 V**. Anything held high does not repeat; falling to 0.5 V does not re-arm; noise between 0.11 and 0.99 V never resets [test].
- A reset puts all eight phases at 0 (rising) and finishes any Wave crossfade. On the sample of the edge the cycles read small positive values (noon, volts): 0.00026, 0.00012, 0.00006, 0.00003, 0.00001, 0.00001, 0.000003, 0.000002 [meas]. The Drift state is kept; TIDE and HIGH are **not** cleared by a RESET edge (only by Initialize) [code].
- After a Reset the cycles start together and then part (spec). The first hour of TIDE has no special swell (section 2.12).
- A RESET jack already high when the module is created or Initialized does not cause a second reset (the Schmitt trigger settles without firing) [code; test comment].
- RESET reads channel 1 only. A reset jumps every cycle to 0 V: up to 5 V in one sample. That is the only jump the spec allows. Reset also makes a Spring trigger fire in some cases (section 6.5).

### 2.15 ALL

[code, `Neap.cpp:121-122`] A polyphonic output with **8 channels** (always 8, whether or not a cable is patched). Channel i carries exactly output Ci, after Level, bit for bit (`writeVoltages(result.cycle)`; the same array feeds C1..C8). TIDE and HIGH are not in it.

### 2.16 Lights

[code, `NeapEngine.hpp:69`, `Neap.cpp:119-125`] Each pulley light P1..P8 shows `(x + 1) / 2`: dark at the bottom of the cycle, full at the top (one colour, per the look). They are linear brightness, not smoothed. A very fast RATE therefore reads as an average grey. The lights follow the shaped value, so Level does not change them [test `levels`: light = (shaped/5 + 1)/2 within 1e-6]. The HIGH light is on or off.

---

## 3. Spring: panel and ports

6 HP (30.48 mm). Title SPRING, subtitle EXPANDER, the foot mark "R K A". No knobs, no inputs. [code: `tools/gen_panels.py`, `SpringLayout.hpp`]

Sixteen outputs in two staggered columns (x 16.2 and 25.4 mm), alternating left and right, each half a pitch (5.7 mm) below the previous, in four labelled groups with a 1.8 mm gap between groups. The group names sit in the left margin at x 6.0: FOLD, MIN/MAX, TRIG, STEPS. The tiles are 1 mm shorter than the default; the MIN/MAX labels are in smaller lettering. The LINK light is at (26.48, 19.5) with a "LINK" note to its left.

| Id | Output | Label | x, y mm | Tooltip |
|---|---|---|---|---|
| 0 | F1 | 1 | 16.2, 25.0 | Fold 1 (0..10 V, 2 \|x\|) |
| 1 | F3 | 3 | 25.4, 30.7 | Fold 3 |
| 2 | F5 | 5 | 16.2, 36.4 | Fold 5 |
| 3 | F7 | 7 | 25.4, 42.1 | Fold 7 |
| 4 | MIN23 | MIN 2-3 | 16.2, 49.6 | Min of 2 and 3 |
| 5 | MAX23 | MAX 2-3 | 25.4, 55.3 | Max of 2 and 3 |
| 6 | MIN67 | MIN 6-7 | 16.2, 61.0 | Min of 6 and 7 |
| 7 | MAX67 | MAX 6-7 | 25.4, 66.7 | Max of 6 and 7 |
| 8 | T2 | 2 | 16.2, 74.2 | Trigger 2 (rising zero crossing) |
| 9 | T4 | 4 | 25.4, 79.9 | Trigger 4 |
| 10 | T6 | 6 | 16.2, 85.6 | Trigger 6 |
| 11 | T8 | 8 | 25.4, 91.3 | Trigger 8 |
| 12 | S14 | 1-4 | 16.2, 98.8 | Steps 1-4 (4 bits: above zero) |
| 13 | S58 | 5-8 | 25.4, 104.5 | Steps 5-8 |
| 14 | SODD | ODD | 16.2, 110.2 | Steps odd (1 3 5 7) |
| 15 | SEVEN | EVEN | 25.4, 115.9 | Steps even (2 4 6 8) |

Light id 0: LINK ("Link (Neap at the left)"). The output count and the three T ids are pinned by `static_assert` in `Spring.cpp:26-27`.

---

## 4. The right-click menus and saved state

### 4.1 Neap's menu

[code, `Neap.cpp:181-195`] Items, in order: a separator; **Drift (slow random wander of each rate)** (checkbox); **Wave** (submenu: Triangle, Sine); **Level** (submenu: ±5 V, 0–10 V); a separator; **Add Spring expander**. Spring has no context menu of its own.

| Item | Choices | Default | JSON key | Stored value | On load |
|---|---|---|---|---|---|
| Drift | on / off | on | `"drift"` | JSON boolean | **only a JSON boolean is read** (`false` turns it off, `true` leaves it on); a missing key, `0`, `1`, a string or anything else keeps the default, on |
| Wave | Triangle, Sine | Triangle | `"wave"` | integer 0 or 1 | integer value 1 gives Sine; anything else (missing, 0, 2, a real such as `1.0`, a string, `true`) gives Triangle |
| Level | ±5 V, 0–10 V | ±5 V | `"level"` | integer 0 or 1 | the same rule: only integer 1 gives 0–10 V |

A saved module's `"data"` object looks like `{"drift": true, "wave": 0, "level": 0}`. The knobs are saved by Rack as ordinary params (ids 0–3). **A missing key sets the default**, unlike Fog (where a missing key keeps the current setting). **The cycles' phases are not saved**: a patch loads with every cycle at phase 0, and Drift starts from its seeds [code, spec "(call)"].

JSON values that do not fit cannot corrupt anything; the code never narrows an integer (it compares the 64-bit value with 1), so Fog's issue #12 does not apply. The parsing is in the Rack-free `NeapState.hpp` (`parseMenu`; `Neap.cpp` only converts each `json_t*` to a `JsonValue`), tested in `test_state`: good values, missing keys, wrong types, and out-of-range integers such as 2, -1 and 2^32+1 [test]. There is still no golden saved-state image for either module [TESTING.md].

**Initialize** (`onReset`) sets Drift on, Wave Triangle, Level ±5 V, and resets the engine: phases to 0, the Weight glide to 0, HIGH low, the RESET trigger to "not yet seen"; the Drift state is kept [code: `NeapEngine::reset`]. Rack resets the knobs.

### 4.2 Add Spring expander

[code, `Neap.cpp:167-179, 190-194`; new in 2.0.39]

- Greyed out (disabled) while Neap's `rightExpander.module` is a Spring, or any module that implements `NeapExpander`. It checks only the module directly to the right.
- Otherwise it creates a Spring, adds it to the engine and the rack, and places it at Neap's right edge (`setModulePosForce`), **pushing the modules to the right along** (the same method as Ballast's "Add Pan expander"). The action is one undo step, named "create Spring" (`history::ModuleAdd`).
- Not covered by any automated test and not run by me. Whether Rack greys the item correctly, whether the placement and the push work in a crowded row, and whether undo removes the module cleanly are [unverified]. The listening guide lists it among the paths no automated run can reach.
- The result can also be reached the ordinary way: add Spring from the browser and drag it against Neap's right side. Rack then sets `rightExpander` itself [unverified].

### 4.3 Spring's state

Spring has no params and saves no data. Initialize sets a flag so that the next call from Neap clears the trigger state and the outputs [code: `Spring.cpp:39-49`, `SpringRun` in `SpringCore.hpp:162-180`].

### 4.4 Bypass

Neither module configures bypass routes, so Rack's default applies (nothing is routed) [code; Rack behaviour unverified].

- A bypassed Neap's `process()` does not run: the cycles freeze where they are, outputs go to whatever Rack's default bypass gives (0 V is expected, [unverified]) and the call to Spring stops. Spring goes unlinked 64 process calls later (section 6.2): outputs 0 V, LINK dark. When bypass ends the cycles resume from where they were.
- A bypassed Spring is skipped by Neap (`!m->isBypassed()`), and its own `process()` does not run either. Spring's `onBypass` and `onUnBypass` ask for a reset, which the next `fromNeap` carries out (on Neap's thread, with every other core access), so the trigger history from before the bypass is never resumed: no late trigger on un-bypass [test `run_reset_latch`, the `SpringRun` helper in `SpringCore.hpp`].

---

## 5. Neap's signal flow

### 5.1 Block diagram

```
 RATE knob ─┐
 RATE CV ──► att ──► e1 = log2(40) − 12(1−RATE) + att×CV ──► clamp [−8.678, +7.322]
                       │
                       ▼  (8 exponentials, only when e1 changes)
   f_i = 2^(e1 − L_i + s_i (e1 − e_noon)) × (1 + d_i)       d_i: Drift, ±0.5 %, per cycle
                       │
 RESET ─► Schmitt 1 V / 0.1 V ─► all phases := 0
                       ▼
   phase_i += f_i × dt        (double precision)
                       ▼
   triangle(p) ──┐
                 ├─► crossfade, 10 ms ─► x_i  (±1)
   sine(p) ──────┘     (Wave menu)
                       │
        ┌──────────────┼───────────────────────────────┬───────────────────────────┐
        ▼              ▼                               ▼                           ▼
   × 5 V = shaped_i   lights P_i = (x+1)/2        TIDE: Σ w_j x_j / Σ w_j × 2.0      
   (±5 V)                                         w_j = 2^(w (j−4.5)),               
        │                                         w = WEIGHT + 0.2×WEIGHT CV,        
        │                                         clamp ±1, 50 ms glide              
        │                                                  ▼                         
        │                                         × limit(·) × 5 V  = TIDE (±5 V)    
        │                                                  │
        │                                                  ▼
        │                                         HIGH gate vs MARK (±0.05 V) ─► 0 / 10 V, HIGH light
        │
        ├──► fromNeap(shaped[8], dt) ──► SPRING (same sample, Neap's thread)
        │
        ▼
   + 5 V if Level 0–10 V ──► C1..C8, ALL (8 channels);   TIDE + 5 V ──► TIDE
```

Spring takes the shaped values before Level and before TIDE, HIGH, WEIGHT and MARK; none of those reaches it (section 6.1).

### 5.2 Block constants

| Block | Constant [code] |
|---|---|
| Phase | double per cycle; `Phasor::step(Hz, dt)` (from `sway/Phasor.hpp`) |
| Wave crossfade | 10 ms linear (`Cycles.hpp:95`) |
| Drift depth / periods | 0.005 / 1/(60 + 9 i) Hz, seeds 1000 + i (`Cycles.hpp:71-73, 96`) |
| TIDE gain | 2.0 (`Tide.hpp:11`) |
| Weight glide | 50 ms one-pole (`Tide.hpp:72`); snap within 1e-4 |
| Weights | 2^(w (j − 3.5)), j = 0..7, recomputed only while w moves |
| HIGH hysteresis | ±0.05 V about MARK (`Tide.hpp:88-91`) |
| RESET | Schmitt, low 0.1 V, high 1 V (`NeapEngine.hpp:48`) |
| Sample time accepted | 0 < dt < 1 s (`NeapEngine.hpp:44`); otherwise the last good one is kept. The cycles also reject a step of 1e9 s or more |
| Lights | `(x + 1) / 2`, 0..1 |
| Maths | `fastExp2`, `fastExp2D`, `fastSin2pi` (`FastMath.hpp`); no libm per sample |

---

## 6. Spring

### 6.1 What Spring sees

[code, `NeapExpander.hpp`, `Neap.cpp:127-130`, `NeapEngine.hpp`]

Each sample, Neap's `process()` finds `rightExpander.module`, skips it if bypassed, and (if it is a `NeapExpander`) calls `fromNeap(result.shaped, sampleTime)`. `shaped[i]` is **5·x_i in ±5 V, after Wave and its crossfade but before Level**. Spring does all of its per-sample work inside that call; its own `process()` only writes the outputs and the light.

**So TIDE, HIGH, WEIGHT, MARK and Level never reach Spring.** Verified [meas `m_level`, 10 minutes at 48 kHz, RATE 0.8, four engines with different WEIGHT (0, 1, −1, 0.3), WEIGHT CV (up to 3 V), MARK (2, −3, 5, −5) and Level (±5 V and 0–10 V) feeding four Spring cores]: 0 samples where any of the sixteen outputs or `shaped[]` differ between them. Control: switching Wave to Sine does change FOLD 1 (on all 480,000 samples tested). The test `levels` asserts `shaped` is identical in both Levels and equals the ±5 V cycle output [test]. So Spring's FOLD and MIN/MAX follow the sine when Wave is Sine, and are always ±5 V-based even when Neap's jacks read 0–10 V. This is as the spec rules ("Spring works on Neap's shaped cycles … before the Level setting"). The listening guide asks the user whether Spring should use TIDE or WEIGHT.

### 6.2 The link and its timeout of 64 process calls

[code, `SpringCore.hpp:129-180`, `Spring.cpp:52-71`; tests `link_timeout`, `run_reset_latch`]

- Neap's call to `fromNeap` increments an atomic counter. Spring's `process()` compares it with the last value it saw, once per call.
- A change means linked (idle count 0). With no change the idle count rises; **at 64 consecutive process calls with no change the link is lost**. At that point the outputs are set to 0 V, the LINK light goes dark and the trigger state is flagged to clear.
- Spring starts unlinked and becomes linked on the first process call that sees a change.
- [meas `m_spring`]: after Neap's last call, **63 further process calls still read as linked, and the 64th reads unlinked**. 64 calls last 1.451 ms at 44.1 kHz, 1.333 ms at 48 kHz, 0.667 ms at 96 kHz and 0.333 ms at 192 kHz. During the 63 linked calls the outputs hold their last values (a trigger that was high stays 10 V for up to 63 samples).
- Steady callers stay linked (a thousand double calls per process call [test]). The count restarts after any change [test].
- Re-linking: the first `fromNeap` after an unlink (or after a bypass or un-bypass of Spring) clears the core and **only records** the cycle values, so no trigger fires on re-link, whatever the cycles were doing [test `reset_no_burst`, `relink_has_no_burst`].
- The same timeout covers: Spring removed (nothing to link), Neap bypassed, Neap deleted, and Spring moved away from Neap's right side. In a multi-threaded Rack, Neap's thread and Spring's can run in either order within a frame, so Spring may read the outputs one sample behind (the spec allows a one-sample delay). The timeout of 64 is far above that skew [unverified in a live Rack].
- `core.out` is written by Neap's thread and read by Spring's `process()` without a lock, by ruling: aligned 32-bit floats (TESTING.md). A read may mix two successive samples' outputs; each value is one valid sample [code].

### 6.3 Outputs

[code, `SpringCore.hpp:13-124`; all tests pass, section 9]

| Output | Law | Range | Notes |
|---|---|---|---|
| F1, F3, F5, F7 | 2·\|x\| of shaped cycles 1, 3, 5, 7 | 0 to 10 V | twice the cycle's rate; NaN gives 0; clamped to 10 V. Measured F1 range 0.0000 to 10.0000 V [meas] |
| MIN23, MAX23 | lower / higher of shaped cycles 2 and 3 | ±5 V | bit-exact [test]; NaN in one input passes the other; clamped to ±10 V |
| MIN67, MAX67 | lower / higher of shaped cycles 6 and 7 | ±5 V | as above |
| T2, T4, T6, T8 | a 10 V pulse of 10 ms at each rising zero crossing of cycle 2, 4, 6, 8 | 0 or 10 V | section 6.4 |
| S14, S58, SODD, SEVEN | 4-bit code from "cycle above 0 V", out as code × 10/15 V | 0 to 10 V in 16 steps | section 6.4 |

### 6.4 Triggers, cascade and steps

**Triggers.** A crossing is "previous sample < 0 and this sample ≥ 0" on the shaped cycle. It fires on the sample of the crossing and lasts a 10 ms timer.

- Width [meas]: 441, 480, 882, 960 samples at 44.1, 48, 88.2, 96 kHz (exactly 10.000 ms); 1,921 at 192 kHz and 7,681 at 768 kHz (one sample over). Tests allow ±1 sample from 8 kHz to 768 kHz and a rate change mid-pulse.
- A crossing while a pulse is high restarts the timer (merged, not a second pulse) [test `trigger_once_per_rise`].
- A falling crossing gives nothing; staying at exactly 0 does not fire; NaN on a source clears its history so it can neither fire nor stick [test, `nan_does_not_fire_or_stick`].
- Counts [meas, 10 min at 48 kHz, RATE 0.8, all four patched]: T2 2,112 pulses (expected 2,112.5), T4 485 (485.9), T6 130 (130.2), T8 26 (26.9). The pulses are high 3.520 %, 0.808 %, 0.217 % and 0.043 % of the time.

**Cascade for unpatched jacks.** `setPatched(k, isConnected())` is refreshed on every call from the T outputs. An unpatched T output passes its triggers on to the next *patched* one, going in the order T2 → T4 → T6 → T8 → T2, OR-merged. Nothing patched: nothing fires. [meas `m_spring`: which cycles' crossings reach each patched jack, for all 16 patch masks; "T2<-{2,6,8}" means T2 receives the crossings of cycles 2, 6 and 8]:

| Patched | Result |
|---|---|
| none | no output |
| T2 only | T2 ← 2,4,6,8 |
| T4 only | T4 ← 2,4,6,8 |
| T6 only | T6 ← 2,4,6,8 |
| T8 only | T8 ← 2,4,6,8 |
| T2, T4 | T2 ← 2,6,8; T4 ← 4 |
| T2, T6 | T2 ← 2,8; T6 ← 4,6 |
| T2, T8 | T2 ← 2; T8 ← 4,6,8 |
| T4, T6 | T4 ← 2,4,8; T6 ← 6 |
| T4, T8 | T4 ← 2,4; T8 ← 6,8 |
| T6, T8 | T6 ← 2,4,6; T8 ← 8 |
| T2, T4, T6 | T2 ← 2,8; T4 ← 4; T6 ← 6 |
| T2, T4, T8 | T2 ← 2; T4 ← 4; T8 ← 6,8 |
| T2, T6, T8 | T2 ← 2; T6 ← 4,6; T8 ← 8 |
| T4, T6, T8 | T4 ← 2,4; T6 ← 6; T8 ← 8 |
| all four | each ← its own only |

The listening guide's patch 5 (T2 and T8 patched) gives T2 ← 2 and T8 ← 4,6,8, as the guide says ("Narrows rings on cycles 4, 6 and 8 together"). The code, the plan, the test `cascade_all_masks` (all 16 masks against the crossings) and my table agree.

**Steps.** Each code is built from four cycles: bit = (shaped x > 0) (exactly 0 V counts as not above), the first named cycle is the most significant bit, volts = code × 10/15, code 15 giving exactly 10 V. Cycle groups: S14 = cycles 1,2,3,4; S58 = 5,6,7,8; SODD = 1,3,5,7; SEVEN = 2,4,6,8. The sixteen levels are 0, 0.6667, 1.3333, 2.0000, 2.6667, 3.3333, 4.0000, 4.6667, 5.3333, 6.0000, 6.6667, 7.3333, 8.0000, 8.6667, 9.3333, 10.0000 V [meas]. They do not fall on semitones (2/3 V apart is 8 semitones at 1 V/oct); a quantiser picks the notes (spec). Checks [meas]: only x1 above 0 gives S14 = SODD = 5.3333 V (code 8) and S58 = SEVEN = 0; only x4 above 0 gives S14 = 0.6667 V (code 1) and SEVEN = 2.6667 V (code 8). Over 10 minutes the 16 codes of S14 and SEVEN each occupy 6.2 to 6.3 % of the time; S58 and SODD vary 5.9 to 6.7 %, as slow cycles dwell [meas].

### 6.5 What a Reset does to Spring

A Neap Reset drops each cycle to phase 0, which reads about +0.0003 V next sample. If the cycle was **below 0 V** before the edge, that looks to Spring like a rising zero crossing and its trigger fires; if it was above 0 V nothing happens. [meas, 400 resets about 7 s after the previous, noon]: T2 fired on 170 and T4 on 369 of them (T6 and T8 never, since cycles 6 and 8 were in their upper half at those times). Not in the spec. It is the behaviour of the rule, not a malfunction. In a patch where T2 or T4 strikes something, a RESET pulse will sometimes also strike it. Also, STEPS codes jump at a Reset (all cycles go from wherever they were to "just above 0": every bit becomes 1, so the codes go to 15 = 10 V on the next sample for all four) [code arithmetic: x = +0.0003 V is above 0].

### 6.6 LINK light

Lit (1.0) while linked, dark otherwise [code: `Spring.cpp:59-71`]. It is not smoothed. Unlinked Spring writes 0 V on every output every call.

---

## 7. CPU, sample rates and robustness

### 7.1 CPU

`make bench` cases, 48 kHz, per instance, % of one core [code: `src/neap/bench.inc`]:

- **idle**: defaults, nothing patched (Drift on, Triangle). The cycles always run; there is no sleep, so idle costs the same as playing.
- **outputs**: Sine and 0–10 V, all outputs patched.
- **busy**: Sine, RATE CV and WEIGHT CV moving at full attenuverter, RESET pulsing, MARK swept.
- **spring**: as busy, Spring linked, all four T outputs patched, the core run from the shaped cycles.

| Case | Budget, Apple arm64 | Budget, other (provisional, 3×) | TESTING.md measured | Mine, 1 / 16 instances [meas, `build/bench/bench`, 5 s] |
|---|---|---|---|---|
| idle | 0.4 | 1.2 | 0.185 | 0.200 / 0.209 |
| outputs | 0.5 | 1.5 | 0.250 | 0.274 / 0.275 |
| busy | 0.7 | 2.1 | 0.337 | 0.367 / 0.362 |
| spring | 0.75 | 2.25 | 0.375 | 0.404 / 0.404 |

- My run was made with other work on the machine; it reads 8 to 10 % above the figures in TESTING.md and well inside every budget. Spring's own cost is about 0.04 (spring minus busy) [TESTING.md; mine 0.037].
- The x86-64 and Windows budgets have not been measured on native hardware. **Windows CPU figures are not covered by any automated budget.**
- The bench does not include the module's per-sample `dynamic_cast` to `NeapExpander` and the `rightExpander` lookup in `Neap::process()`, nor Rack's port and light overhead.
- `make libm-calls` (no libm call per sample) runs only on Linux; I did not run it [issue #36: the check is skipped on macOS]. The tables are precomputed so that no libm is needed per sample [code comment]. `make engine-globals` (no writable global state) passed here.

### 7.2 Sample rates

[code, `NeapEngine.hpp:44-45`; tests `sample_rates`, `sample_rate_change`, `bad_sample_times`]

- The engine accepts any sample time with 0 < dt < 1 s (rates above 1 Hz); a non-finite, zero or negative one keeps the last good one. Spring's core takes 0 < dt < 1e9 s the same way.
- Tests run 8 kHz to 768 kHz and changes between them every few hundred samples. Cycle 1 at its fastest keeps 40 Hz at every rate [test]. A change of rate mid-run keeps the phases (no jump) and the frequencies stay in Hz [test `sample_rate_change`].
- Cycle phases are doubles so the slowest cycle still moves at 768 kHz at the bottom clamp (1.4e-11 of a cycle per sample) [test `slowest_long_run`].
- There is no `onSampleRateChange`; each sample uses `args.sampleTime` [code].
- Trigger pulses stay 10 ms of simulated time through a rate change [test `pulse_width_at_every_rate`].

### 7.3 Hostile input and numerical safety

[tests `test_hostile` for Neap and for Spring, built with ASan and UBSan; plan "Review Focus"]

- NaN, ±infinity and ±1e30 on every frame field (RATE, attenuverter, RATE CV, WEIGHT, WEIGHT CV, MARK, RESET, sample time), one at a time and all together, in both Levels and both Waves: every output is finite and inside its range (cycles and TIDE ±5 V, or 0–10 V; HIGH 0 or 10 V; lights 0..1; `shaped` ±5 V), and the module is back to normal when the frame is sane again [test].
- Wave, Level and Drift toggled every 1 to 481 samples; RESET held high, high with noise around its thresholds, and at audio rate: one reset per rising edge, nothing out of range [test].
- Three fixed seeds × 10^6 samples of fuzzed fields, toggles, sample rates and resets (Neap); the same for Spring's core alone, and for Neap's fuzzed engine feeding Spring's core with the link dropping and returning and T outputs patched at random [test].
- No allocation in `process()` or `reset()` (counting `operator new`) [test]. Spring has the same check for `step()`.
- Spring's link counter against a model, and the 16 patch masks with bad indices [test].
- Aliasing: see 7.4.

### 7.4 Aliasing

[TESTING.md, aliasing row] Rule 16 requires a bound for modules that make or bend sound. For Neap and Spring TESTING.md records "Not needed": Neap makes control signals; cycle 1 tops out at 160 Hz (40 Hz plus two octaves of RATE CV), the others are slower; the sine is `fastSin2pi`; the Wave crossfade is 10 ms linear. A scratch measurement with `AliasMeter` put the triangle's corners at about −86 dB re the fundamental at 44.1 kHz (exactly nothing at 48 kHz, where 160 Hz divides the rate), and Spring's F corners at about −74 dB at 44.1 kHz at the fastest. No bound is pinned and no test runs it. I did not repeat this measurement. A QA patch that listens to a cycle at 160 Hz through an audio path (the spec notes that a 40 Hz triangle's and 80 Hz fold's corners are audible on a VCA) can judge it by ear.

---

## 8. Open issues

**No GitHub issue is filed against Neap or Spring** (`gh issue list --state all --limit 200` on 2026-11-24; the latest issue is #77). Issues that touch them:

| # | Title | Relevance |
|---|---|---|
| 36 | Process: libm-calls check is skipped on macOS | Neap's and Spring's no-libm-per-sample check ran on none of the build machine's runs; only Linux CI runs it |
| 67 | Aliasing: measure every module that makes or bends sound, and fix what fails | Neap and Spring are not in its list (TESTING.md says "Not needed") |
| 64 | Polyphony: modules should be polyphonic where it makes sense | Neap's CV and RESET inputs read channel 1 only; ALL is the only polyphonic jack |

### 8.1 Found while preparing this document (not filed)

These are things the code does that the spec did not say, or that QA may want a ruling on. I have not filed any. A reader who agrees should file them rather than reproduce them as new bugs.

1. **MARK −5 and +5 V are dead.** HIGH is high all the time at −5 V and never fires at +5 V; the working range is about −4.87 to +4.87 V (section 2.13). A listening question: should MARK's travel be narrowed, or TIDE's limit raised?
2. **TIDE peaks at 4.92 V**, not 5 V (section 2.12). Harmless; the spec says "within ±5 V".
3. **The 18.8 % against the 25 % aim**: gain 2.0 is the starting value and nobody has tuned it yet (section 2.12). Listening item.
4. **In 0–10 V Level MARK's display stays in ±5 V**; the TIDE jack's threshold reads 7 V at the default (section 2.13).
5. **Pairs beyond 16:1 can sit on whole ratios**: 4–8 is 0.03 % from 18:1 at the bottom of the range, 2–7 crosses exactly 36:1 (section 2.6). By the rule's own definition they are allowed.
6. **A Reset fires Spring triggers** on even cycles that were below 0 V and sets every STEPS code to 15 (section 6.5).
7. **The no-locking margin has 0.04 percentage points of slack at the bottom clamp** and about 2 % remains once Drift moves the cycles (section 2.6).
8. **No manual, README row, roadmap or changelog entry** for either module (section 1.1).

### 8.2 Known limits that are not defects

- Neap runs all the time: idle costs the same as busy (section 7.1).
- Spring outputs may lag Neap's by a sample in Rack (spec: acceptable) [unverified].
- A patch load starts every cycle at phase 0 and Drift from its seeds: two loads give the same pattern (spec).
- With Drift on, nothing repeats from a Reset; with Drift off, everything does (spec).

---

## 9. What the automated suite already covers

All eight test programs pass. Since the whole-branch review, `make test` builds the six non-sanitizer ones (`neap/test_rate`, `test_cycles`, `test_tide`, `test_engine`, `test_state`, `spring/test_core`) at the plugin's flags (`PLUGIN_MATH_TESTS` in `tests.mk`); Spring's `test_hostile` was also run once at the plugin's flags by hand and passes. The two hostile tests use ASan and UBSan at `-O1`. Run time on an Apple M-series Mac: `test_rate` 0.16 s, `test_cycles` 1.7 s, `test_tide` 0.5 s, `test_engine` 0.4 s, Neap `test_hostile` 3.4 s, Spring `test_core` 0.2 s, Spring `test_hostile` 1.7 s. Tests are named for what they guarantee; tolerances are as asserted.

### 9.1 neap/test_rate (tables, rate law, tooltip)

| Test | Guarantees | Tolerance |
|---|---|---|
| noon_periods | cycle 1 = 0.625 Hz; cycle 8 = 0.625/169.07 Hz; Drift scales | 1e-4 relative |
| top_and_bottom | RATE 1 = 40 Hz; RATE 0 = 40/4096 Hz | 1e-4 |
| cv_one_volt_per_octave | +1 V doubles, att −1 halves; clamp at 160 Hz and 40/16384 Hz; NaN, ±inf finite | 1e-6; exact |
| spread_zero_at_noon_and_for_cycle_1 | steps, L_i, spread zero at noon, cycle 1 never moves | 1e-5, 1e-6 |
| order_kept | f_i > f_{i+1} at 81 settings | exact |
| no_locking | rule at 81 settings across the clamped range; helper checks | ≥ 0.03 (worst 0.0304) |
| tooltip_text | the three pinned strings; NaN CV; buffer; unit roll-over; 4.8 h, 19.4 h | exact text |

### 9.2 neap/test_cycles (the bank)

| Test | Guarantees | Tolerance |
|---|---|---|
| triangle_shape | 0, 1, 0, −1 at the quarters; rising from 0 | exact |
| frequency_by_crossings | Hz from rising crossings at noon and four octaves up, at 44.1, 48, 96 kHz | 1e-4 |
| slowest_long_run | bottom clamp, 768 kHz, 50 million samples; still moving | 1e-6 |
| drift_bounded_and_moving | 30 min of Drift: ±0.5 %, range > 0.2 %; against an independent reference | 1e-6; 2e-6 |
| drift_off_is_exact_and_reset_keeps_drift | Drift off bit-exact after a Reset; Reset keeps drift | exact |
| wave_crossfade_no_jump | per-sample step ≤ slope + crossfade bound over many toggles; toggling every 2 ms in ±1 | 1e-5 |
| crossfade_takes_ten_ms | 480 steps blend linearly; ends at the pure sine and back | 2e-4; 1e-4 |
| reset_puts_all_at_zero_rising | phases 0; sine target at once; rising | 1e-5 |
| cache_follows_e1_and_nan_is_safe | base Hz cache; NaN e1 keeps the rate; absurd sample time finite | |
| bad_sample_time_is_harmless | NaN, inf, negative dt advance nothing and poison nothing | |
| drift_off_leaves_drift_state | off for N samples then on equals a fresh object | |
| nan_e1_keeps_last_rate | advance equals the previous step's | |

### 9.3 neap/test_tide (TIDE and HIGH)

| Test | Guarantees | Tolerance |
|---|---|---|
| limit_shape | limit(0) = 0, slope 1, monotone, odd, ≤ 1, continuous at 3, NaN → 0 | 1e-4, 1e-6 |
| bounded_24h | 24 simulated hours at noon, Weights −1 to +1: \|TIDE\| ≤ 5 V; peak > 1.5 V; prints the figures of section 2.12 | exact |
| weight_leads | correlation with cycle 1 (Weight −1) and cycle 8 (+1); equal weights at 0 | margin 0.3; 1e-4 V |
| weight_glide | 63 % at 50 ms; step < 0.01 V; settles exactly at 8/48/768 kHz; frozen on bad dt; NaN target keeps; NaN cycle finite | 2e-3; exact |
| high_hysteresis | one switch each way at mark ±0.05 V; ±0.04 V noise never chatters, from either state; NaN keeps | 1e-3 V; 0 flips |

### 9.4 neap/test_engine (the engine)

| Test | Guarantees | Tolerance |
|---|---|---|
| levels | `shaped` Level-free; cycle = shaped or shaped + 5; TIDE + 5; HIGH equal in both; light formula; HIGH vs mark + 5 | exact; 1e-6 |
| reset_edges | 1 V resets once; held high no repeat; no re-arm 0.1–1 V; re-arms below 0.1 V; noise never resets | 0.02 V; 0.1 V |
| no_jumps | RATE, CV, WEIGHT, WEIGHT CV, MARK and Wave moved sinusoidally for 12.5 s: no sample step beyond the bound | the bound of the test (cycles 5 V × (6.4 × 161 / fs + 0.5/(0.010 fs)); TIDE 2× + 1e-3) |
| hostile_inputs, nan_cv_reads_zero | NaN/inf/1e30 on every field; NaN CV equals the knob alone | |
| sample_rate_change | cycle 1 crossings per second at each rate; no jump at a switch | |
| weight_cv_adds_and_clamps | WEIGHT + CV equal to the equal knob alone, in lockstep; control differs | |
| wave_is_wired | Sine and Triangle differ from the same state and each matches its wave | |

### 9.5 neap/test_hostile (sanitizers, allocation, fuzz)

`bad_values_everywhere`, `recovers_after_nan`, `sample_rates`, `bad_sample_times`, `toggles`, `reset_hostile`, `no_allocation_in_process`, `seeded_fuzz` (section 7.3).

### 9.5b neap/test_state (saved menu state)

`parseMenu` (`NeapState.hpp`): good values round-trip; missing keys give the defaults (Drift on, Triangle, ±5 V); `drift` counts only when it is a JSON boolean (0, 1 or a string keep it on); `wave` and `level` count only as the integers 0 or 1 (2^32 + 1, booleans and others give the default).

### 9.6 spring/test_core and spring/test_hostile

| Test | Guarantees |
|---|---|
| fold | 2·\|x\| at twice the cycle's rate, 0..10 V |
| min_max | bit-exact lower and higher of each pair |
| trigger_once_per_rise | one 10 ms (±1 sample) pulse per rising crossing; none on a falling one; a crossing while high merges; a crossing landing exactly on 0 fires, staying at 0 does not; NaN |
| reset_no_burst | a negative value before a reset does not make the first value after it a crossing |
| cascade_all_masks | all 16 masks: each patched output = OR of its own and the unpatched ones before it; mask 0 nothing; only T6 patched takes all four |
| steps_codes_and_order | all 16 codes on each output, first named cycle the MSB, 15 = 10 V |
| sample_time | a bad sample time keeps the last (960 samples at 96 kHz) |
| run_reset_latch | `SpringRun`: a reset request (Initialize, bypass, un-bypass or unlink) clears the trigger history on the next call from Neap, so a crossing that happened while bypassed fires nothing; with no request the same crossing fires |
| link_timeout | unlinked at start; linked on the first change; linked through 63 idle calls; unlinked on the 64th; linked again at the next change; restart of the count; the module contract of reset on unlink |
| (hostile) bad_values_everywhere, nan_does_not_fire_or_stick, relink_has_no_burst, pulse_width_at_every_rate, bad_sample_time_keeps_the_last, bad_indices, link_against_model, no_allocation_in_step, chained_fuzz, seeded_fuzz | section 7.3 |

### 9.7 What else the build runs

`make bench --check` passes (15 module declarations complete). `make engine-globals` passed. I did not run `make libm-calls` (Linux only), `make smoke` (starts Rack) or the Windows build. `Neap.cpp` and `Spring.cpp` compile against the Rack SDK 2.6.6 on macOS arm64.

### 9.8 Not covered by the automated suite

- Anything in Rack itself: the panels, the menus (including **Add Spring expander**), tooltips (RATE's text, the Weight and MARK displays), lights (P1..P8, HIGH, LINK), save and load through Rack, bypass, polyphony as Rack delivers it, Rack's expander linking (`rightExpander`) and the threading order.
- No golden saved-state image: the `drift`, `wave` and `level` keys are not pinned by a stored file; the parsing of their values and its edge cases are tested (`test_state`).
- Windows and Linux builds; CPU on x86-64 or Windows; the libm check on this machine.
- Aliasing against a bound (TESTING.md: not needed).
- Listening: whether any pair locks by ear, whether Drift is worth having, Weight's feel, TIDE's gain, HIGH's rate at MARK +2 V, triangle against sine, and Spring's outputs on a scope and into a quantiser (the listening stop).
- Spring "in step" with Neap on the same sample in a multi-threaded Rack.
- The panel look (the `make smoke` screenshots show only the default panel).

---

## 10. Listening guide cross-check

`/Volumes/share1/VCVRACK/neap-2.0.39-listening.html`. Not a source; the code wins. I found **no disagreement** with the code:

- RATE RANGE: "cycle 1 from 1.7 min to 25 ms; cycle 8 from 4.8 h to a few seconds. Noon gives 1.6 s and 4.5 minutes": matches section 2.3 (cycle 8 at the top is 4.2 s).
- STEP TABLE: the seven steps, "about 169:1, against øchd's 150:1": matches.
- WEIGHT: "a factor of 2 per cycle": matches (weight 2^|w| per cycle).
- TIDE GAIN: "past ±2.5 V 18.8 % of the time against an aim of about 25 %": matches (section 2.12).
- HIGH AT +2 V: the question is open; measured 15.6 % duty and about 617 rises per hour at noon. "In 0–10 V, should MARK follow the offset?": in effect it does (HIGH fires at the same moments) but the knob's displayed value does not move (section 2.13).
- SPRING AND TIDE: "TIDE, HIGH, WEIGHT, MARK and Level never reach it": confirmed bit-exact (section 6.1).
- Patch 5: "With T4 and T6 unpatched their triggers fall through to T8": T8 receives 4, 6 and 8, as stated (section 6.4).
- Patch 4: "Spring S1–4 … struck by cycle 2's triggers": S14 is the code of cycles 1 to 4, T2 the crossings of cycle 2: matches.
- "Ticks … untested are the lights, tooltips, the menu item and LINK": matches section 9.8.

---

## 11. Suggested QA test areas and expected results

Defaults: 48 kHz, RATE noon, attenuverter 0, WEIGHT noon (0), MARK +2 V, Drift on, Wave Triangle, Level ±5 V, no Spring. Spring tests need a Spring directly right of Neap.

**Method notes.**
- Cycle 8 at noon takes 4.5 minutes. To see whole cycles quickly, set RATE to 0.75 (cycle 1 at 5 Hz, cycle 8 at 33.8 s) and use a scope on the poly ALL cable.
- To see TIDE statistics, patch TIDE to a scope with a long timebase, or record 10 minutes at RATE 0.75. The numbers of section 2.12 hold at any RATE.
- For exact frequency checks, turn Drift off (menu).
- To compare Levels, change Level with the cycles running and watch for a pure 5 V offset step and nothing else.

### 11.1 Controls and ports

| Area | Procedure | Pass criteria |
|---|---|---|
| RATE tooltip | hover at RATE 0, 0.25, 0.5, 0.75, 1 | the strings of section 2.3 ("1: 1.7 min · 8: 4.8 h" … "1: 25.0 ms · 8: 4.2 s") |
| RATE periods | Drift off, scope the outputs | cycle 1 period 1.6 s at noon and 25 ms at RATE 1; cycle 8 4.5 min and 4.2 s |
| RATE CV | attenuverter +1; 1 V and 2 V into RATE CV at noon | cycle 1 doubles per volt (period 1.6 s, 0.8 s, 0.4 s); the tooltip follows the CV |
| RATE CV negative | attenuverter +1, −3 V; attenuverter −1, +3 V | slows, never stops or reverses |
| RATE CV clamp | ±10 V at either attenuverter end | cycle 1 stops at 160 Hz and at 2.44 mHz (409.6 s); tooltip "1: 6.3 ms · 8: 1.1 s" and "1: 6.8 min · 8: 19.3 h" |
| NaN or huge RATE CV | ±100 V, a source that can produce NaN | no crash, a finite rate |
| Weight | slow poly scope with TIDE, Weight −1, 0, +1 | TIDE follows cycle 1, an equal sum, cycle 8; TIDE stays inside ±4.92 V |
| Weight CV | +5 V into WEIGHT CV with the knob at noon | the same TIDE as the knob fully right; −5 V as fully left; +5 V with the knob at −1 reaches noon only |
| Weight glide | step WEIGHT CV 0 → 5 V | TIDE does not step; it settles in about 0.25 s (50 ms time constant) |
| MARK | MARK −5 V, 0 V, +2 V, +5 V; HIGH on a scope or meter | HIGH high all the time at −5 V, high about half the time at 0 V, 15.6 % of the time at +2 V (noon, Drift off), never at +5 V (section 2.13); the HIGH light follows |
| HIGH hysteresis | slow ramp through MARK (from a slow LFO into a comparison, or use TIDE) | one edge each way, no chatter |
| RESET | a gate or clock on RESET at several RATEs | every rising edge (above 1 V) restarts all eight cycles at 0 V rising; a held-high gate does not repeat; below 0.1 V re-arms |
| ALL | poly scope or a poly-to-mono splitter | 8 channels equal to C1..C8 |
| Lights | RATE 0.75 | P1..P8 brighten and dim with their cycles (dark at the bottom of the cycle, full at the top); HIGH tile light on while HIGH is 10 V |
| Output ranges | each output on a meter, ±5 V Level | ±5 V triangle, 0 V rising from a reset; sine ±5 V |

### 11.2 Spread, locking and Drift

| Area | Procedure | Pass criteria |
|---|---|---|
| No locking by ear | RATE 0.75, poly scope, 10 minutes | no pair settles into a fixed pattern (the listening stop's question) |
| No locking on a scope | XY scope of cycle i against cycle j for pairs in section 2.6 | no stable Lissajous figure. Pairs 4–8, 1–6 and 2–7 come within 0.03–0.06 % of whole ratios at some RATE: note any lock you see on those |
| Spread | measure the cycle 2 : cycle 3 frequency ratio at RATE 0 and 1 | between 1.927 and 1.933 across the range (the step is 1.93; section 2.6) |
| Order | RATE sweep | cycle i is always faster than cycle i+1 |
| Drift on/off | Drift off, run 10 minutes | exactly repeatable after a RESET; Drift on: the ratios wander by up to ±1 % and nothing repeats |
| Drift is deterministic | two Neaps with Drift on, loaded together | identical drift (section 2.8) |
| Drift step | toggle Drift during play | no step in any cycle (the rate changes smoothly over 60–123 s per segment) |

### 11.3 Wave and Level

| Area | Procedure | Pass criteria |
|---|---|---|
| Wave switch | Triangle ↔ Sine at RATE 1 during play | no step beyond the crossfade (10 ms); no click on a VCA; repeated toggles stay inside ±5 V |
| Wave at rest | Sine | max difference from the triangle 1.05 V at the same phase |
| Level | ±5 V ↔ 0–10 V on all outputs | C1..C8, ALL and TIDE shift by exactly +5 V; HIGH unchanged (0 or 10 V); lights unchanged |
| MARK in 0–10 V | MARK at +2 V in 0–10 V Level; TIDE jack on a meter | HIGH rises at TIDE ≈ 7.05 V and falls at ≈ 6.95 V; the knob still reads 2 V |
| Level and Spring | switch Level while Spring is outputting | Spring's outputs do not change (it reads ±5 V values) |

### 11.4 TIDE and HIGH statistics

| Area | Procedure | Pass criteria |
|---|---|---|
| TIDE beyond ±2.5 V | record TIDE for a long time at noon Weight (a long run at RATE 0.75 shortens it) | about 18.8 % of the time beyond ±2.5 V (±0.5 % after two hours or more) [meas 18.7–18.8 %] |
| TIDE peak | same | never above 4.92 V in magnitude; no flat top at ±5 V |
| TIDE at Weight ±1 | same | about 46 % beyond ±2.5 V; follows the favoured cycle |
| HIGH duty at +2 V | same | about 15.5 % at Weight noon (29 % at ±1); about 617 rises per hour at RATE noon; mean high about 0.9 s |
| HIGH when Weight moves | WEIGHT knob while HIGH is on a scope | no chatter |

### 11.5 Spring

| Area | Procedure | Pass criteria |
|---|---|---|
| Add Spring expander | right-click Neap → "Add Spring expander", with and without a module on Neap's right | a Spring appears directly to the right, the neighbours are pushed along; LINK lights; one undo removes it; the item is greyed out while a Spring is there |
| LINK | Spring without Neap at its left; with Neap; with Neap bypassed; Neap deleted | LINK dark and all outputs 0 V without Neap; lit with Neap; dark within 64 samples of Neap stopping |
| Re-link | bypass Neap and re-enable it, T outputs patched | no burst of triggers on re-link |
| FOLD | F1 on a scope at RATE 0.75 | positive 0 to 10 V, twice cycle 1's rate, a sharp dip to 0 at each crossing |
| MIN/MAX | MIN 2-3 and MAX 2-3 against C2 and C3 on a scope | MIN is always the lower, MAX the higher |
| TRIG | T2, T4, T6, T8 on a scope or counter | one 10 V pulse of 10 ms per rising zero crossing of the cycle: about 2,112 / 485 / 130 / 26 per 10 minutes at RATE 0.8 |
| Trigger fall-through | patch only T2 and T8 (guide patch 5) | T8 pulses on cycles 4, 6 and 8 together; T2 only on cycle 2; check the table of section 6.4 for any other combination |
| STEPS | S1-4 into a quantiser, then S ODD, S EVEN | 16 levels, 0 to 10 V in 0.667 V steps; the code changes when any of its four cycles crosses 0 V |
| Wave and Spring | Wave Sine | FOLD, MIN/MAX follow the sine; trigger times unchanged |
| Spring and TIDE | move WEIGHT and MARK | no effect on any Spring output |
| Reset and Spring | RESET during play with T2 or T4 patched | pulses can appear on triggers whose cycle was below 0 V (section 6.5); STEPS go to 10 V for one sample |
| Initialize on Spring | right-click → Initialize | no burst; the triggers restart cleanly |

### 11.6 Saved patches

| Procedure | Pass criteria |
|---|---|
| save a patch with each Drift, Wave and Level setting; reopen | settings restored; JSON `data` holds `drift` (boolean), `wave` (0 or 1), `level` (0 or 1) |
| reopen a patch with Neap and Spring | the link is there; the cycles start at phase 0 and Drift from its seeds |
| hand-edit JSON: missing keys, `"wave": 2`, `"wave": 1.0`, `"drift": 0`, `"level": "x"` | loads without a crash; missing keys give the defaults; `wave`/`level` other than integer 1 give Triangle/±5 V; `drift` is read only if it is a boolean, so `0` leaves it on |
| Initialize on Neap | Drift on, Wave Triangle, Level ±5 V, knobs to defaults, cycles restart at 0 |
| duplicate the module | copy carries all settings; both drift identically (seeds are the same) |
| module preset (.vcvm) | knobs and menu restored. There are no factory presets |

### 11.7 Sample rates and robustness

| Procedure | Pass criteria |
|---|---|
| 44.1, 48, 88.2, 96, 192, 384, 768 kHz with Drift off | cycle 1 keeps its period in seconds; no crash |
| change the sample rate during play | no jump in the cycles, TIDE or Spring; triggers stay 10 ms |
| NaN, ±inf and 1e30 V on RATE CV, WEIGHT CV and RESET | outputs finite and in range |
| RESET with audio-rate noise or a very fast clock | one reset per rising edge above 1 V; nothing out of range |
| 16 Neaps with Springs | per-instance CPU about the single-instance figure (section 7.1) |

### 11.8 CPU

| Procedure | Pass criteria |
|---|---|
| one Neap idle, 48 kHz | Mac bench 0.2 % of a core (budget 0.4); constant whether or not anything is patched |
| one Neap busy (CVs moving, RESET pulsing) | Mac bench about 0.37 % (budget 0.7) |
| Neap with a linked Spring | Mac bench about 0.40 % (budget 0.75) |
| Windows | no automated budget; record the Rack meter for one idle and one busy Neap, and Neap with Spring, at 48 kHz |

### 11.9 Not for QA to file

The unruled design choices listed under "Status" at the top; the dead MARK ends, TIDE's 4.92 V peak, the near-whole ratios beyond 16:1 and the Reset triggers (section 8.1) are known and described, awaiting the user's rulings.

---

## 12. Glossary

| Term | Meaning |
|---|---|
| ALL | Neap's polyphonic output carrying the eight cycles as channels 1 to 8 |
| Attenuverter | a −1..+1 scale on a CV input; negative inverts |
| Cascade | Spring's rule that an unpatched T output passes its triggers to the next patched one in the order 2 → 4 → 6 → 8 → 2 |
| Crossfade | Neap's 10 ms linear blend between the triangle and the sine when Wave changes |
| Cycle | one of Neap's eight slow triangle or sine waves; cycle 1 is the fastest |
| Drift | a slow random wander of each cycle's rate, at most ±0.5 %, seeded and repeatable |
| e1 | the base-2 logarithm of cycle 1's frequency in Hz; the rate law works in octaves |
| Expander | a module placed against another's side that the host can read; Spring sits at Neap's right |
| FOLD | Spring's output 2·\|x\|, a full-wave rectified cycle at twice its rate |
| HIGH | Neap's gate, 10 V while TIDE is above MARK (0.05 V hysteresis each side) |
| Level | Neap's output range: ±5 V or 0–10 V (adds 5 V to the cycles and TIDE; HIGH and Spring unaffected) |
| LINK | Spring's light, lit while it is receiving Neap's cycles |
| MARK | the HIGH threshold, −5 to +5 V, default +2 V |
| Neap | the small tide; also this module |
| No-locking rule | every pair of cycles with a ratio of 16 or less stays at least 3 % from every whole (and below 6, every half) ratio |
| Phase | a cycle's position in its cycle, 0 to 1; kept as a double |
| Pulley | one of the eight lights on Neap, showing its cycle's level |
| Rising zero crossing | the sample where a cycle goes from below 0 V to 0 V or above; Spring's triggers fire on it |
| Shaped | a cycle's ±5 V value after Wave and its crossfade, before Level; what Spring reads |
| Spread | each cycle's own small slope on its tracking of RATE, so the ratios between cycles drift slightly across the knob |
| Spring | the expander (and the big tide) |
| Steps | Spring's four 4-bit codes, each from four cycles being above or below 0 V, out as 0 to 10 V in 16 levels |
| TIDE | Neap's weighted sum of the eight cycles, soft-limited, peak ±4.92 V |
| Weight | the knob (and CV) that tilts TIDE's sum toward the fast or the slow cycles by a factor 2 per cycle |

---

### Note on the measurement programs

All in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/neap/meas/`, built with `build.sh` (the plugin's flags, `-I` the extracted tree's `src`) against the tree extracted from `2e6e960` at `.../techref/neap/tree`. None touches the repository.

| Program | What it measures |
|---|---|
| `m_lock.cpp` | the no-locking margin over the whole clamped range (160,001 points), per pair; the noon margin; the 81-point grid |
| `m_lock2.cpp` | pairs just past 16:1; the margin with Drift's extremes |
| `m_tide.cpp` | 24 h, 168 h and other runs of `Cycles` + `Tide`: beyond ±2.5 V, peaks, RMS, HIGH duty by MARK and Weight, rises per hour |
| `m_level.cpp` | HIGH in both Levels (3 h); Spring blind to Weight, Mark, TIDE and Level (10 min); output ranges |
| `m_spring.cpp` | link timeout; trigger width at six rates; the cascade table; trigger counts; step codes and volts; fold range |
| `m_rate.cpp` | periods and tooltips; Reset behaviour and its trigger side effect; repeatability from a Reset with Drift off and on; Drift extent over 2 h; `fastSin2pi` error |

The tests and bench were run from the same tree (`make -f tests.mk …`, `build/bench/bench neap <case> <1|16> 5`); their logs are in the `neap/` folder next to the tree. They drive the Rack-free engines as `Neap.cpp` and `Spring.cpp` do. They do not exercise Rack's ports, threading, GUI, menus or tooltips; anything marked [meas] should match Rack at 48 kHz, but Rack-side effects (module order, expander linking, bypass, preset loading) are outside them.
