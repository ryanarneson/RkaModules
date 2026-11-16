# Offline unit tests of the DSP cores; no Rack, libRack or plugin.mk needed.
# The Makefile includes this after plugin.mk (`make test`); CI, which has no
# Rack SDK, runs it on its own:
#   make -f tests.mk test TEST_WERROR=-Werror
# Each src/<module>/test_*.cpp builds to build/test/<module>/test_*.

# Set by the Makefile; on its own, every folder of src/ that has tests.
MODULES ?= $(patsubst src/%/,%,$(sort $(dir $(wildcard src/*/test_*.cpp))))

TEST_BINS = $(patsubst src/%.cpp,build/test/%,$(wildcard $(MODULES:%=src/%/test_*.cpp)))
.PHONY: test
test: shoal-globals engine-globals libm-calls panels-test $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done

# The panel generator's geometry and layout checks, and that the committed
# panels, layout headers, colours and components are what it generates.
# Needs python3 only: no packages, no Inkscape.
.PHONY: panels-test
panels-test:
	python3 tools/test_gen_panels.py

# -fno-unsafe-math-optimizations undoes compile.mk's -funsafe-math-optimizations
# for the firmware, so the plugin and the tests share floating-point semantics.
# -funsigned-char: plain char is unsigned on the Teensy (ARM), signed on x86.
SHOAL_FW_FLAGS = -std=c++17 -fwrapv -fno-strict-aliasing -fno-unsafe-math-optimizations \
	-funsigned-char -Isrc/shoal/shim -Isrc/shoal/firmware
SHOAL_TEST_FLAGS = $(SHOAL_FW_FLAGS) -O1 -g -fsanitize=address,undefined \
	-fno-sanitize=alignment,shift-base -fno-sanitize-recover=all
SHOAL_DEPS := $(shell find src/shoal -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.inc' -o -name '*.ino' -o -name '*.cpp' \) ! -name 'test_*.cpp' ! -path 'src/shoal/testdata/*')

# Other modules' tests (header-only cores). CI adds -Werror.
TEST_WERROR ?=
DSP_TEST_FLAGS = -std=c++17 -O2 -Wall -Wextra $(TEST_WERROR)

# Specific rules before the generic build/test/% (GNU make 3.81 takes the
# first matching pattern rule).
build/test/shoal/Firmware.o: src/shoal/Firmware.cpp $(SHOAL_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc -c $< -o $@

# The firmware object must hold no writable global state: every mutable
# firmware global lives in the per-instance shoal::Context.
.PHONY: shoal-globals
shoal-globals: build/test/shoal/Firmware.o
	tools/check_shoal_globals.sh $<

# Opt-in, not part of `make test`: test_faults' random-combination sweep
# (built once, with the sanitizers) for seeds 1..SHOAL_FUZZ_SEEDS; seed 0 is
# the one `make test` runs. A failing seed reproduces with
# `build/test/shoal/test_faults --sweep <seed>`.
SHOAL_FUZZ_SEEDS ?= 50
.PHONY: shoal-fuzz
shoal-fuzz: build/test/shoal/test_faults
	@seed=1; while [ $$seed -le $(SHOAL_FUZZ_SEEDS) ]; do \
		./build/test/shoal/test_faults --sweep $$seed || exit 1; \
		seed=$$((seed + 1)); \
	done

build/test/shoal/test_shim: src/shoal/test_shim.cpp src/testing.hpp $(SHOAL_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc $< -o $@

# The encoder's detent arithmetic (src/Detents.hpp, shared by every module).
build/test/shoal/test_detents: src/Detents.hpp
# The checked-in state image an earlier build saved (never regenerated).
build/test/shoal/test_state: src/shoal/testdata/GoldenStateV1.hpp

build/test/shoal/test_%: src/shoal/test_%.cpp build/test/shoal/Firmware.o src/testing.hpp
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc $(filter %.cpp %.o,$^) -o $@

build/test/%: src/%.cpp $(wildcard src/*.hpp $(MODULES:%=src/%/*.hpp))
	@mkdir -p $(@D)
	$(CXX) $(DSP_TEST_FLAGS) -Isrc -I$(<D) $< -o $@

# CPU bench of Sway's and Fathom's per-sample engines (src/bench/bench.cpp),
# built as the plugin is: compile.mk's optimisation flags and architecture
# flag, C++11. Fails if an instance is over its module's budget.
BENCH_ARCH = $(if $(filter x86_64 amd64,$(shell uname -m)),-march=nehalem,-march=armv8-a+fp+simd)
BENCH_FLAGS = -std=c++11 -O3 -funsafe-math-optimizations -fno-omit-frame-pointer -g $(BENCH_ARCH)
build/bench/bench: src/bench/bench.cpp $(wildcard src/*.hpp src/sway/*.hpp src/fathom/*.hpp)
	@mkdir -p $(@D)
	$(CXX) $(BENCH_FLAGS) -Isrc $< -o $@
.PHONY: bench
bench: build/bench/bench
	./build/bench/bench

# Tests of the fast maths and of the optimised engines against their
# pre-optimisation references, built with the plugin's floating-point and
# architecture flags so they check what the plugin computes.
PLUGIN_MATH_TEST_FLAGS = -std=c++17 -O3 -funsafe-math-optimizations $(BENCH_ARCH) -Wall -Wextra $(TEST_WERROR)
PLUGIN_MATH_TESTS = build/test/fathom/test_fast_math build/test/fathom/test_engine build/test/sway/test_engine
$(filter $(TEST_BINS),$(PLUGIN_MATH_TESTS)): build/test/%: src/%.cpp $(wildcard src/*.hpp $(MODULES:%=src/%/*.hpp) $(MODULES:%=src/%/testdata/ref/*.hpp))
	@mkdir -p $(@D)
	$(CXX) $(PLUGIN_MATH_TEST_FLAGS) -Isrc -I$(<D) $< -o $@

# Sway's and Fathom's engines hold no writable global state (each instance
# owns all of its state): the same object check as shoal-globals, on an
# object that reaches every engine entry point.
build/test/engine_globals.o: src/bench/engine_globals.cpp $(wildcard src/*.hpp src/sway/*.hpp src/fathom/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++11 -O3 -funsafe-math-optimizations $(BENCH_ARCH) -Isrc -c $< -o $@
.PHONY: engine-globals
engine-globals: build/test/engine_globals.o
	tools/check_shoal_globals.sh $<

# No C maths-library call per sample on the engines' still-knob paths
# (src/bench/test_libm_calls.cpp): MinGW's libm is slow, and timing on macOS
# or Linux cannot see a call creeping back. Needs GNU ld's --wrap, so it
# runs on Linux (CI) and is skipped elsewhere.
LIBM_WRAP = exp2f exp2 expf exp log2f log2 logf log powf pow sinf sin cosf cos sincosf sincos \
	tanf tan tanhf tanh fmodf fmod lroundf lround floorf floor fminf fmin fmaxf fmax
build/test/libm_calls: src/bench/test_libm_calls.cpp src/testing.hpp $(wildcard src/*.hpp src/sway/*.hpp src/fathom/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O3 -funsafe-math-optimizations $(BENCH_ARCH) -Wall -Wextra $(TEST_WERROR) -Isrc $< -o $@ \
		$(LIBM_WRAP:%=-Wl,--wrap=%) -lm
.PHONY: libm-calls
ifeq ($(shell uname -s),Linux)
libm-calls: build/test/libm_calls
	./build/test/libm_calls
else
libm-calls:
	@echo "libm-calls: skipped (needs GNU ld's --wrap; runs on Linux and in CI)"
endif
