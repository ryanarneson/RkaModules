# Offline unit tests of the DSP cores; no Rack, libRack or plugin.mk needed.
# The Makefile includes this after plugin.mk (`make test`); CI, which has no
# Rack SDK, runs it on its own:
#   make -f tests.mk test TEST_WERROR=-Werror
# Each src/<module>/test_*.cpp builds to build/test/<module>/test_*.

# Set by the Makefile; on its own, every folder of src/ that has tests.
MODULES ?= $(patsubst src/%/,%,$(sort $(dir $(wildcard src/*/test_*.cpp))))

TEST_BINS = $(patsubst src/%.cpp,build/test/%,$(wildcard $(MODULES:%=src/%/test_*.cpp)))
.PHONY: test
test: shoal-globals bench-modules engine-globals libm-calls panels-test $(TEST_BINS)
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
# Sluice's hostile-input test: the sanitizers, and the counting operator new.
build/test/sluice/test_hostile: src/sluice/test_hostile.cpp src/testing.hpp $(wildcard src/*.hpp src/sluice/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra $(TEST_WERROR) -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/sluice $< -o $@

# Hadal's hostile-input test: the sanitizers, and the counting operator new.
build/test/hadal/test_hostile: src/hadal/test_hostile.cpp src/testing.hpp $(wildcard src/*.hpp src/hadal/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra $(TEST_WERROR) -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/hadal $< -o $@

# Ballast's hostile-input test: the sanitizers, and the counting operator new.
build/test/ballast/test_hostile: src/ballast/test_hostile.cpp src/testing.hpp $(wildcard src/*.hpp src/ballast/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra $(TEST_WERROR) -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/ballast $< -o $@

# Pan's hostile-input test: the sanitizers, and the counting operator new.
build/test/pan/test_hostile: src/pan/test_hostile.cpp src/testing.hpp $(wildcard src/*.hpp src/pan/*.hpp src/ballast/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra $(TEST_WERROR) -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/pan $< -o $@

# Squall's hostile-input test: the sanitizers, and the counting operator new.
build/test/squall/test_hostile: src/squall/test_hostile.cpp src/testing.hpp $(wildcard src/*.hpp src/squall/*.hpp src/hadal/Random.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra $(TEST_WERROR) -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/squall $< -o $@

# Rigging's hostile-input test: the sanitizers, and the counting operator new.
build/test/rigging/test_hostile: src/rigging/test_hostile.cpp src/testing.hpp $(wildcard src/*.hpp src/rigging/*.hpp src/hadal/Random.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra $(TEST_WERROR) -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/rigging $< -o $@

# Fog's hostile-input test: the sanitizers, and the counting operator new.
build/test/fog/test_hostile: src/fog/test_hostile.cpp src/testing.hpp $(wildcard src/*.hpp src/fog/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra $(TEST_WERROR) -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/fog $< -o $@

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

# Every module with an engine declares it for the CPU bench, the libm check
# and the globals check in src/<module>/bench.inc (src/bench/BenchModule.hpp
# says what goes in it; TESTING.md, "Adding a module"). The tools name no
# module: they include build/bench/modules.inc, which includes every
# declaration file and lists the folders as RKA_BENCH_MODULES(X). It is
# rewritten only when the list changes, so adding or removing a module
# rebuilds the tools and nothing else does.
BENCH_DECLS := $(sort $(wildcard src/*/bench.inc))
BENCH_MODULES := $(patsubst src/%/bench.inc,%,$(BENCH_DECLS))
BENCH_DEPS = build/bench/modules.inc $(BENCH_DECLS) \
	$(wildcard src/*.hpp src/bench/*.hpp $(BENCH_MODULES:%=src/%/*.hpp))
.PHONY: bench-modules-list
build/bench/modules.inc: bench-modules-list
	@mkdir -p $(@D)
	@{ echo '// Generated by tests.mk from src/*/bench.inc; do not edit.'; \
	  for m in $(BENCH_MODULES); do echo "#include \"$$m/bench.inc\""; done; \
	  printf '#define RKA_BENCH_MODULES(X)'; \
	  for m in $(BENCH_MODULES); do printf ' X(%s)' $$m; done; echo; } > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

# The guard (in `make test`): a module folder with a *Engine.hpp must have
# a bench.inc, and every declaration must be complete (idle, outputs and
# busy cases, both budgets of each case set, a libm case with the knobs
# still: `bench --check`).
.PHONY: bench-modules
bench-modules: build/bench/bench
	@ok=1; for e in $(wildcard src/*/*Engine.hpp); do \
		d=$$(dirname $$e); \
		if [ ! -f $$d/bench.inc ]; then \
			echo "bench-modules: module $$(basename $$d) has $$(basename $$e) but no $$d/bench.inc" \
				"(TESTING.md, \"Adding a module\")" >&2; \
			ok=0; \
		fi; \
	done; [ $$ok = 1 ]
	./build/bench/bench --check

# CPU bench of every declared module's per-sample engine
# (src/bench/bench.cpp), built as the plugin is: compile.mk's optimisation
# flags and architecture flag, C++11. Fails if an instance is over its
# case's budget, or if there are no modules.
BENCH_ARCH = $(if $(filter x86_64 amd64,$(shell uname -m)),-march=nehalem,-march=armv8-a+fp+simd)
BENCH_FLAGS = -std=c++11 -O3 -funsafe-math-optimizations -fno-omit-frame-pointer -g $(BENCH_ARCH)
build/bench/bench: src/bench/bench.cpp $(BENCH_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(BENCH_FLAGS) -Isrc -Ibuild/bench $< -o $@
.PHONY: bench
bench: build/bench/bench
	./build/bench/bench

# Tests of the fast maths and of the optimised engines against their
# pre-optimisation references, built with the plugin's floating-point and
# architecture flags so they check what the plugin computes.
PLUGIN_MATH_TEST_FLAGS = -std=c++17 -O3 -funsafe-math-optimizations $(BENCH_ARCH) -Wall -Wextra $(TEST_WERROR)
PLUGIN_MATH_TESTS = build/test/fathom/test_fast_math build/test/fathom/test_engine build/test/sway/test_engine build/test/sluice/test_engine build/test/fog/test_blocks build/test/fog/test_network build/test/fog/test_engine build/test/fog/test_modes build/test/fog/test_stereo build/test/hadal/test_layers build/test/hadal/test_engine build/test/ballast/test_engine build/test/ballast/test_expander build/test/pan/test_core build/test/squall/test_events build/test/squall/test_parts build/test/squall/test_engine build/test/rigging/test_chords build/test/rigging/test_string build/test/rigging/test_strike build/test/rigging/test_engine build/test/wake/test_tap build/test/wake/test_timing build/test/wake/test_loop build/test/wake/test_engine
$(filter $(TEST_BINS),$(PLUGIN_MATH_TESTS)): build/test/%: src/%.cpp $(wildcard src/*.hpp $(MODULES:%=src/%/*.hpp) $(MODULES:%=src/%/testdata/ref/*.hpp))
	@mkdir -p $(@D)
	$(CXX) $(PLUGIN_MATH_TEST_FLAGS) -Isrc -I$(<D) $< -o $@

# Opt-in, not part of `make test` (it takes minutes): Fog's soak, built as the
# plugin is (-O3, the plugin's float flags). Every mode, v = 1, three corners
# of Size, Damp and Drift: 10 minutes of noise, then 3 minutes of silence;
# the level stays under the limiter's bound and the tail follows the T60 law.
build/test/fog/soak: src/fog/soak/soak.cpp $(wildcard src/*.hpp src/fog/*.hpp)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O3 -funsafe-math-optimizations $(BENCH_ARCH) -Wall -Wextra $(TEST_WERROR) -Isrc -Isrc/fog $< -o $@
.PHONY: fog-soak
fog-soak: build/test/fog/soak
	./build/test/fog/soak

# Every declared module's engine holds no writable global state (each
# instance owns all of its state): the same object check as shoal-globals,
# on an object that reaches every engine entry point.
build/test/engine_globals.o: src/bench/engine_globals.cpp $(BENCH_DEPS)
	@mkdir -p $(@D)
	$(CXX) -std=c++11 -O3 -funsafe-math-optimizations $(BENCH_ARCH) -Isrc -Ibuild/bench -c $< -o $@
.PHONY: engine-globals
engine-globals: build/test/engine_globals.o
	tools/check_shoal_globals.sh $<

# No C maths-library call per sample on any declared engine's still-knob
# cases
# (src/bench/libm_calls.cpp): MinGW's libm is slow, and timing on macOS
# or Linux cannot see a call creeping back. Needs GNU ld's --wrap, so it
# runs on Linux (CI) and is skipped elsewhere.
LIBM_WRAP = exp2f exp2 expf exp log2f log2 logf log powf pow sinf sin cosf cos sincosf sincos \
	tanf tan tanhf tanh fmodf fmod lroundf lround floorf floor fminf fmin fmaxf fmax
build/test/libm_calls: src/bench/libm_calls.cpp $(BENCH_DEPS)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O3 -funsafe-math-optimizations $(BENCH_ARCH) -Wall -Wextra $(TEST_WERROR) -Isrc -Ibuild/bench $< -o $@ \
		$(LIBM_WRAP:%=-Wl,--wrap=%) -lm
.PHONY: libm-calls
ifeq ($(shell uname -s),Linux)
libm-calls: build/test/libm_calls
	./build/test/libm_calls
else
libm-calls:
	@echo "libm-calls: skipped (needs GNU ld's --wrap; runs on Linux and in CI)"
endif
