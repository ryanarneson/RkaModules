# Wake: a comprehensive stereo tap delay

Status: built (2026-11-20, 14 HP). Design: `docs/specs/2026-11-20-wake-design.md`;
manual: `docs/wake/README.md`. As built it is a stereo echo for struck sounds, much
smaller than this sketch: three styles (Tape, Analog, Digital), a Ratio between the
sides, ping pong, Freeze, Diffuse, Drift and a clock input. The four taps and their
outputs, the Send and Return, tap tempo, Reverse and Drive were left out. The text
below is the original idea.

The user asked for "a comprehensive delay. stereo/tap. good functionality."

![mockup](wake-mockup.svg)

**What it is.** A full-featured stereo delay, 18 HP: tap tempo and clock
sync, four taps with their own levels and outputs, a feedback path that
can be shaped and patched into, and controls for stereo movement and
texture. "Stereo/tap" was read as both tap tempo and multiple taps; if
only one was meant, the panel can shrink.

**The instrument.** A ship's wake. A Kelvin wake opens at 19.47° either
side of the ship's track whatever its speed, so the two arms are drawn at
exactly that angle from the Time dial and labelled with it. Behind the
arms run the crests, and each crest is a tap. The four taps alternate
down the left and right arms, which is also how they sit in the stereo
field.

**Sketched controls.**

- Time, the hero, scaled 10 ms to 4 s, with CV and attenuverter.
- Tempo, down the left: a lit Tap button, Divide for clock divisions, and
  a Clock input.
- Four taps: a level knob and a light each, on the four crests.
- The repeats, fanning out to the right on arcs from the Time dial:
  Feedback (with CV and attenuverter), Tone and Drive.
- Stereo and texture: Spread, Diffuse, and Mod with Depth.
- Three lit buttons: Ping (ping-pong), Reverse, Freeze.
- Mix, with CV and attenuverter.
- Send and Return, to patch other modules into the feedback loop.
- Stereo in; outputs for each tap and for L and R.

**Open questions.** Tap spacing (even divisions of Time, or adjustable);
tap panning (fixed alternate, as drawn, or per tap); whether Send and
Return are mono or stereo; gate inputs for Freeze and Reverse; maximum
delay time and what happens to pitch when Time moves; whether Diffuse
overlaps Fog; how much of this Eddy should keep.
