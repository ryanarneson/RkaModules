# Doldrums

A stereo freeze and stretch, 16 HP: a ship becalmed, the sound going
nowhere. Doldrums keeps the last 30 seconds of its input on a tape. It can
creep through any part of that tape many times slower than it was played,
or hold one moment of it for as long as you like. The sound is
PaulStretch's (Paul Nasca's method, written afresh): slices of the tape
are taken apart into their frequencies, given new random phases and put
back together, so what comes out is smeared, glassy and never quite
repeats. A tuned comb, **Harmonics**, can keep only a note and its
overtones, so noise comes out as a drone in tune. A trigger can play one
pass and a gate says when it runs, so another module can record it.

![Doldrums panel](Doldrums.png)

## First: freeze and stretch the last few seconds

The defaults, and the thing to try first:

1. Patch a sound into **IN L** (and **IN R** for stereo) and the **L** and
   **R** outputs to your mixer. Leave every knob where it starts: Position
   about 3 seconds back (the knob a little left of fully right), Stretch
   at 8, Mix fully wet.
2. Listen: the module plays the input drawn out 8 times, starting about
   3 seconds behind it and falling further behind (it is reading at an
   eighth of the speed the sound arrives). About every 30 seconds it
   starts again 3 seconds behind.
3. Press **FREEZE**. Its cap lights and the tape stops recording. The
   window's corner says **FROZEN**. A pass starts at the Position flag,
   3 seconds back, and the read head (the gold bar in the window) creeps
   from there to the end of the tape at Stretch's pace: those last 3
   seconds, drawn out 8 times, take about 24 seconds. Then it goes back
   to the flag and plays them again, dissolving from the end into the
   start. Turn **STRETCH** to hear them faster or slower.
4. Press FREEZE again to let it go: the tape records again and the read
   head goes straight back to the flag. The flag is still 3 seconds
   before the freeze, so you first hear those 3 seconds again, drawn
   out, and the new input only after them.

**At the defaults the wet sound is always about 3 seconds or more behind
the input**, and Freeze loops a stretch of the last 3 seconds rather than
holding the instant. To hold the instant, turn Position fully right
(next section).

## Hold what is playing

The sustain-pedal use:

1. Turn **POSITION** fully right (the tooltip says "now").
2. Press **FREEZE** while the sound you want is playing.

The moment that was playing when you pressed it now sounds until you
change something. The window's corner says **FROZEN AT END**. Press
FREEZE again to let it go: the tape starts recording again and the read
head goes straight back to the Position flag, which at fully right is
"now". From there it travels at Stretch's pace while new sound arrives at
full pace. At x1 it keeps up, so you hear the live input at once. Above
x1 it falls behind again: you hear the input from the moment of the thaw
onward, slowed, not the live input (at x8 it is about 26 s behind after
half a minute, then it returns to the flag). The frozen moment dissolves
into the new sound over the first grains (about a third of a second at
x1, two or three seconds at x8). With the menu's **While recording:
Follow** the read head stays on the flag while the tape records, at any
Stretch, so after the thaw you hear the live input at once (about a grain
late), and Stretch acts again at the next freeze (see The menu).

Why it holds: with Position fully right a pass starts at the newest moment
on the tape, and with the tape frozen there is nothing after that moment
to travel through. So the read head stands still at the right end of the
tape and the module plays that one slice, with new random phases every
grain, so it shimmers and does not loop. It does this at any Stretch and
with either setting of "At the end of a pass". **Stretch has nothing to
act on here**: turning it changes nothing you can hear. That is what
"AT END" in the corner says. Turn Position left, or let Freeze go, to
give the read head tape to travel through.

## The panel

From the top:

- **The window**: the 30 s tape, oldest on the left and now on the right,
  over a ruler (next section).
- **POSITION** on the left and **HARMONICS** on the right, each with its
  attenuverter and CV jack beneath it. **STRETCH**, the large dial,
  lettered 1, 2, 4, 8, 16, 32 and ∞, in the middle.
- **BLUR** and **ROOT**; under Stretch, its attenuverter and CV jack.
- **TILT**, **SHIFT**, **PARTIALS**, **WIDTH**.
- **IN L**, the **FREEZE** button, **MIX** (small), **V/OCT** and, on
  ivory, the **L** output.
- **IN R**, the **GATE** for Freeze, **TRIG**, and on ivory **RUN** and the
  **R** output.

"CV" is printed beside each of the three CV jacks.

## The tape and its window

The tape is stereo and holds the last 30 seconds at Rack's sample rate. It
records the input all the time unless it is frozen. It is silent when the
module is added. While it records, the picture scrolls left; frozen, it
stands still.

### What you see

Read this with the module open. From the back of the glass to the front:

- **Cyan peaks: the tape.** Both sides together, oldest on the left, the
  present on the right edge. The glass spans ±6 V, so a ±5 V sound fills
  most of it; a silent tape shows a dim line at 0 V.
  - **Bright peaks** are the part the current pass has still to play, and
    **dim peaks** the rest. Frozen, the bright part runs from the gold bar
    to the right edge, and it shrinks as the gold bar travels right.
    Recording, it runs from the gold bar to the place where the oldest
    sound, about to be recorded over, will catch it up; at Stretch 1, or
    in Follow (FOLLOW), to the right edge. Held, only the slice itself is
    bright. Stopped, nothing is bright.
- **A thin cyan line with a small solid triangle on top: the Position
  flag.** It is where a pass starts, and it is what POSITION sets: fully
  right is now, fully left is as far back as a whole grain can still be
  taken (30 s less a grain). (The technical reference calls this place the
  anchor.) It stays put on the glass while the tape scrolls under it, and
  it moves only when Position moves: the knob, its CV, or a press in the
  window.
- **A short cyan bar along the bottom edge, with a pale band above it:
  the slice.** This is the piece of tape the current grain is made from,
  drawn at its true width: a grain long, about a third of a second at
  Medium, a little over 1 % of the window (half that at Short, twice at
  Long). The band is faint; the bar along its foot is the easier to see.
  Its right-hand end is the gold bar.
- **A gold bar, the full height of the glass, a little bolder than the
  flag's line: the read head.** The exact place the module has reached in
  the tape: the end of the slice being heard.
- **The word in the top right corner** says what state the module is in
  whenever it is not plainly running:
  - nothing: plainly running (a pass travelling, the tape recording);
  - **FROZEN**: the tape is frozen and a pass is travelling through it;
  - **HELD**: the read head stands still because Stretch is at its stop
    (∞), while the tape records;
  - **FOLLOW**: the menu's While recording is set to Follow and the tape
    is recording: the read head stays on the Position flag, and Stretch
    waits for Freeze (it does nothing until the tape is frozen);
  - **FROZEN HELD**: frozen, and the read head stands still because
    Stretch is at its stop;
  - **FROZEN AT END**: frozen with the Position flag at the end of the tape
    (Position fully right, or within a grain of it): the read head is at
    the end of the tape, so Stretch has nothing to act on until Position
    moves left or Freeze is let go. The sound is held. (If Stretch is at
    its stop as well, the corner still says AT END: moving Stretch alone
    would change nothing.)
  - **STOPPED**: in the Stop setting, a pass has ended (or the patch has
    just opened, or Freeze has stopped it) and the module is silent until
    a trigger (in Follow too: STOPPED comes before FOLLOW);
  - **FROZEN STOPPED**: the same, on a frozen tape.
- **The words in the top left corner**, only when the patch's tape had
  trouble: **TAPE NOT READ** or **TAPE NOT SAVED** (see "With the
  patch").
- **The ruler** under the glass: a long tick every tenth of the tape
  (3 s), short ticks between them every 0.75 s.

### What the marks do

The rows that say "Recording" are for the menu's While recording: Drift,
the default; Follow has its own rows below them.

- **Recording, Stretch 1**: the gold bar sits on the flag and keeps up
  with the sound, which scrolls left under them both; you hear the input
  smeared, Position's distance late (plus about a grain). With Position
  fully right, flag and gold bar stand together at the right edge.
- **Recording, Stretch above 1** (x8 is the default): each pass starts
  with the gold bar on the flag. New sound arrives faster than the read
  head reads, so the gold bar falls back to the left of the flag, with
  the sound it is reading. When the oldest sound catches it up the pass is
  over and the gold bar returns to the flag (Go round) or the module
  stops (Stop). At the defaults that is about every 30 seconds.
- **Recording, Stretch turned back to 1**: the gold bar rejoins the flag
  at the next grain (a new pass, by a dissolve) and stays on it.
- **Recording in Follow, any Stretch** (FOLLOW): the gold bar sits on the
  flag at every setting of Stretch, its stop included, and the sound
  scrolls left under them both, as at Stretch 1: you hear the input
  smeared, Position's distance late (plus about a grain). Stretch's
  tooltip adds "(waits for Freeze)". A pass never ends, so RUN stays high.
- **Follow chosen while recording**: the gold bar rejoins the flag at the
  next grain (a new pass, by a dissolve) and stays on it. **Drift chosen
  while following**: the gold bar leaves the flag where it is and falls
  behind at Stretch's pace (no new pass). Stopped, either choice leaves
  it stopped.
- **Frozen in Follow**: everything below is the same as in Drift.
- **Frozen, the flag back in the tape** (FROZEN; the default after a
  freeze): the peaks stand still. The gold bar travels right from the
  flag at Stretch's pace; at x1 a second of tape a second, at x32 it
  barely moves. When it reaches the right edge the pass is over: it jumps
  back to the flag (Go round), or rests on the flag and the corner says
  FROZEN STOPPED (Stop).
- **Frozen, Position fully right** (FROZEN AT END): the flag and the gold
  bar are pinned together at the right edge and the slice sits just to
  their left. There is no tape ahead of the read head, so Stretch has
  nothing to act on: changing it does not slow anything down. Turn
  Position left (or press in the window) to give it tape to travel
  through.
- **Stretch at its stop** (HELD or FROZEN HELD): the gold bar does not
  move. Frozen, it stays where it is. Recording (in Drift), the gold bar
  and the slice ride left with the sound they hold, while the flag stays
  put; once that sound has been recorded over they go off the left edge.
  The corner still says HELD and the sound still plays (it is kept, not
  read from the tape again). Turning Position then puts them back on the
  flag, holding the slice there.
- **Stopped** (STOPPED or FROZEN STOPPED): the gold bar rests on the
  flag, nothing is bright, and nothing sounds until a trigger (or a press
  in the window) starts a pass from the flag.
- **At a thaw** (Freeze let go while the module is sounding): the gold
  bar returns to the flag at once and a new pass starts there. Stopped,
  it stays stopped.
- **Turning POSITION, or a CV at Position**: the flag moves, and the
  gold bar moves by the same amount, wherever it has got to; the pass
  goes on (stopped, the gold bar stays on the flag; held while recording,
  it goes to the flag).
- **A press or a drag in the window: play from here.** The flag goes
  under the mouse and the gold bar goes to the flag: a new pass starts
  there, exactly as a trigger at TRIG does. So a press just left of the
  gold bar moves playback back to that point, and a drag scrubs the
  flag and the gold bar together.

### Setting Position with the mouse

Press in the window to put the Position flag under the mouse and play
from there; drag to move it, playing from each place it passes.
**POSITION** turns to match, and the whole gesture is one undo step;
undoing it puts Position back as the knob would (the gold bar keeps its
distance from the flag; no new pass). With a CV at Position's jack, the
knob is set so that knob plus CV, as the CV is at that moment, puts the
flag under the mouse; the knob stops at its ends, so a large CV can leave
the flag short of the mouse. The flag cannot go further left than a grain
from the oldest end of the tape, so a press at the very left edge sets
Position fully left. The right mouse button opens the menu, as anywhere
on the panel.

A press is not the same as turning the knob. The knob (and its CV) moves
the flag and the gold bar together without starting a pass, so a CV
sweeping Position never restarts the sound. A press starts a pass, as a
trigger does. **In the Stop setting a press starts a pass and raises
RUN**, so a stray click can start a recorder gated by RUN. Frozen, a press
at the right edge holds (nothing to travel through: FROZEN AT END).

## Position and Stretch

**The read head** (the gold bar) is the end of the slice the next grain
takes. It is what you hear.

**POSITION** sets the Position flag, where a pass starts: fully right is
now, fully left is as far back as a whole grain can still be taken (30 s
less a grain). The tooltip says how far back: "now" or "12.3 s ago"; you
can type a number of seconds, or "now". Default: about 3 s back (0.9 of
the knob's travel: 2.97 s at the Medium grain).

**What Stretch does, and what it does not.** STRETCH sets how fast the
read head travels through the tape: at 1 a second of tape each second, at
8 an eighth of a second each second. It never lowers the pitch: every
grain keeps the pitches of the slice it was made from. On a steady sound
(a held chord, a drone) slow and fast sound alike, because each grain is
much like the last; the difference is heard on sound that changes. It has
nothing to act on when the read head has no tape ahead of it (frozen with
Position fully right: FROZEN AT END). At exactly 1, the dial's left stop,
the read head sits on the Position flag while recording and keeps up with
the input; at anything above 1 it falls steadily behind the input until
the pass ends, and brought back to 1 it rejoins the flag. That is the
menu's While recording: Drift, the default. **In While recording: Follow,
Stretch waits for Freeze**: while the tape records the read head stays on
the flag at any Stretch, its stop included, and Stretch takes effect from
the moment the tape is frozen. The window's corner says FOLLOW meanwhile.

The tooltip shows the figure ("x8.0", "x128") and "held" at the stop,
followed by "(waits for Freeze)" while the module is following; you can
type a figure ("8", "x8", "128") or "held". Default: 8.

- Over the lettered travel, from 1 at the left stop up to the 32, it
  doubles every 45 degrees: 1, 2, 4, 8, 16, 32.
- **The last sixth of the dial**, from the 32 to the ∞ at the end, rises
  without limit: 64 about 30 % of the way along it, 128 half way, and
  at the very end the read head does not move at all. That end
  is **held**.

**Frozen**, the tape stands still and the read head travels right,
towards now. When it reaches the end of the tape the pass is over.

**Not frozen** (in Drift), the read head moves through the sound at the
same pace, but new sound arrives at full pace, so the read head falls
behind now and drifts left in the window. When the oldest sound, about to
be recorded over, catches it up, the pass is over. At Stretch 1 it sits on the flag
and never returns: you hear a smeared copy of the input, late by
Position's distance and about a grain. With the defaults (Position about
3 s back, x8, not frozen) a pass lasts about 30 seconds and then starts
again from the flag. In Follow it sits on the flag at any Stretch, as at
Stretch 1 in Drift.

**At the end of a pass** (the menu, below) the read head goes back to the
Position flag and plays again, or stops and waits for TRIG.

**Turning Position** (the knob or its CV) moves the read head by the same
amount, at the next grain, wherever it has got to; it does not start the
pass again. If that would take it off the tape it stops at the edge.
Held while recording, it puts the read head on the flag instead (below).
Frozen, a turn far enough right to push the read head to the end of the
tape ends the pass at once. A press in the window is different: it plays
from the flag (above).

**Held** means the read head stands still and one slice sounds for as
long as it stays so, with fresh random phases every grain. It has two
causes:

- **Stretch at its stop** (the ∞ end of the dial, or a CV pushing it
  there): HELD, or FROZEN HELD. In Follow the stop holds only once the
  tape is frozen (FROZEN HELD); while it records it follows (FOLLOW).
- **Nothing to travel through**: the tape frozen with the Position flag
  less than a grain from the end of the tape (FROZEN AT END). Position
  fully right, frozen, is the usual case.

While held:

- The module keeps that slice's loudnesses and does not read the tape
  again, so when the tape is recording the held sound stays even after the
  tape under it has been recorded over.
- **Moving Position takes a new slice**. Frozen, the read head moves by
  as much as the flag does (held to the ends of the tape): with Stretch
  at its stop this is a scrub through the 30 s, by hand or by CV.
  Recording, the new slice is taken at the flag itself, and held there.
- A pass never ends, in either setting of "At the end of a pass", and RUN
  stays high.
- **Leaving held** (Stretch off its stop, or Position turned left so there
  is something to travel through): the read head goes on from where it
  is if that place is still on the tape, and from the flag if not (and,
  recording at exactly x1, from the flag).
- **Changing Grain while held** takes the slice again at the new size if
  its place is still on the tape (while recording, with some room to
  spare); if not, the held sound keeps its size until it is let go.

**A patch reopened with Stretch at its stop and the tape not frozen**
(in Go round, in Drift) says HELD and holds silence: it holds the first
slice of a tape that has only just started recording. Turning Stretch
from its stop to x8 leaves it silent until the read head has crept up to
the moment the patch opened (about Position's distance times the Stretch:
roughly 20 to 24 s at the defaults), then the sound fades in. What ends
it promptly, once the tape holds more than Position's distance: turning
Position (the new slice is taken at the flag, and held), a trigger at
TRIG, a press in the window, or Stretch at exactly 1 (each puts the read
head on the flag). (In Follow the stop does not hold while the tape
records, so such a patch follows the input from the start.)

**Not frozen, with Position fully left**, a pass has less than a grain to
travel before the oldest sound catches it. The module then takes its slice
at the flag every grain: you hear the input as it was about 30 seconds
ago, at any Stretch. It is not held (the corner says nothing), it never
ends, even in the Stop setting, and RUN stays high.

**Stretch CV**: knob plus attenuverter × volts ÷ 10, so at full
attenuverter 10 V crosses the whole dial. The attenuverter starts at 0:
turn it to use the CV. A CV that pushes past the end is a stop (held).

**Position CV**: the same rule; at full attenuverter 10 V crosses the
whole tape.

With a cable in Stretch's, Position's or Harmonics' CV jack, that knob's
tooltip adds what is heard with the CV whenever the two differ, for
example "x8.0 (x16.0 with CV)".

## Freeze

**FREEZE** (the button, which latches) or a high gate at **GATE** freezes
the tape; either one is enough. The gate is high from 1 V and low again
under 0.1 V. The cap is lit while the tape is frozen, by either.

Frozen, the tape stops recording and the 30 s stay as they are. The
recording fades out over 5 ms at the freeze and back in over 5 ms when
Freeze ends, so no step is written onto the tape. Recording goes on from
the same place.

**Freeze going on, in Go round, starts a new pass from the Position
flag**, so the frozen moment is reached at once instead of after a long
creep.

**Freeze going on, in the Stop setting**, does one thing whatever was
happening before:

- with a pass to travel (the flag back in the tape, as at the default),
  it ends any pass, running or not, and the module waits, FROZEN STOPPED,
  silent, with RUN low, for a trigger at TRIG (or a press in the window);
- with nothing to travel through (the flag within a grain of the right
  end, as at Position fully right), it holds: FROZEN AT END, sounding.

A trigger arriving together with the freeze starts a pass.

**Freeze going off (a thaw) sends the read head back to the Position flag
at once**: a new pass starts there, in either setting, whenever the
module is sounding (running or held). With Position fully right that is
"now". The first grains after the thaw still hold the frozen moment's
last frames as well as the new input, so it dissolves in: within about a
grain at x1, about Stretch grains above that (two or three seconds at
x8). From there the read head travels at Stretch's pace while new sound
arrives at full pace, as in any recording pass: at x1 it keeps up and you
hear the live input; above x1 it falls behind again, so you hear the
input from the thaw onward, slowed, not the live input (at x8 about 26 s
behind after half a minute, then back to the flag). That is with Position
fully right. **With the flag back in the tape** (Position's default is
about 3 s back), the tape did not move while frozen, so the flag is
Position's distance before the freeze: the read head plays that stretch
of sound from before the freeze first, and reaches the new input only
after it: at x1 after about 3 s at the default (then 3 s late); at x8
the 3 s are drawn out (about 24 s) and only a little of the new input is
heard before the pass returns to the flag. **In Follow** the read head
stays on the flag at any Stretch and plays on at the input's own speed:
first Position's distance of the tape from before the freeze (about 3 s
at the default), then the new input, that far late; with Position fully
right, the live input within about a grain. Stopped
(Stop setting, waiting for TRIG), a thaw leaves it stopped: no pass, and
RUN stays low, so a recorder gated by RUN is not restarted. A thaw that
starts a new pass while RUN is high makes RUN's 1 ms dip, as any new pass
does.

**Freeze while held by Stretch at its stop** starts a new pass from the
flag like any freeze in Go round, so the held sound is replaced by the
slice at Position's place (in Stop, with a pass to travel, it stops). To
keep a moment, freeze first, then hold. Thawed while held at the stop,
the read head goes to the flag and holds the slice there (in Follow it
follows from the flag instead).

## TRIG, RUN and the end of a pass

**TRIG** puts the read head back on the Position flag and starts a new
pass at any time: in mid pass, held or stopped. It acts on a rising edge
(high from 1 V, low again under 0.1 V). A press in the window does the
same.

**RUN** is a gate: 10 V while a pass is sounding.

- It rises when the first grain of a pass begins to sound, so it follows
  what you hear, not the moment the trigger came.
- In the **Stop** setting it falls when the pass's last grain has faded
  out, and stays low until the next pass. A recorder gated by RUN catches
  the fade in and the fade out.
- In **Go round** one pass dissolves into the next and something is always
  sounding. RUN goes low for 1 ms where each new pass's first grain
  begins, as a marker, and is otherwise high.
- A trigger at TRIG in mid pass (or a press in the window, a thaw,
  Stretch brought back to 1 while recording, or Follow chosen while the
  read head is off the flag) starts a new pass and so makes the same 1 ms
  dip, in either setting.
- Held, for either reason, RUN stays high; so it does while following.

The menu's **At the end of a pass**:

- **Go round** (the default): at the end of a pass the read head returns
  to the flag and plays again, dissolving from the old place to the new.
- **Stop (until TRIG)**: at the end of a pass the wet sound fades out with
  its last grain and stays silent until a trigger at TRIG (or a press in
  the window). The corner says STOPPED. While stopped, turning Position
  or setting Stretch does not wake it, with one exception: frozen,
  Position turned fully right (nothing to travel through) wakes it into
  the hold. Freeze going on stops a pass with tape to travel (above).
  Choosing Stop does not end the pass under way: the module stops when
  it ends, or at the next freeze. (Recording at exactly x1, or in Follow
  at any Stretch, a pass never ends, because the read head sits on the
  flag: it goes on until a freeze. Follow chosen while stopped does not
  wake it.) Choosing Go round while stopped starts
  a pass at once. **A patch opened with Stop chosen waits for a
  trigger**, unless its tape is frozen with nothing to travel through,
  which is held and sounds at once.

## The menu

Right-click the panel:

- **Grain**: **Short (0.17 s)**, **Medium (0.34 s)** (the default) or
  **Long (0.68 s)**: how long a slice is. See Grains, below.
- **Wide (each side its own random phases)**: off by default. Off, the two
  sides share their random phases and keep the difference in phase they
  had on the tape, so a sound in the middle stays in the middle and a
  sound to one side stays there; a mono input gives two identical sides.
  On, each side gets its own random phases, so any sound, mono included,
  fills the whole width.
- **At the end of a pass**: **Go round** or **Stop (until TRIG)** (above).
- **While recording**: **Drift** (the default) or **Follow**: what the
  read head does while the tape is not frozen. Each has a line under it
  in the menu.
  - **Drift**, "Stretch acts on the input as it records": above x1 the
    read head travels at Stretch's pace, falls behind the input and
    returns to the Position flag when the oldest sound catches it: a
    slowed, evolving digest of the input that skips most of it. At x1 it
    sits on the flag; at the stop it holds. This is how 2.0.47 and
    earlier behaved while recording.
  - **Follow**, "Stretch waits for Freeze; the input plays on at the
    flag": the read head stays on the flag whatever Stretch says, so
    you hear the input as it comes (smeared, Position's distance late,
    plus about a grain), and Stretch takes effect once the tape is
    frozen: capture first, stretch after. The window's corner says
    FOLLOW and Stretch's tooltip adds "(waits for Freeze)". Stretch's stop
    does not hold while following: to hold, freeze.

  Frozen, the two are the same in every respect. A press in the window,
  TRIG, Freeze and the Stop setting keep their rules in both.
- **Keep the frozen tape with the patch**: on by default. See "With the
  patch".

## Grains

A grain is the slice of tape turned into sound at one time. A new grain
starts every half grain, and each overlaps the last by half; because each
has its own random phases, the overlap is a crossfade between two
unrelated sounds, shaped so the loudness stays steady through it. So
everything that changes between one grain and the next (a jump of the read
point, a new comb, a new Tilt) comes in as a dissolve over half a grain.

| Rate | Short | Medium | Long |
|---|---|---|---|
| 44.1 kHz | 8192 samples, 0.19 s | 16384, 0.37 s | 32768, 0.74 s |
| 48 kHz | 8192, 0.17 s | 16384, 0.34 s | 32768, 0.68 s |
| 88.2 kHz | 16384, 0.19 s | 32768, 0.37 s | 65536, 0.74 s |
| 96 kHz | 16384, 0.17 s | 32768, 0.34 s | 65536, 0.68 s |
| 192 kHz | 32768, 0.17 s | 65536, 0.34 s | 131072, 0.68 s |

A grain is never longer than 131072 samples, so above 192 kHz Long is
shorter than 0.68 s (at 384 kHz it is the same size as Medium). A longer
grain smears more and resolves pitch more finely; a shorter one follows
changes sooner.

Frequencies under 16 Hz are dropped, so the random phases cannot make the
output wander. Silence in gives exact silence out once the last grain has
ended. The random numbers come from a generator seeded when the module is
added: a reopened patch has new random phases, as any noise source has.

## Blur, Tilt and Shift

Knobs only, each read once a grain. Each does nothing at all at its
default.

- **SHIFT** moves the whole spectrum up or down by up to two octaves (±24
  semitones). Noon is no shift, exactly, within a quarter of a semitone
  either way; the tooltip says "0" there and "+7.0" (semitones) elsewhere.
  What would pass the top of the band is dropped, not folded back. A tone
  shifted down stays one tone. A tone shifted up is spread over more
  frequency lines in proportion to the shift (see What to expect).
- **BLUR** smooths the spectrum along a scale of pitch, so it is as wide in
  the bass as in the treble: 0 to 1 octave (the tooltip says octaves). At
  full a chord becomes coloured noise. It keeps the sound's energy.
  Default 0.
- **TILT** is a slope across the spectrum about 1 kHz: noon is flat, left
  darker, right brighter, up to 6 dB per octave either way (the tooltip
  says dB per octave). The gain at any frequency is held within ±24 dB.
  It is a gain, not only a cut: fully right raises everything above 1 kHz,
  by 6 dB at 2 kHz and up to 24 dB from 16 kHz, so bright material can
  reach the output's soft bound; fully left raises the bass the same way
  (up to 24 dB at about 60 Hz and below).

They act in that order (Shift, Blur, Tilt, then the comb), so what Shift
moves meets the others at its new frequency.

## Harmonics

A comb in the spectrum that passes only the energy near a note and its
overtones.

- **HARMONICS** is the amount: 0 % (the default) takes the comb out of the
  path altogether; 100 % passes only its teeth; between, the two are mixed.
  Its CV, at full attenuverter, crosses it with 10 V.
- **ROOT** is the comb's note, in semitones from C1 to C7, C4 at noon.
  Default C2. The tooltip names the note ("C2"); you can type a note
  ("F#3", "Bb1") or a number of semitones from C4.
- **V/OCT** moves the note from Root by a volt an octave: at 0 V the comb
  is on Root's note, as an oscillator is. It is not held to semitones.
  Voltages past ±12 V read as ±12 V.
- **A polyphonic cable at V/OCT makes a chord**: one comb for each
  channel, up to 16, all with the same Partials and Width, and a frequency
  passes if any of them passes it.
- **PARTIALS**, 1 to 32 in whole steps: the teeth stand at the note and its
  overtones (the note times 1, 2, 3 and so on), equally strong. Teeth above
  the top of the band, or under 16 Hz, are left out. Default 8.
- **WIDTH** is how wide each tooth is, from 5 cents (a clear pitch) to 300
  (the source comes back through); the tooltip says cents, and you can
  type a number of cents. Default 25 cents. A tooth has a soft, bell-shaped
  edge, and is never narrower than the transform can show: the frequency
  line that holds a tooth always passes whole, so no tooth disappears on a
  low note.
- **Loudness.** The comb's output is brought back up by an amount
  measured from the sound itself, grain by grain: as much as the comb
  took away, never more than 8 times (+18 dB), and the same for both
  sides so a panned sound stays where it is. So a sound already in tune
  with the comb (a saw or a chord on Root's notes) keeps its level as
  Harmonics comes up: measured within half a dB. Noise, which the comb
  thins much more, is brought up by the full +18 dB and still comes out
  quieter (about 6.6 dB down at the defaults). A sound between the teeth
  is cut, not raised to full level (a sine half way between the first two
  teeth comes out about 22 dB down). Silence stays silence. The gain on a
  frequency is (1 − h) + h × makeup × tooth, h being Harmonics; at h = 0
  the comb is out of the path altogether.
- **Timing.** A change of note, Partials or Width reaches the sound at the
  next grain's start, or at the one after if the comb was being rebuilt,
  and then dissolves in over half a grain: at worst about a grain's length
  before the dissolve begins (0.34 s at Medium). The comb follows notes at
  the grain's pace and no faster. A V/OCT step never clicks.

## Mix and the latency

**MIX** is dry to the left and wet to the right, at equal power between.
Default fully wet. It moves smoothly (10 ms for its whole travel).

The dry sound is not delayed. **The wet sound is about a grain late**: a
slice is taken when it is complete, and is heard over the grain that
follows. On top of that, each grain's work is spread over the samples
after its start, so a grain starts to sound a fixed time after it was
taken: 640 samples at 44.1 and 48 kHz (14.5 and 13.3 ms), 1180 at 88.2 kHz
and 1280 at 96 kHz (13.4 and 13.3 ms): about 13 to 15 ms from 44.1 kHz
up. Below 44.1 kHz it stays 640 samples (20 ms at 32 kHz, 29 ms at
22.05 kHz, 80 ms at 8 kHz), except Short at 8 kHz, where it is 510
samples (64 ms). So mixed
dry and wet are not in time; Mix is for blending a texture under its
source, not for a tight effect.

## With the patch

Saved with the patch: the knobs, the Freeze button, and the five menu
settings (a patch saved before While recording existed opens in Drift).
The random seed is not saved.

**A frozen tape is kept with the patch**, as `tape.wav` in the module's
folder inside the patch (32-bit float stereo, in volts, at Rack's rate:
about 11.5 MB at 48 kHz, 23 MB at 96 kHz). It is written when the Freeze
button freezes the tape, a moment after the press (once the 5 ms fade
onto the tape has been written), and again when you save the patch if it
has not been written by then; in both cases only when:

- the Freeze **button** is on (a tape frozen by the gate alone is not
  kept: the button is what the patch saves, and the tape follows it);
- **Keep the frozen tape with the patch** is on (turning it on while the
  tape is frozen writes the tape then);
- the tape has not been written since it froze (one freeze, one write).

So a frozen tape survives quitting Rack without saving: Rack's autosave
keeps the module's folder, and the patch reopens frozen with its tape.
Freezing anew writes the new tape over the old one. With the button off
or Keep off, the file is removed at the next save of the patch (not at
the thaw itself: until a save, the file is only read if the patch opens
with the button on). A tape that is not frozen stores nothing. **A sound
that is only held** (Stretch at its stop, the tape recording) **is not
kept**: to keep a sound, freeze it with the button. Remember that Freeze
while held takes the slice at Position's place (Freeze, above).

A reopened patch has the same tape and the same Position, and starts its
pass from the Position flag: in Go round at once, in Stop waiting for a trigger
unless there is nothing to travel through (then held). Until the tape has
been read (on a background thread, a moment after the patch opens) the
tape is silent and frozen, and saves leave the file alone.

**A patch that opens with Freeze on but has no tape to read opens
thawed**: the Freeze button is let go, and the module records and plays
as if it had never been frozen, rather than sitting frozen on a silent
tape. That is so when the tape file is missing (a frozen patch quit
before this build, or a file deleted by hand: a line in Rack's log says
so, and nothing shows in the window), when Keep the frozen tape with the
patch was off, and when the file is there but cannot be read (below). A
cable at GATE that is high when the patch opens still freezes the tape, as
it always does, so with one the tape stays frozen (and silent) until the
gate falls: nothing was kept for a gate.

**A tape file that is there but cannot be read** (damaged, cut short, not a
WAV) is not loaded: the Freeze button is let go as above, the window says
**TAPE NOT READ** and the log says why. Most such files are refused as the
patch opens, before a sample is played; one that fails while it is read
(a moment later) lets go of the button as soon as the read fails, unless
you have pressed Freeze yourself meanwhile, in which case the button stays
as you left it. The file stays until the next save, which removes it (the
button is off), or until a new freeze writes over it. The words stay
until the file is written or removed, or the module is initialised.

**A frozen tape that could not be written** is not kept: the window says
**TAPE NOT SAVED** and the log says why. Written at the freeze, the old
file is left as it was, and the next save tries again; at a save, the old
file is removed. The words stay until the tape is written or a save
removes the file as planned.

**When Rack's sample rate changes**, a frozen tape is converted to the new
rate off the audio thread; until it is ready the tape is silent and stays
frozen, and the wet sound fades with its last grains. A tape that is not
frozen starts again from silence, the recording fading in over 5 ms so
nothing clicks onto the tape. The file stays at its old rate until the
tape is frozen anew (it is converted again each time it is read).

**Duplicating a frozen module** (Ctrl+D): what the copy holds is not yet
tested. The copy opens with Freeze on and reads the tape file from its
own folder in the patch, so it may hold the original's tape, an older
one, or silence.

## Specifications

- **IN L, IN R**: audio, ±5 V. IN R takes IN L when unpatched.
  Polyphonic cables are summed.
- **GATE** (Freeze), **TRIG**: high from 1 V, low under 0.1 V.
- **Stretch CV, Position CV, Harmonics CV**: knob plus attenuverter ×
  volts ÷ 10, held to the knob's range. These, GATE and TRIG take a
  polyphonic cable's first channel.
- **V/OCT**: 1 V an octave, up to 16 channels, each a comb; held to ±12 V.
- NaN or infinite voltages at any input read as 0 V.
- **L, R**: exact up to 8 V, then softly bounded, never past 10 V.
- **RUN**: 0 or 10 V; the dip is 1 ms (at least one sample).
- **Tape**: 30 s, stereo, at Rack's rate; 11.5 MB at 48 kHz.
- **Grain**: see the table under Grains. Overlap: half a grain.
- **Latency of the wet sound**: about one grain, plus 640 samples at 44.1
  and 48 kHz, 1180 at 88.2 kHz, 1280 at 96 kHz, 2560 at 192 kHz.
- **Sample rates**: 8 kHz to 768 kHz. Outside that the module keeps its
  old rate or is silent, and the log says so.
- **CPU**, % of one core at 48 kHz on an Apple M-series Mac (M5 Pro),
  measured 2026-11-26: 0.11 at the defaults with nothing patched (a grain
  is made every half grain even of silence), 0.11 recording a tone, 0.33
  at its busiest (the Long grain, a 16-note comb with V/OCT moving, Blur
  and Shift up). The work is spread so no one sample does more than one
  transform: the most work on any single sample measured about 70 µs at 96 kHz
  with the Long grain and Wide on, and 34 µs at 48 kHz, with the core at
  its working clock. From a light load the first grains took up to about 1.7 times as
  long (about 110 to 120 µs at 96 kHz).

## What to expect

These are limits of the method or of this build, found while it was
built. None is a fault you can fix with a setting, unless one is named.

- **The wet sound is about a grain late** (0.17 to 0.68 s at 48 kHz) plus
  13 to 15 ms (more below 44.1 kHz: see Mix and the latency), on top of
  Position's distance (about 3 s at the default). Mixed with the dry sound
  it is not in time.
- **A pure tone wavers in level.** Where two grains overlap, their random
  phases may add or cancel, and a pure tone fills only a few frequency
  lines, so its level over ten seconds can wander by up to about 2 dB at
  the Long grain (about 1 dB at Short); over a minute it averages out to
  within half a dB. Noise holds steady, within a few hundredths of a dB;
  a held chord of a few notes stays within about half a dB over each ten
  seconds (measured on a held triad). This waver is part of the method's sound.
- **A frozen full-level sound peaks well above its input.** A held 5 V
  sine comes out at the same loudness but as a narrow band of noise, whose
  peaks reach 13 to 14 V. The output's soft bound (exact up to 8 V) then
  works on those peaks, adding a little roughness (its products are about
  37 dB down). Any sound comes out with the peaks of noise at its own
  loudness, so a loud pad or chord can reach the bound too. Turn the
  input down if you hear it.
- **Noise through the comb comes out quieter** than it went in (about
  6.6 dB at the defaults at full Harmonics): the comb's makeup stops at
  +18 dB. Material in tune with the comb keeps its level.
- **The comb follows notes at the grain's pace.** A new note starts to
  dissolve in up to a grain after it arrives, so a quick line runs
  together. Slow chord changes work best; Short follows fastest.
- **Low roots want the Long grain.** A tooth is never narrower than about
  2.7 of the transform's frequency lines, which at a low note is wide in
  cents: about 200 cents at 65 Hz (C2) with the Medium grain at 48 kHz.
  Long halves that; Short doubles it.
- **A tone shifted up is widened** in proportion to the shift: at +24
  semitones (×4) with the Medium grain, a tone is about 47 Hz wide. Large
  upward shifts sound rough.
- **A held sound is not kept with the patch** unless the tape is frozen by
  the button (above).
- **A change of Grain bumps the level for one grain** (up to about 3 dB,
  most from Long to Short), as the two sizes overlap. It does not click.
- **Each freeze by the button writes the tape to disk** (when Keep the
  frozen tape with the patch is on): about 11.5 MB at 48 kHz, 23 MB at
  96 kHz, 184 MB at 768 kHz, a moment after the press, on a background
  thread.
  Nothing in the audio waits for it, and it is not heard.
- **Deleting the module while it reads or writes its tape** (just after a
  patch opens, during a rate change's conversion, or just after a press of
  Freeze) holds Rack's audio until that read or write finishes.

## Patches to try

- **Hold a chord.** Rigging (or Fathom) into IN L and IN R, POSITION
  fully right. While a chord sounds, press FREEZE: FROZEN AT END, the
  chord left sounding under the piece. Turn Blur up for a softer bed; Tilt left to darken it.
  Press FREEZE again to let the music move on.
- **A tuned drone from noise.** Hadal's L and R into IN L and IN R.
  HARMONICS to 100 %, Grain Long (menu). Shoal running Harrington 1200:
  its B, C and D outputs (the chord's three notes) merged into one
  polyphonic cable (VCV's Merge, from Fundamental) into V/OCT. Shoal's
  pitch CV reads 0 V as a C, so with ROOT on any C the comb plays Shoal's
  notes; set its octave by ear to the voice it accompanies (C2, the
  default, or lower for a deeper drone). Trigger Harrington's moves slowly (a slow clock or
  Squall into its TRIG 2 to 4): each new chord dissolves in over about a
  grain. Width at its default gives a clear pitch; turn it up to let the
  noise back in. Partials sets how bright it is.
- **A slow scan.** Fill the tape with something that changes over the 30 s
  (a field recording from Eddy, a Rigging phrase), turn STRETCH to its
  stop (∞), then press FREEZE: FROZEN HELD, the slice on the Position
  flag (a freeze starts its pass there). Neap's cycle 5 into Position
  CV, POSITION at noon and its attenuverter fully right, so Neap's ±5 V
  crosses the whole tape. Each grain takes the slice where Neap has put the
  flag (watch the flag, the gold bar and the slice sweep across the
  window together): the 30 s are scanned back and forth, a cycle about every half
  minute at Neap's default Rate. Neap's TIDE in its place (Weight to the
  right, so the slow cycles lead) wanders over the tape and does not
  retrace its path. Turn Stretch back off the stop for the read head to
  creep on from wherever the scan has left it.
- **Capture, then stretch** (While recording: Follow):
  1. Menu, While recording: **Follow**, and At the end of a pass: **Go
     round** (the default; in Stop the freeze would wait, FROZEN STOPPED,
     for a trigger). Patch a sound into IN L and IN R.
     The corner says FOLLOW and the gold bar sits on the flag: you hear
     the input as it comes, smeared, Position's distance late, at any
     Stretch.
  2. Play the phrase you want to stretch (up to 30 s). When it is over,
     press **FREEZE**: the tape stops and a pass starts from the flag.
  3. Press in the window just left of the phrase's start (its first peaks
     on the glass): the flag goes there and the pass plays from it.
  4. Set **STRETCH** and listen: the phrase is drawn out at that pace, to
     the end of the tape, then again from the flag (Go round). Press in
     the window again to start from elsewhere.
  5. To go back to the live input: turn POSITION fully right (or press
     at the window's right edge: FROZEN AT END, the last moment held),
     then let FREEZE go. The gold bar goes to the flag at now and you
     hear the input within about a grain. If you let FREEZE go with the
     flag still back at the phrase's start, the gold bar goes there and
     plays on at the input's own speed: first the tape from the flag to
     the freeze (the phrase again, unstretched), then the new input, as
     far behind it as the flag is.
- **Draw a clip out into a long loop** (Eddy into Doldrums into Eddy):
  1. **Eddy A**: load the clip (right-click, Load file), LOOP off. Its L
     and R outputs into Doldrums' IN L and IN R.
  2. **Doldrums**: menu, At the end of a pass: **Stop (until TRIG)**.
     Leave Freeze off. The pass under way goes on; there is no need to
     wait for it to end.
  3. Right-click POSITION and type a little more than the clip's length,
     in seconds ("11" for a 10 s clip), so that once the clip is on the tape
     the Position flag will be just before its start. (Not fully right:
     there a freeze would hold and sound.)
  4. Press PLAY on Eddy A. The clip goes onto Doldrums' tape (you see it
     scroll in from the right). As soon as it ends, press Doldrums'
     **FREEZE**. The pass under way ends at once and the corner says
     **FROZEN STOPPED**: within about a grain nothing sounds and RUN is
     low.
  5. Put the Position flag exactly at the clip's start with the
     **POSITION knob** (or by typing seconds), watching the flag in the
     window. Nothing sounds while you do. Do not press in the window for
     this: a press plays from there, which in Stop starts a pass (if you
     do, let it play out to FROZEN STOPPED again before step 7). The pass
     will run from the flag to the end of the tape, so anything recorded
     after the clip and before the freeze is stretched too: freeze
     promptly. The first grain is the slice that ends at the flag, so the
     clip's onset comes in over the first grains, as a fade in: that is
     the method, not a flag set late; leave the flag at the clip's start.
  6. Set STRETCH (at 8 a 10 s clip becomes about 80 s).
  7. **Eddy B**: Doldrums' L and R into its IN L and IN R, and Doldrums'
     **RUN** into its **REC** gate. Patch RUN while it is low (it is, while
     STOPPED), or Eddy B sees a rising edge and starts recording.
  8. A single trigger into Doldrums' **TRIG** (a button module, or Beacon:
     its push button, its A output into TRIG). RUN rises as the first grain
     sounds, so Eddy B starts recording with the fade in; it falls when the
     last grain has faded, so Eddy B stops with the fade out and plays the
     recording back as its tape. That is exactly one pass.
  9. Leave Freeze on until the pass has ended. Letting Freeze go while the
     pass is still playing starts a new pass from the flag, on a tape that
     records again: RUN dips and Eddy B starts a new recording over its
     take. Unfreeze only after the corner says FROZEN STOPPED.

  Eddy B holds 5 minutes at 48 kHz (2:54 at 96 kHz): a 10 s clip at up to about x30, or the whole
  tape at up to x10. A second trigger at TRIG records a new pass over the
  first.

## Bypass

Bypassed, the inputs pass to the outputs (IN R taking IN L when
unpatched); RUN is low and the Freeze cap dark. The module does nothing
else while bypassed: the tape does not record and Freeze is not seen (a
Freeze pressed or released meanwhile acts when the bypass ends). When the
bypass ends, the tail of the last grain from before it is heard, and the
recording starts again with no fade, so the tape has a join there.

## Other details

- **Initialize** silences the tape, returns every knob and the Freeze
  button to its default, and the menu to its defaults (Medium, Wide off,
  Go round, Drift, Keep on). The recording starts again on the silent tape with
  a 5 ms fade in, so nothing clicks onto it. The grains already playing
  fade out on their own, with RUN still high until they have ended, and
  Mix moves to its default smoothly.
- **Undo after Initialize** brings back the knobs, the Freeze button and
  the menu, but not the tape: Freeze comes back on over the new, nearly
  empty tape (a new freeze), and that tape is written at once over the
  one in the module's folder. The patch file you last saved still holds
  the old tape: to get it back, reopen that file without saving.
- **Randomize** leaves Freeze, Mix and the menu alone (it would otherwise
  freeze the tape or take the effect away).
- Each Doldrums holds its tape in memory: 11.5 MB at 48 kHz. While a
  frozen tape is converted to a new rate, the old one is kept beside it
  until the new one is ready.
- The lettering in the window is D-DIN Condensed, shipped with the plugin
  under its licence (SIL Open Font License 1.1).
- The transform is PFFFT by Julien Pommier, vendored unchanged (see the
  plugin's README).
