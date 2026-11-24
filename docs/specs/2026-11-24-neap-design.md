# Neap and Spring: eight slow cycles and their expander: design

Status: design, 2026-11-24. Panels: `2026-11-24-neap-mockup.png` and
`2026-11-24-spring-mockup.png` (drawn by draft entries for
`tools/gen_panels.py`; both pass its checks). Grows from the idea in
`docs/future/neap.md` (four summed cycles with a height knob each); keeps
its Kelvin tide machine and its sum and High-water gate, and takes its
eight outputs from Instruō øchd. Spring, the expander, is new.

## Intent

The user wants "an 8 output Neap, each CV output is tuned/cycles
differently. sort of like Instruo Ochd", and an expander after øchd's
(ø4^2). Neap is roadmap item 12 (S effort): very slow, layered modulation
for pieces that change over many minutes.

Asked how much of the tide-machine idea should survive, the user chose
**B**: eight cycles, plus the tide (their sum), a High-water gate and one
Weight knob, rather than plain øchd (A) or a height knob per cycle (C).

The instrument stays Kelvin's tide-predicting machine: a row of pulleys
turning at their own rates, one wire running over them, and the wire's
free end drawing the tide. Neap's eight cycles are the pulleys; TIDE is
the wire. A spring tide is the opposite of a neap tide: the big one,
when sun and moon pull together. Spring, the expander, takes the same
eight cycles to their extremes: folded, compared, counted and stepped.

### What øchd does (measured 2026-11-23 and 24)

The VCV port of øchd (Instruo 2.2.0, proprietary: measured, nothing
copied), recorded through VCV Recorder at Rate max (57 s) and noon
(230 s):

| Output | Max | Noon | Step (max) | Step (noon) |
|---|---|---|---|---|
| 1 | 154.8 Hz | 4.77 Hz | | |
| 2 | 84.8 Hz | 2.63 Hz | 1.83 | 1.81 |
| 3 | 42.3 Hz | 1.33 Hz | 2.01 | 1.98 |
| 4 | 22.0 Hz | 0.70 Hz | 1.92 | 1.90 |
| 5 | 10.4 Hz | 0.33 Hz | 2.12 | 2.15 |
| 6 | 4.70 Hz | 0.15 Hz | 2.21 | 2.11 |
| 7 | 2.12 Hz | 0.068 Hz | 2.22 | 2.29 |
| 8 | 1.01 Hz | 0.032 Hz | 2.09 | 2.14 |

- Each output is roughly an octave below the last, never exactly; the
  span is about 150:1.
- The steps shift by up to 5 % across the Rate knob: from noon to max
  each output speeds up by 30.4 to 32.5 times, so each channel tracks a
  little differently, like analogue exponential converters.
- No drift: each rate holds to better than 0.001 % over the recording.
- Triangles, about ±5.5 V.

What makes øchd good is the uneven spacing: no output locks to another,
so the eight never fall into a repeating pattern. Neap keeps that idea
with its own numbers.

## Goals, non-goals, anti-goals

Goals:
- Eight slow cycles about an octave apart with uneven steps, from one
  Rate; a pattern that takes a very long time to repeat.
- Slow enough for pieces that change over minutes to hours, fast enough
  for a visible wobble at the top of the knob.
- TIDE: one line that is the weighted sum of all eight, with one Weight
  knob setting whether the fast or the slow cycles lead it.
- HIGH: a gate while the tide is above a mark the user sets.
- Spring: sixteen derived outputs from the same eight cycles, as ø4^2
  gives øchd, for gates, steps and rectified shapes.

Non-goals:
- Audio rate. Neap is for slow modulation; Sway has an Audio range.
- A height (or phase) knob per cycle (option C).
- Per-cycle rate knobs, clock sync, polyphonic voices (more than one
  Neap per cable).
- Wave shapes beyond triangle and sine; Sway is the shaping LFO.

Anti-goals (a failure even if it works):
- Any two outputs that audibly lock into a repeating pattern at some
  Rate setting.
- A jump on any output when Rate, Weight, Rate CV or the wave setting
  changes (Reset is the only jump).
- TIDE that sits near the middle all the time, so HIGH almost never
  fires, or that clips flat at the rails.
- Spring outputs out of step with Neap's own outputs.

## What the user decided

- Eight outputs, each tuned and cycling differently, "sort of like
  Instruo Ochd" (2026-11-23).
- An expander after øchd's (2026-11-23).
- Option B: the eight cycles plus TIDE, HIGH and one Weight knob
  (2026-11-24).

Everything below that is not in this list is the designer's call,
marked *(call)*, for the user to correct.

## Approaches

- **A. Straight øchd (about 6 HP):** eight outputs, Rate, CV, Reset. The
  smallest, but it is øchd again and drops Neap's identity.
- **B. Eight cycles plus the tide (about 8 HP):** A, plus TIDE, HIGH and
  Weight. One knob and two jacks more than A, and it keeps the tide
  machine. **Chosen.**
- **C. A height knob per cycle:** the original sketch, at eight. Eight
  more knobs; no longer small. Dropped.

For the derived outputs: put them on the expander (chosen, at the user's
request) rather than growing Neap, so Neap stays small and Spring is
bought only by those who want it.

## Neap

### Panel (8 HP)

As the mockup (`2026-11-24-neap-mockup.png`, seen by the user
2026-11-24), top to bottom:

- **RATE**, a dial on an ivory face numbered 0 to 12 (a long tick per
  two octaves of its range), with its attenuverter and **RATE CV** jack
  to its right, joined by a trace.
- The wire: a brass rope from the left edge zigzagging over eight pulley
  lights, cycle 1 on the left; each light shows its cycle's level.
- **WEIGHT** (left) with its **WEIGHT CV** jack beside it, and **MARK**
  (right).
- Outputs on tiles in three columns: **1 2 3 / 4 5 6 / 7 8 ALL**, then
  **RESET** (bare input, bottom left), **TIDE** and **HIGH** (with its
  light on the tile).
- The draft spec passes `tools/gen_panels.py`'s checks at 8 HP; the
  builder starts from it.

### The cycles

- **Frequencies:** cycle 1 runs at the Rate's frequency; each next one
  is slower by a fixed step. Proposed steps *(call; the builder may
  reorder or retune them within the tests):*

  | Step | 1→2 | 2→3 | 3→4 | 4→5 | 5→6 | 6→7 | 7→8 |
  |---|---|---|---|---|---|---|---|
  | Ratio | 2.16 | 1.93 | 2.25 | 1.93 | 1.93 | 2.23 | 2.17 |

  Cycle 8 is 169 times slower than cycle 1.
- **No locking, the rule** *(call):* every pair of cycles whose ratio
  is 16 or less stays at least 3 % away from any whole-number ratio,
  and pairs at 6 or less also stay 3 % away from any half (2.5, 3.5,
  ...), at every Rate setting including CV's extra octaves. Pairs
  further apart than 16:1 are too far apart to be heard locking. The
  table above passes with a 3.3 % margin at noon (found by search
  2026-11-24). An earlier table that only kept neighbours clear of 2, 4
  and 8 put cycles 1 and 5 within 1 % of 24:1; this rule catches that.
- **Tracking spread** *(call, from øchd, limited by the rule):* each
  cycle's speed follows the Rate with its own slightly different slope (a
  fixed table, not random), so the outputs line up a little differently
  at each knob position. Cycle 1 has zero slope and is the reference:
  RATE's periods and RATE CV's 1 V/oct are exact for it. Slopes for
  cycles 2 to 8 are kept small enough to pass the rule across the whole
  range, which the search found to be about 0.15 % per octave or less
  (about 1 % across the knob, much less than øchd's 5 %). A bigger
  spread would sweep pairs through near-locks as the knob turns. The
  listening stop judges whether so small a spread is worth keeping.
- **Drift** *(call; øchd's port has none):* a slow random wander on each
  cycle's rate, at most ±0.5 %, changing over minutes. Menu: **Drift**
  on (default) or off. With Drift off and the same Rate, Neap is fully
  repeatable from a Reset.
- **Wave** *(call):* triangle (default, as øchd) or sine, a menu item
  for all eight. Changing it mid-cycle keeps each cycle's phase; the
  output may bend but does not jump by more than the two shapes differ
  at that phase. The builder keeps the switch click-free by crossfading
  over about 10 ms.
- **Level:** ±5 V. Menu: **±5 V** (default) or **0–10 V**, for all
  outputs including TIDE *(call)*.

### Rate

- **RATE** spans 12 octaves *(call)*. At the top, cycle 1 runs at 40 Hz
  and cycle 8 every 4.2 s; at noon (default), cycle 1 every 1.6 s and
  cycle 8 every 4.5 minutes; at the bottom, cycle 1 every 100 s and
  cycle 8 every 4.8 hours.
- The tooltip shows cycle 1's and cycle 8's actual periods at that
  setting, spread included ("1: 1.6 s · 8: 4.5 min").
- **RATE CV:** 1 V/oct through its attenuverter (full right is +1 V/oct,
  full left −1 V/oct), added to the knob, clamped to the knob's range
  plus two octaves at each end. Rate never stalls or runs backwards
  (øchd stalls on negative CV; Neap does not) *(call)*.
- **RESET:** a rising edge (above 1 V, below 0.1 V to re-arm) puts all
  eight cycles at the start of their cycle, 0 V rising. After a Reset
  the cycles start together (a spring tide), then part.

### TIDE and Weight

- **TIDE** is the weighted sum of the eight cycles. Cycle i's weight is
  2^(w·(i − 4.5)), with w from −1 (Weight full left: cycle 1, the
  fastest, leads) to +1 (full right: cycle 8, the slowest, leads); noon
  weighs them equally.
- Scaling *(call):* the sum is divided by the sum of the weights times
  a gain, then soft-limited so it stays within ±5 V and never clips flat.
  The builder tunes the gain by ear so that at noon Weight the tide
  spends a fair share of its time beyond ±2.5 V (a target, not a test:
  roughly a quarter, measured over a long run).
- **WEIGHT CV:** ±5 V sweeps the knob's full range, added to the knob.
  Weight changes glide over about 50 ms so TIDE does not jump.

### HIGH and Mark

- **HIGH** is 10 V while TIDE is above **MARK**, else 0 V, with 0.1 V of
  hysteresis.
- **MARK** runs −5 V to +5 V; default +2 V *(call)*. In 0–10 V mode the
  mark follows (0 to 10 V).

### ALL

- **ALL** is a polyphonic output carrying the eight cycles as channels 1
  to 8, for poly modules (a VCA bank, Pan's CV) *(call)*.

### Lights

- Each pulley's light shows its cycle by brightness: dark at the bottom
  of the cycle, full at the top (RKA's jewel has one colour, and the look
  allows no other) *(call)*. The HIGH tile's light is lit while
  HIGH is high.

## Spring (the expander)

### Placement and link

- Spring sits **directly to Neap's right** and reads Neap's eight cycles
  each sample. It follows the Ballast/Pan expander precedent
  (`src/ballast/BallastExpander.hpp`, `rightExpander`), but as a single
  module, not a chain: one Spring per Neap.
- Spring works on Neap's **shaped** cycles (after Wave and its
  crossfade, so FOLD and MIN/MAX follow the sine when it is chosen) but
  **before** the Level setting (always ±5 V internally), then states its
  own levels below *(call)*. TRIGGERS fire on the shaped signal's rising
  zero crossing. For both shapes that is the same moment in the cycle,
  so the crossfade cannot add or drop a crossing.
- The link follows Ballast: Neap finds Spring on its right and writes its
  eight values into it from Neap's own process, so a bypassed or absent
  Neap simply stops writing and Spring sees itself unlinked *(call)*.
- A one-sample delay between the two is acceptable.
- Without Neap on its left (or with Neap bypassed), every output is 0 V
  and a small LINK light is dark; linked, it is lit.

### Outputs (16) *(call: øchd's expander's set, in Neap's terms)*

- **FOLD 1, 3, 5, 7:** full-wave rectified cycles 1, 3, 5 and 7
  (2·|x|, 0 to 10 V, RKA's unipolar standard), each at twice its
  cycle's rate.
- **MIN and MAX of 2 & 3, MIN and MAX of 6 & 7:** the lower and the
  higher of each pair, ±5 V.
- **TRIGGERS 2, 4, 6, 8:** a 10 ms, 10 V trigger each time its cycle
  crosses 0 V rising. Cascading: an unpatched trigger output passes its
  triggers on to the next patched one in the order 2 → 4 → 6 → 8 → 2,
  merged.
- **STEPS 1–4, 5–8, ODD, EVEN:** a 4-bit count from whether each of
  four cycles is above 0 V (first named cycle is the most significant
  bit), out as 16 even steps from 0 V (code 0) to 10 V (code 15), so
  2/3 V apart *(call)*. They don't fall on semitones; a quantiser
  after them picks the notes. The listening stop checks this. ODD is cycles 1, 3, 5, 7;
  EVEN is 2, 4, 6, 8.

### Panel (6 HP)

The user chose a staggered layout after øchd's expander (2026-11-24),
over rows of four at 8 HP and 2×2 groups at 10 HP; and no brass wire.

- Two columns of outputs (x 16.2 and 25.4 mm), each jack half a step
  (5.7 mm) below the one before, alternating left and right, so each
  group of four zigzags: FOLD 1, 3, 5, 7; MIN 2-3, MAX 2-3, MIN 6-7,
  MAX 6-7; TRIG 2, 4, 6, 8; STEPS 1-4, 5-8, ODD, EVEN. A 1.8 mm gap
  between groups.
- Each group's name (FOLD, MIN/MAX, TRIG, STEPS) in the left margin,
  level with its group.
- Tiles 1 mm shorter than the default (top 6.5, bottom 4.5 mm), so the
  sixteen fit; the MIN/MAX tiles' names in smaller lettering.
- LINK light top right, under the screw; subtitle EXPANDER; the short
  foot mark "R K A" (as the other 6 HP panel), which a bottom-left
  screw needs.
- The mockup's draft spec is the starting point; the builder may adjust
  within the checks.

## Code and files

As every module (CLAUDE.md, TESTING.md "Adding a module"):

- `src/neap/`: Rack-free headers: the eight-cycle bank (phases, the step
  and spread tables, drift); the rate mapping (knob and CV to Hz, the
  tooltip text); the tide (weights, scaling, limiter, Mark gate); and
  Spring's derived outputs (fold, min/max, triggers with cascading,
  steps). `Neap.cpp` and `Spring.cpp` map params, ports and lights onto
  them.
- Reuse where it fits: `src/sway/Phasor.hpp`, `Rate.hpp`, `RandomGen.hpp`
  for phase, rate clamping and the drift's random source, rather than new
  copies.
- `src/FastMath.hpp` for anything per sample; no libm per sample (sine
  from a table or FastMath; drift's random from the existing generator,
  updated at a control rate).
- Saved state: Drift, Wave and Level menu settings through `dataToJson`;
  the cycles' phases are not saved (a reloaded patch starts from Reset)
  *(call)*.
- Bypass: Rack's default; Spring sees a bypassed Neap as unlinked.
- `src/neap/bench.inc` (Neap alone; Neap with Spring), panel entries
  for both, plugin.json (slugs `Neap` and `Spring`), manuals
  `docs/neap/README.md` (Spring in the same manual), README table,
  CHANGELOG, roadmap, and `docs/future/neap.md` marked built.

## Testing

TESTING.md applies in full. Particular to Neap and Spring:

- **Frequencies.** With Drift off, at 44.1, 48 and 96 kHz: cycle 1 gives
  the stated periods at the top, noon and bottom of RATE within 0.01 %;
  every cycle matches the step and spread tables within 0.01 % at those
  settings.
- **No locking.** For the shipped tables (steps and spread), at 81 Rate
  settings from two octaves below the knob's bottom to two above its
  top, every pair meets the rule in The cycles.
- **Spread.** Zero for cycle 1 and at noon for all; each cycle follows
  its stated slope; it never changes the order (cycle i is always faster
  than cycle i+1).
- **Drift** stays within ±0.5 % and moves; off gives a bit-identical run
  from a Reset.
- **Rate CV** is 1 V/oct for cycle 1 at full attenuverter, reversed at full left,
  and clamped as stated; huge, NaN and inf CV give a finite rate.
- **Reset** puts every cycle at 0 V rising on the next sample; a held
  gate does not keep resetting; re-arm at 0.1 V.
- **No jumps.** Moving Rate, Weight, either CV and the Wave setting
  mid-run: no sample-to-sample step beyond the signal's own slope (the
  Wave crossfade's bound for that case).
- **TIDE** stays within ±5 V for 24 simulated hours at every Weight;
  Weight full left and right follow cycle 1 and cycle 8 most closely
  (correlation); noon weighs equally.
- **HIGH** is high exactly while TIDE is above MARK, with hysteresis; no
  chatter on a slow crossing.
- **ALL** carries eight channels equal to outputs 1–8 to the bit.
- **Spring:** FOLD is 2·|x| at twice the rate; MIN/MAX are the pair's lower
  and higher to the bit; each trigger fires once per rising crossing,
  10 ms wide; cascading for every patched/unpatched combination; STEPS
  give all 16 codes with the stated bit order; unlinked gives 0 V and
  the LINK light dark; a bypassed Neap counts as unlinked.
- Sanitizers, no allocation in process, extreme and changing sample
  rates, seeded fuzz, bench budgets, no libm per sample, Linux and
  Windows builds.
- **Aliasing** (TESTING.md rule 16): at the top of RATE cycle 1 is 40 Hz
  and FOLD 80 Hz, so the triangle and fold corners are audible on a VCA.
  The builder measures against a bound or records in TESTING.md's table
  why none is needed.

## Listening stop

Before these are fixed, the user tries a numbered test build with
everything in place: the RATE range and its default; the step table (does
it feel alive, does any pair lock audibly); the tracking spread and
Drift (are they worth having); Weight's curve; the TIDE's gain and how
often HIGH fires at the default Mark; triangle against sine; and Spring's
outputs on a scope and into a quantiser.

## Left to the builder

- The phase representation and how often Rate and CVs are read.
- The spread table's exact values within the no-locking rule; drift's
  speed. The step table may be re-searched if the spread needs it, as
  long as it passes the rule.
- The limiter's curve and TIDE's gain, by ear within the tests.
- Final panel positions and labels (from the mockups), the pulley
  lights' colours.
