# Doldrums: freeze and stretch: design

Status: built, 2026-11-26 (see below). Designed 2026-11-26, approved by
the user ("yes approved"). Panel: `2026-11-26-doldrums-mockup.png`,
drawn through `tools/gen_panels.py` from a draft description that is not
in the repo; its positions are listed under Panel. Grows from the idea
and the PaulXStretch notes in `docs/future/doldrums.md`.

(2026-11-26: built, on branch `doldrums`; not yet heard by the user, whose
listening stop comes with the first test build. Notes marked "As built"
below say where the build differs from or settles what this design says,
from the build's ledger (`.superpowers/sdd/2026-11-26-doldrums/progress.md`);
the manual is `docs/doldrums/README.md`, the listening guide
`docs/doldrums/listening.html`, the technical reference
`docs/doldrums/technical-reference.md`.)

Lines marked *(call)* are the designer's choices, not things the user
asked for; the user saw most of them in conversation and let them stand.

## Intent

Doldrums holds a moment of sound for as long as it is wanted, or draws a
stretch of sound out to many times its length: the smeared, glassy sound
of PaulStretch. Nothing in the collection does this, and the roadmap
calls it central to dark ambient, which is the music the user makes.

It is a ship becalmed: the sound stops going anywhere.

Three uses shaped it, in the user's words or agreed with them:

- **Hold what is playing.** A chord from Fathom or a strike from Rigging,
  frozen and left sounding under the piece.
- **Draw recent sound out.** The last half minute is always on a tape;
  any moment of it can be found again, held, or crept through slowly.
- **Make long loops from short clips.** "load a clip from one Eddy,
  stretch it out via doldrums, then record it back into another eddy,
  looping." This is why there is a RUN gate and a play-once setting.

And one addition the user asked for before the design began
(`docs/future/doldrums.md`): **Harmonics**, a comb in the spectrum that
keeps only the energy near a note and its overtones, so that noise or a
field recording comes out as a tuned drone.

## Research, in short

- **The method** is PaulStretch's (Paul Nasca), read in PaulXStretch on
  2026-11-23: take a long slice of sound (a *grain* in this document),
  keep how loud each frequency is, throw the phases away, give every
  frequency a fresh random phase, turn it back into sound, and overlap
  the results. The random phases are the sound. It is not a phase
  vocoder.
- **Its code is not used.** PaulXStretch's core is GPL-2.0-only, which
  cannot go into this GPL-3.0-or-later plugin. The method is public and
  is written here afresh. The builder does not copy from
  `~/ws/plugins/paulxstretch`.
- **The FFT** is pffft, which Rack's SDK already ships (`pffft.h`, in
  `libRack`); nothing is vendored.
  (As built, 2026-11-26: pffft is vendored, unchanged, in
  `src/dep/pffft/` (upstream commit 67a4a6a, provenance in its
  `README.md`), its symbols renamed `rka_pffft_*` by `rka_prefix.h` so they
  cannot meet libRack's copy. The SDK ships only the header; the code is
  inside libRack, which the offline tests and CI cannot link, and this way
  the plugin and the tests run the same transform. Licence FFTPACK's,
  BSD-like, compatible with GPL-3.0-or-later.)
- **Cost, measured 2026-11-26** on the development Mac (Apple M5 Pro),
  one real forward transform through `pffft_transform_ordered`:
  10 µs at 16384 points, 28 µs at 32768, 56 µs at 65536 (medians; worst
  of 400 runs under 85 µs). A grain needs four transforms and some passes
  over the bins, about six times a second. The 2026-11-23 notes assumed a
  worker thread would be needed; the measurement says it is not.

## Goals, non-goals, anti-goals

**Goals**

- The PaulStretch sound, in stereo, from live input, with no clicks
  anywhere: every change the module makes to what is heard is a dissolve.
- A tape of the last 30 s with a window on it, so a moment can be found
  by eye and by ear.
- Stretch from 1 to a true stop on one dial.
- Harmonics: a tuned comb, playable from V/OCT, a chord from a
  polyphonic cable.
- One pass on a trigger with a gate that says when it runs, so another
  module can record it.
- A frozen tape comes back with the patch.
- Least astonishment (the user's standing rule): what the window, the
  lights and the tooltips say is what is heard, in every state. Each
  state named in this document has to be visible on the panel.

**Non-goals**

- Loading or saving files. Eddy does both, and Eddy into Doldrums into
  Eddy is the route. *(call; the user heard it and did not object)*
- A Length control or a second end point. The stretched part runs from
  Position to the end of the tape. *(call, accepted "for a start")*
- CV on Blur, Tilt, Shift, Partials, Width and Mix. The user: "leave it
  with the jacks as presented."
- Polyphonic audio. Stereo in, stereo out. Only V/OCT is polyphonic.
- Pitch-preserving time stretch of rhythmic or transient material. This
  method smears every attack; that is what it is for.
- A spectrum display. The window shows the tape.
- Low latency. The wet sound is about one grain late, and the manual
  says so.

**Anti-goals** (it would be a failure even if it worked)

- A click, a step or a level jump at Freeze, at a return to Position, at
  a V/OCT step, at a grain-size change or at a tape arriving.
- A state the panel does not show: stopped after a single pass, held at
  infinity, frozen, with nothing in the window to say so.
- Output that differs from run to run for the same input and seed, or
  between real time and an offline render.
- Work on the audio thread that arrives in one lump big enough to
  crackle on a slow machine.

## What the user decided

- It listens to recent material, not only to the present: a tape with a
  Position control ("2").
- The behaviour under "Controls and behaviour" below, as first described
  ("looks good for a start").
- The jacks as drawn: CV on Stretch, Position and Harmonics; V/OCT; the
  Freeze gate; TRIG. "CV" lettered beside each CV jack. A real infinity
  sign on the dial.
- The RUN gate output and "play once" in the menu ("yes").
- The engine does its work on the audio thread, in stages ("1").
- Harmonics is in (2026-11-23).

## Approaches

Three ways of building the engine were put to the user; the sound and
the controls are the same in each.

1. **On the audio thread, in stages** (chosen). Each grain's work is cut
   into steps, done on successive samples. The same output every run, no
   threads to race, the same offline as live. A small state machine to
   write and test.
2. **On a worker thread**, as the 2026-11-23 notes proposed. Flat load
   on the audio thread on any machine, at the cost of lock-free
   hand-offs, a rule for a late grain, and tests that depend on timing.
   The measurement above says the load it would move is small.
3. **On the audio thread, all at once.** The simplest code; every grain
   lands on one sample. Fine on a fast machine, a possible crackle on a
   slow one at 96 kHz with the long grain.

Chosen: 1, for 3's predictability without its lump and without 2's
threads. A worker is still used, as in Eddy, for the one job that must
not run on the audio thread: reading and writing the tape's file and
converting its rate.

## Panel (16 HP)

As the mockup, which passes the generator's checks at these positions
(mm; columns are Eddy's: 7.8, 24.22, 40.64, 57.06, 73.48):

| | Column 1 | Column 2 | Column 3 | Column 4 | Column 5 |
|---|---|---|---|---|---|
| window | TAPE: x 5.0, y 19.5, 71.28 wide, 14.0 high, ruler beneath in 320ths | | | | |
| 48 | Position | | Stretch, the hero, at y 54, face r 16 | | Harmonics |
| 61 | its attenuverter | | | | its attenuverter |
| 73 | its CV jack | Blur (74.5) | Stretch's attenuverter (78) | Root (74.5) | its CV jack |
| 88 | Tilt | Shift | Stretch's CV jack (90) | Partials | Width |
| 102 | IN L | Freeze (cap) | Mix (atten size, 104) | V/OCT | L (tile) |
| 116 | IN R | GATE (freeze) | TRIG | RUN (tile) | R (tile) |

- Stretch's face: 36 ticks, long every 6; lettered 1, 2, 4, 8, 16, 32 at
  -135, -90, -45, 0, 45, 90 degrees, and an infinity sign at 135.
- **The infinity sign is drawn, not set in type**: D-DIN Condensed has
  no such glyph, and its 8 laid on its side read as a tilted 8 beside the
  real 8 at noon. The mockup draws a figure of eight on its side, centred
  on the words' radius at 135 degrees and turned 135 degrees so its long
  axis follows the rim, 2.8 mm long and 1.7 mm high, stroke 0.32 mm in
  the ink, no fill. The generator can only letter a face today; it needs
  a small addition for a drawn mark on a face, with the mark counted in
  the clearance checks and covered by `tools/test_gen_panels.py`.
- "CV" in the dim colour beside each of the three CV jacks (to the
  right of the left and centre ones, to the left of the right one),
  because the names of Tilt, Width and Mix sit directly beneath.
- Traces: each CV jack up through its attenuverter to its knob.
- Subtitle: FREEZE & STRETCH.
- The Freeze cap is lit while the tape is frozen, by the button or the
  gate.
- The mockup's positions are the starting point; the builder may adjust
  them to pass the checks, and shows the user the result if anything
  moves by more than a millimetre or two.

## Controls and behaviour

### The tape

- Stereo, 30 s at the engine's sample rate, always recording the input
  unless frozen. Silent when the module is added.
- It is a ring: the newest sample is "now", the oldest is 30 s ago.
- **Freeze** (the button, which latches, or a high gate at GATE; either
  one freezes *(call)*) stops the recording. The 30 s stay as they are
  until Freeze ends, when recording goes on from the same place.
- Recording fades out and in over a few milliseconds at Freeze's edges,
  so no step is written to the tape. *(call)*
- IN R unpatched takes IN L.

### Grains

- A grain is the slice of tape that is turned into sound at one time.
  Its length is the **Grain** setting in the right-click menu: Short,
  Medium (the default) or Long, about 0.17, 0.34 and 0.68 s. In samples
  it is the power of two nearest that time at the engine's rate (8192,
  16384 and 32768 at 44.1 and 48 kHz; twice those at 88.2 and 96 kHz),
  capped where the builder finds it must be. *(call; in PaulStretch the
  grain's length is the setting that most changes the character)*
  (As built, 2026-11-26: capped at 131072 samples, so above 192 kHz Long
  is shorter than 0.68 s; at 384 kHz and up Medium and Long are one size.
  The windows: Hann to take a grain, the sine window to lay it down (its
  squares at half a grain apart sum to 1, so noise has no ripple at the
  hop's rate: measured 0.09 to 0.18 dB).)
- A new grain starts every half grain, and each overlaps the last by
  half. Because every grain has its own random phases, two grains never
  line up, and the overlap is a crossfade between two unrelated sounds:
  the windows are chosen so that the loudness stays steady through it.
- **Everything that changes between one grain and the next is therefore
  a dissolve over half a grain**, with nothing more to build: a jump of
  the read point, a new comb, a new Tilt. This is what "dissolve" means
  everywhere below.
- For each grain and each side: the slice is taken from the tape at
  whole samples (never between them), windowed and transformed; only the
  loudness of each frequency is kept; Shift, Blur, Tilt and the comb act
  on those, in that order; each frequency is given a random phase; the
  result is transformed back, windowed and added to the output.
- Frequencies under about 16 Hz are dropped, so random phases there
  cannot make the output wander. *(call)*
- **Stereo.** By default the two sides share their random phases and
  keep the difference in phase they had on the tape, so a sound in the
  middle stays in the middle and a sound to one side stays there. The
  menu's **Wide** gives each side its own random phases: any source,
  mono included, fills the whole width. *(call; for the listening stop)*
- The random numbers come from a small seeded generator in the engine.
  A module takes its seed from Rack when it is added; a test gives its
  own. Same seed and same input: same output, to the bit.
- Silence in gives exact silence out once the last grain has ended.

### The read point, Position and Stretch

The read point is the end of the slice that the next grain will take.

- **Position** says where a pass starts: fully right is now, fully left
  is as far back as a grain can still be taken whole (30 s less one
  grain). Call that place the anchor. Default: fully right. Its CV, at
  full attenuverter, crosses the whole tape with 10 V.
  (As built, 2026-11-26, the user's decision at the first test build:
  the default is 0.9, about 3 s back (2.97 s at Medium), not fully right.
  At fully right a fresh freeze holds and leaves Stretch nothing to act
  on, which read as Stretch doing nothing. So at the defaults the module
  plays about 3 s or more behind the input, and Freeze loops a stretch
  of the last 3 s (about 24 s at x8) rather than holding the instant; the
  plain hold is Position fully right, and the manual leads with the new
  default and then the hold. Only the default changed: range, law, id
  and saved state are as before.)
- **Stretch** says how fast the read point moves on through the sound
  from there: at 1 it moves a second of tape each second, at 8 an eighth
  of a second each second, at infinity not at all.
- **Turning Position moves the read point by the same amount, at once**
  (at the next grain), wherever it has got to. It does not start the
  pass again. If that would take it off the tape it stops at the edge.
- **TRIG** puts the read point back on the anchor and starts a new
  pass, at any time: in mid pass, held, or stopped.
- **Freeze going on also starts a new pass from the anchor.** Without
  this the read point, which may have fallen up to 30 s behind, would
  have to creep to the end of the tape before the frozen moment was
  reached. Freeze going off changes nothing: the read point goes on
  from where it is. *(call, after the builder's read of this document)*
  (As built, 2026-11-26, after the final review, superseding the build's
  rulings: in *stop*, Freeze going on is one rule, whatever was happening
  before. With a pass to travel it ends any pass and the module waits,
  STOPPED and silent with RUN low, for TRIG; a pass running at the time
  does not go on. With nothing to travel through (the anchor within a
  grain of the end of the frozen tape, as at Position fully right) it is
  held and sounds. Frozen with nothing to travel through is held whenever
  it is true: at a freeze, when a patch opens, and when Position is turned
  fully right while stopped, which wakes it into the hold. This meets both
  "stays silent until a trigger at TRIG" and "held ... with either
  setting", gives the Eddy patch a clean RUN (nothing sounds or gates
  between the freeze and the trigger, with no wait for a pass to end),
  and makes the same press do the same thing whatever came before. TRIG on
  the freeze's grain still starts a pass. In *go round* Freeze always
  starts a pass from the anchor. Freeze going on while held at the
  Stretch stop starts a pass from the anchor like any freeze in go round
  (the held slice is replaced by the anchor's), and in stop, with a pass
  to travel, stops.)
  (As built, 2026-11-26, the user's ruling at the first test build,
  superseding "Freeze going off changes nothing": **a thaw returns the
  read point to the anchor at once**, a new pass from Position by the
  usual dissolve, whenever the module is sounding (running or held), in
  either setting. With Position fully right that is straight back to the
  live input. A module that is STOPPED stays stopped at a thaw: no pass,
  no RUN, so a recorder gated by RUN is not restarted. A thaw that starts
  a new pass while RUN is high dips it for 1 ms, as any new pass does.
  Held at the Stretch stop, the thaw takes the slice at the anchor once
  and holds it there, on a tape now recording. The first grains after a
  thaw at Position fully right hold the frozen tape's last frames as well
  as the new input (it is what "now" holds); at Stretch 1 the new input
  fills the slice within a grain, at Stretch s within about s grains.)
- **Frozen**, the tape stands still and the read point travels right,
  towards now. When it arrives at the end of the tape, the pass is over
  and it returns to the anchor and goes again.
- **Not frozen**, the read point still moves on through the sound at the
  same pace, but new sound is arriving at full pace, so it falls behind
  now, and in the window it drifts left. When the oldest sound, about to
  be recorded over, catches up with it, the pass is over and it returns
  to the anchor. At Stretch 1 it keeps pace and never returns: the
  output is a smeared copy of the input, late by Position's distance and
  a grain.
  (As built, 2026-11-26, the user's observation at the first test build:
  at Stretch exactly 1, recording, the read point is on the anchor. Built
  first as "keeps pace from wherever it is", Stretch turned up and back
  to 1 left the module late by any amount for ever with the dial at x1;
  now, if at pace 1 the read point is not on the anchor at a grain, it
  returns there (a new pass, once) and keeps pace on it. Frozen, x1
  travels from where it is; paces under 1 are unchanged.)
- **While recording: Drift or Follow** (a menu item).
  (Added 2026-11-26, at the user's request on listening to the first test
  build: "I like the evolving, but the pXs way is intriguing as well";
  the names are theirs. **Drift**, the default, is every rule above: a
  tape not frozen is still stretched, the read point falling behind and
  returning to the anchor. **Follow** is PaulXStretch's way, capture
  first and stretch after: while the tape records the read point is on
  the anchor at every Stretch, its stop included (as at Stretch 1 above),
  so the output is the input smeared, late by Position's distance and a
  grain, and Stretch acts only once the tape is frozen. Arriving there
  from elsewhere (Follow chosen with the read point behind, or held) is
  one return to the anchor, a new pass, once; Stretch's stop does not
  hold while recording in Follow (to hold, Freeze); a stopped module
  stays stopped; TRIG, Freeze, a thaw and a press in the window keep
  their rules, so a thaw in Follow returns the read point to the anchor
  and it plays on from there at the input's own speed, at any Stretch:
  first Position's distance of the tape from before the freeze, then the
  new input (with Position fully right, the live input within a grain);
  Drift chosen while following goes on from the anchor and falls
  behind at the pace, with no new pass. Frozen, the two are the same in
  every respect. The window says FOLLOW while following, so the panel
  says why Stretch does nothing, and Stretch's tooltip adds "(waits for
  Freeze)". Saved as "whileRecording", 0 Drift or 1 Follow; a patch
  without it opens in Drift.)
- **With Position fully right and the tape frozen there is nothing to
  travel through**: the anchor is the end of the tape. The module is
  then **held**, exactly as it is with Stretch at its stop (below), at
  any Stretch and with either setting of "At the end of a pass": the
  moment at which Freeze was pressed sounds until something changes, RUN
  is high, and the window says held. The same goes for any pass that
  would be shorter than one grain. This is the plain "hold what is
  playing" use and the manual leads with it. *(call)*
  (As built, 2026-11-26: the same is NOT true of a tape that is not
  frozen with Position fully left, which has less than a grain to travel
  before the oldest sound catches it (the plan's second deliberate
  difference): such a pass takes its slice at the anchor every grain, so
  the output follows the input about 30 s late at any Stretch; it is not
  held (the window says nothing), it never ends, even in stop, and RUN
  stays high. For the listening stop.)
  (As built, 2026-11-26: frozen, a turn of Position far enough right to
  push the read point past the end of the tape ends the pass at once, with
  no grain at the very edge. For the listening stop.)

**The Stretch dial.**

- Over the first five sixths of its travel (-135 to 90 degrees) it
  doubles every 45 degrees: 1, 2, 4, 8, 16, 32, as lettered. Default: 8.
- In the last sixth (90 to 135 degrees) it runs from 32 to a true stop: through 64 and 128
  with room to set them, rising without limit, and at the end of the
  travel the read point does not move at all. A CV that pushes past the
  end is also a stop. The law in this stretch is the builder's.
  (As built, 2026-11-26: with u = 6t - 5 over the last sixth, stretch =
  32 / (1 - u)^2, written as pace = 1.125 (1 - t)^2 so it is exactly 0 at
  the end: 64 at u = 0.293, 128 at u = 0.5. The Listening stop below says
  "last eighth"; it is this last sixth.)
- The tooltip gives the figure ("x8.0", "x128") and, at the stop,
  "held".
- CV: at full attenuverter, 10 V crosses the dial. *(call; the builder
  matches the convention of Eddy's and Wake's CVs if it differs)*

**Held (Stretch at the stop).**

- One slice sounds for as long as the dial stays there, its random
  phases renewed every grain, so it shimmers and does not loop.
- The module keeps that slice's loudnesses and stops reading the tape,
  so when the tape is not frozen the held sound stays even after the
  tape under it has been recorded over. *(call)*
- Moving Position while held takes a new slice at the new place: with
  the tape frozen this is a scrub through the 30 s, by hand or by CV.
  (As built, 2026-11-26: a move is decided in whole frames of lag, so a
  CV that is not moving moves nothing. If the held place has already been
  recorded over, the move takes the new slice at the anchor, Position's
  own place, rather than at the oldest edge; so a held sound under a
  modulated Position follows it. For the listening stop.)
  (Added 2026-11-26, the fix wave's re-review: on a tape that is
  recording, a move takes the new slice at the anchor whether or not the
  held place is still on the tape. Shifting it by the change kept a patch
  reopened with Stretch at its stop, which holds the silence from before
  it opened, silent under any turn of Position for up to 30 s. Frozen, a
  move still shifts the held place by the change: the scrub.)
- When Stretch leaves the stop the read point goes on from where it is
  if that place is still on the tape, and from the anchor if not.
- A change of Grain while held takes the slice again at the new size if
  its place is still on the tape, by a dissolve; if not, the held slice
  keeps its size until it is let go. *(call)*
  (As built, 2026-11-26: while recording, "still on the tape" keeps a
  margin of half the new grain and four Long grains for the rebuild.)

### Play once, and RUN

- Menu, **At the end of a pass**: *go round* (the default) or *stop*.
- *Stop*: when a pass ends the wet sound fades out with its last grain
  and stays silent until a trigger at TRIG starts the next pass. A patch
  opened with this setting waits for a trigger. *(call)*
- **RUN** is a gate: 10 V while a pass is sounding. It rises when the
  first grain of a pass begins to sound, so it follows what is heard and
  not when the trigger arrived. In *stop* it falls when the pass's last
  grain has faded and stays low until the next pass, so a recorder gated
  by it catches the fade in and the fade out. In *go round* one pass
  dissolves into the next and something is always sounding: RUN is then
  low for 1 ms at the moment the new pass's first grain begins, as a
  marker of the join and no more. A trigger at TRIG in mid pass starts a
  new pass and so makes the same 1 ms dip, in either setting. Held, for
  either reason, RUN stays high and a pass never ends. *(call)*
- Eddy's REC gate acts on its edges, and a recording shorter than 50 ms
  is treated specially (`docs/specs/2026-11-21-eddy-design.md`, "Rec"):
  the builder reads that before fixing RUN's timing.
  (As built, 2026-11-26: the dip is max(1, round(0.001 x rate))
  samples. Every high stretch of RUN lasts at least a hop less 1 ms
  (92 ms at Short, 44.1 kHz), so Eddy's 50 ms rule never throws away a
  recording RUN gates.)
- The patch this is for: Eddy A's outputs into Doldrums; Freeze once the
  clip is on the tape; Position to the clip's start; RUN into Eddy B's
  REC gate and the outputs into its inputs; *stop* chosen; a trigger at
  TRIG. Eddy B records exactly one pass. Eddy B holds 5 minutes: a 10 s
  clip at up to 30 times, the whole tape at up to 10.

### Blur, Tilt, Shift

Knobs only. Each is read once a grain.

- **Shift** moves the spectrum up or down by up to two octaves, in
  the middle at noon, where it does nothing at all. It is continuous,
  with a detent at noon. What would pass the top of the band is dropped,
  not folded back. A frequency lands between the lines of the transform
  and is shared between its neighbours, so a shifted tone stays one tone.
  (As built, 2026-11-26: down, a bin's power is shared between the two
  bins either side of where it lands (a scatter). Up, each output bin
  gathers the power at its frequency divided by the ratio, interpolated,
  scaled to keep the energy: a scatter upward left every other bin empty
  on noise, which with random phases repeats within the grain. The cost:
  a tone shifted up is widened in proportion to the ratio, about 47 Hz at
  +24 with the Medium grain, rough at large upward shifts. A lone tone
  shifted by a ratio that is not whole is up to 0.5 dB off. "Nothing at
  all at noon" is exact within +-0.25 semitone.)
- **Blur** smooths the spectrum along a scale of pitch (so it is as wide
  in the bass as in the treble). At 0 it does nothing at all; at full it
  is about an octave wide and a chord has become coloured noise. It
  keeps the sound's energy. Default 0.
- **Tilt** is a slope across the spectrum, in dB per octave about 1 kHz:
  noon is flat and does nothing at all, left is darker, right brighter,
  to about 6 dB per octave either way, with the gain held within a
  bound. *(call)*
  (As built, 2026-11-26: Tilt's gain is held within +-24 dB. Blur's knob
  runs 0 to 1 octave; at full its half-power points measured 0.9996
  octaves apart.)

### Harmonics

- **Harmonics** is the amount: at 0 the comb is out of the path
  altogether; at full only the comb's teeth pass; between, the two are
  mixed. Default 0. Its CV, at full attenuverter, crosses it with 10 V.
- **Root** is the note of the comb. It moves in semitones from C1 to C7
  with C4 at noon. Default C2. *(call; the default is for the listening
  stop)*
- **V/OCT** moves the note from Root by a volt an octave: at 0 V the
  comb is on Root's note, as an oscillator's is. It is not held to
  semitones.
- **A polyphonic cable at V/OCT makes a chord**: one comb for each
  channel, up to 16, all with the same Partials and Width, and a
  frequency passes if any of them passes it.
- **Partials**, 1 to 32 in whole steps: the comb's teeth stand at the
  note and its overtones, equally strong. Teeth above the top of the
  band are left out. Default 8. *(call)*
- **Width** is how wide each tooth is, from about 5 cents (a clear
  pitch) to about 300 (the source comes back through). Default about 25.
  A tooth is never narrower than the transform can show, so no tooth
  disappears on a low note. *(call)*
  (As built, 2026-11-26: the distance of a bin from a tooth is measured
  to the bin's nearer edge, so the bin that holds a tooth passes whole;
  sigma is never less than one bin's width at the tooth. So the narrowest
  tooth is about 2.7 bins wide at -3 dB: about 200 cents at 65 Hz with
  the Medium grain at 48 kHz. Low roots want the Long grain; for the
  listening stop. Teeth under 16 Hz are left out, as the low cut would
  remove them anyway.)
- A tooth has a soft, bell-shaped edge.
- A change of note, Partials or Width dissolves in; a V/OCT step never
  clicks or jumps. It reaches the sound at the next grain's start, or at
  the one after if a rebuild of the comb was under way (see Staging): at
  worst a whole grain's length before the dissolve begins, 0.34 s at
  Medium. The comb follows notes at the grain's pace and no faster; a
  quick line runs together. The manual says so.
- **Loudness.** A comb passes a small part of a noisy sound's energy.
  The comb's output is brought up by an amount worked out from the comb
  alone (how much of a flat spectrum it would pass), held within a
  bound, so that turning Harmonics up does not make the sound sink away.
  *(call; the amount and the bound are for the listening stop)*
  (As built, 2026-11-26: as the plan: g = min(8, 1 / sqrt(mean of mask^2
  over the band)), the gain (1 - h) + h g mask.)
  (As built, 2026-11-26, the final review's fix: that rule assumed a flat
  spectrum, so a sound already on the teeth (a saw on C2 at Harmonics 1)
  came out 16.7 dB over its input, into the soft bound. Reversed: the
  makeup is measured from the sound, per grain, one for both sides: before
  = the power reaching the comb (after Shift, Blur and Tilt), after = the
  power the comb would leave at g = 1, both summed over the two sides, and
  g = sqrt(before / after) held to [1, 8]; 1 when either is 0 or not
  finite. Measured at the defaults, 48 kHz, Harmonics 1, recording: a saw
  on the root +0.2 dB, a chord on three roots +0.5 dB, white noise
  -6.6 dB (it would need +25 dB; the bound gives +18), a sine between the
  first two teeth -22 dB (cut, raised by no more than the bound). At
  Harmonics 0 the comb is out, to the bit.)

### Mix, inputs and outputs

- **Mix**: dry to the left, wet to the right, at equal power between.
  Default fully wet. Smoothed. The wet sound is about a grain late and
  the dry is not; the manual says so.
- Levels: the wet sound of a steady input is as loud as the input,
  within 1 dB, at any Stretch, with the comb out, **measured over ten
  seconds or more**. Over a shorter time a pure tone wavers: where two
  grains overlap, their random phases may add or cancel. That waver is
  part of this method's sound on pure tones and is not removed; noise
  and dense chords do not show it.
  (As built, 2026-11-26: "within 1 dB over ten seconds" cannot hold for
  a pure sine at the Long grain, which has only 29 hops in 10 s: over 200
  seeds its worst 10 s window reached 2.09 dB, though the mean over a
  minute stays within 0.5 dB. Noise holds the 10 s rule (worst 0.02 dB).
  So for a sine the test bounds the 60 s mean and pins the worst 10 s
  window per grain size at its measured worst; the user is told.)
  (As built, 2026-11-26: a held full-scale (5 V) sine comes out at the
  same RMS as narrow-band noise with peaks of 13 to 14 V, so the soft
  bound below works on it; its products measure -37 dB (pinned at
  -34). Any held sound has a noise-like crest. Equal loudness was kept;
  for the listening stop, the alternative being a quieter wet level.)
- The output is softly bounded as Wake's and Eddy's are, and untouched
  below the bound.
  (As built, 2026-11-26: Eddy's bound, lifted to `src/SoftBound.hpp` and
  shared: the identity up to 8 V, a ceiling of 10 V.)
- NaN or infinite input samples and CVs read as 0 V.
- Bypassed, the inputs pass to the outputs.

### The window

- It shows the 30 s of tape as a waveform (both sides together, as
  peaks), oldest on the left and now on the right. While recording, the
  sound scrolls left; frozen, it stands still.
- The anchor (Position) is marked, and so is the read point. The slice
  being taken is shown at its true width. The part between the read
  point and the end of the pass is bright, the rest dim.
- It says, in a corner, what state the module is in whenever that is
  not plain running: frozen, held, stopped (waiting for a trigger). The
  builder chooses the words and marks; every one of these states must
  be readable at Rack's usual zoom.
  (As built, 2026-11-26: top right, FROZEN, HELD, STOPPED, and where two
  apply both, FROZEN HELD or FROZEN STOPPED; nothing when plainly running.
  Top left, the stored tape's trouble: TAPE NOT READ (a file there that
  could not be read; kept while the button is on) or TAPE NOT SAVED (a
  save meant to write the frozen tape and could not). Each sits on a patch
  of the glass's colour. The anchor is a cyan line with a flag, the read
  point an ivory line, the slice a pale band with a bar at its foot.)
  (As built, 2026-11-26, the user's decisions at the first test build:
  the read point (the manual's read head) is drawn in the theme's brass,
  bolder than the flag's line, and reads as gold on the glass; the ivory
  line was hard to tell from the flag. A tape frozen with nothing to
  travel through says FROZEN AT END, so the window says why Stretch does
  nothing; HELD and FROZEN HELD are now only Stretch's stop; with both
  causes, FROZEN AT END; STOPPED still wins. The manual calls the anchor
  the Position flag.)
  (Added 2026-11-26, at the user's request, with While recording: Follow:
  recording in Follow the corner says FOLLOW, the read head on the flag;
  STOPPED still comes first, and frozen the words are the frozen ones.)
- Held with the tape not frozen, the read point's mark rides left with
  its sound and off the edge; the corner still says held.
- **Dragging in the window sets Position** (the knob follows), as
  Eddy's window sets Start: Eddy's listening stop found that this was
  missed when absent. *(call)*
  (As built, 2026-11-26: a press puts the anchor under the mouse, a
  drag follows, one undo step per gesture; with a Position CV the knob is
  set so knob plus CV lands there. Not yet tried in a live Rack.)
  (As built, 2026-11-26, the user's ruling at the first test build: a
  press in the window means "play from here". The press, and each move of
  a drag, puts the Position flag under the mouse as before and also
  starts a new pass from it, as a trigger at TRIG does: so a press just
  left of the read head moves playback back there, and a drag scrubs flag
  and head together. In Stop a press starts a pass and raises RUN (a stray
  click can start a gated recorder). The knob and the CV keep the
  Position rule, the read head keeping its distance from the flag with no
  restart, so a CV sweeping Position does not restart the pass; undo of a
  window gesture restores Position by that rule too.)
- The ruler printed under the glass marks tenths: 3 s each.
- Drawing takes nothing from the audio thread: the waveform is a reduced
  copy (a few hundred columns of peaks), kept up to date as the tape is
  written.

### With the patch

- Saved: the knobs, the Freeze button, and the menu settings (Grain,
  Wide, At the end of a pass, Keep the frozen tape). The seed is not
  saved: a reopened patch has new random phases, as any noise source
  has. *(call)*
- **A frozen tape is kept with the patch**, as one file in the patch's
  storage folder, as Eddy keeps its tape (32-bit float stereo, in volts,
  at Rack's rate; about 11.5 MB at 48 kHz). It is written when the patch
  is saved, only if the Freeze **button** is on and the tape has not
  been written since it froze. A tape frozen by the gate alone is not
  kept: the button is the state the patch saves, and the file follows
  it. A tape that is not frozen stores nothing. *(call)*
  (As built, 2026-11-26, Task 15, after the user found at 2.0.48 that a
  frozen module was still frozen after quitting and restarting Rack but
  its tape was gone, so nothing played: Rack sends a module's save only
  when the user saves the patch to a file, never for its autosave or at
  quit, so a tape written only at a save was lost by a quit. The tape is
  now also written when the button freezes it: soon after the freeze, once
  its fade is on the tape, with Keep on, once a freeze, on the module's
  worker (about 11.5 MB at 48 kHz; nothing on the audio thread waits for
  it), by the same rules and the same write as a save. Rack's autosave
  keeps the storage folder, so a quit and a restart find the file. The
  save stays as the backstop and removes the file when the button or
  Keep is off (a thaw does not remove it at once); a freeze whose write
  fails removes the older file, as a save that cannot write does, so a
  quit then opens thawed rather than on an earlier tape; every write and
  removal is the worker's, one at a time. Turning Keep on while frozen
  writes the tape.)
- Menu: **Keep the frozen tape with the patch**, on by default.
- A reopened patch has the same tape and the same Position, and starts
  its pass from the anchor. A sound that was only held (Stretch at the
  stop, tape not frozen) is not kept: the manual says to freeze to keep.
- When the engine's rate changes, a frozen tape is converted off the
  audio thread, as Eddy's is; a tape that is not frozen starts again
  from silence.
  (As built, 2026-11-26: the stored file stays at its old rate until
  the tape is frozen anew; reading it again converts once, which is
  better than converting a conversion. While a tape is being read or
  converted, a save leaves the file exactly as it is (an autosave during
  a patch's load would otherwise lose it).)
- A patch saved without its tape file opens with a silent tape and no
  error.
  (As built, 2026-11-26: and a file that is there but cannot be read
  opens silent too, but says TAPE NOT READ in the window and why in the
  log, and is kept while the button is on. Deleting the module while its
  tape is being read holds Rack's audio until the read ends (the worker
  is joined under Rack's lock); the manual says so. Whether a duplicated
  frozen module (Ctrl+D) carries its tape was not tried.)
  (As built, 2026-11-26, Task 15, superseding the bullet and the note
  above: a patch that opens with the Freeze button on but gets no tape
  opens thawed, never frozen and silent, as Spindrift's does: the button
  is let go (the param set to 0, by the UI side, never the audio thread)
  and the module records and plays as one never frozen. That is so for a
  missing file (a line of information in the log, nothing in the
  window), for Keep off, and for a file that cannot be read (TAPE NOT
  READ and a warning, as before; the file is no longer kept, since the
  button is off: the next save removes it, or a new freeze writes over
  it). Most bad files are refused by their header as the patch opens,
  before a sample; one that fails in the read lets go of the button as
  soon as the read fails, unless the user has pressed Freeze since the
  patch opened. A gate high at open still freezes. Deleting the module
  while it writes its tape also holds Rack's audio until the write ends.)
- Initialize silences the tape and returns everything to its default.
  (As built, 2026-11-26: the menu too. After Initialize, and after a rate
  change when not frozen, the recording on the silent tape fades in from
  0 over 5 ms, as after a thaw, so no step is written onto it. At
  Initialize the grains already laid down play out with RUN still high,
  and Mix moves to its default over its 10 ms ramp. Undo after Initialize
  brings back the knobs, the Freeze button and the menu but not the tape.)

## Aliasing

By the three kinds in `docs/DSP-ALIASING.md`:

1. **Generated discontinuities:** none. Every change is a dissolve, and
   the tape's own edges at Freeze are faded.
2. **Nonlinearities:** only the soft bound at the output, which acts
   above the bound and is the one Wake and Eddy use.
   (As built, 2026-11-26, measured by `test_alias` at 44.1 and 48 kHz:
   at 1 V everything outside a skirt of 32 grain bins at least 60 dB down,
   worst -64.1 dB; a tone shifted past the top -130 dB or less; at 5 V the
   bound's products pinned at -34 dB, worst -37.0 dB.)
3. **Variable-rate reads:** none. Grains are taken at whole samples at
   the tape's own rate; Stretch changes where they are taken, not how
   fast they are read. Shift moves loudnesses between frequencies and
   drops what would pass the top of the band.

Particular to this method, and measured the same way: a held tone comes
out as that tone with a narrow skirt of noise round it, as wide as the
grain's window makes it. The test bounds everything outside the skirt.

## Code and files

As every module (CLAUDE.md, TESTING.md "Adding a module"):

- `src/doldrums/`: Rack-free headers, the module a thin map onto them.
  Likely parts: the ring tape; the read point and its passes (pure
  logic: anchor, travel, returns, held, play once, RUN); the grain
  (windows, transform, the spectral steps, the overlap); the tables
  (comb, Blur, Tilt, Shift) and their rebuilding; the control laws; the
  staging; the engine; the window's reduced copy; `Doldrums.cpp`.
- **Staging.** A grain's work is done in a fixed sequence of steps. No
  step is larger than one transform or one pass over the bins. **The
  steps are spaced apart, not done on neighbouring samples**: Rack runs
  the engine a block of samples at a time, and steps on neighbouring
  samples would all land in one block, which is the lump this approach
  exists to avoid. They are spaced evenly at a fixed interval of at
  least 64 samples at 48 kHz (more at higher rates, in proportion), the
  same every grain, and the grain begins to sound a fixed number of
  samples after its boundary; that delay is part of the stated latency.
  The bound: the most work on any one sample is one transform and a
  small fixed amount more, under 100 µs on the development Mac at 96 kHz
  with the long grain (one transform there measures 56 µs), which is
  under a tenth of a 64-sample block at 48 kHz. The bench measures it.
  (As built, 2026-11-26: nine grain steps and the add, ten pieces a gap
  apart, so a grain sounds ten gaps after its boundary: 640 samples at
  44.1 and 48 kHz, 1280 at 96 kHz. The tables are fed half way between
  gaps, 32 gap units of work a call. Worst single sample measured 68.7 to
  70.7 µs at 96 kHz, Long (an inverse transform and its pass), 32.4 to
  33.3 µs at 48 kHz, nothing split. That is at the core's working clock:
  from a light load the first grains took up to 1.7 times as long (about
  115 µs), which no split of a transform could bring under the bound. At
  8.0 to 8.5 kHz with the Short grain the pieces are (hop - 1) / 10 = 51
  samples apart, under the 64 asked for. A change of Grain overlaps the
  two sizes for one grain: a bump of up to about 3 dB (most from Long to
  Short), no click; against the anti-goal's "level jump", accepted.)
  The tables are rebuilt a part at a time on the same path, never in one
  go; any one rebuild is finished within half a grain; and a new table
  takes over at a grain boundary. If the controls keep moving, a rebuild
  already begun is finished and the next one starts from the newest
  values.
- pffft through the SDK's header (`dep/include/pffft.h`). Its setups and every buffer, for all
  three grain sizes, are made off the per-sample path (when the module
  is added and when the rate changes). No allocation on the audio
  thread.
  (As built, 2026-11-26: the vendored pffft (see Research), wrapped by
  `src/Fft.hpp` as `rka::RealFft`.)
- No libm on the per-sample path (`src/FastMath.hpp`, tables): the
  random phases come from the generator and a sine table.
- **The worker**, for the tape's file only: write at save, read at load,
  rate conversion; and the swap of a tape under a running engine without
  a lock on the audio thread. The pattern is Eddy's
  (`src/eddy/TapeJobs.hpp`, `src/eddy/WorkerBook.hpp`), with
  `src/WavFile.hpp` and `src/Resample.hpp`. Whether Eddy's code is
  lifted into shared headers or its pattern copied is the builder's.
- Reused: `TapeWindow` and `src/TapeGesture.hpp` for the window and its
  drag, `src/Overview.hpp` if it suits a scrolling ring, `src/Detents.hpp`,
  the soft bound, `rkatest::AliasMeter`. `src/Tape.hpp` is built round a
  moving write head at any speed and may not suit a plain ring.
  (As built, 2026-11-26: the window is the module's own, `DoldrumsWindow`
  in `Doldrums.cpp` drawing `src/doldrums/TapeView.hpp`'s snapshots;
  `TapeWindow`, `src/TapeGesture.hpp`, `src/Overview.hpp` (its columns
  stand still; a scrolling ring needs columns tied to absolute frames),
  `src/Detents.hpp` and `src/Tape.hpp` are not used. Reused: the soft
  bound (lifted to `src/SoftBound.hpp`), `src/WavFile.hpp`,
  `src/Resample.hpp`, `rkatest::AliasMeter`; Eddy's worker pattern was
  copied (`TapeStore.hpp`, `TapeKeeper.hpp`), not shared.)
- The generator's addition for a drawn mark on a face (Panel).
- `src/doldrums/bench.inc` (idle; frozen and held; busy: recording, the
  long grain, a 16-note comb with V/OCT moving, Blur and Shift up), the
  panel entry, `plugin.json` (slug `Doldrums`), `MODULES` in the
  Makefile, the manual `docs/doldrums/README.md`, the listening guide and
  technical reference (CLAUDE.md), the README table, CHANGELOG, and the
  roadmap and `docs/future/doldrums.md` marked built.

## Testing

TESTING.md applies in full. Particular to Doldrums:

- **Repeatable.** Same seed, same input, same controls: the same output
  to the bit, twice, and with the engine driven a sample at a time or in
  blocks.
- **The read point**, as pure logic with no sound: the anchor for
  stated Positions; the pace for stated Stretches, frozen and not; a
  return exactly when the pass ends, in both cases; the pass's length
  against the formula; Position turned in mid pass moves the read point
  by that much and does not restart; the edge; TRIG; held; leaving held
  on and off the tape; Freeze going on (a new pass) and off (no change);
  nothing to travel through, and a pass shorter than a grain, in both
  settings; play once; and RUN for all of these, against a table written
  from this document.
- **Stretch.** A tape with a marker tone every second, frozen: at 2, 8
  and 32 the markers come out 2, 8 and 32 s apart, within a grain. The
  dial's law hits its lettered values at their marks and is monotone to
  the stop; at the stop the read point's pace is exactly 0.
- **Held.** A chord held for ten minutes: its spectrum does not drift,
  its level over each ten seconds stays within 1 dB, it does not repeat (no period found), and
  with the tape recording something else underneath it does not change.
- **Level.** White noise and a sine, at each grain size, at Stretch 1, 8
  and held: wet RMS over ten seconds within 1 dB of the input's. For
  noise, the ripple at the grain's rate is under a stated bound. For a
  sine, the waver where grains overlap is measured and pinned at what it
  is, not bounded tightly (see Mix). Silence in: exact silence out.
- **No clicks.** Freeze on and off by button and by gate; a return;
  TRIG; Position by knob and by a stepped CV; a V/OCT step of two
  octaves; Partials and Width swept; Harmonics from 0; Grain changed
  while sounding; the stop reached and left; a tape arriving from a
  patch; play once ending: no jump at the output beyond what the
  signal's slope explains, and no step written to the tape.
- **Does nothing at rest.** Shift at noon, Blur at 0, Tilt at noon and
  Harmonics at 0 each leave the loudnesses untouched to the bit; with
  all four so, the output equals a build with those steps removed.
- **Shift.** A sine comes out as one tone at the shifted pitch within a
  stated error across the range; shifted past the top it is gone, with
  nothing folded back.
- **Blur.** Energy kept within 1 dB across the knob; the width at full
  against its stated value.
- **Tilt.** The slope measured on noise at stated settings; the bound.
- **The comb.** Noise in: peaks at the note and its overtones within a
  stated error in cents, for roots from C1 to C7, at each grain size;
  the count equals Partials; nothing above the top of the band; tooth
  width against Width; a three-note chord gives three combs; V/OCT
  tracks an octave a volt within a stated error over ±3 V; a tooth on a
  low note at the narrowest Width is still there. The loudness rule
  against its formula and its bound.
- **Stereo.** By default a mono input gives identical sides, and a tone
  panned by level and by a phase difference keeps both; Wide gives
  uncorrelated sides from a mono input (correlation under a stated
  figure).
- **Aliasing** (rule 16), at 44.1 and 48 kHz: a loud sine at 0.1 to 0.4
  of the rate, held, at each grain size, with and without Shift up:
  everything outside the tone's skirt at least a stated number of dB
  down, measured, dated, and shown to fail with a fault put in.
- **Staging.** The steps for a grain finish within the stated number of
  samples at every grain size and rate; the most work done on any one
  sample is measured in the bench and bounded (TESTING.md 10), at 48 and
  96 kHz with the long grain and the busiest tables; no allocation, lock
  or libm on the path.
- **With the patch.** Golden saved state. A frozen tape comes back to
  the bit; a tape that is not frozen stores no file; the file is not
  written twice for one freeze; a missing file opens silent (as built,
  Task 15: thawed, the button let go; the tape written at the freeze
  without a save); a rate
  change converts a frozen tape (a tone keeps its pitch within 1 cent)
  with no lock, allocation or free on the audio thread.
- **The window.** The reduced copy matches the tape's peaks column by
  column while recording, frozen and after a tape arrives; the marks'
  places for stated cases; each state's indication is drawn; the smoke
  test's screenshot shows it with sound on the tape. Its cost is in the
  bench.
- **The patch it was asked for**, as an engine test: a clip on the tape,
  frozen, play once, a trigger: RUN is high for the pass and no longer,
  and what sounds while it is high is the whole pass with its fades.
- Sanitizers, extreme and changing sample rates, seeded fuzz of every
  control, gate and menu setting together, instances independent, bench
  budgets, Linux and Windows builds.

Least astonishment (the user's rule, 2026-11-25): beyond the cases
above, the builder tries the patches a person would make first (a chord
frozen and left; noise into a comb played from a sequencer; a slow LFO
on Position; Stretch swept end to end while sounding; Freeze tapped
quickly) and raises anything that surprises as a bug, even where this
document asked for it.

## Listening stop

Before these are fixed, the user listens on a numbered test build:

- the three grain sizes, and which is the default;
- the stereo default (the source's own image, or Wide);
- the Stretch dial's last eighth, 32 to the stop;
- Position fully right as the default, and how "nothing to travel
  through" feels when frozen there; Freeze starting a new pass;
- how quickly the comb follows a played line, at each grain size;
- Blur's and Tilt's ranges;
- Root's default and range; Partials' default; Width's range;
- the comb's loudness rule;
- whether the window says enough (held, frozen, stopped), and whether a
  view of the spectrum is missed;
- whether the 30 s is the right length;
- whether CV on Tilt or Blur is missed;
- the Eddy to Doldrums to Eddy patch, end to end.

(As built, 2026-11-26: the listening guide, `docs/doldrums/listening.html`,
adds the build's own items: Freeze in stop waiting for TRIG; Position
fully left while recording; a held sound under a moving Position; Freeze
while held; a thaw from a hold; the comb at low roots; Shift's roughness
upward; the pure tone's waver; a full-level sound's peaks; the bump at a
Grain change; and what could not be tried without a live Rack: the drag
and its undo, the menu, dark panels, a rate change while running and
duplicating a frozen module.)

## Left to the builder

- The windows for taking a grain and laying it down, within the level
  tests.
- The Stretch law between 32 and the stop.
- How Shift shares a frequency between its neighbours, and how Blur is
  smoothed, within their tests.
- The staging: how many steps, how many samples, how the bins are
  divided; set from the bench.
- The cap on the grain's size at very high sample rates.
- The fade times at Freeze's edges, Mix's smoothing.
- The words and marks in the window, and its reduced copy.
- Whether Eddy's worker code is shared or its pattern copied.
- What Rack's Randomize does (as Eddy: nothing that freezes or silences
  the module by surprise).
- Every stated bound that this document leaves as "a stated figure":
  measured, set, dated.
