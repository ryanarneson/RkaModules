# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# One folder per module: code and test_*.cpp tests in src/<module>/,
# panels in res/<module>/ (generated from res-src/<module>/). src/ itself
# has the shared plugin entry point and widgets.
MODULES = sway fathom shoal sluice fog

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

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk


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
