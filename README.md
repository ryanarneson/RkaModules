# RKA

VCV Rack 2 modules.

| Module | HP | Description |
|---|---|---|
| [**Sway**](docs/sway/README.md) | 18 | Polyphonic LFO: classic and morphing waves, random, EOC, clock sync, phase spread, one-shot |
| [**Fathom**](docs/fathom/README.md) | 10 | Deep bass drone voice: sub sine, 7-saw supersaw, drift, ladder filter |
| [**Shoal**](docs/shoal/README.md) | 28 | The Ornament & Crime v1.3.6 firmware, all 13 apps, on emulated hardware, with a mouse-driven settings list |

Each module's manual is in its own folder.

## Development

### Layout

One plugin (`rka`, one `.vcvplugin`). All code is under `src/`, one
folder per module; assets and docs sit at the top level, also per module:

```
src/plugin.cpp           plugin entry point: registers every module
src/plugin.hpp           shared includes, extern Model* per module
src/widgets.hpp          shared knobs and lights, so modules match
src/Detents.hpp          mouse and scroll travel to encoder detents (rka::Encoder)
src/RkaTheme.hpp         palette accent colours for C++, generated
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
res/components/          the plugin's own knobs, port, switch, button cap and
                         light seat as SVGs, generated
res/<module>/            final panels, text as paths (what Rack loads)
res-src/<module>/        generated SVGs with live text (input to Inkscape)
docs/<module>/           the module's manual and panel image
docs/specs/, docs/plans/ design docs and implementation plans
tools/gen_panels.py      panel layouts for every module: the one source
                         for SVGs and positions
tools/test_gen_panels.py its tests (geometry, layout checks, stale outputs)
tools/fonts/             D-DIN Condensed, the Instrument panels' typeface
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

Every module shares one theme, "Nightglow": a navy panel, slate cards with
thin borders, light labels, and neon accents (cyan, violet, magenta, amber,
green). Outputs are colour-coded by accent. The palette lives in
`ACCENTS`/`THEMES` in `tools/gen_panels.py`; the default panel and Rack's
"dark panels" variant are the same design, the dark one dimmer. Rack can't
blur, so glows are a faint wide stroke under a thin bright one. Use the
widgets in `src/widgets.hpp` (`rka::BigKnob`, `rka::MidKnob`,
`rka::SmallKnob`, `rka::Trim`, `rka::CyanMagentaLight`, and for Shoal
`rka::HoldButton`, a momentary button that shift-click latches,
`rka::LatchButton`, a click-to-toggle switch in the same shape that
lights itself cyan when on,
`rka::Encoder`, an endless encoder turned by drag or scroll wheel, and
`rka::PixelScreen`, a 128x64 1-bit frame drawn in one accent) rather than
stock ones so modules match. `rka::Trim` is the plugin's own 4.6 mm
attenuverter; `gen_panels.py` draws its SVGs into `res/components/` from
the palette, and its `RADII` table keeps components clear of each other
and draws ticks edge to edge.

Nightglow is being replaced by "Instrument" (brass and ivory on near-black;
`docs/specs/2026-11-15-instrument-look-design.md`), one module at a time. A
panel chooses with `look="instrument"` in its description; until every
module has moved, both looks live in the generator and an unconverted module
regenerates byte for byte. The Instrument language (limb, enamel faces,
output tiles, signal traces, screen windows, the nameplate and foot mark) is
documented at "The Instrument look" in `tools/gen_panels.py`, and its
`SPECIMEN` panel uses every part: `make specimen` draws it, components in
place, to `build/specimen/` as SVG and as PNG at Rack's 100 % and 300 %
zoom. Its components are `rka::HeroKnob`, `rka::FaceKnob`, `rka::LimbKnob`,
`rka::Atten`, `rka::KnurlEncoder`, `rka::BrassPort`, `rka::Toggle2`,
`rka::CapButton`, `rka::CapLatch` and `rka::JewelLight`, with artwork in
`res/components/` in normal and dark-panel variants. Instrument panels are
lettered in D-DIN Condensed only, from `tools/fonts/`: `make panels` shows
Inkscape that folder and nothing else, and stops if Inkscape would set the
text in any other face. Besides component clearance the generator fails a
layout whose trace grazes a control or tile it does not belong to, runs
under a screen or through lettering, or bends more than once; whose traces
cross or touch; or whose lettering (the foot mark included) overlaps a
component, other lettering, a screen, the limb, or a face or tile it does
not sit on. A misspelt key in a description fails too.

### Changing a panel

Edit the layout in `tools/gen_panels.py`, then `make panels`. That rewrites
the light and dark SVGs and `src/<module>/<Module>Layout.hpp`, so positions in the
panel and the C++ can't drift apart. Rack can't draw `<text>`, so Inkscape
converts text to paths on the way to `res/<module>/`. Commit `res/` so builds don't
need Inkscape.

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
   `"res/<folder>/<Name>.svg"`. Group controls into `cards` in the layout
   and colour outputs with `label_accents`.
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
