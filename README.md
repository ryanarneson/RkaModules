# RKA

VCV Rack 2 modules.

| Module | HP | Description |
|---|---|---|
| [**Sway**](docs/sway/README.md) | 6 | Polyphonic LFO: sine, triangle, saw and pulse-width square |

Each module's manual is in its own folder.

## Development

### Layout

One plugin (`rka`, one `.vcvplugin`). All code is under `src/`, one
folder per module; assets and docs sit at the top level, also per module:

```
src/plugin.cpp           plugin entry point: registers every module
src/plugin.hpp           shared includes, extern Model* per module
src/widgets.hpp          shared knobs and lights, so modules match
src/RkaTheme.hpp         palette accent colours for C++, generated
src/testing.hpp          CHECK macro for the offline unit tests
src/sway/                the Sway module
  Sway.cpp               module (params, ports, polyphony) and widget
  Rate.hpp, Phasor.hpp,  DSP units, no Rack dependency, unit-tested
  Waves.hpp
  test_*.cpp             offline unit tests, one per DSP unit
  SwayLayout.hpp         component positions, generated, do not edit
res/sway/                final panels, text as paths (what Rack loads)
res-src/sway/            generated SVGs with live text (input to Inkscape)
docs/sway/               the module's manual and panel image
docs/specs/, docs/plans/ design docs and implementation plans
tools/gen_panels.py      panel layouts for every module: the one source
                         for SVGs and positions
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
widgets in `src/widgets.hpp` (`rka::BigKnob`, `rka::SmallKnob`,
`rka::CyanMagentaLight`) rather than stock ones so modules match.

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
