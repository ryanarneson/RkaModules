# Changelog

## Test builds

- 2.0.4 (2026-11-16): Hadal: Crackle soft-clipped and about 9 dB louder
- 2.0.3 (2026-11-16): Hadal: first build for listening (rumble, hiss, crackle, Depth, Drift); Fog bottom row at 116 mm
- 2.0.2 (2026-11-16): Fog: L/R phase fix below 300 Hz, Drift/Damp/Visibility smoothing, wet level scales with decay time, faster leveller attack, factory presets and manual
- 2.0.1 (2026-11-16): Fog: cache-stagger CPU fix, silence sleep, matched wet level, wet leveller, FRET/SMOKE/ICE voicings, ICE feed-forward octave

## 2.0.0 (unreleased)

- New module: **Fog**, a 10 HP stereo reverb:
  - Visibility sets the decay time in seconds, 0.4 s to 80 s, lettered
    CLEAR to DENSE; the time holds at any Size, and the tail always decays.
  - Size, Damp, Bloom and Drift with CV and attenuverters, and CV on Mix.
  - Four modes in the menu: HAAR (dense wash), FRET (dark, slow), SMOKE
    (echo-led) and ICE (glassy, with an octave shimmer).
  - Wet level scales with decay time, as in ValhallaRoom, matched to dry at
    noon Visibility; a wet leveller and limiter keep loud drones in check.
  - Low cut, drift rate and dry kill in the menu; the low end is
    mono-compatible below 300 Hz.
  - Sleeps (near-zero CPU) when the input and tail are quiet.
  - Four factory presets: Becalmed, Nebula, Harbour, Whiteout.
  - Drawn as a visibility scale.
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
  - Drawn as a pendulum.
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
  - Drawn as an echo sounder.
- New module: **Sluice**, a 10 HP quad VCA:
  - Four rows, each with a Level dial (default closed) and a CV input;
    CV adds to Level, 10 V across the travel.
  - One Lin/Exp switch for all rows (Exp is a 60 dB decibel taper).
  - DC-coupled, so it works on audio and on CV.
  - Unpatched inputs take the row above (the rope); unpatched outputs mix
    into the row below. Copies add up: 4x with all four rows open.
  - Polyphonic to 16 voices; a mono row spreads into wider rows.
  - A light per row showing how open it is.
  - Drawn as a sluice gate.
- New module: **Shoal**, 28 HP: the Ornament & Crime v1.3.6 firmware and
  all 13 apps in hardware mode; its settings save with the patch.
  - Drawn as ripples from two sources.
- One look for every module, "Instrument": brass and ivory on near-black,
  main controls on enamel dial faces, knurled knobs, a traced line from
  each CV jack through its attenuverter to the control it moves, outputs
  on ivory tiles, cyan only for lights and screens, lettered in D-DIN
  Condensed. The plugin's own knobs, jacks, buttons, lights and screws;
  Rack's dark-panels preference gets the same drawing turned down.
