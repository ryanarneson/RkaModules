# Beacon

A polyphonic envelope, 10 HP: the light that comes up, holds and fades.
ADSR, a one-shot AR and Hold on a switch (after the Instruō cèis), Attack, Decay
and Release from 1 ms to 60 s, Shape from a slow swell through linear to
the classic analog curve, a gate for every stage, a trigger at each stage
start, and a window that draws the envelope with a dot riding it.

![Beacon panel](Beacon.png)

## The panel

From the top:

- The **window**: the envelope the knobs make, lettered A D S R (A and R in AR).
- **SHAPE**, and the **ADSR / AR / HOLD** switch. Click its upper half
  to move it up one position, its lower half to move it down one, or drag
  it; it stops at the ends, so going from AR to ADSR never passes
  through Hold.
- **A**, **D**, **S** and **R**, each with its stage light, over its
  **CV** input and its stage **gate** output (on the ivory tiles).
- **GATE** and **RETRIG**; the push button under GATE.
- On ivory tiles: **TRIG**, **OUT** and **INV**.

## Modes

- **ADSR** (switch up, the default): a rising edge on GATE starts Attack.
  Attack and Decay run while the gate is high, then Sustain holds. When the
  gate falls, Release runs from wherever the envelope is, even in the
  middle of Attack. How long the gate stays high decides how much of the
  envelope you get (below).
- **AR**: a one-shot. A rising edge on GATE runs Attack all the way to
  10 V, then Release at once. How long the gate stays high makes no
  difference, and the D and S knobs and gates play no part; the window
  draws only A and R. Use it for triggers, clock pulses, and any time you
  want the same envelope from every edge.
- **HOLD**: the envelope stops and Beacon is a slew limiter. The
  **Sustain CV** input is the signal; the Sustain knob adds 0 to 10 V.
  OUT follows it, rising at Attack's rate and falling at Decay's (the knob
  sets the time for a 10 V move). Release, Shape, GATE, RETRIG and the
  button do nothing here.

  In Hold the A, D and S gates mean "rising", "falling" and "caught up";
  an input that moves slower than the slew reads as S.

(Patches saved before build 2.0.44 with the switch on Trig open in AR. A
pulse shorter than Attack behaves as before; a longer gate now gives AR
instead of running on into Decay and Sustain.)

### Gates, pulses and triggers: what you get

What reaches GATE, and how long it stays high compared with Attack and
Decay, decides the envelope in ADSR mode. In AR mode it never matters.

| Into GATE | ADSR | AR |
|---|---|---|
| A trigger, or a pulse shorter than Attack | A blip: it rises only while the pulse is high, then releases from there. A 1 ms trigger into a 100 ms Attack barely moves. | Up to 10 V, then Release |
| A pulse longer than Attack, shorter than Attack + Decay | Up to 10 V, part of the way down Decay, then Release from there | Up to 10 V, then Release |
| A gate longer than Attack + Decay | The full ADSR: Sustain holds until the gate falls, then Release | Up to 10 V, then Release (the rest of the gate is ignored) |

Pulse widths are easy to misjudge. A Sway square at 1 % width is high for
1 % of its cycle: 10 ms at 1 Hz, 100 ms at 0.1 Hz. Into ADSR with a 50 ms
Attack, the first is a blip and the second rises to the top and starts to
decay before it releases. Into AR both give the same envelope. If you want
every pulse to give the whole shape, use AR; if you want the gate's length
to shape the envelope (a key held longer sustains longer), use ADSR.

### Edges while it runs, and RETRIG

In ADSR and AR, a new rising edge while the envelope runs starts Attack
again from where the envelope is, and **RETRIG** does the same without a
new gate (only while the gate is high). The level never jumps.

An Attack that starts part of the way up finishes the curve it would have
drawn from the bottom: it picks it up at the present level and takes only
the time that is left. So a retrigger from Sustain at 6 V takes 40 % of
Attack at Shape noon (about 64 % at the default Shape, 80 % fully right,
12 % fully left, where the curve's slow start is already behind it). Fast
triggers into a long Attack climb steadily and reach the top in Attack's
time, as on an analog envelope. Decay and Release always take their set
time, from wherever they start, as an analog release does.

Moving the switch never moves the output: from Hold back to ADSR or AR,
Attack starts from the present level with the gate high, Release with it
low. Between ADSR and AR the running stage carries on; an Attack that is
running when you switch to AR goes straight on into Release.

## Controls

- **A, D, R**: 1 ms to 60 s, each decade an equal share of the turn (noon
  is about 245 ms). The tooltip shows ms or s; type a time to set it
  ("250" or "250 ms", "1.5 s"). Defaults 10 ms, 300 ms, 1 s.
- **S**: the sustain level, 0 to 10 V (default 5 V). Moving it during
  Sustain moves the level smoothly, so an LFO on its CV makes a tremolo.
- **CV**: added to the knob, 10 V for the whole travel; the time is held
  between 1 ms and 60 s, the sustain level between 0 and 10 V.
- **SHAPE**: one curve for every stage. Noon is linear; right, the
  classic contour (a fast, rounded attack, decays with a long tail); left,
  the mirror (an attack that swells, decays that hold and then fall).
  Default three quarters right. Turned while a stage runs, the rest of
  the stage takes the new curve from where the level is, so the level
  does not jump and the stage still ends on time.
- **Push button**: a gate on every channel while held (shift-click
  latches it).
- **GATE, RETRIG**: gates at the plugin's usual thresholds (high at 1 V,
  low below 0.1 V).

### RETRIG for legato

With notes played legato (the next key down before the last is up) the
gate never falls, so there is no new rising edge and the envelope stays
in Sustain. Patch the sequencer's or MIDI-CV's trigger to **RETRIG**: each
trigger sends the envelope back up through Attack from its present level,
so it never jumps and the held notes stay connected. RETRIG does nothing
while the gate is low.

## Outputs

- **OUT**: 0 to 10 V (Hold: ±10 V). **INV**: OUT upside down.
- **A, D, S, R**: 10 V while that stage runs. In Hold: A while the slew
  is limiting upward, D while limiting downward, S when OUT has caught
  the input. With Sustain at 0, the S gate still goes high while the gate is
  high; the output sits at 0 V.
- **TRIG**: a 1 ms, 10 V trigger at the start of every stage, including
  the return to rest at the end of Release.
- The stage lights are lit while that stage runs, on channel 1; a stage
  too short to see still lights for 40 ms.

## The window

The envelope drawn from the knobs (with channel 1's CVs) and Shape, as the
mode will run it: in ADSR the four stages, lettered A D S R; in AR only
Attack and Release. It is a picture of the envelope, not a meter. Each
stage's width grows with the log of its time, so a 1 ms Attack stays
visible beside a 60 s Release; the window shows shape and position, not a
ruler, and a scope beside it will not line up in time. The curve in each
stage is the curve the output follows: at Shape noon both are straight.

The knobs' shape is drawn dim. The running stage, and its letter, are
bright, and a dot rides it where channel 1 is: always at the output's
level, always in the running stage's own span.
- An Attack that starts part-way (a retrigger, a gate back during
  Release) is the drawn Attack curve itself: the dot joins it at the
  present level and rides it to the top.
- A Release that cuts a stage short (a gate falling mid-Attack) is drawn
  in the R span, bright, from the level it started at down to 0 V.
- Sustain is bright while it runs; with Sustain at 0 V its span stays dim
  and the dot waits at the end of the line.

In Hold the window shows a scrolling line of the output.

## Patches to try

- **A plain voice.** MIDI-CV GATE into GATE and V/OCT into Fathom; OUT
  into a Sluice level CV, and again into Narrows' cutoff. Play notes,
  turn A, D, S and R in turn, then Shape fully left, at noon and fully
  right on the same times.
- **Legato.** MIDI-CV's trigger into RETRIG as well as its gate into
  GATE. Overlapping notes each climb from where the envelope is.
- **Slow swells.** Squall's GATE into GATE, AR, A about 8 s, R 20 s,
  Shape left; OUT into Sluice's level CV on a Fathom drone, into Fog.
  Each gate swells the drone in and lets it fade. Try Shape right.
- **Events from the stage gates.** The A gate into Rigging's STRUM (a
  strike as Attack starts) and TRIG into Narrows' PING: a long gate gives
  five pings: the start of Attack, Decay, Sustain and Release, and the
  return to rest.
- **Ducking.** INV into Sluice's level CV on an open drone: the drone
  gives way while the envelope rises and comes back as it falls.
- **Glide.** A sequencer's V/OCT into the Sustain CV, Hold, S fully
  left; OUT into Fathom's V/OCT. A sets the glide up, D the glide down.
  Watch the A, D and S gates.
- **Chords.** MIDI-CV with four voices: GATE into GATE, OUT into a
  polyphonic Sluice. Each voice has its own envelope; the window and
  lights follow voice 1.
- **The same pluck from any pulse.** A Sway square at 1 % width into
  GATE, AR, A 5 ms, R 300 ms, OUT into Sluice. Change Sway's rate: every
  pulse gives the same pluck. Switch to ADSR to hear the pulse's width
  shape it instead.
- **Pulses on a held sound.** Hold the push button (or send a long gate)
  and put Squall's GATE or a clock into RETRIG: the envelope re-swells
  from where it is. D and S set how far each pulse falls back.

## Polyphony

In ADSR and AR, one envelope for each channel of GATE (one when it is
unpatched); in Hold, one for each channel of the Sustain CV. RETRIG and
the CVs take a channel each from a polyphonic cable, or one value for all
from a mono one. Every output carries the channel count.

## Other

- Bypass: the outputs fall silent, and every envelope goes to rest at
  0 V; on un-bypass nothing resumes (a gate still high starts a new
  Attack from 0 V).
- Initialize: every envelope at rest at 0 V.
- A sample rate change keeps every envelope where it is.
- Saved with the patch: the knobs and the switch.
