# Changelog

## 2.0.0 (unreleased)

- New module: **Sway**, a 12 HP polyphonic LFO:
  - SIN, TRI, SAW, SQR (pulse width) plus a MORPH output with SKEW, SMOOTH
    (round or wavefold) and MORPH (sin → tri → saw → square).
  - RAND output, a new random value every cycle, from stepped to gliding
    as SMOOTH turns left.
  - EOC trigger at the end of every cycle (chain one-shot envelopes).
  - Slow (to 4.5 h per cycle), Normal and Audio ranges; FM with
    attenuverter, 1 V/oct at full.
  - CLOCK sync with ÷16…×16 ratios on the RATE knob; RESET; HOLD.
  - 1-SHOT: RESET runs one cycle, for envelopes.
  - CHANNELS (1–16) and SPREAD for phase-offset polyphony.
  - CV with attenuverters for PW, PHASE, SKEW, SMOOTH, MORPH.
  - The phase is kept in double precision, so slow rates stay accurate.
  - A rate light on the RATE dial, following channel 1: it blinks once
    per cycle up to 10 Hz and glows steadily at half brightness above.
  - Drawn in the "Instrument" look, as a pendulum.
- New module: **Fathom**, a 10 HP bass drone voice:
  - A sine sub one or two octaves under the pitch, and a seven-saw
    supersaw body with the JP-8000's detune and mix curves (after
    Befaco Oneiroi / Szabo), high-passed at the fundamental.
  - PITCH (snapped to semitones, C2 at centre; free in the menu), SUB,
    DETUNE, SPREAD and DRIFT.
  - DRIFT walks each saw's pitch and level slowly so a held note moves.
  - A Moog-style four-pole ladder filter, 2x oversampled, with CUTOFF,
    RES (to self-oscillation) and DRIVE into its saturators; the low end
    holds steady as resonance rises. Low-pass 24, low-pass 12, band-pass
    and high-pass in the menu, and a choice of the sub going through the
    filter or around it.
  - CV with attenuverters for DETUNE, DRIFT, RES, DRIVE and CUTOFF
    (1 V/oct).
  - Stereo out through a soft clipper, plus a separate SUB out.
- "Nightglow" look shared by all modules: navy panels, glowing cards,
  colour-coded outputs, white knobs, and the plugin's own small
  attenuverter trim; panels can carry glowing rings.
- New module: **Shoal**, 28 HP: the Ornament & Crime v1.3.6 firmware and
  all 13 apps in hardware mode; its settings save with the patch.
