# Beacon: envelope and function generator

Status: built, 2026-11-23 (10 HP, polyphonic). The manual is
[docs/beacon/README.md](../beacon/README.md); the design is
`docs/specs/2026-11-23-beacon-design.md`. The text below is the original
idea.

**What changed from the idea.** No Loop button: envelopes are the
focus, and a slow repeating shape is Sway's job. The lamp became a
window that draws the envelope with a dot riding it. It is 10 HP, not 8.
Hold (a slew limiter, after the Instruō cèis 2) was added. The inverted
output stayed. Added too: a gate for each stage, a trigger at every stage
start, RETRIG and Shape.

![mockup](beacon-mockup.svg)

**What it is.** An ADSR envelope that can loop into a slow repeating
shape.

**The instrument.** A lighthouse. The lamp at the top is a light, not a
knob: it glows with the envelope's level, on an enamel face. The two
brass lines falling from it are the edges of the beam, and the stage
knobs sit on graduated arcs across it.

**Sketched controls.** Attack and Decay on the first arc, Sustain and
Release on the second, a lit Loop button between them. Gate in, Trig in
and a Time CV in. Outputs: Env, Inv (inverted) and End (end of cycle).

**Open questions.** Curve shapes; time ranges, given the user's slow
ambient use; what Time CV scales; whether Loop ignores Sustain;
polyphony. The lamp is the first hero that is not a control, which the
look's rules do not cover yet.
