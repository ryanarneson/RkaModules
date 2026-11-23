# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# One folder per module: code and test_*.cpp tests in src/<module>/,
# panels in res/<module>/ (generated from res-src/<module>/). src/ itself
# has the shared plugin entry point and widgets.
MODULES = sway fathom shoal sluice fog hadal ballast pan squall rigging wake narrows eddy spindrift beacon

# FLAGS will be passed to both the C and C++ compiler
FLAGS += -Isrc
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)
SOURCES += $(filter-out $(wildcard $(MODULES:%=src/%/test_*.cpp)),$(wildcard $(MODULES:%=src/%/*.cpp)))

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)
# Eddy's window letters the tape's length in the panels' typeface, with its licence.
DISTRIBUTABLES += tools/fonts/D-DINCondensed.ttf tools/fonts/OFL-1.1.txt tools/fonts/COPYING.txt

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk


# Rack extracts every package in its plugins folder at startup, in no fixed
# order, so an older rka package left beside the new one can be the one that
# loads (#45). Remove them before plugin.mk's install copies the new one.
install: remove-old-packages
.PHONY: remove-old-packages
remove-old-packages:
	rm -f "$(PLUGINS_DIR)"/$(SLUG)-*.vcvplugin

# Regenerate panels: layout -> res-src/<module>/*.svg + src/<module>/*Layout.hpp
# -> Inkscape text-to-path -> res/<module>/*.svg, and the components in
# res/components/. Needs Inkscape; the results are committed so plain builds
# (and CI) don't. Instrument panels are lettered from tools/fonts/ only.
# On macOS the Inkscape cask doesn't put the CLI on PATH.
INKSCAPE ?= $(or $(shell command -v inkscape),$(wildcard /Applications/Inkscape.app/Contents/MacOS/inkscape),inkscape)
.PHONY: panels
panels:
	python3 tools/gen_panels.py --inkscape "$(INKSCAPE)"

# The Instrument look's specimen panel (every primitive and component), with
# the components drawn in place, to build/specimen/: SVG, and PNG at Rack's
# 100 % and 300 % zoom. Not a module; nothing goes to res/.
.PHONY: specimen
specimen:
	python3 tools/gen_panels.py --specimen --inkscape "$(INKSCAPE)"

# Offline unit tests (`make test`) and the Shoal test flags; tests.mk also
# runs on its own, without the Rack SDK (CI does).
include tests.mk

# Firmware.cpp is C++17 inside a C++11 plugin. compile.mk puts -std=c++11 in
# CXXFLAGS and then appends $(FLAGS), so FLAGS (not CXXFLAGS) lands last.
# -fvisibility=hidden: the firmware's names stay inside plugin.dylib (the
# plugin's own objects still link to shoal::Core); the tests build without it.
build/src/shoal/Firmware.cpp.o: FLAGS += $(SHOAL_FW_FLAGS) -fvisibility=hidden

# Spindrift's vendored Beads, the same way: SPINDRIFT_FW_FLAGS from tests.mk,
# hidden names, and its include root as a system one (-isystem: upstream's
# headers warn under the plugin's -Wall -Wextra; the tests reach them the
# same way). The module compiles the engine and Beads' headers inline, so it
# gets the same flags (TESTING.md rule 9).
# Shoal vendors stmlib too (another revision, with its own changes), and
# both are linked into one plugin: Spindrift's is renamed spindrift_stmlib
# here, so neither copy's functions (inline ones included) stand in for the
# other's at link time. The tests link one copy each and need no rename.
SPINDRIFT_PLUGIN_FLAGS = $(SPINDRIFT_FW_FLAGS) -isystem src/spindrift -Dstmlib=spindrift_stmlib
build/src/spindrift/BeadsFirmware.cpp.o: FLAGS += $(SPINDRIFT_PLUGIN_FLAGS) -fvisibility=hidden
build/src/spindrift/Spindrift.cpp.o: FLAGS += $(SPINDRIFT_PLUGIN_FLAGS)

.PHONY: shoal-bench
shoal-bench:
	@mkdir -p build/test/shoal
	$(CXX) $(SHOAL_FW_FLAGS) -O2 -Isrc src/shoal/bench/bench_core.cpp src/shoal/Firmware.cpp -o build/test/shoal/bench_core
	build/test/shoal/bench_core

# Load the installed plugin in a real (headless) Rack and render module
# screenshots. See tools/rack_smoke.sh.
.PHONY: smoke
smoke: dist
	tools/rack_smoke.sh
