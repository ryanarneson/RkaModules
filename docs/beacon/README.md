# Beacon

A polyphonic envelope, 10 HP: the light that comes up, holds and fades.
Gate, Trig and Hold on a switch (after the Instruō cèis), Attack, Decay
and Release from 1 ms to 60 s, Shape from a slow swell through linear to
the classic analog curve, a gate for every stage, a trigger at each stage
start, and a window that draws the envelope with a dot riding it.

*First draft: the defaults and Shape's ends are to be tuned by ear.*

![Beacon panel](Beacon.png)

## The panel

From the top:

- The **window**: the envelope the knobs make, lettered A D S R.
- **SHAPE**, and the **GATE / TRIG / HOLD** switch.
- **A**, **D**, **S** and **R**, each with its stage light, over its
  **CV** input and its stage **gate** output (on the ivory tiles).
- **GATE** and **RETRIG**; the push button under GATE.
- On ivory tiles: **TRIG**, **OUT** and **INV**.

## Modes

- **Gate** (switch up, the default): a rising edge starts Attack; Attack
  and Decay run while the gate is high, then Sustain holds. When the gate
  falls, Release runs from wherever the envelope is.
- **Trig**: Attack always runs to the end. If the gate is still high,
  Decay and Sustain follow; if not, Release starts from the top. A trigger
  gives an AR (AD) envelope, a long gate the full ADSR.
- **Hold**: the envelope stops and Beacon is a slew limiter. The
  **Sustain CV** input is the signal; the Sustain knob adds 0 to 10 V.
  OUT follows it, rising at Attack's rate and falling at Decay's (the knob
  sets the time for a 10 V move). Release, GATE, RETRIG and the button do
  nothing here.

In Gate and Trig, a new rising edge while the envelope runs restarts
Attack from where the envelope is, and **RETRIG** does the same without a
new gate (only while the gate is high). Every stage takes exactly its set
time, wherever it starts.

Moving the switch never moves the output: from Hold back to Gate or Trig,
Attack starts from the present level with the gate high, Release with it
low.

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
  Default three quarters right.
- **Push button**: a gate on every channel while held (shift-click
  latches it).
- **GATE, RETRIG**: gates at the plugin's usual thresholds (high at 1 V,
  low below 0.1 V).

## Outputs

- **OUT**: 0 to 10 V (Hold: ±10 V). **INV**: OUT upside down.
- **A, D, S, R**: 10 V while that stage runs. In Hold: A while rising,
  D while falling, S when settled.
- **TRIG**: a 1 ms, 10 V trigger at the start of every stage, including
  the return to rest at the end of Release.
- The stage lights show channel 1's stage; a stage too short to see
  keeps its light for a moment (40 ms).

## The window

The envelope drawn from the knobs (with channel 1's CVs) and Shape. Each
stage's width grows with the log of its time, so a 1 ms Attack stays
visible beside a 60 s Release; the window shows shape and position, not a
ruler. The knobs' shape is drawn dim. The running stage's letter is
bright, and an Attack, Decay or Release that is running is drawn bright
over its span from where it really started: a gate that rises while
releasing draws Attack from that level up to 10 V, a gate that falls
mid-Attack draws Release from that level down to 0 V. A dot rides the
bright line where channel 1 is. Sustain is bright while it runs; with
Sustain at 0 V its span stays dim and the dot waits at the end of the
line. In Hold the window shows a scrolling line of the output.

## Polyphony

In Gate and Trig, one envelope for each channel of GATE (one when it is
unpatched); in Hold, one for each channel of the Sustain CV. RETRIG and
the CVs take a channel each from a polyphonic cable, or one value for all
from a mono one. Every output carries the channel count.

## Other

- Bypass: the outputs fall silent.
- Initialize: every envelope at rest at 0 V.
- A sample rate change keeps every envelope where it is.
