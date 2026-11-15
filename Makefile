# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# One folder per module: code and test_*.cpp tests in src/<module>/,
# panels in res/<module>/ (generated from res-src/<module>/). src/ itself
# has the shared plugin entry point and widgets.
MODULES = sway fathom shoal

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
# -> Inkscape text-to-path -> res/<module>/*.svg. Needs Inkscape; the results are committed so
# plain builds (and CI) don't.
# On macOS the Inkscape cask doesn't put the CLI on PATH.
INKSCAPE ?= $(or $(shell command -v inkscape),$(wildcard /Applications/Inkscape.app/Contents/MacOS/inkscape),inkscape)
.PHONY: panels
panels:
	python3 tools/gen_panels.py
	for f in $(MODULES:%=res-src/%/*.svg); do \
		out="res/$$(basename $$(dirname $$f))"; mkdir -p "$$out"; \
		$(INKSCAPE) "$$f" --export-text-to-path --export-plain-svg --export-filename="$$out/$$(basename $$f)" || exit 1; \
	done

# Offline unit tests of the DSP core; no Rack or libRack needed.
# Each src/<module>/test_*.cpp builds to build/test/<module>/test_*.
TEST_BINS = $(patsubst src/%.cpp,build/test/%,$(wildcard $(MODULES:%=src/%/test_*.cpp)))
.PHONY: test
test: shoal-globals $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done

# -fno-unsafe-math-optimizations undoes compile.mk's -funsafe-math-optimizations
# for the firmware, so the plugin and the tests share floating-point semantics.
SHOAL_FW_FLAGS = -std=c++17 -fwrapv -fno-strict-aliasing -fno-unsafe-math-optimizations \
	-Isrc/shoal/shim -Isrc/shoal/firmware
SHOAL_TEST_FLAGS = $(SHOAL_FW_FLAGS) -O1 -g -fsanitize=address,undefined \
	-fno-sanitize=alignment,shift-base -fno-sanitize-recover=all
SHOAL_DEPS = $(shell find src/shoal -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.inc' -o -name '*.ino' -o -name '*.cpp' \) ! -name 'test_*.cpp')

# Firmware.cpp is C++17 inside a C++11 plugin. compile.mk puts -std=c++11 in
# CXXFLAGS and then appends $(FLAGS), so FLAGS (not CXXFLAGS) lands last.
build/src/shoal/Firmware.cpp.o: FLAGS += $(SHOAL_FW_FLAGS)

build/test/shoal/Firmware.o: src/shoal/Firmware.cpp $(SHOAL_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc -c $< -o $@

# The firmware object must hold no writable global state: every mutable
# firmware global lives in the per-instance shoal::Context.
.PHONY: shoal-globals
shoal-globals: build/test/shoal/Firmware.o
	tools/check_shoal_globals.sh $<

build/test/shoal/test_shim: src/shoal/test_shim.cpp $(SHOAL_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc $< -o $@

build/test/shoal/test_%: src/shoal/test_%.cpp build/test/shoal/Firmware.o
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc $^ -o $@

build/test/%: src/%.cpp $(wildcard src/*.hpp $(MODULES:%=src/%/*.hpp))
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc -I$(<D) $< -o $@

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
