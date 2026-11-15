# RKA design theme

A one-page summary of what RKA modules are meant to look and feel like.
The detail lives in `docs/specs/2026-11-15-instrument-look-design.md`;
ideas for future modules are in `docs/future/`. Where this page and the
design doc disagree, the design doc wins.

## The theme

**A ship's set of precision instruments.** Every module is one
instrument from that set, or one thing a navigator measures: Sway is a
pendulum, Fathom an echo sounder, Shoal the water itself. Panels are
vintage brass and ivory on near-black, built to look engineered, not
decorated.

The music they are for is dark, space and horror ambient: long drones,
slow movement, deep space. Modules should favour slow ranges, stereo
throughout, and changes that unfold over minutes.

## Inspiration

- **Instruō**, for layout: non-linear, offset, the artwork and the
  control positions being the same thing.
- **Geodesics and Shapetaker**, for colour: dark grounds, brass, ivory, a
  vintage cast.
- **Mutable Instruments, Venom and Bastl**, admired; their restraint,
  density and character are the bar.
- **Benn Jordan's Ornament & Crime emulation**, for black and gold with a
  single cyan accent. Its framing and monospace type were tried and
  declined.
- **Real instruments**: the sextant, chronometer, aneroid barometer,
  clinometer, tide-predicting machine, Plimsoll mark. When a panel needs
  a scale or a shape, take it from the real thing.

## The big rules

1. **Nothing is decoration.** Every line is a scale, a centre mark, a
   construction line, a signal trace or a label. If a line cannot say
   what it measures, marks or connects, it goes.
2. **Geometry comes from the module's idea.** A module has an origin and
   its arcs are struck from it; controls sit on those arcs. The arc
   carrying the main knobs is a graduated brass limb.
3. **Main controls sit on enamel faces**: ivory discs with brass bezels,
   carrying their scale in ink, in real units where the control has them.
4. **Knobs are knurled**, at every size.
5. **Every CV path is traced**: one unbroken line from jack, through
   attenuverter, to the control it moves, meeting that control square-on.
6. **Outputs sit on ivory tiles; inputs are bare.** No frames round
   groups of jacks.
7. **Cyan means lit.** It is the only cool colour and is used only for
   lights and screen contents.
8. **A dial turns clockwise for more**, even where the real instrument
   reads the other way.
9. **The mark stands alone**: "R K A · INSTRUMENTS", one line at the
   foot, overlapping no knob, button or jack.
10. **Panels are tight.** As narrow as the controls allow, with no dead
    space.
11. **Panels are generated**, from one description, by
    `tools/gen_panels.py`. None is drawn or edited by hand.

Colours, in brief: ground `#15171c`, brass `#c9a46a`, ivory `#efe6d2`,
cyan `#6fd6e0`. The full palette is in the design doc.

## Naming

- **One word, nautical, short.** Sway, Fathom, Shoal, Wake, Fog, Knot.
  Short enough to be engraved large at the top of a narrow panel; a long
  name (Thermocline) has to shrink, which is a cost.
- **Best of all, the thing is the function.** A wake trails behind; a
  sluice lets water through; a Plimsoll line marks the limit; dead
  reckoning works out a position from heading and speed. Prefer a name
  that explains the module over one that merely sounds maritime.
- **Each full-size module has an instrument**, and the name and the
  panel geometry come from it together. If two modules would share an
  instrument, one is renamed and redrawn (Wake and Eddy).
- **Avoid names already in use** by well-known modules (hence Neap, not
  Tide). Names in `docs/future/` are working titles and have not been
  checked against the VCV Library.
- **Small utilities** (2 HP) take a nautical name without an instrument:
  Cleat, Splice, Reef.

Lettering on panels:

- Engraved, letterspaced capitals. No monospace.
- Control names are plain words a musician would use (Rate, Feedback,
  Mix); the theme belongs to the module's name and its hero dial, not to
  every label.
- Symbols must read at a glance: "/" for divide, and a large "×".
- A jack is labelled whenever a trace alone does not say what it is.
