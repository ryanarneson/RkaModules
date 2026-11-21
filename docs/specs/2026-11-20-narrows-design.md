# Narrows: a stereo filter: design

Status: design, 2026-11-20. Panel: `docs/future/narrows-mockup.png`
(approved by the user as a sketch; its generator entry is in "Panel"
below). Grows from the idea in `docs/future/narrows.md`.

*As built (2026-11-21):* four things differ from the text below; the
manual, `docs/narrows/README.md`, has the numbers as built.

- **Scale.** Inside, 28 V is 1.0, not 10 V. At the 10 V scale Fathom's
  ladder self-oscillates at under 1 V.
- **Res.** Fathom's square law up to feedback 4 at 95 % of the knob, then
  a straight line to 4.6 at the top (not 4.1 r², onset near 99 %). The
  sine is 4 to 5 V at full, not 4 to 9 V. The engine corrects the
  ladder's pitch above the onset.
- **Comb level.** The comb's input gain is sqrt(1 − f²), so noise keeps
  its level and the peaks stand up to about 16 dB over dry (the user,
  after listening: "let it go louder"), not "no more than about 6 dB".
- **Core parameter.** 1 is Ladder (the toggle up, the default), 0 is
  State.

## Intent

The user asked for a filter: "use Instruō I-ō47 as perhaps a starting
point. It should do LP/HP and band-pass with an optional comb if it can
fit in. Stereo if possible", and "I do like Moog filters as well, the
filter in the Fathom is pretty good."

The collection has no filter outside Fathom's voice. Narrows is the one
filter to put after anything in it: after Hadal's noise, Rigging's
strings, Wake's echoes, or on its own as a struck or self-oscillating
voice. It is stereo, and polyphonic (issue #64: new modules are
polyphonic from their design onward).

The picture is a narrows: the strait where a channel closes between two
shores, and only part of the water gets through.

## Goals, non-goals, anti-goals

Goals:
- Low-pass, band-pass and high-pass on one pair of outputs, with a Mode
  control that sweeps smoothly through them and takes a CV.
- Two characters on a switch: Fathom's ladder (24 dB per octave) and a
  state-variable filter (12 dB per octave).
- Resonance up to self-oscillation, in tune under V/OCT, so the filter is
  also a sine voice.
- A drive stage.
- A tuned comb that can be turned in ahead of the filter.
- A Ping input that strikes the filter.
- Width from one knob (Spread).
- Polyphony: a filter pair per channel, up to 16.

Non-goals:
- A notch response, and all responses out at once (the user: six output
  jacks were too many).
- A slope control: each core keeps its own slope.
- A comb with its own tuning or its own outputs.
- A fine-tune knob (the tooltip takes a typed frequency).
- CV on Drive or Spread; linear FM; separate CVs for left and right.
- A close copy of the I-ō47 or of any Moog filter.

Anti-goals:
- A click or thump when Mode, the core switch, Comb or the number of
  channels changes, by hand or by CV.
- Zipper noise when Cutoff moves.
- A level that runs away: Res, Comb and Drive all at their tops with a
  loud input must stay bounded and must not clip harshly.
- A self-oscillating filter that is out of tune.
- The two cores sitting at different pitches or resonances for the same
  knob positions, so that the switch jumps.
- A 16-voice patch that costs a large share of a core.
- An ordinary stereo patch paying for polyphony it does not use.
- Any change to how Fathom sounds.

## What the user decided

- A filter after the I-ō47, with low-pass, band-pass and high-pass, a
  comb, and stereo.
- Both cores: Fathom's ladder and a state-variable filter ("lets go
  both").
- Two output jacks and a Mode control, not six jacks.
- The layout of the mockup: the core switch beside the Cutoff dial; Mode
  and its CV on the bottom rows ("yep looks good").
- **Polyphonic stereo**: each channel of IN L pairs with the same channel
  of IN R and has its own filter pair; only the channels present are
  processed.
- **Mode sweeps smoothly** LP → BP → HP, and its CV sweeps freely.
- **The comb sits before the filter and is tuned to Cutoff.**
- The remaining behaviour below was proposed by the designer and accepted
  as a whole ("nothing. go for it"). Those points are marked *designer's
  call*.

## Approaches

Measured on the Mac at 48 kHz (a scratch bench, 2026-11-20): one stereo
pair of Fathom's ladder as it stands costs about 0.45 % of a core, so 16
voices would cost about 7 %. A stereo pair of the state-variable filter
costs about 0.02 %.

1. **Four filters at once (chosen; the designer's call, the user handed
   over the how).** The ladder's arithmetic is written once for a sample
   type that is either one float or four floats side by side. Fathom uses
   it with one float, unchanged. Narrows uses it with four: the left and
   right of two voices go through in one pass. A scratch version of this
   measured about 0.15 to 0.3 % for four ladders, so 16 voices come to
   roughly 1.5 to 2.5 %. The cost is a small new shared header for the
   four-wide type and care that Fathom's output stays bit-identical.
2. **Reuse the ladder as it stands, one filter at a time.** No new shared
   code, but about 7 % of a core at 16 voices on the Mac and more on the
   user's PC. Rejected on cost.
3. **Drop the 2× oversampling for Narrows, or cap the voices at 8.**
   Halves the cost either way. Without oversampling a driven or
   self-oscillating ladder aliases audibly; a cap drops voices silently.
   Rejected.

## Panel (14 HP)

As the mockup. Rows at 64, 83, 102 and 116 mm and five columns at 7.8,
21.7, 35.56, 49.4 and 63.3 mm, the same as Wake, so the two line up.

- **Cutoff**, the hero, on an enamel face lettered 16, 64, 250, 1K, 4K,
  16K, with HZ under it.
- **Core**, a two-position toggle to the right of the dial, lettered
  LADDER above and STATE below.
- Row of knobs at 64: **Drive**, **FM**, **Res**, **Comb**, **Spread**.
- Row of jacks at 83: **V/OCT**, FM's CV, Res's CV, Comb's CV, **PING**.
  The three CVs are joined to their knobs by a line and lettered CV.
- Bottom rows: **IN L** and **IN R** in the first column; Mode's CV in
  the second column at 108; **Mode** on a small face lettered LP, BP, HP
  at (42.5, 108); the **L** and **R** outputs on ivory tiles in the last
  column.

The generator entry that drew the mockup is saved as a patch at
`/private/tmp/claude-501/-Users-rarneson-ws/97691046-d5f6-488f-b6d4-8130759f4663/scratchpad/narrows_mockup_entry.patch`.
If that file is gone, the entry is rebuilt from this section and the
mockup. Two things in it are stale: its comment still speaks of three
stereo output pairs, and it is marked MOCKUP.

The shores are not drawn in the mockup (only three rings round the dial).
The builder may draw them if they fit the generator's rules without
crowding the lettering.

## Signal path

Per voice and per side, in this order:

1. **Input**, in volts, scaled so 10 V is 1.0 inside (a 5 V signal is
   0.5, the level Fathom's ladder tests use: the ladder's own tanh stages
   are still nearly straight there). The Ping pulse is added here.
2. **Comb**, at the engine's rate.
3. **Drive** gain.
4. **Up to twice the rate**, through the halfband pair Fathom uses.
5. **The core**, ladder or state-variable, with the Mode mix taken from
   its internal points. The ladder keeps its own soft limit at ±2
   inside, which is ±20 V and so only a backstop here.
6. **Down to the engine's rate**, the drive's make-up gain, and out in
   volts (×10).
7. **Output limit:** a soft limit that starts at 8 V and never passes
   10 V, the same curve as Wake's outputs. It sits after the downsampler,
   so the halfband's overshoot cannot carry the output past 10 V.

Both cores run at twice the rate (*designer's call*): both are driven and
both self-oscillate, and one path for the two keeps the switch simple.

## Controls and behaviour

### Cutoff, V/OCT and FM

- The knob runs **16 Hz to 16.4 kHz** over ten octaves
  (`16 × 2^(10 × knob)`), so the face's letters fall at equal steps.
  Default 1 kHz (knob 0.6). The tooltip shows hertz and takes a typed
  frequency ("440", "440 Hz", "2.5 k").
- **V/OCT** adds 1 V per octave, per voice. 0 V is the knob's frequency.
- **FM** is an attenuverter on the FM input's voltage, default 0: fully
  right is +1 V per octave, fully left −1 V per octave.
- Spread (below) adds its offset per side.
- The sum is held between 8 Hz and the lower of 20 kHz and 0.45 of the
  sample rate.
- The cutoff is worked out for every sample, so V/OCT and FM work at
  audio rate. The knob is smoothed (about 5 ms) so turning it does not
  zipper; the jacks are not smoothed.

### Core: Ladder or State

- **Ladder** (default; the user likes Fathom's filter): Fathom's
  four-pole transistor ladder. Low-pass is 24 dB per octave.
- **State**: a state-variable filter, 12 dB per octave, with a soft clip
  in its resonance path so it self-oscillates at a bounded level, and a
  soft clip on its input so Drive has something to push into. Fathom's
  `Svf.hpp` is linear and is the starting point for the maths, not the
  finished core.
- The switch **crossfades** over about 30 ms. Only the selected core
  runs, except during the fade.
- The incoming core starts from rest but must not leave a gap: a
  self-oscillating voice with no input is back within 3 dB of its level
  inside 100 ms of the switch, at 100 Hz and above. Left alone it would
  take seconds to grow from its noise source, so the incoming core is
  kicked (for example by feeding it the outgoing core's output during
  the fade). Its phase may differ; it must not click.
- **The two cores are matched** (*designer's call*):
  - the same Cutoff gives the same pitch in self-oscillation, within the
    tuning limits below;
  - both begin to self-oscillate at the same Res position;
  - a self-oscillating sine comes out at about the same level from both
    (within 3 dB, and inside the 4 to 9 V under Res);
  - at Res 0 in low-pass, the passband gain of both is unity.

### Mode: LP, BP, HP

- The dial sweeps over a half turn: **LP** at the left end, **BP** at the
  middle, **HP** at the right end. Default LP.
- **CV:** 10 V is the dial's full travel, added to it and held to the
  ends. A polyphonic CV sets each voice.
- At the three lettered points the response is the pure one. Between
  them the output is a straight blend of the two neighbouring responses:
  - **State:** a weighted sum of the filter's low-pass, band-pass and
    high-pass outputs. Halfway between two points each has weight 1/2.
  - **Ladder:** the weights on the ladder's five internal points slide
    in a straight line between the rows Fathom already uses: low-pass 24
    (0, 0, 0, 0, 1), band-pass (0, 0, 4, −8, 4) and high-pass
    (1, −4, 6, −4, 1). The ladder's passband make-up (which Fathom
    applies in low-pass only) scales with the low-pass weight.
- The in-between shapes differ between the two cores. That is accepted
  (the user chose the smooth sweep knowing it).
- The dial and the CV are smoothed (about 5 ms), so a stepped CV does not
  click.
- The tooltip names the place: "LP", "BP", "HP", or the blend, for
  example "LP 60 % / BP 40 %".

### Res

- 0 to 100 %, default 0. The ladder's feedback is 4.1 × Res², the law
  Fathom's RES knob uses (`src/fathom/DroneVoice.hpp`), so the two feel
  alike; self-oscillation is at a feedback of 4, which is about 99 % of
  the travel. The state core begins at the same position.
- **CV:** 10 V is the full travel, added to the knob and held to 0 to
  100 %.
- In low-pass the passband keeps its level as Res rises (Fathom's ladder
  does this already; the state core must as well).
- **Tuning:** a self-oscillating filter follows V/OCT within 5 cents
  from 30 Hz to 4 kHz at 44.1 kHz and above, in both cores, at the onset
  of self-oscillation, and within 15 cents from there to Res 100 %.
  Fathom's ladder alone is about 10 cents out
  (`src/fathom/test_ladder.cpp`) and its pitch falls a little as the
  oscillation grows. Any correction is an offset to the cutoff made in
  Narrows; the shared ladder maths does not change.
- **Level:** a self-oscillating sine comes out between 4 and 9 V peak.
- Self-oscillation starts from silence (the ladder already has a tiny
  noise source for this; the state core needs the same). The noise is
  one stream per voice, shared by its two sides.

### Drive

- 0 to 100 %, default 0. At 0 the filter is clean for signals up to
  ±5 V: in the passband at Res 0, a 5 V sine comes out within 1 dB of
  the small-signal gain and with under 3 % distortion. Rising, it is a
  gain of up to +24 dB into the core's saturation.
- The output is brought back down as Drive rises, so the sound gets
  dirtier but not much louder: a 5 V sine in the passband comes out
  within 6 dB of its level at Drive 0, across the whole travel.
- The gain is smoothed.

### Comb

- A feedback comb ahead of the filter: a short delay fed back on itself.
  The knob is **off at noon** (default). To the right the feedback is
  positive, which puts peaks at the cutoff frequency and its harmonics
  (a pipe-like tone). To the left it is negative, which puts the peaks
  between those (hollower, and an octave lower in pitch).
- **Tuning:** the delay is one period of that side's cutoff, so the comb
  follows the Cutoff knob, V/OCT, FM and Spread. Its pitch stops rising
  where the delay would be under 2 samples, and it reaches down to 8 Hz.
- **Amount:** the feedback runs to ±0.95 at the ends, on a curve that
  gives the middle of each half useful travel. It never reaches 1, so
  the comb cannot run away.
- **Level:** the comb's input is turned down as the feedback rises, so
  a peak does not come out more than about 6 dB above the dry level.
- **CV:** ±5 V is the full travel each way, added to the knob.
- At exactly noon (knob and CV summing to 0) the comb is out of the path
  and the signal is bit-exact. The delay is still written, so turning
  the comb in does not click.
- A jump in the delay (a stepped V/OCT) is slid over a few milliseconds,
  so it bends and does not click.

### Spread

- 0 to 100 %, default 0. The left cutoff moves down and the right up by
  the same amount: half an octave each way at full.
- At 0, with the same signal on both sides, left and right are
  identical to the bit.
- The comb follows each side's cutoff.

### Ping

- A rising edge at PING (0.1 V / 1 V Schmitt trigger) adds one short
  pulse to the input of both sides of that voice: about 1 ms long, a
  smooth single hump, as loud as a 5 V input.
- It goes in before the comb and Drive, so with Res up the filter rings
  at the cutoff and dies away, and with Comb up it sounds plucked.
- It needs no audio patched.
- A one-channel PING cable strikes every voice. A polyphonic one strikes
  each voice from its own channel.

### Voices

- The number of voices is the most channels on **IN L, IN R, V/OCT or
  PING**, and at least 1.
- FM, Res, Comb and Mode CVs: a one-channel cable applies to every
  voice; a polyphonic cable sets each voice from its own channel, and a
  voice past its last channel reads 0 V (Rack's usual rule).
- **IN L alone** feeds both sides; so does IN R alone.
- **A one-channel audio cable feeds every voice** (Rack's usual rule):
  one source into a chord of filters set by a polyphonic V/OCT. A
  polyphonic audio cable with fewer channels than there are voices
  gives silence to the voices past its last channel.
- **Outputs** carry one channel per voice. With nothing in the R output,
  L carries the average of the two sides of each voice, as on Wake.
- A voice that is added starts from rest; a voice that is removed stops.
  Neither may click in the voices that remain.
- Only the voices present are computed. Voices go through the filter
  four sides at a time (two voices), so 1 or 2 voices cost one pass and
  16 cost eight.

### Bypass and other

- Bypassed, IN L passes to L and IN R to R, with all channels; either
  input alone goes to both.
- What goes into the filter is held to ±24 V. NaN and infinities at any
  jack are treated as 0.
- A change of sample rate, and Initialize, bring every voice to rest.
- Saved with the patch: the knobs and the Core switch. Nothing else;
  there is no menu item.
- plugin.json tags: Filter, Polyphonic, and the stereo tag the other
  stereo modules use.

## Code and files

Follows the collection's pattern: a Rack-free engine in headers, a thin
module, tests beside the engine.

- **`src/Float4.hpp`** (new, shared): four floats side by side, built on
  the compiler's vector type (GCC and Clang both have it; those are the
  three toolchains here). It needs only arithmetic, min, max, a
  broadcast and per-lane access. No Rack headers and no platform
  intrinsics.
- **The ladder's arithmetic, shared.** The stage maths of
  `src/fathom/Ladder.hpp` and the all-pass cascade of
  `src/fathom/Halfband.hpp` are written once for a sample type, and
  hand back the ladder's five internal points so the caller mixes them.
  Fathom's `Ladder` keeps its interface and its four mixes, and must
  give **bit-identical output** to today's. Fathom's existing reference
  test only bounds a difference, so the first step freezes a copy of
  today's `Ladder.hpp` and `Halfband.hpp` as a test reference and
  compares the new ones to it exactly, on the Mac and on Linux. (Tests
  do not run on Windows; it builds the same source.) Where the shared code lives
  (`src/` or `src/fathom/`) is the builder's choice.
- **`src/narrows/`**: tuning constants, the state core, the comb, the
  ping pulse, the mode mix, and `NarrowsEngine.hpp`, which takes a frame
  of per-voice voltages (as Sway's and Sluice's engines do) and returns
  per-voice left and right. `Narrows.cpp` only maps params, ports and
  channels onto it.
- The comb's delays are `rka::DelayLine`s, one per voice and side,
  allocated for the longest delay (one period of 8 Hz) at the current
  sample rate, off the audio thread.
- `bench.inc`, tests, `tests.mk`, Makefile `MODULES`, `plugin.json`
  (slug `Narrows`), `src/plugin.hpp`, `src/plugin.cpp`, the manual in
  `docs/narrows/README.md` with a screenshot, `README.md`,
  `CHANGELOG.md`, the roadmap and `docs/future/` pages, and
  `TESTING.md`'s table and budget rows: as every module.
- No libm per sample with the knobs still (TESTING.md). The per-sample
  cutoff uses the fast exponential in `src/FastMath.hpp` and the
  ladder's rational tangent.

## Testing

By `TESTING.md`; every test is shown to fail by a mutation. Beyond the
usual set (hostile inputs under the sanitizers, sample rates 44.1 to
768 kHz, no allocation on the audio path, golden saved state, Linux and
Windows builds):

- **Fathom unchanged:** the new ladder and halfband equal the frozen
  copies to the bit, in all four of Fathom's mixes; Fathom's existing
  tests pass untouched.
- **Four-wide against one-wide:** each lane of the four-wide ladder
  equals the one-float ladder given the same input and settings.
- **The pure responses:** at LP, BP and HP in each core, the measured
  response has the right shape and slope (24 or 12 dB per octave for the
  low-pass; the band-pass peaks at the cutoff; the high-pass mirrors).
- **Mode's sweep:** continuous (no step in the output as Mode moves),
  and exactly the pure response at the three points.
- **Core match:** self-oscillation pitch, onset position and level agree
  between the cores within the limits above.
- **Tuning:** self-oscillation within 5 cents over 30 Hz to 4 kHz, at
  44.1, 48 and 96 kHz.
- **Bounded:** Res, Comb and Drive at their tops with a ±10 V input stay
  within ±10 V at the outputs and hold no DC.
- **Comb:** bit-exact at noon; peaks where they should be either way;
  peak level within its limit; no click on engaging.
- **Spread 0:** left equals right to the bit.
- **Ping:** rings at the cutoff with Res up; one edge, one pulse.
- **Voices:** each voice is independent of the others (a voice's output
  is the same alone as among 16); changing the count leaves the
  remaining voices' output unbroken; mono CVs reach every voice.
- **No clicks:** core switch, Mode steps by CV, Comb through noon.
- **Bench cases:** one voice Ladder, one voice State, 16 voices Ladder
  with everything moving. Budgets by TESTING.md's rule from the
  measurements. Expected from the scratch bench: about 0.3 % or less for
  one or two voices in Ladder and about 2 % for 16.

## Listening stop

One stop, when the engine plays in Rack and before the manual is
written. The user listens for, and the builder then fixes:

- how well the two cores match on the switch;
- the in-between sounds of Mode in each core;
- Drive's range and its make-up;
- the comb's curve and level, both ways;
- the Ping pulse's length and loudness;
- Res near the top: the onset of self-oscillation and its level.

## Left to the builder

- The state core's nonlinear design (where the soft clips sit, how its
  damping maps from Res) within the matching rules above.
- The comb's feedback curve, its input scaling and whether its loop
  carries a gentle low-pass.
- The exact shape of the Ping pulse and of Drive's make-up curve.
- Smoothing times, the crossfade's shape, and how unused lanes in a
  four-wide pass are kept quiet.
- Whether the shores are drawn.
- The name "Narrows" was searched for among VCV Library modules on
  2026-11-20 and nothing was found, but the search was not conclusive.
  Check the Library itself before release.
