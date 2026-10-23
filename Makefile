# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# One folder per module: code and test_*.cpp tests in src/<module>/,
# panels in res/<module>/ (generated from res-src/<module>/). src/ itself
# has the shared plugin entry point and widgets.
MODULES = sway

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
test: $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done

build/test/%: src/%.cpp $(wildcard src/*.hpp $(MODULES:%=src/%/*.hpp))
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc -I$(<D) $< -o $@

# Load the installed plugin in a real (headless) Rack and render module
# screenshots. See tools/rack_smoke.sh.
.PHONY: smoke
smoke: dist
	tools/rack_smoke.sh
