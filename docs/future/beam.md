# Beam: stereo width

Status: idea, 2026-11-26. Not designed; no sketch yet. The name is a
working title and has not been checked against the VCV Library. Added
when the user asked what the roadmap was missing.

**What it is.** Sets how wide a stereo signal is, from mono through as
it came to wider than it came, and keeps the lows in the middle. It
also gives the mid and the side as separate signals, to treat apart and
put back.

**The name.** A ship's beam is its width at the widest point.

**Why.** Fog, Wake, Narrows, Eddy and Spindrift are all stereo, and
nothing in the collection narrows, widens or checks a stereo signal.
Several wide reverbs and echoes stacked lose their centre, and low
rumble (Hadal, Fathom's SUB) wide in stereo is a problem on speakers
and when a piece is cut or mastered.

**Sketched controls.** Not drawn. Likely 6 to 8 HP. Width (the hero: 0
is mono, noon is unchanged, full is wider) with CV and attenuverter;
Bass Mono (the frequency under which the signal is made mono, off at
its lowest setting). Stereo in, stereo out. Possibly Mid and Side
outputs with Mid and Side returns, which take over when patched.

**Open questions.** Whether this is its own module or part of
Thermocline, the crossover, which already splits lows from highs and
could carry a width for each band; whether Width past noon needs a
limit so a near-mono input is not turned into noise; a correlation
light that warns when the signal would cancel in mono; whether the mid
and side jacks earn their panel space; a Width control at Ballast's
output instead.
