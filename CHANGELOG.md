# Changelog

## Test builds

- 2.0.24 (2026-11-21): Narrows: final review fixes (the core switch keeps the level with sound passing; a comb opened by CV from noon starts at pitch); manual
- 2.0.23 (2026-11-20): Narrows: the comb is louder (it keeps noise at its level, so its peaks and a pinged comb stand out)
- 2.0.22 (2026-11-20): Narrows: first build for listening (polyphonic stereo filter: ladder and state cores, Mode sweep LP/BP/HP, drive, self-oscillation, comb, Ping, Spread)
- 2.0.21 (2026-11-20): Wake: final review fixes (crossfades add no level, so a clocked or modulated Digital echo decays as it should; Freeze holds exactly the echo that was playing; no clicks on re-freeze or on Time changes with Drift on; a paused clock keeps its tempo)
- 2.0.20 (2026-11-20): Wake: first build for listening (stereo echo: Tape, Analog and Digital styles, Ratio, ping pong, Freeze, Diffuse, Drift, clock)
- 2.0.19 (2026-11-20): Hadal: Rumble audible at depth (band 160-70 Hz, overtones from a soft clip that deepens with Depth); 2.5 V RMS at full with peaks inside +-5 V; default 80 %
- 2.0.18 (2026-11-20): Hadal: Rumble louder (3.2 V RMS at full, twice the old level; default 70 %)
- 2.0.17 (2026-11-19): Rigging: final review fixes (the pitch glide ends; DC cannot pile up in a string or thump through a level knob; Ring changes ramp; a slewed note is one strike; Initialize cancels a rake; typed values in the tooltips)
- 2.0.16 (2026-11-19): Rigging: first build for listening (six tuned strings: drone from any sound, struck by STRUM or a new note at V/OCT)
- 2.0.15 (2026-11-19): Shoal: final review fixes (lanes always match the pixels shown, overlay clipped to the rack view, Harrington patch in the manual)
- 2.0.14 (2026-11-19): Shoal: upper screen lanes and smooth views (traces, X/Y, note circle, grid); strip dividers; patches to try in the manual
- 2.0.13 (2026-11-18): Squall: final review fixes (Gust in average-gap time: no burst on a clock switch, no rate change under Weather CV; inf on Clock/Freeze reads 0 V)
- 2.0.12 (2026-11-18): Squall: first build for listening (random gates and CV: Weather, Gust, Memory, Clock, Freeze)
- 2.0.11 (2026-11-18): Ballast: expander walk capped at 32 steps (a looped chain cannot hang); Pan: LINK dark when bypassed
- 2.0.10 (2026-11-17): Pan: attach decided on Ballast's thread (fixes crackle with several engine threads)
- 2.0.9 (2026-11-17): Pan: pan law menu, no boost by default (fixes clipping at hard pans)
- 2.0.8 (2026-11-17): Pan: first build for listening; Ballast: either input alone is centred mono, DC blocker fades on (#38), keeps state across rate changes (#39), #40 fixes
- 2.0.7 (2026-11-17): Ballast: final review fixes (bypassed expanders skipped, NaN stopped at the mix, cheap chain walk)
- 2.0.6 (2026-11-17): Ballast: first build for listening (four stereo channels, VCA level CV, Master, CHAIN, DC blocker, CLIP)
- 2.0.5 (2026-11-17): Hadal: final review fixes (CRACKLE jack mono sum, cheap reset, no libm on the audio path)
- 2.0.4 (2026-11-16): Hadal: Crackle soft-clipped and about 9 dB louder
- 2.0.3 (2026-11-16): Hadal: first build for listening (rumble, hiss, crackle, Depth, Drift); Fog bottom row at 116 mm
- 2.0.2 (2026-11-16): Fog: L/R phase fix below 300 Hz, Drift/Damp/Visibility smoothing, wet level scales with decay time, faster leveller attack, factory presets and manual
- 2.0.1 (2026-11-16): Fog: cache-stagger CPU fix, silence sleep, matched wet level, wet leveller, FRET/SMOKE/ICE voicings, ICE feed-forward octave

## 2.0.0 (unreleased)

- New module: **Narrows**, a 14 HP polyphonic stereo filter:
  - Two filters on a switch: Fathom's four-pole ladder (24 dB per octave)
    and a state-variable filter (12 dB), matched in pitch, in where they
    start to sing and in level; the switch crossfades without a click.
  - Mode sweeps smoothly from low-pass through band-pass to high-pass, by
    hand or by CV.
  - Cutoff from 16 Hz to 16.4 kHz with V/OCT and an FM attenuverter, both
    at audio rate.
  - Res up to self-oscillation from 95 %: a sine of 4 to 5 V that follows
    V/OCT within 1 cent as it starts and 7 cents at full.
  - Drive into the core's saturation, levelled; a comb before the filter,
    tuned to the cutoff, with its peaks on the harmonics (right) or
    between them (left); Spread sets the two sides' cutoffs apart, up to
    half an octave each way.
  - PING knocks the filter with a short pulse: a tuned ping with Res up, a
    pluck with Comb up.
  - A filter pair for every channel of a polyphonic cable, up to 16; a
    one-channel cable feeds every voice. CV on FM, Res, Comb and Mode;
    soft-limited outputs.
  - Drawn as a strait.
- Fathom: its ladder filter and oversampler are now shared code with
  Narrows; Fathom's output is unchanged (to the bit in the Mac and Linux
  builds; within rounding on any compiler).
- New module: **Wake**, a 14 HP stereo echo:
  - Echoes from 10 ms to 10 s behind a struck sound, in three styles:
    Tape (pitch bends when Time moves; repeats soften), Analog (darker and
    grittier, more so at long times) and Digital (clean; Time changes
    without a bend).
  - Ratio sets the two sides' times against each other in seven steps;
    Ping Pong bounces the echoes left and right.
  - Feedback to 110 %, bounded; Tone darkens or thins each repeat; Diffuse
    smears them; Drift wavers the echo time, each side its own way.
  - Freeze (button and gate) holds what is in the delay, unchanged, for as
    long as wanted.
  - A clock input: Time becomes a multiple of the clock, 1/4 to 4.
  - CV on Time (with attenuverter), Feedback and Mix; stereo in and out;
    soft-limited.
  - Drawn as a ship's wake.
- New module: **Rigging**, 14 HP, six tuned strings:
  - Any sound in keeps the strings ringing: a tuned drone from noise. A
    trigger at STRUM strikes them, or with nothing in STRUM a new note at
    V/OCT does; both at once, in one tuning.
  - Seven chords (Unison, Octaves, Fifths, Minor, Sus, Cluster, Bell), a
    level per string, Tune and V/OCT.
  - Ring from 0.1 s to 60 s, the drone's loudness held steady across it;
    Damp darkens the strings as they ring; Spread rakes a strike across the
    six, from a stab to 1.5 s.
  - CV on Chord, Ring and Damp; stereo in and out; soft-limited.
  - Drawn as a ship's rigging.
- New module: **Squall**, an 8 HP random source of gates and CV:
  - Weather sets how much happens: one event in 10 minutes (Calm) to 8 a
    second (Stormy); with a clock, the chance of an event per tick.
  - Gust bunches events into self-exciting bursts and lulls without
    changing the average; Memory replays the last 4, 8 or 16 events, from
    never to an exact loop, saved with the patch.
  - Step, Swell (a smooth drift through the same values) and a gate whose
    length follows the weather; four ranges; Freeze; CV and attenuverter
    on Weather, Gust and Memory.
- New module: **Pan**, a 6 HP expander for Ballast:
  - Pan knob, CV and attenuverter for each of Ballast's four channels.
  - Mono sources panned; stereo sources by true pan (default) or balance.
  - Pan law: no boost (default; panning never raises a level) or constant
    loudness (up to +3 dB at the sides).
  - Centre changes nothing; attaching glides in from centre; LINK light;
    add it from Ballast's menu (Add Pan expander).
- New module: **Ballast**, a 12 HP four-channel stereo mixer:
  - Four stereo channels (IN R normalled to IN L), each with a Level knob
    (silence to +6 dB, unity at the long tick) and a level CV on the VCA
    law: 0 V silent, 10 V the knob's level, never above it.
  - Master with its own CV; CHAIN inputs before Master, for eight channels
    from two Ballasts; L alone carries the mono sum.
  - DC blocker (menu, on by default) and a soft limit (8 V knee, 10 V
    ceiling) with a CLIP light.
  - Expanders on its right side (Pan). Either input alone is a centred mono
    source (2.0.7 played IN R alone on the right only). Turning the DC
    blocker on fades an offset away; a sample-rate change keeps its state.
  - Drawn as a clinometer.
- New module: **Hadal**, a 10 HP rumble and noise bed:
  - Three layers, Rumble, Hiss and Crackle, each with a level knob (audio
    taper) and its own output.
  - Depth, 0 to 11 km, darkens every layer at once while holding its level.
  - Drift: slow random wander of each layer's level and colour, per side,
    and of crackle's density; still at 0.
  - CV with attenuverters on all five controls.
  - L/R mix (rumble mono-safe, hiss wide, crackle panned); a patched layer
    output leaves the mix; L alone carries the mono sum.
  - Drawn as a depth gauge.
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
  - In friendly mode the upper screen marks each output's lane with
    dividers and letters, and draws six apps' views itself with smooth
    lines: four stacked traces (Quadraturia, Piqued, Dialectic Ping Pong),
    X/Y curves (Low-rents), the grid path and note circle (Automatonnetz)
    and the note circle with its moves and chord (Harrington 1200).
    Hardware mode keeps the o_C screen. The voltage strip has a cell per
    output.
  - The manual has patches to try for each app.
- One look for every module, "Instrument": brass and ivory on near-black,
  main controls on enamel dial faces, knurled knobs, a traced line from
  each CV jack through its attenuverter to the control it moves, outputs
  on ivory tiles, cyan only for lights and screens, lettered in D-DIN
  Condensed. The plugin's own knobs, jacks, buttons, lights and screws;
  Rack's dark-panels preference gets the same drawing turned down.
