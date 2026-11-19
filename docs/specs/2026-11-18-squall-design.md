# Squall: design

Date: 2026-11-18. Status: approved by the user. A builder
check raised four questions, each settled as a designer's call: how
Memory and Gust share timing, the history across clock changes, Freeze
on Swell, and saving Step and Swell.

Squall is a random source of gates and CV, 8 HP, in the Instrument look.
It is drawn as an aneroid barometer. Squall is the tenth `rka` module.
The user moved it ahead of Plimsoll and Lull on the roadmap
(`docs/future/ROADMAP.md`, row 21): Plimsoll is skipped for now, since
Ballast's soft limit and DC blocker already keep the output safe, and Lull, a
standalone slew, becomes a low priority.

![mockup](2026-11-18-squall-mockup.svg)

## Intent

The user makes dark, space and horror ambient in VCV Rack. Squall is the
weather of a piece. It makes **events and drifting CV in equal measure**
(the user's words: "both equally"):

- **Events** are gates to strike voices: Ping and Buoy later, and today
  Hadal, Fog or an envelope into Sluice.
- **Drifting CV** gives slow movement to Fathom, Fog, Pan and Sluice.

One dial, **Weather**, sets how much is happening, from Calm to Stormy.
Events and changes come in **gusts** with calm stretches between them,
the way wind does, rather than at an even random rate. The weather can
**remember**: a pattern can settle in, repeat, and wear away.

What exists already, and why Squall is still needed:
- **Sway's RAND output** is a new value each LFO cycle, stepped or
  gliding.
- **Shoal** has random apps.

Neither has gusts, memory, or gates whose length follows the weather,
and neither is built for minutes between events.

The ideas came from a survey of other makers' random modules, which the
user reviewed and agreed to:
- Weather sets density.
- Gust clusters events (as Marbles' "Clusters" does).
- Memory replays the recent past (as Marbles' Déjà Vu does).
- Swell drifts organically.
- Step and Swell move together.

## Goals, non-goals, anti-goals

Goals:

- Gates, a stepped CV and a smooth CV, all telling one story.
- A huge range of pace, from about one event every 10 minutes to about
  8 a second, on one dial.
- Bursts and lulls (Gust) that never change the overall amount Weather
  sets.
- Recurring patterns on demand (Memory), from never to an exact loop.
- Free-running, or locked to a clock.
- CV, with an attenuverter, on Weather, Gust and Memory.
- All of `TESTING.md` from the start.

Non-goals:

- **A Noise output.** Hadal makes noise. The sketch's Noise output is
  dropped (the user agreed).
- **Polyphony.** Squall is one weather system, and two Squalls make two.
  A polyphonic CV uses its first channel.
- **A Reset input.** The user decided against it. Squall has no step 1
  to return to.
- **Seeds or recallable sequences,** to make the same weather on demand.
  Memory covers recurring patterns.
- **Scales, quantising or pitch.** Shoal (and Bearing, later) do those.
- **Chaos systems** (Lorenz and others).
- **A Slew knob.** Swell is always smooth, and Weather sets its pace.
- **Lull's job.** A standalone slew stays a separate, low-priority idea.

Anti-goals, which would make Squall a failure even if it works:

- **A pulse you can hear.** At Gust 0 events are memoryless random, not
  a jittered clock. At any Gust the timing must never settle into a
  steady beat, unless Memory is locking a loop on purpose.
- **Gust that changes "how much".** Turning Gust must change the rhythm
  of events, not the average number of them over a long stretch.
- **Swell that sounds like an LFO.** Swell must have no regular period,
  no overshoot past its range, and no corners: its slope is continuous.
- **A dead module.** At Calm, Squall can be silent for many minutes.
  The event light and the manual must make clear that it is working,
  not broken.
- **Clicks where Squall drives a level.** Step jumps by design. Swell
  must never jump, including when the range menu, Weather, Freeze or a
  reload changes things.

## Approaches

These were settled with the user one decision at a time.

**What Squall is for.** Events, drifting CV, or both. The user chose
**both equally**.

**With a clock patched.**
- **A. Events land only on clock ticks,** and Weather is the chance of
  an event on each tick.
- **B. The clock sets only the overall pace,** and events still fall
  between ticks.

The user chose **A**. With no clock patched, Squall runs free.

**How long a gate lasts.**
- A fixed 10 ms trigger.
- A length that follows the weather.
- Both, as separate outputs.

The user chose **a length that follows the weather**: about a quarter
of the average gap between events, held to 5 ms to 4 s. A trigger input
only reads the gate's start, so the gate still strikes things.

**How Gust clusters events.**
- **A. Self-exciting.** Each event briefly raises the chance of another,
  so bursts grow and die away by themselves. This is the aftershock
  model (a Hawkes process).
- **B. Two moods.** Squall switches between a calm rate and a gusty
  rate, which gives bursts with hard edges.
- A third option, a noise-modulated rate, was dropped. It sounds like an
  LFO on Weather CV, which the user can already patch.

The user chose **A**.

**CV.** At first the design had CV on Weather and Memory. The user then
**added CV and an attenuverter to Gust**, and Memory got an
attenuverter too (*designer's call*), so all three columns work alike.

**Output ranges.** The user asked for **±5 V, 0 to 10 V, ±2 V and
±1 V**, in a right-click menu.

**Subtitle.** The user rejected "RANDOM WEATHER" as not descriptive
enough and chose **RANDOM GATES & CV**.

**Weather's range.** The user asked for Calm slower than first proposed
(one event every 2 minutes) and agreed that 8 a second is busy enough
for Stormy.

**Face size.** The user kept the Weather face at 18 mm radius. It fills
most of the 8 HP width, as in the sketch.

## Panel (8 HP)

As in the mockup. The panel generator makes the real one; the uncommitted
draft entry `"Squall"` in `tools/gen_panels.py` is the starting point.

- **Title** SQUALL, subtitle RANDOM GATES & CV.
- **Weather**, the hero knob, at (20.32, 34). It has an ivory barometer
  face, 18 mm radius, with 24 ticks, a long one every 6. The words CALM,
  FAIR, CHANGE, RAIN and STORMY sit at -135, -67.5, 0, 67.5 and 135
  degrees, and WEATHER is on the face.
- **Three columns** at x = 7.6, 20.32 and 33.04: Gust, Weather's CV
  path, and Memory.
  - Gust and Memory are standard knobs at y = 60.
  - All three attenuverters are at y = 72.5 and the CV jacks at y = 83,
    the same heights as Hadal's.
  - Each CV is traced up through its attenuverter to its control.
    Weather's trace runs into the face.
- **CLOCK**, the **event light** and **FREEZE** at y = 102.
- **STEP, SWELL, GATE** on ivory tiles at y = 116.

The 83, 102 and 116 mm rows line up with Hadal, Ballast, Fog and Sway
(the user's rule: modules line up with one another). Screws go top
right and bottom left, as on the other panels. The generator's checks
must pass.

## Controls and behaviour

### Weather: how much is happening

Weather position w runs from 0 to 1: knob + Weather CV / 10 V ×
attenuverter, clamped. With the attenuverter fully clockwise, ±5 V from
noon sweeps the whole range.

**Free-running**, the average event rate is

    rate(w) = (1 / 600 s) × 4800^w

That gives a geometric scale, with each word a real point on it:

| Word | w | Average |
|---|---|---|
| CALM | 0 | one event every 10 min |
| FAIR | 0.25 | about one every 72 s |
| CHANGE | 0.5 (noon, the default) | about one every 9 s |
| RAIN | 0.75 | about one a second |
| STORMY | 1 | 8 a second |

**With a clock patched,** Weather is the chance of an event on each
tick:

    p(w) = 256^(w − 1)

So Calm is about one tick in 256, Fair one in 64, Change one in 16, Rain
one in 4, and Stormy every tick (*designer's call*: the same
"multiply by a constant per word" shape as the free rate).

### Gust: how events bunch up

Gust g runs from 0 to 1 (knob + CV law as above). The default is 0.3
(*designer's call*: a little gusty, so the first sound shows what Squall
is).

Gust works as a self-exciting process:
- Each event adds a bump to the event rate, which then decays. Gust sets
  the branching ratio n = g × n_max, where n_max is about 0.9, tuned by
  ear.
- The background rate is rate(w) × (1 − n). This keeps the long-run
  average at rate(w) whatever Gust is. It is the anti-goal above, and a
  test.
- The bump's decay time scales with the average gap: τ = k / rate(w),
  where k is a builder's starting figure, about 0.25 to 0.5, tuned by
  ear. A burst at Calm lasts minutes; at Stormy, a fraction of a second.

At **Gust 0**, events are a Poisson process: independent, with no burst
and no pulse.

**With a clock,** the same model runs on the tick grid. Each tick's
chance is the background chance plus the decaying bumps, clamped to at
most 1. The long-run chance per tick stays p(w) until the clamp binds
(near Stormy). At Stormy every tick is an event anyway.

### Memory: whether the weather repeats

Memory m runs from 0 to 1 (knob + CV law). The default is 0, so
everything is new.

- Squall keeps a history of its recent events. Each event stores:
  - the gap before it;
  - its value;
  - its gate length.

  Gaps and gate lengths are stored **as fractions of the average gap**
  at the time, in both modes (*designer's call*).
- **Loop length** L is 4, 8 (default) or 16 events, set in the
  right-click menu and saved.
- **One decision per event** (*designer's call*, after the builder
  check). At each event, Squall decides what the **next** event will be:
  - **With probability m, a replay.** The next event is a copy of the
    one L back in the history. It comes after that event's stored gap
    (scaled by the current average gap), with its stored value and gate
    length.
    - Gust's process cannot fire during that gap. The replay owns the
      timing.
  - **Otherwise, new.** Gust's process times the next event, and its
    value and gate length are new.
  - **Replayed events count as events for Gust.** They add their bump,
    so a new event after a run of replays still feels the burst it was
    in.
  - Either way, every event goes into the history.
- At m = 1 the last L events loop exactly. Around 0.7, a pattern holds
  but wears away. The share of replays is m.
- **Clocked, a stored gap becomes whole ticks:** round(gap ÷ p(w)), at
  least 1. Since a clocked event's gap was recorded as ticks × p(w), the
  loop replays the exact tick counts while Weather stays put.
- **Patching or unpatching the clock keeps the history.** The same
  stored pattern carries on in the new mode, so a locked pattern
  survives.
- Storing everything relative to the average gap means turning Weather
  speeds up or slows down a locked loop without breaking it.
- **The long-run amount:** with Memory up, replayed gaps average what
  they averaged when made. So the rate stays at rate(w), give or take
  the luck of the stored loop.
- The history is **saved with the patch** (*designer's call*), so a
  locked pattern comes back on reload. A new Squall starts with an empty
  history and makes new events until it has L of them.

### Outputs

- **GATE:** 0 or 10 V, high for a quarter of the current average gap
  (free-running: 0.25 / rate(w); clocked: 0.25 × the measured tick
  period / p(w)), held to 5 ms to 4 s.
  - If an event comes while the gate is high, the gate drops for about
    1 ms and rises again, so every event is a separate rising edge.
  - A replayed event uses its stored length, held to the same limits.
- **STEP:** jumps to the event's value at each event and holds it.
  - New values are drawn from an even spread across the output range
    (*designer's call*).
  - Step changes only at events, so Gust's bursts are bursts of changes.
- **SWELL:** a smooth drift through the same values.
  - It follows Step through a smooth, non-overshooting follower whose
    time scales with the average gap, so its pace follows Weather.
  - It never reaches a value faster than about half the average gap,
    and it settles to within 5% of a held Step value within about two
    average gaps.
  - In a burst, it smooths through the quick changes rather than chasing
    each one. In a lull, it arrives and rests.
  - Its slope is continuous, so it never has corners. A critically
    damped two-pole follower is the starting point; the builder may
    choose another method that meets these rules.
- **Range** (right-click menu, saved): ±5 V (default), 0 to 10 V, ±2 V
  or ±1 V, for Step and Swell together.
  - Values are stored as 0 to 1 and mapped to the range on output.
  - A range change moves Step at once, and Swell glides to the new
    mapping, so it never jumps.
- **The event light** flashes for about 100 ms on each event, and stays
  visible at any Weather. At Calm, the light is how you know Squall is
  alive.

### Inputs

- **CLOCK:** a Schmitt trigger at 0.1 V and 1 V (the house standard).
  - Patched, Squall is clocked: events land only on rising edges.
  - Unpatched, it runs free.
  - The tick period is measured between edges for the gate length. Until
    two edges have arrived, the gate is 10 ms.
- **FREEZE:** a gate, with the same Schmitt levels. While high:
  - No events happen, and clock ticks are ignored.
  - Step holds.
  - **Swell brakes to a stop over about 20 ms and holds** (*designer's
    call*, after the builder check). An instant stop would put a corner
    in it, against the anti-goal. It may coast a hair past where it was
    when Freeze rose.
  - On release, Swell starts again from rest, so its slope stays
    continuous.
  - Gust's bumps stop decaying, and free-running time is paused, so
    releasing Freeze carries on where it stopped.
  - A gate already high ends at its normal time.
- **CV inputs** for Weather, Gust and Memory: control = knob + CV / 10
  V × attenuverter, clamped. The attenuverters default to 0. A
  polyphonic CV uses channel 1.
  - CV on Weather changes the rate at once, with no smoothing needed,
    since it changes when events happen, not a level.
  - Swell's pace follows the new rate smoothly.

### Bypass

All outputs are 0 V and the Gate is low (*designer's call*: Squall has
no input to pass through).

## Code and files

This follows `README.md` "Adding a module" and `TESTING.md` "Adding a
module", as Hadal and Ballast did:

- **Engine:** `src/squall/SquallEngine.hpp` (`rka::SquallEngine`, fed a
  `SquallFrame`, returning a `SquallResult`), Rack-free. The event
  process, the memory, the gate timer and the Swell follower live in
  their own small headers.
- **Module:** `src/squall/Squall.cpp`.
- **Tests:** `src/squall/test_*.cpp`.
- **Bench:** `src/squall/bench.inc`.
- **Panel:** the `"Squall"` entry in `tools/gen_panels.py` (`res/squall/`,
  `SquallLayout.hpp`).
- **Manual:** `docs/squall/README.md`, with `docs/squall/Squall.png`.
- **Registration:**
  - `plugin.json`: slug `Squall`, tags Random, Clock modulator (or the
    closest Rack tags), Sample and hold;
  - `src/plugin.*`, the Makefile's `MODULES`, `tests.mk`, `README.md`'s
    table, `CHANGELOG.md`;
  - `docs/future/`:
    - mark `squall.md` built;
    - in `ROADMAP.md`, mark Squall built, move Plimsoll to "as a patch
      calls for them" with its reason, mark Lull low priority, and
      correct the two stale Pan lines (row 4, and row 13's "Ballast's
      pan inputs");
    - the note in Hadal's spec about "Squall's Noise output" is now
      moot, so add a line there.

**Randomness:**
- Every random source is seeded on construction, so tests are
  deterministic.
- Two instances get different seeds.
- No `rand()` and no global state, so `engine-globals` passes.

**Real-time rules:**
- No allocation and no libm per sample (FastMath); the history is a
  fixed array of 16.
- No denormals, and NaN-safe comparisons.
- Event timing works at every sample rate from 44.1 kHz to 768 kHz.
- An unusable rate is ignored and the last good one kept, as on Fog.
- **Timing at Calm** must not drift or overflow: a 10-minute gap at 768
  kHz is 4.6 × 10^8 samples. Count in doubles or 64-bit integers, not
  floats.

**Saved state:**
- Knobs and attenuverters (by Rack);
- `range`, `loopLength`, and the history (with each event's gap, value
  and gate length).
- **Step's and Swell's current values** (*designer's call*, after the
  builder check). A reloaded patch resumes where it was, and Swell
  doesn't jump or glide in from 0 V.
- A non-finite or out-of-range saved value is replaced by its default.

## Testing

Everything in `TESTING.md` applies from the start. Specific to Squall,
with each test shown to fail against a deliberate mutation:

- **Rate law.** At each of the five words, free-running at Gust 0, the
  measured average rate over a long run is within a stated tolerance of
  rate(w).
- **Gust keeps the amount.** At w = 0.5 and Gust 0, 0.5 and 1, the
  long-run rate is the same within tolerance.
- **Gust clusters.** The spread of gaps grows with Gust. The
  coefficient of variation is about 1 at Gust 0 and clearly above 1 at
  Gust 1, and so is the count of events per window relative to its mean.
- **No pulse.** At Gust 0 the gaps are exponential (a goodness-of-fit
  test), and the gap sequence has no autocorrelation peak.
- **Clocked.** Events happen only on clock edges. The chance per tick
  matches p(w) at Gust 0. Stormy fires on every tick.
- **Memory.**
  - At m = 0, no replays: values don't repeat at lag L beyond chance.
  - At m = 1, once the history is full, the output is exactly periodic
    with L events.
  - At m = 0.7, the share of replays is 0.7 within tolerance.
  - A locked loop keeps its shape when Weather changes, and the gaps
    scale.
  - The history, and Step's and Swell's values, survive a save and
    reload.
  - A locked loop carries on through patching or unpatching the clock.
  - During a replayed gap, Gust's process fires nothing.
- **Gate.**
  - Its length is 0.25 of the average gap, held to 5 ms and 4 s.
  - An event during a high gate gives a fresh rising edge.
  - Clocked gates use the measured tick period.
- **Step.** It changes only at events, its values cover the range
  evenly, and it stays inside the selected range for all four ranges.
- **Swell.**
  - It stays inside the range.
  - It has no step: the per-sample change is bounded.
  - It has no corner: the change in slope is bounded.
  - It settles to within 5% of a held Step value within two average
    gaps.
  - Its pace follows Weather.
  - It doesn't jump on a range change, Freeze, release or reload.
- **Freeze.**
  - While high there are no events, and Step holds.
  - Swell brakes within about 20 ms with a bounded change of slope,
    then holds.
  - On release, timing carries on, and a pending Gust burst resumes.
- **CV.** The knob + CV law holds on all three controls, and a
  polyphonic CV uses channel 1.
- **Long gaps.** At Calm and at 768 kHz, a simulated hour gives a
  correct rate, with no overflow and no drift.
- **Hostile input and sample rates.**
  - NaN and ±inf on every CV and on Clock and Freeze.
  - Sample rates from 1 Hz to 768 kHz, 0 and negative.
  - Outputs stay finite and in range, and Squall recovers at 48 kHz.
- **Seeds.** One seed gives identical runs, and different instances
  differ.
- **No allocation** in `process()`, no libm per sample, no global state.
- **Seeded fuzz** of every knob, CV, clock and Freeze: outputs stay
  finite and in range.
- **Bench:**
  - idle: nothing patched;
  - outputs: everything patched, at defaults;
  - busy: Stormy, Gust 1, Memory 0.7, all CVs moving, clocked.

  The budgets come from measurement. Squall should cost less than
  Hadal.
- **Smoke screenshot,** and the user's check in Rack.

**Listening stop.** The plan stops for the user to listen before Gust's
n_max and k and Swell's follower are fixed. The reference patch is
Squall's Gate into Hadal (or an envelope into Sluice), with Swell on
Fog's Bloom or Fathom.

## Left to the builder

- n_max (about 0.9) and k (about 0.25 to 0.5), within the anti-goals,
  tuned at the listening stop.
- Swell's follower method and time constants, within the rules above.
- How the measured tick period follows a changing clock.
- The event light's colour, from the house palette.
- The test tolerances marked "stated", set from measurement and
  justified in the plan.
