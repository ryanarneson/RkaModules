# Rigging: six tuned strings: design

Status: design, 2026-11-19. Panel: `2026-11-19-rigging-mockup.png` (drawn
by a draft entry in `tools/gen_panels.py`, marked MOCKUP). Grows from the
idea in `docs/future/rigging.md`.

## Intent

The user wants two things from Rigging, in their words: "definitely slow
tuned drone, but also struck and decay as hits/stabs in the mix."

Rigging is six tuned strings. Whatever sound comes in keeps them ringing
at their pitches: Hadal's Hiss into Rigging is a tuned drone, a second
voice from modules already built. A trigger at STRUM strikes the strings by
itself, so Squall's Gate into Rigging is a complete struck voice with
nothing in the audio input. Both happen at once, on the same strings, in
the same tuning.

The picture is wind singing in a ship's rigging: six brass stays from a
masthead to six knobs, each stay a string.

## Goals, non-goals, anti-goals

Goals:
- A steady, full tuned drone from noise or any audio, lasting as long as
  the input does and fading over up to a minute after it.
- A struck voice from a trigger alone: from a stab (all six at once, short
  Ring) to a slow rake that tolls for tens of seconds.
- Seven tunings for the six strings, chosen for dark, space and horror
  music (the user approved the list).
- Each string's level by hand, to voice the chord.
- In tune: with V/OCT, at every Damp and Ring setting.
- CV over Chord, Ring and Damp.

Non-goals:
- Bars, bells and other materials (the user chose strings only, after
  first asking for a Material control). True bells are Buoy's, later.
- Polyphony: one set of six strings; a polyphonic cable uses its first
  channel.
- A drone with nothing patched in and no strikes (no self-oscillation, no
  built-in wind). Hadal is the wind.
- Strike strength from the trigger's voltage; attenuverters on the CVs; CV
  for Spread, Mix or the string levels.
- Saving the strings' ringing with the patch.

Anti-goals:
- A level that runs away: a loud input with Ring at its longest must not
  blow up, clip harshly or grow without limit.
- A drone whose loudness swings wildly as Ring is turned.
- Clicks or zipper noise when Chord, Tune, V/OCT, Ring or Damp change, by
  hand or by CV.
- Strings audibly out of tune with each other or with other modules on
  the same V/OCT.
- A DC offset at the outputs.

## What the user decided

- Both uses, drone and struck (above).
- A strike is made inside Rigging, from a trigger alone, and spreads over
  the six strings low to high by a control (proposed, not objected to).
- The Chord list below ("looks good").
- Strings only (option 1 of three: strings; bars and bells; both with a
  Material control).
- The six string-level knobs stay.
- The panel as drawn in the mockup, 14 HP, with nothing changed: no
  attenuverters, CV on Chord, Ring and Damp only, the subtitle "SIX TUNED
  STRINGS".

## Approaches

1. **Strings (chosen).** Each of the six is a modelled string: a delay
   line fed back through a damping filter (Karplus-Strong, as a
   waveguide). Noise through it gives a tone with a full set of overtones;
   a strike is a pluck that mellows as it rings. Cheap, stays in tune over
   the range once the loop's own delay is accounted for, and is the picture
   on the panel. Every string is harmonic, so "Bell" is six harmonic notes
   at a bell's spacing: bell-like, not a true bell.
2. **Bars and bells.** Each "string" a small set of tuned ringing filters.
   Purer and glassier, true bell tones, but a thinner drone, more cost per
   string, and Buoy already has that ground.
3. **Both, with a Material control.** The widest range for about twice the
   sound work and one more control. The user picked this, then changed to
   1.

The reference for a string of this kind is Mutable Instruments' Rings
(`~/ws/plugins/eurorack/rings/dsp`, MIT; `string.cc`, `string.h`,
`plucker.h`, `strummer.h`, with `stmlib/dsp/delay_line.h` and
`filter.h` from its `stmlib` submodule): read for its method, not copied.
The checkout is upstream's `master` (f4cfa76, October 2026) with its
submodules. What its string does that bears on this design (read
2026-11-19; the Rings code has not changed in substance since 2020, nor
stmlib's DSP since 2023):

- **Decay and brightness are separate.** A gain per trip round the loop,
  worked out from the wanted decay time and the string's period, sets how
  long it rings; a low-pass sets how the overtones die. That is Ring and
  Damp here.
- **The low-pass follows the string's pitch** and never comes closer than
  two octaves above the fundamental (24 to 84 semitones above it), so the
  fundamental loses very little to it. That is how Damp can darken a
  string without killing its ring, and it bounds how pure "fully right"
  can be: harmonics from about the fourth up die fast, the first two or
  three stay.
- **The filters' own delay is taken off the string's length** (one sample
  for its three-tap filter, a table for its second filter): the tuning
  compensation this design asks for.
- **A Hermite read** of the delay (stmlib's is the same four-point
  interpolation as `src/DelayLine.hpp`), and no DC blocker in the plain
  string's loop.
- **Rings can strum itself**: with nothing in its trigger input it
  strikes when the note at V/OCT changes, or when it hears an onset in
  the audio. Rigging takes the first of these (the user, 2026-11-19; see
  "Strum and Spread"); it does not listen for onsets.
- **A pluck is noise for one period of the string**, low-passed, not a
  fixed few milliseconds: the burst's length follows the pitch.
- It also has stiffness (an all-pass in the loop that stretches the
  overtones, piano-like) and an endless-sustain setting. Neither is in
  this design; stiffness is the cheapest way to a "material" control if
  one is wanted later.

Beads (`~/ws/plugins/eurorack/beads`, added to that repository in October
2026) is a granular processor: nothing in it bears on Rigging. It is the
reference for Spindrift.

## Panel (14 HP)

As the mockup. From the top:

- Title RIGGING, subtitle SIX TUNED STRINGS.
- A masthead mark upper right; six brass stays from it to the six
  **string level** knobs, numbered **1** (the lowest string) to **6**,
  stepping down from left to right.
- **TUNE**, the hero, on an enamel face at the left, lettered in octaves
  (below); **V/OCT** under it at 83 mm.
- **CHORD**, **RING**, **DAMP** in a row, each with its **CV** jack under
  it at 83 mm and a traced line between.
- At 102 mm: **STRUM** (trigger in), **SPREAD**, the **strike light**,
  **MIX**.
- At 116 mm: **IN L**, **IN R**, and **L**, **R** out on ivory tiles.

Rows at 83, 102 and 116 mm line up with the other modules. The generator's
checks decide final positions; the mockup's are the starting point.

## Controls and behaviour

### Tune and V/OCT: the lowest string

- **Tune**: −24 to +24 semitones, snapped to semitones, 0 at **C2
  (65.41 Hz)**, as Fathom's Pitch. The face is lettered −2, −1, 0, +1, +2
  (octaves). The tooltip shows the note of string 1 ("C2").
- **V/OCT** adds 1 V per octave. Tune + V/OCT is string 1's pitch.
- Each string's frequency is clamped to 8 Hz at the bottom and to the
  lower of 12 kHz and 0.4 × the sample rate at the top; a string at the
  clamp stays there (it does not fold or fall silent).
- A change of pitch (Tune, V/OCT, Chord) glides: each string moves to its
  new pitch over about 30 ms (to be set by ear), so a ringing string bends
  to the new note without a click. A menu item for the semitone snap, as
  Fathom's **Free pitch**, is included.

### Chord: the other five strings

Seven settings, a detent each; the tooltip shows the name. Offsets are
above string 1, in semitones unless marked.

| Chord | Strings 1 to 6 | Sound |
|---|---|---|
| Unison | 0, 0, 0, 0, 0, 0, detuned −8, −4, 0, +3, +7, +11 cents | A thick single-pitch drone |
| Octaves | 0, 12, 12, 24, 24, 36, the doubled ones ±5 cents | Open and hollow |
| Fifths | 0, 7, 12, 19, 24, 31 | Dark, neither major nor minor |
| Minor | 0, 7, 12, 15, 19, 24 | Sad, settled |
| Sus | 0, 5, 7, 12, 17, 19 | Unresolved, spacious |
| Cluster | 0, 1, 2, 3, 4, 5 | Dissonant, horror |
| Bell | frequency ratios 1, 2, 2.4, 3, 4, 6 (a bell's hum, prime, tierce, quint, nominal and the next) | Metallic, for hits |

The default is **Fifths**. Detune amounts are to be set by ear at the
listening stop; the notes are fixed.

**Chord CV** adds to the knob: 10 V across the seven settings (a step
every 1.43 V), so ±5 V from the middle setting reaches both ends. The sum
is rounded to a setting, with a little hysteresis so a CV resting on a
boundary does not chatter.

### String levels

Six knobs, 0 to 100 %, default 100 %. Each scales its string's output
only (the string still rings and still takes its share of the input), so
a level brought up mid-drone fades in a string that is already sounding.
Smoothed against zipper noise.

### Ring: how long the strings sound

- The time for a string to fall by 60 dB after its input stops, with
  Damp fully left: **0.1 s** fully left to **60 s** fully right, on a
  logarithmic scale; default about 4 s. The tooltip shows seconds.
- The same time for every string, low or high (the feedback of each is
  set from its own pitch), where a string can do it (my call, from the
  reviewer's arithmetic):
  - a string cannot ring for less than a few of its own periods: below
    about 20 periods of Ring (0.3 s at C2, 1.2 s at 16 Hz) it is a thud of
    no clear pitch, and the stated time is not promised;
  - 60 s is promised for strings up to **1 kHz**; above that the string
    may fall short, but rings at least 10 s up to 2 kHz. (The knobs alone
    reach about 2.1 kHz: C4 with Bell's top string.)
- **Loudness under a steady input is kept roughly constant across Ring**:
  the input's share into each string falls as Ring rises, so turning Ring
  up makes the drone longer and smoother, not ten times louder. "Roughly"
  is within 6 dB over the whole of Ring, for white noise in, at the
  default Tune, Chord and Damp (my call: at heavy Damp a long Ring leaves
  little but each string's fundamental, which is honestly quieter; making
  that up would take 20 dB and more of gain and throw tuned inputs into
  the limiter).
- A strike is not scaled by that share: a strike at Ring 60 s is as loud
  at its start as one at Ring 1 s.
- **Ring CV** adds to the knob, 10 V across its travel.

### Damp: how the brightness dies

- A low-pass filter in each string's loop. Fully left the string stays
  bright for its whole Ring; turned up, the upper overtones die sooner,
  until fully right the string settles almost at once to a soft, near-pure
  tone. Default about a third.
- Damp also shortens the whole ring somewhat, as on a real string (my
  call, changed from "only the overtones": a filter that left the
  fundamental untouched at every pitch would need gain above 1 below it,
  which is unstable). Ring's stated time is the time at Damp fully left.
  At the default Damp a string up to 1 kHz keeps at least half of it; how
  far full Damp may shorten it is set by ear at the listening stop.
- Damp must not detune the string (the filter's own delay is taken off the
  string's length).
- **Damp CV** adds to the knob, 10 V across its travel.

### Strum and Spread: the strike

- A rising edge at **STRUM** (Schmitt trigger, 0.1 V / 1 V) strikes the
  strings: each gets a short burst of noise (about one period of that
  string, as Rings' pluck, its exact length and brightness to be set by
  ear; a little different every time,
  seeded per instance so two Riggings do not strike identically).
- **Spread** sets the time from string 1's strike to string 6's, with the
  others evenly between, always low to high: **0** fully left (all six at
  once, a stab) to **1.5 s** fully right (a slow rake), on a curve with
  fine control near zero; default about 60 ms.
- **With nothing patched to STRUM, a new note at V/OCT strikes the
  strings** (the user asked for this after seeing it in Rings): one cable
  from a sequencer plays Rigging. A new note is a jump of more than 0.4
  semitone (33 mV) within a millisecond; a slow glide or a gentle LFO on
  V/OCT does not strike. After a strike from a note, none follows until
  V/OCT has kept still for 10 ms, so a noisy, stepping or slewed edge is
  one strike. The Tune knob and
  Chord never strike. With a cable in STRUM, only STRUM strikes.
- A new trigger during a rake starts a new rake; strikes still to come
  from the old one are dropped.
- A strike is as loud as a healthy drone (my call on the numbers): one
  string struck alone peaks at about ±2.5 V; all six at once (Spread 0,
  every level up) at about ±6 V, under the soft limit's knee where it can
  be; the strings of a slow rake are each as loud as one alone.
- The **strike light** flashes on every trigger (about 0.1 s).
- Strikes do not depend on the audio input or on Mix's dry side: with Mix
  fully dry the strings are silent, strikes included.

### The audio input

- **IN L**, **IN R**: IN R is normalled to IN L and IN L to IN R, so one
  cable in either is a mono source (Ballast's rule).
- Strings 1, 3 and 5 are driven by L and 2, 4 and 6 by R. With a mono
  source all six get the same signal.
- Any signal works; noise gives the evenest drone.

### Outputs, stereo and Mix

- The six strings are placed across the stereo field at fixed positions,
  the lowest in the middle and the higher ones further out, alternating
  left and right (positions to be set by ear; string 1 is centred).
  Summed to mono (L + R) nothing cancels.
- **Mix**: fully left the dry input only, fully right the strings only,
  equal power between; **default fully right** (a drone from noise wants
  no noise in it).
- Level: white noise at ±5 V in, Fifths, every level up, Ring and Damp at
  their defaults, gives about ±3 V peaks out (as built and as the user
  heard it at the listening stop; first written as ±5 V). A soft limit on the string
  sum (as Ballast's and Fog's) keeps anything hotter under about ±10 V
  without a hard edge, and each string's loop is kept bounded by itself
  whatever the input.
- No DC at the outputs. (A DC blocker inside a string's loop detunes low
  strings, 17 cents at 16 Hz for a 1 Hz blocker, so it belongs outside
  the loop or its delay must be taken off the string's length.)
- Whatever bounds a string's loop must be exactly linear below its knee
  (as Ballast's curve), or long Rings come out short.

### Bypass

IN L to L and IN R to R (with the same normalling), so a bypassed Rigging
passes the dry sound.

### Other

- NaN or infinite input samples and CVs read as 0 V.
- A sample-rate change retunes the strings and clears them.
- Nothing is saved beyond the knobs and the Free pitch setting.

## Code and files

As every module (CLAUDE.md, TESTING.md "Adding a module"):

- `src/rigging/`: Rack-free headers, the module a thin map onto them.
  Likely parts: one string (delay, loop filter, tuning compensation,
  feedback from Ring, DC blocker); the strike (burst and rake timing); the
  chord table; the engine (controls every N samples, six strings, pan,
  mix, limiter); `Rigging.cpp`.
- `src/DelayLine.hpp` (shared, Hermite reads) is available; whether it or
  an all-pass fractional delay tunes better is the builder's choice,
  decided by the tuning test.
- `src/FastMath.hpp` for anything per sample; no libm per sample.
- `src/rigging/bench.inc` (idle, outputs, busy), panel entry, plugin.json
  (slug `Rigging`), manual `docs/rigging/README.md`, README table,
  CHANGELOG, roadmap and `docs/future/rigging.md` marked built.
- Mono in when both outs are unpatched: the engine may sleep, but a strike
  or a ringing string must not be lost when a cable arrives; simplest is
  to keep running (the strings are cheap).

## Testing

TESTING.md applies in full. Particular to Rigging:

- **Tuning.** For string pitches from 30 Hz to 2 kHz, at Damp 0, 0.5 and
  1, with Ring long enough for at least 200 periods, at 44.1, 48 and
  96 kHz: the measured fundamental (from a strike, by an independent
  method such as period counting over many cycles or a DFT peak with
  interpolation) is within **3 cents** of the wanted pitch. V/OCT of +1 V
  doubles it within the same. From 16 to 30 Hz: within 10 cents.
- **Chord table.** Each setting's six measured pitches against the table
  worked out independently in the test.
- **Ring.** At Damp fully left, the fundamental's measured T60 after a
  strike within 20 % of the tooltip, for strings from 30 Hz to 1 kHz and
  Ring from 20 periods of the string to 60 s; at 2 kHz, at least 10 s
  with Ring at 60 s. At the default Damp, at least half the tooltip for
  the same strings.
- **Constant loudness.** Steady white noise in, default Tune, Chord and
  Damp: output RMS across Ring within 6 dB.
- **Damp.** More Damp gives a lower spectral centroid half a second after
  a strike, and never a longer ring.
- **Strike.** A trigger alone gives sound, as loud at its start at Ring
  60 s as at Ring 1 s; Spread 0 strikes all six in the
  same sample; Spread at its top strikes them 0.3 s apart in order; a new
  trigger cancels the rest of a rake; no two strikes' bursts are equal.
  With STRUM unpatched: a step of a semitone on V/OCT strikes once, a
  1 V/s ramp and a 0.2 semitone step do not, and with STRUM patched no
  V/OCT change strikes.
- **No clicks.** A step on V/OCT, a Chord change and a fast Ring or Damp
  CV on a ringing string: no sample-to-sample jump beyond what the
  signal's own slope explains.
- **Bounded.** ±10 V noise, a ±10 V square at a string's pitch, DC, and
  NaN/inf, with Ring at 60 s for minutes: output finite, under the
  limiter's bound, no DC, and silence returns after the input stops.
- **Stereo.** Mono compatibility (L + R has every string); L-only input
  equals R-only input; bypass passes the input.
- Sanitizers, no allocation in process, extreme and changing sample rates,
  seeded fuzz, denormals in six decaying strings, bench budgets, no libm
  per sample, Linux and Windows builds.

## Listening stop

Before these are fixed, the user listens on a numbered test build: the
string's tone as a drone from Hadal's Hiss and when struck; the strike's
burst; Ring's range and the constancy of level; Damp's curve; the detunes
in Unison and Octaves; the Bell tuning; the stereo positions; the glide
time; Spread's range.

## Left to the builder

- The string's exact structure (interpolation, loop filter order, where
  the DC blocker and any saturation sit), within the tests.
- How often controls are read, and the smoothing times.
- The strike burst's shape; pan law and positions; limiter curve (reuse
  Ballast's or Fog's if it fits).
- Lettering of the Tune face within the generator's rules; whether the
  strike light also glows with the strings' level.
- Bench budgets, from measurement.
