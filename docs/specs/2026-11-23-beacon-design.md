# Beacon: an envelope generator: design

Status: design, 2026-11-23. Panel: `2026-11-23-beacon-mockup.png` (drawn
by a draft entry for `tools/gen_panels.py`, not yet in the repo's PANELS). Grows
from the idea in `docs/future/beacon.md`; drops its Loop button.

## Intent

The user reaches for an ADSR envelope more than anything RKA lacks. Shoal
can make envelopes, but it is a big module for a small job. They asked for
"a smaller envelope generator that's flexible (can do ADSR/AD/ADR, etc),
but sticks to envelopes as the focus. something like the cèis from
instruo." Asked what about cèis made it the model, they said: **its
outputs, and the way it switches.**

So Beacon is cèis's idea in the Instrument look:

- **The outputs:** besides the envelope, a gate for each stage and one
  trigger at every change of stage, so one envelope also drives events.
- **The switching:** no list of envelope types. One three-way switch,
  Gate / Trig / Hold, and where the knobs sit, give ADSR, ADR, AR, AD and
  a slew limiter.

(cèis 2 manual, read 2026-11-23:
`https://analoguehaven.com/instruo-specialist-synthesizers/ceis2/manual.pdf`.)

The user also pointed at Mutable Instruments Tides
(`~/ws/plugins/eurorack/tides2`, MIT): Beacon's engine works the way
Tides' ramp generator does (Approaches).

The name is a lighthouse's: a light that marks where you are. Beacon's
window does that for the envelope: it draws the shape the knobs make and
shows where the envelope is in it.

## Goals, non-goals, anti-goals

Goals:
- ADSR, ADR (Sustain at 0), AR and AD from one switch and four knobs.
- Stage times from about 1 ms to 60 s on the knob itself: snappy enough
  for a pluck, slow enough for an ambient swell (Fog's tails run to 70 s).
- Exact stage times: a typed or shown "2 s" is 2 s.
- A gate per stage, a trigger per stage change, the envelope and its
  inverse.
- A Hold mode that makes it a slew limiter, as cèis's does.
- Polyphonic from the start (issue #64 names Beacon).
- CV on all four stages.
- A small window that draws the envelope and shows where it is.

Non-goals:
- Looping or cycling into an LFO (Beacon's idea page had a Loop button;
  the user wants envelopes as the focus; Sway is the LFO).
- Extra stages (hold, delay), multi-segment editing (Mutable Stages'
  ground), drawable curves.
- Tides' spread outputs (four envelopes with stretched timings) and its
  unshaped linear copy.
- A time range switch: the user ruled for one long range on the knob.
- Audio-rate cycling.

Anti-goals (a failure even if it works):
- A jump in the envelope when it is retriggered or a gate ends early.
- A stage time that is not what the knob or tooltip says.
- Short times that are fiddly to set because the long range crowds them.

## What the user decided

- The model is cèis: its outputs and its Gate / Trig / Hold switching
  (2026-11-23).
- Stage times: one long range on each knob, no range switch.
- Keep Hold (the slew limiter).
- Add an inverted output.
- Build it the Tides way: timed stages (Approaches, 2).
- **Every stage takes exactly its set time, wherever it starts** (option
  A of two put to the user): Release at 2 s from Sustain at 5 V takes 2 s;
  a retrigger from 8 V takes the full Attack time to reach 10 V. Rejected:
  times as rates for a full 10 V, under which stage lengths shift with
  Sustain.
- **Panel:** as the mockup. A window at the top in place of the first
  sketch's lamp (the user: the lamp "doesn't really have much use"); a
  small push button for a manual gate under the GATE jack.
- **The window draws the envelope from the knobs, with a dot riding it**
  (option 1 of two; rejected: a live scrolling trace).

## Approaches

1. **cèis-style analog glide.** Each stage is a one-pole heading for a
   target past its goal (an RC charging); Shape sets how far past.
   Retrigger from the current level, a knob moved mid-stage and Hold's
   slew all come free. But stage times are approximate (time to reach
   "near" the goal), and Shape only spans linear to exponential.
2. **Tides-style timed stages (chosen).** Each stage runs a phase from 0
   to 1 over exactly its time, and the phase goes through a curve. Times
   are exact; Shape can span log through linear to exponential. A change
   mid-stage (retrigger, a gate ending early) takes the level where it is
   as the new stage's start (Tides' `RampWaveshaper` breakpoint,
   `tides2/ramp/ramp_shaper.h:234-247`), so nothing jumps. Hold is its own
   small glide.
3. **Vendor Tides 2.** Rejected: Tides has no decay-to-sustain-then-
   release, so ADSR would be forced onto an AD/AR engine, and its outputs
   are not cèis's.

The user picked 2, for exact times under a 60 s knob with typed values,
and because Tides has already solved the retrigger problem.

## Panel (10 HP)

As the mockup (the idea page said 8 HP; 13 jacks, 5 knobs, a switch, a
button, four stage lights and the window need 10). Top to bottom:

- **The window** (about 42 x 17 mm), under the nameplate (below).
- **Shape** (left) and the **GATE / TRIG / HOLD switch** (right), in a
  row.
- **The four stage knobs:** Attack, Decay, Sustain, Release, each with a
  small light at its shoulder, lit while its stage runs (cèis's lit
  faders).
- **Four CV inputs** under them, traced to their knobs.
- **Four stage gate outputs** (A, D, S, R) on ivory tiles.
- **GATE** (the Gate/Trig input) and **RETRIG**; under GATE, the small
  **push button** (a manual gate); TRIG, OUT and INV on ivory tiles.

The mockup draws the switch with the existing two-position toggle; the
three-position switch is a new component.

Knobs, not sliders: the Instrument look has no fader component, and adding
one is out of scope.

## Controls and behaviour

### Envelope (Gate and Trig modes)

- **Stages:** Attack rises from where the envelope is to 10 V; Decay falls
  to Sustain's level; Sustain holds while the gate is high; Release falls
  to 0 V. A stage always starts from the envelope's present level and
  takes exactly its set time to reach its goal, however far that is (the
  user's ruling). The curve is the stage's full curve rescaled between
  that start and the goal (Tides' breakpoint rule). Decay with Sustain at
  10 V, or any stage already at its goal, ends at once.
- **Gate mode** (cèis's): a rising edge starts Attack. Attack and Decay
  run while the gate is high, then Sustain holds. The falling edge starts
  Release from wherever the envelope is. A trigger, or a gate shorter
  than Attack (+ Decay), goes straight to Release.
- **Trig mode** (cèis's): a rising edge starts Attack, and Attack always
  runs to the end. If the gate is still high after Attack, Decay and
  Sustain follow as in Gate mode; if it is low, Release starts at the top.
  So a trigger gives AR (Attack, then Release: AD in other modules'
  terms), and a long gate gives the full ADSR.
- **A rising edge while the envelope is running** restarts Attack from the
  present level, in both modes.
- **RETRIG** (cèis 2's): a rising edge restarts Attack from the present
  level without a new gate. It does nothing while the gate is low.
- **Shapes from the knobs:** Sustain at 0 is ADR; Decay at its minimum
  with Sustain at full is AR; Trig mode with a trigger is AR/AD.
- **The push button:** pressing it is a gate held for as long as it is
  held (in both modes), on every channel, as if GATE were high.
- **Inputs:** GATE and RETRIG read as gates with the plugin's usual
  Schmitt thresholds (0.1 V / 1 V, as Wake's clock).

### Times

- **Attack, Decay and Release:** 1 ms to 60 s on an exponential knob, so
  each decade takes the same share of the turn; noon is about 245 ms. The
  tooltip shows ms or s, and a typed time sets it (as Wake's Time).
  *Final review:* three digits from 10 ms up ("10.0 ms", "47.3 ms",
  "245 ms", "1.00 s", "10.0 s"), so the text is within 0.5 % of the
  time; below 10 ms one decimal ("4.7 ms").
  Defaults: the builder's call, tuned by ear (Listening stop).
- **CV:** added to the knob, 10 V for the knob's whole travel, no
  attenuverter (as cèis). The result is held between 1 ms and 60 s.
- **Sustain:** a level, 0 to 10 V on the knob; its CV adds 1 V per volt
  (±10 V), and the sum is held to 0 to 10 V in Gate and Trig modes.
- Knob and CV changes act on a running stage smoothly: the stage's rate
  follows the new time from where it is, with no jump in level.
- *Designer's call:* **Sustain moved during the Sustain stage** is
  followed, through a short smoothing (about 2 ms), so a step on the knob
  or its CV does not click but an LFO on Sustain still moves the level
  (tremolo). The No-jumps test allows that smoothing's slope.

### Shape

- One knob for every stage. **Noon is linear.** Fully right is the
  classic analog contour (cèis's clockwise end): a fast-starting,
  rounded attack and exponential decay and release. Fully left is the
  mirror: an attack that starts slowly and speeds up (a swell) and decays
  that hold near the top and then fall. Between, it crossfades.
- The curves may come from Tides 2's tables
  (`tides2/resources/lookup_tables.py`, MIT; keep the notice) or be
  computed; the builder chooses. Either way the stage's time stays exact.
- Default: the builder's call, by ear (likely right of noon).

### Hold mode (slew limiter, cèis's)

- The envelope stops. The **Sustain CV input** becomes the signal to slew
  (±10 V); the Sustain knob adds 0 to 10 V of offset.
- OUT follows it: rising at Attack's rate, falling at Decay's. The rate
  is set so that a 10 V change takes the knob's time (so the knob reads as
  "time for full range"). Release is unused.
- Stage gates: A high while rising, D high while falling, S high while
  settled; R low. TRIG fires on each change between them.
- Shape does not bend the slew in Hold (a plain rate limit), unless the
  builder finds a clean way to apply it.
- GATE, RETRIG and the push button do nothing in Hold.
- *Designer's call:* **rising, falling and settled.** Settled (S) when the
  output equals the input after the rate limit, that is when the slew is
  not limiting; A while it is limiting upwards, D downwards. A slow LFO
  the slew can follow reads as settled. A small hysteresis (about 1 mV)
  keeps the gates and TRIG from flickering at the boundary.

### Mode switches while running

*Designer's call.* Each voice keeps one level, shared by the envelope and
the slew, so the switch itself never moves the output:
- **Gate/Trig to Hold:** the slew starts from the present level.
- **Hold to Gate/Trig:** with the gate high, Attack starts from the
  present level; with it low, Release runs from the present level to 0 V
  (a rise when the level is negative), over Release's time.
- **Gate to Trig or back:** the running stage carries on; the new mode
  applies from the next edge.
- **Channels that appear** (the channel count changes on a switch or a
  repatch) start at 0 V, idle; channels that go away are dropped.

### Outputs

- **OUT:** 0 to 10 V (Hold: ±10 V plus the offset, held to ±10 V).
- **INV:** OUT times −1 (0 to −10 V).
- **A, D, S, R gates:** 10 V while that stage runs, else 0 V.
- **TRIG:** a 1 ms, 10 V trigger at the start of every stage, including
  the return to idle at the end of Release. *Designer's call:* a stage
  start while the pulse is high restarts its 1 ms (pulses merge, as
  Rack's `PulseGenerator`), so stage starts under 1 ms apart give one
  longer pulse. A stage gate is high for at least one sample even when its
  stage ends at once.
- Stage lights mirror the gates (channel 1), updated every 16 samples;
  a stage lit even for one sample shows for at least 40 ms (a UI frame),
  the hold counted from the stage's start, so a longer stage's light goes
  out as it ends.

### The window

- **What it draws:** the envelope the knobs (with their CVs) and Shape
  make now: Attack up to 10 V, Decay to Sustain, a stretch of Sustain,
  Release to 0 V, each stage in its Shape curve, lettered A, D, S, R
  underneath. The stage that is running, and its letter, are bright; the
  rest dim. A dot sits where the envelope is: its stage and how far
  through it.
- **Time axis:** not to scale. Each stage's width grows with the log of
  its time, so a 1 ms Attack stays visible beside a 60 s Release; Sustain
  gets a fixed width. The window shows shape and position, not a ruler.
- **Part-way starts** (ruled at the listening stop, test build 2.0.34):
  the knobs' shape stays drawn dim as the reference, and the running
  Attack, Decay or Release is also drawn bright from the level where it
  really started to its goal, along the curve the voice is following;
  the dot rides that bright line. Where it is drawn across the window is
  under "Skipped stages" below. A gate that rises while releasing draws
  Attack over its own span from, say, 4 V up to 10 V; a gate that falls
  mid-Attack draws Release from, say, 9.7 V where Attack was cut off in
  the Attack span down to 0 V at the right edge. The engine publishes channel 1's
  stage start and goal levels for this. Decay's goal is the sustain level
  when Decay began, so a Sustain move during Decay makes the bright Decay
  end at the old level, as the voice really does (it then settles to the
  new level in Sustain).
- **Skipped stages** (issue #76, after test builds 2.0.35 and 2.0.36):
  when the running stage skipped others, it is drawn bright from where
  the last stage was cut off (its phase then, across its span; a cut-off
  Sustain counts as its span's end) to the end of its own span, so the
  path is continuous: a gate falling half-way up Attack draws Release
  from mid-Attack span; falling 1.3 ms into a 300 ms Decay, from just
  after the end of Attack. The engine publishes channel 1's previous
  stage and how far it had got for this (leaving Hold counts as from
  idle). A retrigger's Attack stays on its own span.
- **Sustain at 0 V:** the Sustain span stays dim, and while the envelope
  is in Sustain the dot sits at the end of the line (bottom right) rather
  than mid-span. The S light and gate are unchanged: lit while the gate is
  high, as on cèis.
- **Hold mode:** the window shows the slew instead: a short scrolling
  line of the output, or the input and output levels as two marks; the
  builder proposes which in the test build. *Designer's call* that it
  should show something useful there rather than a stale envelope.
  *Built:* the scrolling line, sampled at 60 points a second of the UI's
  frame clock (not once per frame), so it scrolls at the same speed at 60
  and 144 Hz.
- **Polyphony:** channel 1.
- **Hand-off:** the engine publishes channel 1's stage and phase (and
  level, the running stage's start and goal, and the stage before it and
  how far it got) for the window,
  lock-free; nothing per-sample crosses, and the curve is computed on the
  UI side from the params.
- **Unpatched and idle:** the shape is drawn, no stage is lit, the dot
  rests at the start.

### Polyphony

- **Gate and Trig modes:** the channel count is GATE's (or 1 when
  unpatched). RETRIG and each CV: a polyphonic cable gives each channel
  its own; a mono cable applies to every channel.
- **Hold mode:** the channel count is the Sustain CV's.
- Every output carries the channel count; each channel has its own stage
  gates and trigger.
- plugin.json tag `Polyphonic`.

### Bypass and other

- Bypass: Rack's default (outputs silent); there is nothing to pass
  through. Hold mode's slew has no bypass route either. *Final review:*
  bypass and un-bypass reset every voice to idle at 0 V and put the
  lights out, so the window shows idle and no Release runs on un-bypass.
- Saved state: the switch is a param, so nothing goes through
  `dataToJson` unless the builder adds something.
- Sample rate changes keep the stage and level; times stay in seconds.

## Code and files

As every module (CLAUDE.md, TESTING.md "Adding a module"):

- `src/beacon/`: Rack-free headers; `Beacon.cpp` maps params, ports,
  polyphony and lights onto them. Likely parts: one voice's stage machine
  and curve; the slew (Hold); the engine looping over channels; the time
  knob mapping and typed-value parsing (Rack-free, tested, as Wake's
  `Timing.hpp`).
- Any code taken from Tides 2 keeps its MIT notice and is listed in
  `LICENSE`-style notes as Beads' is; it is not vendored whole.
- `src/FastMath.hpp` for anything per sample; no libm per sample.
- `src/beacon/bench.inc` (idle, outputs, busy; a 16-channel case), the
  panel entry, plugin.json (slug `Beacon`), manual `docs/beacon/README.md`,
  README table, CHANGELOG, roadmap and `docs/future/beacon.md` marked
  built.
- A three-position switch is a new component in `src/widgets.hpp`;
  build it from the existing toggle. *Final review:* it steps by
  direction, not as a stock switch (which wraps 0 -> 1 -> 2 -> 0, so Trig
  to Gate would pass through Hold): a click on the upper half moves it one
  position up (toward Gate), on the lower half one down (toward Hold),
  held at the ends; a vertical drag moves it too; one gesture is one undo
  step. The decision is in the Rack-free `src/Toggle3Step.hpp`, tested in
  `test_switch`.
- **The window:** a widget in `src/widgets.hpp` (or `src/beacon/`), as
  Eddy's `TapeWindow` and Spindrift's `GrainWindow`; its geometry (stage
  widths on the log axis, the curve's points, where the dot goes) in a
  Rack-free header with offline tests (TESTING.md, "UI logic is Rack-free
  too"). The generator's `screens` entry draws its bezel.

## Testing

TESTING.md applies in full. Particular to Beacon:

- **Times.** Each stage from rest takes its set time within 1 sample or
  0.1 %, across the range (1 ms, 10 ms, 1 s, 60 s), at 44.1, 48 and
  96 kHz, at Shape left, noon and right.
- **Levels.** Attack ends at 10 V, Decay at Sustain, Release at 0 V
  exactly; Sustain holds its level for minutes.
- **Modes.** Gate mode: a short gate goes to Release mid-Attack; Trig
  mode: a trigger completes Attack, then Release; a long gate in Trig mode
  gives the full ADSR. ADR (Sustain 0) and AR (Decay min, Sustain full)
  shapes.
- **No jumps.** Retrigger mid-Release, mid-Decay and mid-Attack; RETRIG;
  a gate ending in Attack or Decay; a knob or CV moved mid-stage; a mode
  switch while running: no sample-to-sample step beyond the stage's own
  slope.
- **Gates and trigger.** Each stage gate is high exactly while its stage
  runs; TRIG fires once per stage start, 1 ms wide, merging when starts
  are under 1 ms apart.
- **Part-way starts.** A retrigger from 8 V, Release from Sustain at 5 V
  and Decay to a high Sustain each take exactly their set times.
- **Mode switches** as stated, including channels appearing and going.
- **Shape.** Noon is linear within a tolerance; left and right bend the
  stated way; the time is the same at every Shape.
- **Hold.** A step on Sustain CV rises in Attack's time per 10 V and
  falls in Decay's; the gates show rising, falling and settled; the
  offset adds.
- **Polyphony.** 16 channels with different gates are independent;
  mono CVs apply to all; the channel count follows GATE (Hold: Sustain
  CV).
- **Inverted.** INV is −OUT to the bit.
- **The window's geometry** (offline): stage widths grow with log time
  and stay inside the window at every knob setting; the drawn curve
  matches the engine's curve at each Shape; the dot's position for a
  given stage and phase; idle and Hold cases.
- **Typed times.** The tooltip's text parses back to the same time.
- Sanitizers, no allocation in process, extreme and changing sample rates,
  seeded fuzz (gates, CVs, mode changes), NaN/inf on every input, bench
  budgets, no libm per sample, Linux and Windows builds.
- **Aliasing** (TESTING.md rule 16): Beacon makes control signals, not
  audio, but a 1 ms corner is audible on a VCA. The builder measures the
  fastest settings against a bound, or records in TESTING.md's table why
  none is needed.

## Listening stop

Before these are fixed, the user tries a numbered test build into Sluice
and Narrows: the Shape curves at each end and the default; the default
times; how short times feel at the bottom of the knob; Hold's slew on a
V/OCT line; the window (whether it reads at a glance, and what it
shows in Hold).

## Left to the builder

- How the curve is computed (tables or a function) and how Shape
  crossfades, within the tests.
- How often knobs and CVs are read, and any smoothing.
- The defaults for every knob, tuned by ear.
- Final positions within the look's rules (the mockup's layout passes
  the generator's checks); lettering of the time knobs; the window's
  colours, line weights and the dot.
- Bench budgets, from measurement.
