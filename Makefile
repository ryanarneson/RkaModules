# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk


# Regenerate panels: layout -> res-src/*.svg + src/*Layout.hpp -> Inkscape
# text-to-path -> res/*.svg. Needs Inkscape; the results are committed so
# plain builds (and CI) don't.
# On macOS the Inkscape cask doesn't put the CLI on PATH.
INKSCAPE ?= $(or $(shell command -v inkscape),$(wildcard /Applications/Inkscape.app/Contents/MacOS/inkscape),inkscape)
.PHONY: panels
panels:
	python3 tools/gen_panels.py
	@mkdir -p res
	for f in res-src/*.svg; do \
		$(INKSCAPE) "$$f" --export-text-to-path --export-plain-svg --export-filename="res/$$(basename $$f)" || exit 1; \
	done

# Offline unit tests of the DSP core; no Rack or libRack needed.
TEST_BINS = test/test_lfo_core
.PHONY: test
test: $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done

test/%: test/%.cpp src/*.hpp
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc $< -o $@

# Load the installed plugin in a real (headless) Rack and render module
# screenshots. See tools/rack_smoke.sh.
.PHONY: smoke
smoke: dist
	tools/rack_smoke.sh
