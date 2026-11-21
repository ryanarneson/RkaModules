# RKA

VCV Rack 2 modules.

| Module | HP | Description |
|---|---|---|
| [**Sway**](docs/sway/README.md) | 12 | Polyphonic LFO: classic and morphing waves, random, EOC, clock sync, phase spread, one-shot |
| [**Fathom**](docs/fathom/README.md) | 10 | Deep bass drone voice: sub sine, 7-saw supersaw, drift, ladder filter |
| [**Sluice**](docs/sluice/README.md) | 10 | Quad VCA: Level and CV per row, Lin/Exp, DC-coupled, polyphonic, normalled inputs and a mix downward |
| [**Fog**](docs/fog/README.md) | 10 | Stereo reverb: Visibility sets the decay in seconds (0.4 to 80 s), four modes, Size, Damp, Bloom, Drift, CV on all five |
| [**Ballast**](docs/ballast/README.md) | 12 | Four-channel stereo mixer: Level and a VCA level CV per channel, Master with CV, CHAIN inputs, a DC-blocked, soft-limited out, ready for expanders |
| [**Pan**](docs/pan/README.md) | 6 | Ballast's pan expander: pan, CV and attenuverter per channel; true stereo pan or balance; no-boost or constant-loudness pan law |
| [**Squall**](docs/squall/README.md) | 8 | Random gates and CV: Weather (one event in 10 min to 8 a second), Gust bursts, Memory loops, Step, Swell and a gate that follows the weather, free or clocked, Freeze |
| [**Rigging**](docs/rigging/README.md) | 14 | Six tuned strings: a drone from any sound, or struck by a trigger or a new note; seven chords, a level per string, Ring to 60 s, Damp, Spread, CV on Chord, Ring and Damp |
| [**Wake**](docs/wake/README.md) | 14 | Stereo echo, 10 ms to 10 s: Tape, Analog and Digital styles, a Ratio between the sides, ping pong, Freeze, Diffuse, Drift, a clock input, CV on Time, Feedback and Mix |
| [**Narrows**](docs/narrows/README.md) | 14 | Polyphonic stereo filter: a ladder and a state-variable core on a switch, Mode sweeps low-pass, band-pass, high-pass, Drive, Res to a playable sine, a comb tuned to the cutoff, Spread, Ping, CV on FM, Res, Comb and Mode |
| [**Hadal**](docs/hadal/README.md) | 10 | Rumble and noise bed: Rumble, Hiss and Crackle darkened together by Depth (0 to 11 km), slow Drift, CV on all five, a layer out each and an L/R mix |
| [**Shoal**](docs/shoal/README.md) | 28 | The Ornament & Crime v1.3.6 firmware, all 13 apps, on emulated hardware, with a mouse-driven settings list |

Each module's manual is in its own folder.

## Development

### Layout

One plugin (`rka`, one `.vcvplugin`). All code is under `src/`, one
folder per module; assets and docs sit at the top level, also per module:

```
src/plugin.cpp           plugin entry point: registers every module
src/plugin.hpp           shared includes, extern Model* per module
src/widgets.hpp          the plugin's own knobs, jacks, buttons, lights, screw
src/Detents.hpp          mouse and scroll travel to encoder detents (rka::Encoder)
src/RkaTheme.hpp         the palette's colours for C++, generated
src/testing.hpp          CHECK macro for the offline unit tests
src/sway/                the Sway module
  Sway.cpp               module (params, ports, polyphony) and widget
  Rate.hpp, Phasor.hpp,  DSP units, no Rack dependency, unit-tested
  Waves.hpp, Shaper.hpp,
  ClockSync.hpp,
  RandomGen.hpp, Voice.hpp
  test_*.cpp             offline unit tests, one per DSP unit
  SwayLayout.hpp         component positions, generated, do not edit
src/fathom/              the Fathom drone voice, same shape: Fathom.cpp,
                         Svf.hpp, SuperSaw.hpp, Drift.hpp, Ladder.hpp,
                         Halfband.hpp, DroneVoice.hpp, test_*.cpp,
                         FathomLayout.hpp
src/sluice/              the Sluice quad VCA: Sluice.cpp, SluiceEngine.hpp
                         (Rack-free), test_*.cpp, bench.inc, SluiceLayout.hpp
src/fog/                 the Fog reverb: Fog.cpp, FogEngine.hpp and the Rack-free
                         parts (Voicing.hpp, ...), test_*.cpp, bench.inc, FogLayout.hpp
src/hadal/               the Hadal noise bed: Hadal.cpp, HadalEngine.hpp and the
                         Rack-free layers (NoiseLayer.hpp, Crackle.hpp, Drift.hpp,
                         Random.hpp), test_*.cpp, bench.inc, HadalLayout.hpp
src/ballast/             the Ballast mixer: Ballast.cpp, BallastEngine.hpp (Rack-free),
                         BallastExpander.hpp (the expander interface), test_*.cpp,
                         bench.inc, BallastLayout.hpp
src/pan/                 the Pan expander: Pan.cpp, PanCore.hpp (Rack-free, a
                         BallastExpander), test_*.cpp, bench.inc, PanLayout.hpp
src/squall/              the Squall random source: Squall.cpp, SquallEngine.hpp and the
                         Rack-free parts (Events.hpp, Memory.hpp, Gate.hpp, Swell.hpp,
                         SquallTuning.hpp), test_*.cpp, bench.inc, SquallLayout.hpp
src/rigging/             the Rigging strings: Rigging.cpp, RiggingEngine.hpp and the
                         headers it is built from (Chords.hpp, String.hpp, Strike.hpp,
                         RiggingTuning.hpp), test_*.cpp, bench.inc, RiggingLayout.hpp
src/wake/                the Wake echo: Wake.cpp, WakeEngine.hpp and the headers it is
                         built from (Timing.hpp, Colour.hpp, Smear.hpp, WakeTuning.hpp;
                         the shared src/DelayTap.hpp), test_*.cpp, bench.inc, WakeLayout.hpp
src/narrows/             the Narrows filter: Narrows.cpp, NarrowsEngine.hpp and the
                         headers it is built from (Laws.hpp, StateCore.hpp, Comb.hpp,
                         Ping.hpp, NarrowsTuning.hpp; Fathom's ladder, shared as
                         src/fathom/LadderCore.hpp, and src/Float4.hpp), test_*.cpp,
                         bench.inc, NarrowsLayout.hpp
src/shoal/               Shoal: the Ornament & Crime v1.3.6 firmware
  Shoal.cpp              module and widget, mapped onto shoal::Host
  FriendlyList.hpp       the friendly-mode list widget on the lower screen
  ListLayout.hpp         the list's geometry, hit testing and gesture bookkeeping (Rack-free)
  Host.hpp               shoal::Host: one Core driven from the audio engine
  Core.hpp               shoal::Core, the firmware's only public header
  PatchState.hpp         the patch data's decode rules (version, length, range)
  Firmware.cpp           the firmware, shim and core as one C++17 unit
  FirmwareContext.inc    per-instance firmware state (shoal::Context)
  Friendly.hpp           friendly mode's row model and edits (FriendlyModel, FriendlyEdit)
  SettingsAccess.inc     raw settings table for every app
  Friendly.inc           friendly mode's framework: helpers, the table of adapters, the edit checks
  FriendlyAdapters<n>.inc  the per-app adapters, a file per group of apps (1: Quadraturia, Low-rents; 2: Harrington 1200, Automatonnetz, Dialectic Ping Pong, Viznutcracker sweet, References; 3: CopierMaschine, Quantermain, Meta-Q; 4: Piqued, Sequins, Acid Curds)
  CoreImpl.inc           shoal::Core's implementation
  CoreTest.hpp           test-only hooks into the core
  test_util.hpp          helpers shared by the Shoal tests
  test_*.cpp             offline tests, under the sanitizers
  testdata/              GoldenStateV1.hpp: a saved state image every build must load
  firmware/              vendored firmware; local changes in CHANGES.md
  shim/                  Arduino and Teensy stand-ins
  bench/bench_core.cpp   CPU cost per app (make shoal-bench)
res/components/          the plugin's own knobs, port, switch, button cap,
                         light seat and screw as SVGs, generated
res/<module>/            final panels, text as paths (what Rack loads)
res-src/<module>/        generated SVGs with live text (input to Inkscape)
docs/<module>/           the module's manual and panel image
docs/specs/, docs/plans/ design docs and implementation plans
tools/gen_panels.py      panel layouts for every module: the one source
                         for SVGs and positions
tools/test_gen_panels.py its tests (geometry, layout checks, stale outputs)
tools/fonts/             D-DIN Condensed, the panels' one typeface
packages/                release packages from tools/cross_build.sh and
                         tools/mac_dist.sh
```

The Makefile's `MODULES` list names the module folders; it compiles
`src/<module>/*.cpp` (except `test_*.cpp`), ships `res/`, and builds each
`src/<module>/test_*.cpp` into `build/test/` for `make test`.

### Everyday commands

```
make              # build plugin.so/.dll/.dylib
make test         # unit-test the DSP cores (no Rack needed)
make install      # package and install into your Rack user folder
make smoke        # load in a real headless Rack, screenshot each module
make panels       # regenerate panels after editing tools/gen_panels.py
make panels-test  # the generator's own tests (part of make test)
make specimen     # draw the Instrument look's specimen to build/specimen/
make shoal-bench  # Shoal: CPU per app, fails above 2 % of a core
make shoal-globals  # Shoal: the firmware object holds no writable globals
tools/cross_build.sh   # Windows + Linux packages via Docker -> packages/
tools/mac_dist.sh      # (on a Mac) arm64 + x64 packages -> packages/
```

All of these work on Linux and macOS. On a Mac, Windows and Linux builds
come from Docker, and both Mac CPUs build natively, so one laptop produces
all four packages.

`make smoke` writes `build/screenshots/<module>.png`. It catches what unit
tests can't: a plugin that won't load, a widget that crashes, a broken panel.

### Look and feel

Every module shares one look, "Instrument": brass and ivory on near-black,
enamel dial faces, knurled knobs, and a traced line from each CV jack
through its attenuverter to the control it moves. Each panel is one
instrument from a ship's set (Sway a pendulum, Fathom an echo sounder,
Shoal ripples from two sources). `docs/DESIGN-THEME.md` is the one-page
summary of the look and its rules; the full design is
`docs/specs/2026-11-15-instrument-look-design.md`.

The palette (`INSTRUMENT`) and the language (limb, enamel faces, output
tiles, signal traces, screen windows, the nameplate and foot mark) live in
`tools/gen_panels.py`, documented at "The Instrument look" there; its
`SPECIMEN` panel uses every part: `make specimen` draws it, components in
place, to `build/specimen/` as SVG and as PNG at Rack's 100 % and 300 %
zoom. Rack's "dark panels" variant is the same drawing turned down. Use the
plugin's own components from `src/widgets.hpp`, never stock ones:
`rka::HeroKnob`, `rka::DialKnob`, `rka::FaceKnob`, `rka::LimbKnob`,
`rka::Atten`, `rka::KnurlEncoder` (an endless encoder turned by drag or
scroll wheel), `rka::BrassPort`, `rka::Toggle2`, `rka::CapButton` (a
momentary button that shift-click latches), `rka::CapLatch`,
`rka::JewelLight`, `rka::Screw` and `rka::PixelScreen` (a 128x64 1-bit
frame drawn in cyan), with artwork generated into `res/components/` in
normal and dark-panel variants. Colours drawn in code come from the
generated `src/RkaTheme.hpp`. Panels are lettered in D-DIN Condensed only,
from `tools/fonts/`: `make panels` shows Inkscape that folder and nothing
else, and stops if Inkscape would set the text in any other face.

Besides component clearance the generator fails a layout with a component
or dial face partly off the panel; a limb under a dial face, tile or
screen; a CV input with an attenuverter (`PARAM_<N>_ATT` beside
`INPUT_<N>` or `INPUT_<N>_CV`) and no trace from the jack through it; a
trace that grazes a control or tile it does not belong to, runs under a
screen or through lettering, or bends more than once; traces that cross
or touch; screens whose bezels touch; or lettering (the foot mark included)
overlaps a component, other lettering, a screen, the limb, or a face or
tile it does not sit on. The rack screws (`screws` in a description,
placed by the module from its layout header's `SCREWS`) are kept clear of
all of these as well. A misspelt key in a description fails too.

### Changing a panel

Edit the layout in `tools/gen_panels.py`, then `make panels`. That rewrites
the light and dark SVGs and `src/<module>/<Module>Layout.hpp`, so positions in the
panel and the C++ can't drift apart. Rack can't draw `<text>`, so Inkscape
converts text to paths on the way to `res/<module>/`. The committed panels
were made with Inkscape 1.4.4, and the offline test reads that version's
output; `make panels` prints the version it found and warns if it is another
one (a different version may write the outlines differently). Commit `res/`
so builds don't need Inkscape. `make test` fails if a committed generated file is not what
the generator writes now (a hand edit, or `make panels` not run); without
Inkscape it cannot check the lettering's outlines in `res/`, so after
`make panels` also see that `git status` shows nothing you did not expect.

Each panel also carries a hidden `components` layer (red params, green
inputs, blue outputs, magenta lights), so `$RACK_DIR/helper.py createmodule`
still works on it as the official tutorial describes.

### Adding a module

1. Pick a folder name (lowercase, e.g. `sway`) and add it to `MODULES` in
   the Makefile.
2. Add its layout to `PANELS` in `tools/gen_panels.py` with `dir=` set to
   that folder, then `make panels`.
3. `src/<folder>/<Name>.cpp` with the module and widget; include
   `<Name>Layout.hpp` and `widgets.hpp`, and load panels from
   `"res/<folder>/<Name>.svg"`. Draw it in the Instrument language
   (`docs/DESIGN-THEME.md`): an origin and its arcs, faces under the main
   controls, tiles under the outputs, traces for the CV paths.
4. Declare `extern Model* model<Name>;` in `src/plugin.hpp`, register it in
   `src/plugin.cpp`.
5. Add the module to `plugin.json`, bump the minor version.
6. Keep DSP in Rack-free headers in `src/<folder>/`, with tests in
   `src/<folder>/test_*.cpp` using `testing.hpp` (picked up by `make test`
   and CI automatically).
7. Write `docs/<folder>/README.md` and link it from the table at the top.

## Setting up a build machine

Every platform needs the Rack SDK unzipped somewhere and `RACK_DIR` pointing
at it. SDK: `https://vcvrack.com/downloads/Rack-SDK-<version>-<platform>.zip`
with platform `lin-x64`, `win-x64`, `mac-x64` or `mac-arm64`. The version
must have the same major as the Rack you test in. Rack itself (free) is
`https://vcvrack.com/downloads/RackFree-<version>-<platform>.zip`.

### Linux (Ubuntu)

```
sudo apt install unzip git gdb curl cmake libx11-dev libglu1-mesa-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev zlib1g-dev \
  libasound2-dev libgtk2.0-dev libgtk-3-dev libjack-jackd2-dev jq zstd \
  libpulse-dev pkg-config autoconf libtool
sudo apt install inkscape xvfb   # panels, headless smoke test
```

On this machine: SDK in `~/ws/sdk/Rack-SDK`, Rack Free in
`~/ws/sdk/rack-app/Rack2Free` (run `rack`, an alias in `~/.bashrc`; Rack
must be started from its own folder). User folder: `~/.local/share/Rack2`.

### macOS

```
brew install git wget cmake autoconf automake libtool jq python zstd pkg-config
```

Also:

- Xcode command-line tools: `xcode-select --install`
- [VCV Rack 2](https://vcvrack.com/Rack) in `/Applications`, where
  `make smoke` finds it (or set `RACK_BIN` to the `Rack` executable inside
  the `.app`)
- `brew install --cask inkscape docker` for `make panels` and
  `tools/cross_build.sh`. The Makefile finds Inkscape inside the app bundle.
  On Apple Silicon, enable **Use Rosetta for x86_64/amd64 emulation** in
  Docker Desktop settings, because the toolchain image is x86 only.

For everyday `make`, unzip the SDK for your own CPU and set `RACK_DIR` in
`~/.zshrc` (macOS uses zsh, not bash):

```
export RACK_DIR="$HOME/ws/sdk/Rack-SDK"
```

`tools/mac_dist.sh` builds release packages for both CPUs. Each CPU needs
its own SDK, so the script downloads them on first use to
`~/ws/sdk/Rack-SDK-<version>-mac-<arch>`, separate from `RACK_DIR`. Rack's
user folder on macOS is `~/Library/Application Support/Rack2`;
`make install` finds it by itself.

### Windows

Install [MSYS2](https://www.msys2.org/) and use the **MinGW 64-bit** shell
(not the default MSYS shell):

```
pacman -Syu
# restart the shell, then:
pacman -Syu git wget make tar unzip zip mingw-w64-x86_64-gcc mingw-w64-x86_64-gdb \
  mingw-w64-x86_64-cmake autoconf automake libtool jq python zstd mingw-w64-x86_64-pkgconf
```

Antivirus can slow the build badly; exclude the source folder.

### Without those machines

- `tools/cross_build.sh` builds Windows and Linux on any Docker host,
  Apple Silicon included.
- CI (`.github/workflows/build.yml`) runs the unit tests, then builds
  win-x64, lin-x64, mac-x64 and mac-arm64. Pushing a `v*` tag drafts a GitHub
  release with all four packages attached.

## Releasing

1. Bump `version` in `plugin.json` (`2.MINOR.REVISION`: minor for new modules
   or features, revision for fixes; major must stay 2 for Rack 2) and update
   `CHANGELOG.md`.
2. Commit, tag `v<version>`, push the tag. CI drafts the release.
3. To list in the VCV Library, open an issue at
   [VCVRack/library](https://github.com/VCVRack/library) with the source URL
   and the commit/tag; for updates, comment on that issue. The library builds
   from source, so a tag alone doesn't ship anything.

## License

GPL-3.0-or-later. See `LICENSE-GPLv3.txt`.
