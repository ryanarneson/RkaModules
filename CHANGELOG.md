# Changelog

## 2.0.0 (unreleased)

- New module: **Sway**, an 18 HP polyphonic LFO:
  - SIN, TRI, SAW, SQR (pulse width) plus a MORPH output with SKEW, SMOOTH
    (round or wavefold) and MORPH (sin → tri → saw → square).
  - STEP and RAND random outputs, new values every cycle.
  - Slow (to 4.5 h per cycle), Normal and Audio ranges; FM with
    attenuverter, 1 V/oct at full.
  - CLOCK sync with ÷16…×16 ratios on the RATE knob; RESET; HOLD.
  - 1-SHOT: RESET runs one cycle, for envelopes.
  - CHANNELS (1–16) and SPREAD for phase-offset polyphony.
  - CV with attenuverters for PW, PHASE, SKEW, SMOOTH, MORPH.
  - The phase is kept in double precision, so slow rates stay accurate.
- "Nightglow" look shared by all modules: navy panels, glowing cards,
  colour-coded outputs, white knobs.
