# Wake: a stereo echo: design

Status: design, 2026-11-20. Panel: `2026-11-20-wake-mockup.png` (drawn by
a draft entry in `tools/gen_panels.py`, marked MOCKUP). Grows from the idea
in `docs/future/wake.md`, and is much smaller than it.

## Intent

The user's first ask was "a comprehensive delay. stereo/tap. good
functionality." Asked what a delay does in their pieces, they said:

- "I like Valhalla Delay with a lot of options. Tape, digital, analog.
  Probably don't need that depth though for this type of music."
- "When we use the Rigging Strum or the upcoming Ping modules, it's good
  to put a delay on there to add some depth and space and a bit of
  echoing rhythm."

So Wake is an echo for struck sounds. It sits after Rigging or Ping and
gives each hit depth, space and a rhythm of repeats, in one of three
characters. It is not the patching hub the idea page sketched.

The picture is a ship's wake: what a passing sound leaves behind it.

## Goals, non-goals, anti-goals

Goals:
- Echoes from 10 ms to 10 s behind a hit, from one repeat to repeats that
  hold for minutes.
- Three characters, Tape, Analog and Digital, that differ audibly in how
  the repeats sound and in what happens when Time moves.
- Rhythm and width from the two sides: left and right at related times,
  and ping-pong.
- Repeats that can sink back into the space (Diffuse) and move a little
  (Drift).
- Freeze: hold what is in the delay and loop it.
- Echo times that follow a clock.
- CV over Time, Feedback and Mix.

Non-goals:
- Separate taps with their own levels and outputs, and a Send and Return
  into the feedback loop (the user: "leave out the separate taps at least
  on the user control side").
- A tap-tempo button: the Clock input does it.
- Close copies of particular tape or bucket-brigade units, or Valhalla
  Delay's depth of options.
- Reverse, ducking, pitch shifting in the loop.
- Echoes longer than 10 s: that is a looper, which is Eddy.
- Polyphony: a polyphonic cable is summed (as Rigging's audio inputs).
- Saving the delay's contents with the patch.

Anti-goals:
- A level that runs away: Feedback at its top with a loud input must stay
  bounded and must not clip harshly.
- Clicks or zipper noise when Time, Ratio, Style, Freeze or Ping Pong
  change, by hand, by CV or by clock.
- A frozen loop that fades, dulls or builds up.
- A DC offset at the outputs or building in the loop.
- Left and right cancelling when the output is summed to mono.

## What the user decided

- The use: echoes behind struck sounds (above).
- Three characters after Valhalla Delay's, without its depth.
- Rhythm from the two sides and ping-pong ("the approach above is a good
  start").
- No separate taps on the panel.
- The control set in the mockup, including Diffuse and Drift ("yes").
- The ping-pong button is lettered PING PONG.
- Time runs 10 ms to 10 s ("that's fine").

## Approaches

1. **One engine, three characters (chosen; the designer's call, the user
   was told).** One stereo delay with a moving read point. Tape is its
   natural behaviour: when Time moves the read point glides, so pitch
   bends. Analog adds a darker, grittier loop whose bandwidth falls as
   Time lengthens. Digital crossfades to the new time, so there is no
   bend, and its loop is clean. One buffer, one set of tests, and the
   buffer code is reusable by Eddy and Spindrift.
2. **Three separate delays.** A tape model, a bucket-brigade model and a
   digital delay, each built to its own design. Closer to each original,
   but three times the building and tuning, for differences this music
   will not show.
3. **One clean delay with a Character knob.** A single macro from clean
   to worn. Simplest, but it loses the distinct behaviours (above all the
   pitch bend or the lack of it), which is what the user likes in
   Valhalla Delay.

## Panel (14 HP)

As the mockup. Rows at 64, 83, 102 and 116 mm and five columns, the same
as the Narrows sketch, so the two line up.

- **Time**, the hero, on an enamel face lettered 10, 32, 100, 316 (MS),
  1S, 3.16S, 10S, each at its true place on the scale. Under it, in the middle column, its attenuverter and CV.
- **Style**, a button to the right of the dial, and three lights under it
  lettered TAPE, ANALOG, DIGITAL. Each press steps to the next.
- Row of knobs: **Feedback**, **Tone**, (Time's attenuverter), **Ratio**,
  **Mix**.
- Row of jacks: Feedback CV, **CLOCK**, Time CV, **GATE** (freeze), Mix CV.
- Bottom rows: IN L and IN R on the left; **Diffuse** and **Drift**
  between the rows; the **Freeze** button under its gate jack and the
  **Ping Pong** button under that; L and R outputs on ivory tiles.

The wake's two arms are not drawn in the mockup; the builder may add them
(a Kelvin wake opens at 19.47 degrees either side of the track) if they
fit the generator's rules without crowding the lettering.

## Controls and behaviour

*As built (2026-11-20, after the final review):* the loop's colour and
Diffuse sit on the way into the delay line, so the line holds exactly what
is heard and a frozen loop is the echo that was playing. A crossfade
(Digital's change of time, and the release from Freeze) is linear and
takes 40 ms; a freeze waits for one to finish. A clock edge more than four
periods after the last is a restart, not a period; a second such
interval within 2x of the first is the new period (2026-11-25, #91). On a
release, the line's end is joined to the audio one loop older and the
write crossfades from the loop's continuation into the new sound, so the
line holds no hole (2026-11-25, #93). Drift is a wander of
each side's tape speed, so short echoes waver less than long ones. With R
unpatched L carries the average of the two sides.

### Time: the echo time

- 10 ms to 10 s on a log scale; default noon, 316 ms. The tooltip shows
  milliseconds or seconds.
- This is the left side's time. The right side's is set by Ratio.
- **CV:** added in octaves. At full attenuverter 1 V doubles the time, so
  ±5 V covers the whole range from noon. The attenuverter defaults to 0.
- The result is held between 10 ms and 10 s.
- **When Time moves,** what you hear depends on Style (below). In no
  style does it click.

### Clock

- A rising edge at CLOCK (0.1 V / 1 V Schmitt trigger) sets the tempo from
  the time between the last two edges.
- With CLOCK patched, the Time knob picks a multiple of the clock's
  period, in steps: 1/4, 1/3, 1/2, 2/3, 3/4, 1, 3/2, 2, 3, 4. Noon is 1,
  centred: 1 spans the knob from 0.45 to 0.55 (2026-11-25, #90).
  The tooltip shows the ratio.
- Time's CV moves along the same steps (*designer's call*, so a clocked
  echo is always on the clock's grid): the knob's position and the CV,
  through the attenuverter, are added (10 V is the full travel at full
  attenuverter), and the sum snaps to the nearest step.
- If the result is over 10 s it is halved until it fits; under 10 ms,
  doubled.
- If the clock stops, the last tempo holds. Unpatching CLOCK returns the
  knob to milliseconds, moving to the new time as any Time change does.
- Before two edges have arrived, the knob's own time is used.

### Ratio: the two sides

- Seven steps. At noon both sides have the same time. To the right the
  right side is shorter: 3/4, 2/3, then 1/2 of Time. To the left the left
  side is shorter by the same steps, and the right side has Time.
- The knob snaps to the steps; the tooltip names them (for example
  "R 2:3").
- The shorter side is never under 10 ms.

### Feedback: how many repeats

- 0 gives one echo. The knob runs to 110 %; 100 % is marked on the scale.
  Default 40 %.
- Below 100 % the repeats die away; how fast also depends on Tone and
  Style, which take a little off every pass.
- From 100 % up the repeats sustain or grow, into the loop's soft bound.
  The loop is bounded in every style, so this can never run away.
- **CV:** 10 V is the full travel of the knob, added to it (no
  attenuverter).

### Tone: how the repeats change

- A filter in the feedback loop, so its effect adds up with each repeat.
- Noon is flat. To the left each repeat is darker (a low-pass coming down
  to about 500 Hz at the end of the travel). To the right each repeat is
  thinner (a high-pass coming up to about 1 kHz).
- The first echo passes through the filter once.

### Style: three characters

Each press of the button steps Tape, Analog, Digital, Tape. Default Tape.
A change of style crossfades over a few tens of milliseconds.

- **Tape.**
  - When Time moves, the read point glides to its new place, so pitch
    bends up or down and settles, as a tape machine changing speed.
    The bend is at most two octaves, held there at most about 1 s
    (BEND_MAX_S); a bigger change crossfades the rest of the way, so
    the new echo arrives within a second.
  - Each pass through the loop is gently saturated, loses a little top
    (about 6 kHz) and a little bottom.
- **Analog.**
  - Time changes bend the pitch as in Tape, a little faster.
  - Darker than Tape, and darker still as Time gets longer (about 5 kHz
    at short times, falling to about 1.5 kHz at long ones), as a
    bucket-brigade line clocked slowly.
  - Each pass is driven harder, so repeats grow gritty as they go round.
- **Digital.**
  - When Time moves, the output crossfades to the new time: no pitch
    bend.
  - The loop is clean and full-band; only Tone changes the repeats.
  - It still has the soft bound that keeps Feedback above 100 % in check,
    which acts only on loud signals.

The exact corners, drive amounts and glide times are tuned by ear at the
listening stop.

### Diffuse: sinking back

- Smears each pass through the loop with a short chain of all-pass
  filters, so repeats blur further each time round and the echo turns
  toward a wash.
- At 0 it is out of the loop entirely: repeats are exact.
- It must not change the echo time by more than a few milliseconds, or
  the loudness of the repeats by more than about 2 dB.
- Fog's `Diffuser.hpp` is the starting point.

### Drift: slow movement

- A slow random wander of each side's echo time, independent on the two
  sides, so the repeats waver in pitch and the image widens.
- At 0 nothing moves in any style. Default 15 %.
- At full, the wander reaches about ±30 cents of pitch.
- The pace is fixed and slow (seconds), as Fog's and Hadal's Drift.

### Freeze

- The button latches and lights. The GATE jack freezes while it is high
  (1 V Schmitt trigger). Either one freezes.
- How it works (*designer's call*): writing stops, and each side plays
  the last stretch it recorded, as long as that side's echo time at the
  moment of freezing, round and round. Nothing is written back, so the
  frozen sound cannot fade, dull or build however long it is held.
  Feedback, Tone, Diffuse and the style's loop colour have no effect on
  it.
- The loop's join is crossfaded (about 10 ms), using the audio recorded
  just before the stretch, so it does not click.
- While frozen, Time, Ratio, the clock and Ping Pong are held as they
  were; changes to them take effect on release, as any change does. Each
  side loops what it holds, so a frozen ping-pong keeps its left and
  right content but no longer bounces. Changing playback speed of a held
  loop is Eddy's job.
- Drift still wavers the playback, so a frozen sound is not static.
- Entering and leaving fade over about 10 ms. The dry signal still passes
  through Mix.
- The button's state is saved with the patch.

### Ping Pong

- The button latches and lights; saved with the patch. Default off.
- Off: each side feeds back into itself. A stereo input stays stereo.
- On: the average of the two inputs goes into the left side (so a mono
  source enters at its own level), the left side's output feeds the right
  side at full level, and the right side's output feeds back to the left
  through Feedback. Echoes alternate left, right, left.
- So at Feedback 0 there is one echo on the left and one, as loud, on the
  right; Feedback then sets how each left-right pair falls against the
  one before.
- With Ratio off noon, the bounce has an uneven, long-short rhythm.
- The change fades, with no click.

### Inputs, outputs and Mix

- Either input alone is a mono source to both sides (as Rigging and
  Ballast). Several channels on a cable are summed.
- **Mix:** dry to wet, equal power, as Rigging's. Default 35 %. CV: 10 V
  is the full travel, added to the knob.
- Every output has the family's soft limit (8 V knee, 10 V ceiling).
- With only L patched, L carries the mono sum of both sides, as on the
  other modules.
- Input samples are limited to ±24 V on the way into the delay, as
  Rigging's.

### Bypass and other

- Bypassed, the inputs pass to the outputs, with the same normalling.
- NaN or infinite input samples and CVs read as 0 V.
- A sample-rate change clears the delay and releases Freeze; a gate
  still high must fall and rise to freeze again (2026-11-25, #92).
- Initialize clears the delay and unfreezes.
- Saved: the knobs, Style, Freeze and Ping Pong. Not the delay's contents.

## Code and files

As every module (CLAUDE.md, TESTING.md "Adding a module"):

- `src/wake/`: Rack-free headers, the module a thin map onto them. Likely
  parts: the delay side (write, moving read, crossfaded read); the loop
  colour per style; the diffuser; the clock follower; the engine;
  `Wake.cpp`.
- **The buffer is shared code.** Recording into a long buffer and reading
  it at a moving, fractional position goes in a shared header under
  `src/` with its own tests, because Eddy and Spindrift will use it.
  `src/DelayLine.hpp` (Hermite reads) is the starting point; extend it or
  add beside it. Designing Eddy's or Spindrift's use of it is out of
  scope.
- No allocation on the audio thread. Ten seconds of stereo at high sample
  rates is large; the builder chooses when the memory is taken and what
  the longest time is above 192 kHz (it may be shorter there, and the
  manual says so).
- `src/FastMath.hpp` for anything per sample; no libm per sample.
- `src/wake/bench.inc` (idle, outputs, busy), the panel entry,
  plugin.json (slug `Wake`), manual `docs/wake/README.md`, README table,
  CHANGELOG, roadmap and `docs/future/wake.md` marked built.
- A three-light stepped button is new to the plugin; it is built from the
  existing cap button and jewel lights.

## Testing

TESTING.md applies in full. Particular to Wake:

- **Echo time.** An impulse in, at Feedback 0, Diffuse 0, Drift 0: the
  echo arrives at Time within 1 sample or 0.1 %, whichever is larger,
  across the range, in each style, at 44.1, 48 and 96 kHz.
- **Ratio.** Each step's two times against the table.
- **Clock.** A steady clock and each knob step give the stated multiple;
  a CV moves along the same steps and never between them; results over
  10 s are halved; a stopped clock holds the tempo.
- **Feedback.** At 50 % in Digital with Tone at noon, successive echoes
  fall by 6 dB within 0.5 dB.
- **Bounded.** Feedback at 110 % with ±10 V noise, a square at the echo's
  pitch, DC and NaN/inf, for minutes, in each style: output finite, under
  the limit, no DC building.
- **Styles.** A Time step in Tape and Analog gives a pitch glide that
  settles on the new time; in Digital the same step gives no pitch change
  (measured on a sine in the delay). Analog's bandwidth falls as Time
  lengthens. Digital's loop is flat within 1 dB from 30 Hz to 15 kHz at
  48 kHz with Tone at noon.
- **Tone.** Left lowers the spectral centroid of successive echoes; right
  raises it; noon leaves a Digital repeat's spectrum unchanged.
- **Diffuse.** At 0 a Digital repeat is a scaled copy of the echo before
  it. Above 0 a repeat's energy is spread over more time than the one
  before, with its level and arrival time inside the stated bounds.
- **Drift.** At 0 the echo time is constant to the sample. At full the
  two sides wander independently within the stated depth.
- **Freeze.** A frozen loop's RMS and spectrum after 5 minutes are within
  0.5 dB of the first pass (at Drift 0 it repeats exactly); the loop is
  as long as the echo time when frozen; input, Time, Ratio and Ping Pong
  changes during freeze do not alter it; the join and leaving freeze do
  not click.
- **Ping Pong.** A left-only impulse appears left, then right, then left;
  at Feedback 0 exactly one echo a side, equal in level; a mono source
  enters at the level it has with Ping Pong off; with it off, a
  stereo source's left never appears on the right.
- **No clicks.** Time by knob, CV and clock; Ratio steps; Style; Freeze;
  Ping Pong: no sample-to-sample jump beyond what the signal's own slope
  explains.
- **Stereo.** Mono compatibility; L-only input equals R-only input;
  bypass passes the input.
- Sanitizers, no allocation in process, extreme and changing sample rates,
  seeded fuzz, denormals in a decaying loop, bench budgets, no libm per
  sample, Linux and Windows builds.

## Listening stop

Before these are fixed, the user listens on a numbered test build, after
Rigging's Strum: each style's tone and how far apart the three sit; the
pitch glide's speed in Tape and Analog; Feedback's top; Tone's range;
Diffuse's amount; Drift's depth; the Ratio steps; the clock multiples; the
defaults.

## Left to the builder

- The delay's structure (interpolation, where the filters, saturation,
  diffuser and DC blocker sit in the loop), within the tests.
- How often controls are read, and the smoothing and fade times.
- The soft bound's curve (reuse Ballast's or Rigging's if it fits).
- The diffuser's stages and delays.
- Whether Analog has a faint hiss of its own; if it does it must be
  silent with nothing in the delay.
- When buffer memory is taken, and the longest time above 192 kHz.
- Lettering of the Time face and the Feedback scale within the
  generator's rules; the wake's arms.
- Bench budgets, from measurement.
