# Future modules

Ideas for modules that could follow Sway, Fathom and Shoal, noted so they
can be picked up later. Each is a concept and a rough panel sketch in the
Instrument look (`docs/specs/2026-11-15-instrument-look-design.md`), not a
design: names are working titles and have not been checked against the VCV
Library, and the control sets are first guesses made to draw the panels.
Sluice, Fog, Hadal, Ballast (with its Pan expander), Squall, Rigging, Wake, Narrows, Eddy and Spindrift have since been designed and built (their pages point to
their specs and manuals); the rest have not been through a design
conversation yet.
A design for any of them that makes or bends sound names its aliasing
sources and what is done about each (`docs/DSP-ALIASING.md`).

A suggested order for building them, with the user's music in mind, is
in [ROADMAP.md](ROADMAP.md).

The thread running through them is a ship's instrument set. Sway keeps
time, Fathom sounds the depth, Shoal is the water itself; each idea here
takes one more instrument and builds its panel geometry from it.

| Module | What it does | The instrument | Width |
|--------|--------------|----------------|-------|
| [Wake](wake.md) | Stereo echo (built) | A ship's wake | 14 HP |
| [Eddy](eddy.md) | Tape loop that also loads and saves files, with a waveform window (built) | An eddy, drawn as a spiral in the sketch | 16 HP |
| [Neap](neap.md) | Very slow, layered modulation | A tide-predicting machine | 12 HP |
| [Bearing](bearing.md) | Quantiser | A compass rose | 10 HP |
| [Squall](squall.md) | Random source (built) | An aneroid barometer | 8 HP |
| [Knot](knot.md) | Clock, divider, multiplier | Knots counted on a log line | 8 HP |
| [Beacon](beacon.md) | Envelope and function generator | A lighthouse | 8 HP |
| [Ballast](ballast.md) | Four-channel stereo mixer (built) | A clinometer | 12 HP |
| [Sounding](sounding.md) | Eight-step sequencer | A line of soundings on a chart | 12 HP |
| [Fog](fog.md) | Reverb (built) | A visibility scale | 10 HP |
| [Sextant](sextant.md) | Scanner and crossfader | A sextant | 8 HP |
| [Tackle](tackle.md) | Four attenuverters with offset | Block and tackle | 8 HP |
| [Spindrift](spindrift.md) | Granular cloud, with a grain window (built, as Beads) | An anemometer | 20 HP |

## Where the nautical thing is the function

A second set, chosen because in each the sea-going thing is not just a
name: it is what the module does.

| Module | What it does | The instrument | Width |
|--------|--------------|----------------|-------|
| [Ping](ping.md) | Sonar voice: a tone and its echo | Active sonar | 8 HP |
| [Buoy](buoy.md) | Bell voice struck by simulated waves | A bell buoy | 10 HP |
| [Foghorn](foghorn.md) | Low two-tone drone voice | A diaphone foghorn | 10 HP |
| [Plimsoll](plimsoll.md) | Limiter | The Plimsoll load-line mark | 8 HP |
| [Thermocline](thermocline.md) | Crossover | The layer between warm and cold water | 8 HP |
| [Narrows](narrows.md) | Polyphonic stereo filter: low-pass, band-pass, high-pass, comb (built) | A strait | 14 HP |
| [Seastate](seastate.md) | Wavefolder | The sea-state scale | 8 HP |
| [Reckoner](reckoner.md) | Wandering X/Y position from heading and speed | Dead reckoning | 10 HP |
| [Signal](signal.md) | Morse rhythm from a word | A signalling lamp | 12 HP |
| [Watch](watch.md) | Long-form timer in eight bells | The ship's watch | 10 HP |
| [Log](log.md) | CV recorder | A barograph | 12 HP |

## For dark, space and horror ambient

The user makes dark, space and horror ambient, in VCV Rack, in Eurorack
and in Ableton. Asked which of the collection fits that work, the
designer's reading was:

- **Core.** Sound: Fathom, Foghorn, Ping, Buoy. Space and texture: Fog,
  Spindrift, Wake. Slow movement: Sway, Neap, Reckoner, Squall.
  Long-form structure: Watch, Log, Shoal. Mixing and safety: Ballast,
  Plimsoll, Thermocline. Glue: Tackle, Splice, Lull, Hold, Cleat.
- **Matters less for beatless music.** Knot, Sounding, Bearing (Shoal
  covers all three), Seastate, and Beacon, Sextant and Signal, which are
  useful in places but not central.

These nine were added to fill what was missing. The designer's first four
are Doldrums, Rigging, Hadal and Sluice.

| Module | What it does | The instrument | Width |
|--------|--------------|----------------|-------|
| [Doldrums](doldrums.md) | Spectral freeze and stretch | A ship becalmed | 10 HP |
| [Rigging](rigging.md) | Six tuned strings (built) | Wind in the rigging | 14 HP |
| [Hadal](hadal.md) | Rumble and noise bed (built) | The deepest ocean zone | 10 HP |
| [Sluice](sluice.md) | Four VCAs (built) | A sluice gate | 10 HP |
| [Doppler](doppler.md) | Frequency shifter | The Doppler effect | 8 HP |
| [Rust](rust.md) | Decay and wear | Rust | 8 HP |
| [Helm](helm.md) | Scene morph: eight scenes, six outputs | A ship's wheel | 12 HP |
| [Lee](lee.md) | Ducker and envelope follower | The sheltered side | 8 HP |
| [Fetch](fetch.md) | Player for long field recordings, read from disk (added 2026-11-21) | The fetch: open water the wind has crossed | not drawn |

Eddy, in the first table, was reworked into a tape loop as part of this.

## Small utilities, 2 HP

A standard jack is 8 mm across, so anything with a jack needs 2 HP. At
this width the look reduces to: the name small at the top with a
crosshair origin beneath it, one trace down the centre line through every
control (breaking behind each label), outputs on ivory tiles, and the
mark at the foot shortened to "R K A". These take a nautical name without
an instrument of their own; there is no room for one.

| Module | What it does |
|--------|--------------|
| [Cleat](cleat.md) | Multiple: one input to five outputs |
| [Splice](splice.md) | Precision adder: four inputs summed at unity |
| [Reef](reef.md) | Two attenuators |
| [Lull](lull.md) | Slew limiter with separate rise and fall |
| [Hold](hold.md) | Sample and hold, with track |
| [Bell](bell.md) | Manual trigger and gate; gate to trigger |

Two 1 HP ideas were sketched and not kept for now: Telltale, a strip of
lights showing the outputs of the RKA module on its left, and Batten, a
blank panel graduated as a ruler.

The mockups are sketches: sizes are approximate and the typeface is a
stand-in.

Rulings from the user that apply to every panel here: the "R K A ·
INSTRUMENTS" mark sits at the foot, overlapping no knob, button or jack;
and a dial turns clockwise for more of what it does.
