# Shoal Stage 1 (Firmware Core, Headless) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the Ornament & Crime v1.3.6 firmware on a desktop CPU behind a fake-hardware layer, as a Rack-free class `shoal::Core` that can be instantiated many times, with offline tests proving all 13 apps boot, tick, save, restore and survive hostile input.

**Architecture:** The firmware is vendored into `src/shoal/firmware/` and compiled as one translation unit (`src/shoal/Firmware.cpp`) together with a small shim (`src/shoal/shim/`) that stands in for Arduino and the Teensy peripherals. Every mutable firmware global moves into one `Context` struct per instance, reached through a thread-local pointer; the firmware's call sites keep their names through `name` → `name_ref()` macros. `src/shoal/Core.hpp` is the only header the rest of the plugin (stage 2) and the tests include; it exposes ticks, voltages, controls, frames, settings and state, and hides every firmware type.

**Tech Stack:** C++17 for `Firmware.cpp` and tests (the rest of the plugin stays C++11), Rack SDK 2.6.6 only for the plugin build (`RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64` in every shell), `src/testing.hpp` (`CHECK`, `rkatest::finish`), Clang/GCC address and undefined-behaviour sanitizers, POSIX shell.

**Spec:** `docs/specs/2026-11-14-shoal-design.md`. This plan covers its "Order of work" item 1 only. Stages 2 to 4 get their own plans.

**Upstream:** `~/ws/plugins/O_C`, commit `cfbffab4e2a1b162480264d4ef57c2bcb41580c6`, directory `software/o_c_REV/` (called `$UP` below).

## Global Constraints

- No Rack include anywhere under `src/shoal/` in this stage. (Spec: "Firmware core, headless. No Rack code.")
- Keep upstream file names and layout under `src/shoal/firmware/`. Every local change carries a `// SHOAL:` comment and a line in `src/shoal/firmware/CHANGES.md`. (Spec, Vendoring)
- Keep every upstream licence header. The licence notes must not claim MIT for files that have no header. (Spec, Vendoring)
- `Firmware.cpp` compiles with `-std=c++17 -fwrapv -fno-strict-aliasing`. (Spec, Desktop-CPU hazards)
- Shoal tests build with `-fsanitize=address,undefined -fno-sanitize=alignment,shift-base -fno-sanitize-recover=all -fwrapv -fno-strict-aliasing -O1 -g`. Divide-by-zero and shift-exponent checks stay on: they are where ARM and x86 differ.
- Core tick is 60 µs (16,666.67 Hz). CV inputs update every third tick with the firmware's one-quarter smoothing. Outputs hold between ticks. (Spec, Timing and Voltages)
- Output volts = `(code − 24551) / 6553.6`. Input 12-bit code = `offset − volts × 409.6`, clamped to 0..4095, with `offset` 2730 (Hardware range) or 2048 (±5 V range); `pitch_cv_scale` 15360. (Spec, Voltages; firmware `OC_calibration.ino`)
- Triggers: Schmitt, low below 0.1 V, high above 1 V. (CLAUDE.md)
- Scripts are portable between macOS and Linux: no `sed -i`, no `readlink -f`, check `uname -s` before platform tools. macOS ships GNU make 3.81: a more specific pattern rule must appear before the generic one. (CLAUDE.md)
- `make test` keeps building and passing the Sway and Fathom tests unchanged.
- Before calling a task done: `make test`. Before the stage is done: `make`, `make smoke`. (CLAUDE.md)
- Commit per task on branch `shoal`, message ending with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`.

## Review Focus

Conditions the spec implies but does not spell out. Each has a test pinned to its owning task.

1. **Hostile voltages.** NaN, ±infinity and ±100 V on a CV or trigger input: NaN reads as 0 V, the rest clamp, nothing faults. (Task 4, `testHostileVolts`)
2. **Trigger shorter than a tick.** A pulse high for 10 µs between two ticks clocks the app exactly once; two rising edges inside one tick count as one. (Task 4, `testShortPulseLatched`)
3. **Engine rates far from 48 kHz.** At 8 kHz several ticks run per sample; at 768 kHz most samples run none; the long-run tick count is exact in both. (Task 4, `testTickCountExact`)
4. **Bad saved data.** All zeros, all 0xFF, random bytes, and a valid image with one flipped bit: `restore` returns false or loads cleanly, never faults, and the core is left running with defaults when it returns false. (Task 7, `testCorruptImage`)
5. **Instance lifetime.** 64 cores created and destroyed in interleaved order, and a core destroyed while another thread is ticking a different core: no fault, no cross-talk, the thread-local pointer is never left dangling. (Task 3, `testLifetimes`)
6. **Switching apps mid-run.** `selectApp` between every ordered pair of apps while gates are high and CV is moving: no fault, outputs finite, the new app's menu draws. (Task 5, `testSwitchUnderLoad`)

## File Structure

```
src/shoal/
  Core.hpp               public API; no firmware or shim includes
  Firmware.cpp           the one translation unit: shim, firmware, context, Core
  firmware/              vendored o_c_REV, upstream names kept
    CHANGES.md           upstream commit, files dropped, every local change
    shoal_prototypes.h   forward declarations Arduino would have generated
  shim/
    Arduino.h            types, FASTRUN, pin API, time, random, irq no-ops
    EEPROM.h             EEPROM object and EEPtr over the context's 2048 bytes
    dspinst.h            portable C versions of the Teensy DSP intrinsics
    Context.hpp          State macros, thread-local current pointer, Scope
    Pins.hpp             pin levels, encoder quadrature synthesiser
    Volts.hpp            volts <-> codes, Schmitt trigger, TickClock (Rack-free, header-only)
  FirmwareContext.inc    struct Context: every former global, and each name_ref()
  CoreImpl.inc           shoal::Core member functions
  SettingsAccess.inc     per-app table of settings objects (raw access)
  test_util.hpp          helpers shared by the tests
  test_shim.cpp  test_boot.cpp  test_instances.cpp  test_io.cpp
  test_ui.cpp    test_apps.cpp  test_state.cpp      test_faults.cpp
  bench/bench_core.cpp   CPU measurement; in a subfolder so the plugin build's
                         src/shoal/*.cpp wildcard does not pick up its main()
tools/check_shoal_globals.sh   fails if Firmware.o has writable globals
```

`Firmware.cpp` includes, in this order: shim headers; `firmware/shoal_prototypes.h`; the firmware `.cpp` files; the firmware `.ino` files in `LC_ALL=C` name order; `FirmwareContext.inc`; `SettingsAccess.inc`; `CoreImpl.inc`.

---

### Task 1: Vendor the firmware, shim primitives, build rules

**Files:**
- Create: `src/shoal/firmware/**` (copied), `src/shoal/firmware/CHANGES.md`
- Create: `src/shoal/shim/Arduino.h`, `shim/EEPROM.h`, `shim/dspinst.h`, `shim/Pins.hpp`, `shim/Volts.hpp`
- Create: `src/shoal/test_shim.cpp`
- Create: `LICENSE-shoal-firmware.md` (shipped by the Makefile's existing `LICENSE*` wildcard)
- Modify: `Makefile`

**Interfaces:**
- Produces: `shoal::voltsToAdc12(float volts, int offset) -> int`, `shoal::dacToVolts(uint32_t code) -> float`, `struct shoal::Schmitt { bool process(float v); bool high; }`, `struct shoal::TickClock { void setSampleRate(double hz); int advance(); }` (returns ticks due for one sample), `struct shoal::EncoderPins { void turn(int detents); void poll(bool& a, bool& b); }`, all in `shim/Volts.hpp` / `shim/Pins.hpp`, header-only, no firmware includes.
- Produces: Makefile variables `SHOAL_FW_FLAGS`, `SHOAL_TEST_FLAGS`; rule `build/test/shoal/Firmware.o`; rule `build/test/shoal/test_%`.

- [ ] **Step 1: Copy upstream, untouched, and commit it alone**

Copy `$UP` to `src/shoal/firmware/` except: `src/drivers/ADC/`, `src/drivers/FreqMeasure/`, `src/drivers/SH1106_128x64_driver.cpp`, `OC_debug.h`, `OC_debug.cpp`, `util/util_SPIFIFO.h`, `util/util_profiling.h`, `VBiasManager.h`, `resources/`, `.DS_Store`. Verify with `diff -r` against `$UP` that every copied file is byte-identical. Write `CHANGES.md` with the upstream URL, the commit hash, and the dropped list. Commit: `Shoal: vendor O&C firmware v1.3.6 (cfbffab), unmodified`.

This commit exists so that every later firmware change shows as a diff.

- [ ] **Step 2: Licence notes**

Write `LICENSE-shoal-firmware.md`: authors (Patrick Dowling, Max Stadler, Tim Churches), upstream URL and commit, "MIT where the file says so; `OC_DAC.cpp` GPL v3; Mutable Instruments derived files MIT; files without a header: licence unconfirmed, to be confirmed with upstream before any public release".

- [ ] **Step 3: Write the failing test `src/shoal/test_shim.cpp`**

```cpp
// testVolts
CHECK(std::fabs(shoal::dacToVolts(24551) - 0.f) < 1e-4f, "0 V");
CHECK(std::fabs(shoal::dacToVolts(0) - (-3.7462f)) < 1e-3f, "bottom");
CHECK(std::fabs(shoal::dacToVolts(65535) - 6.2537f) < 1e-3f, "top");
CHECK(shoal::voltsToAdc12(0.f, 2730) == 2730, "hw zero");
CHECK(shoal::voltsToAdc12(1.f, 2730) == 2320, "1 V = 409.6 codes, rounded");
CHECK(shoal::voltsToAdc12(100.f, 2730) == 0 && shoal::voltsToAdc12(-100.f, 2730) == 4095, "clamp");
CHECK(shoal::voltsToAdc12(std::nanf(""), 2730) == 2730, "NaN is 0 V");
CHECK(shoal::voltsToAdc12(5.f, 2048) == 0 && shoal::voltsToAdc12(-4.99f, 2048) > 4090, "±5 V window");
// testSchmitt: 0 -> 0.5 stays low; -> 1.01 goes high; -> 0.5 stays high; -> 0.09 goes low
// testTickClock: for rate in {8000, 44100, 48000, 96000, 768000}: sum of advance() over
//   10 s of samples == 166666 or 166667
// testEncoderPins: turn(+3) then 6 polls yields exactly three (A falling while B low) patterns
//   as read by the firmware's Encoder::Read rule: a&3 == 2 && b&3 == 0 ; turn(-2) mirrors on B
// testDspinst: each function in shim/dspinst.h against its definition, on 1000 random inputs
//   and the corner values 0, 1, -1, INT32_MIN, INT32_MAX, e.g.
//   signed_multiply_32x16b(a,b) == (int32_t)(((int64_t)a * (int16_t)(b & 0xffff)) >> 16)
//   multiply_u32xu32_rshift32(a,b) == (uint32_t)(((uint64_t)a * b) >> 32)
```

`shim/dspinst.h` must provide every function the firmware calls (list them with `grep -rhoE "\b(signed_|multiply_)[a-z0-9_]+\(" src/shoal/firmware | sort -u`), each as plain C on 64-bit intermediates, taking definitions from the comments in upstream `extern/dspinst.h`.

- [ ] **Step 4: Makefile**

Add `shoal` to `MODULES`. Add, **above** the generic `build/test/%` rule:

```make
SHOAL_FW_FLAGS = -std=c++17 -fwrapv -fno-strict-aliasing -Isrc/shoal/shim -Isrc/shoal/firmware
SHOAL_TEST_FLAGS = $(SHOAL_FW_FLAGS) -O1 -g -fsanitize=address,undefined \
	-fno-sanitize=alignment,shift-base -fno-sanitize-recover=all
SHOAL_DEPS = $(shell find src/shoal -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.inc' -o -name '*.ino' -o -name '*.cpp' \) ! -name 'test_*.cpp')

build/src/shoal/Firmware.cpp.o: FLAGS += $(SHOAL_FW_FLAGS)

build/test/shoal/Firmware.o: src/shoal/Firmware.cpp $(SHOAL_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc -c $< -o $@

build/test/shoal/test_shim: src/shoal/test_shim.cpp $(SHOAL_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc $< -o $@

build/test/shoal/test_%: src/shoal/test_%.cpp build/test/shoal/Firmware.o
	@mkdir -p $(@D)
	$(CXX) $(SHOAL_TEST_FLAGS) -Isrc $^ -o $@
```

`test_shim` links no firmware. Until Task 2 creates `Firmware.cpp`, add a `Firmware.cpp` containing only a comment so the plugin build has a file to compile.

- [ ] **Step 5: Run to see it fail, implement the shim headers, run to pass**

Run: `make test`. Expected after implementation: `test_shim: all checks passed`, and the existing Sway and Fathom lines unchanged.

- [ ] **Step 6: Commit** `Shoal: shim primitives, build rules, licence notes`

---

### Task 2: The firmware compiles and boots (one global instance)

**Files:**
- Create: `src/shoal/Core.hpp`, `src/shoal/CoreImpl.inc`, `src/shoal/firmware/shoal_prototypes.h`, `src/shoal/test_util.hpp`, `src/shoal/test_boot.cpp`
- Modify: `src/shoal/Firmware.cpp`, `src/shoal/shim/Arduino.h`, `shim/EEPROM.h`
- Modify (firmware, each change marked and logged): `OC_ADC.h/.cpp`, `OC_DAC.h/.cpp`, `OC_digital_inputs.h/.cpp`, `src/drivers/display.h/.cpp`, `OC_calibration.ino`, `OC_ui.cpp`, `OC_apps.ino`, `o_c_REV.ino`, others as the compiler demands

**Interfaces:**
- Consumes: Task 1's shim headers.
- Produces `src/shoal/Core.hpp` (complete declaration; later tasks fill in bodies that this task may leave as stubs returning 0/false):

```cpp
namespace shoal {
constexpr int kNumApps = 13, kNumChannels = 4;
constexpr int kFrameW = 128, kFrameH = 64, kFrameBytes = 1024;
constexpr int kStateBytes = 2048;
constexpr double kTickHz = 1e6 / 60.0;
enum Button : uint8_t { BUTTON_UP, BUTTON_DOWN, BUTTON_L, BUTTON_R };
enum Encoder : uint8_t { ENCODER_L, ENCODER_R };
enum class InputRange : uint8_t { Hardware, Bipolar5 };
struct SettingInfo { const char* name; int min, max, def; const char* const* valueNames; };

class Core {
public:
	Core();  ~Core();
	Core(const Core&) = delete;  Core& operator=(const Core&) = delete;
	// Inputs. Call at any time between ticks; a rising gate edge is latched until the next tick.
	void setCv(int ch, float volts);
	void setGate(int ch, float volts);
	void setInputRange(InputRange r);
	void tick();                       // one 60 µs firmware tick, plus UI poll and main-loop pass when due
	float out(int ch) const;           // volts
	uint32_t outCode(int ch) const;    // raw 16-bit DAC code
	// Controls.
	void setButton(Button b, bool down);
	void turnEncoder(Encoder e, int detents);
	void setEncoderAcceleration(bool on);
	// Screens. dst is kFrameBytes; see pixel().
	void drawMenu(uint8_t* dst);
	void drawScope(uint8_t* dst);
	static bool pixel(const uint8_t* frame, int x, int y);
	// Apps.
	int app() const;
	void selectApp(int index);
	static const char* appName(int index);
	bool appMenuOpen() const;
	// Settings of the current app (raw: range-clamped, no app guards).
	int channels() const;
	int settingCount() const;
	SettingInfo settingInfo(int index) const;
	int setting(int channel, int index) const;
	bool setSettingRaw(int channel, int index, int value);
	// State.
	void save(uint8_t* out);           // kStateBytes
	bool restore(const uint8_t* in);   // kStateBytes; false = rejected, defaults loaded
	void resetToDefaults();
	void seed(uint32_t s);             // all firmware random sources
private:
	struct Impl;  Impl* impl_;
};
}
```

- Produces `src/shoal/test_util.hpp`: `void run(shoal::Core&, double seconds)`, `void pulse(shoal::Core&, int ch, double highSeconds = 0.002)`, `bool allFinite(shoal::Core&)`, `struct Lcg { uint32_t next(); float volts(float lo, float hi); }`.

- [ ] **Step 1: Write the failing test `src/shoal/test_boot.cpp`**

```cpp
// testBoot
shoal::Core c;
CHECK(c.app() == 0, "boots into CopierMaschine");
CHECK(std::string(shoal::Core::appName(0)) == "CopierMaschine", "name");
CHECK(std::string(shoal::Core::appName(12)) == "References", "last name");
run(c, 1.0);                                   // 16667 ticks, inputs at 0 V
CHECK(allFinite(c), "outputs finite");
for (int ch = 0; ch < 4; ch++)
	CHECK(c.out(ch) >= -3.75f && c.out(ch) <= 6.26f, "out %d in hardware range", ch);
```

- [ ] **Step 2: Make it compile**

Work from the compiler's errors. Decisions already made:

- `firmware/shoal_prototypes.h` forward-declares every `PREFIX_init/_storageSize/_save/_restore/_handleAppEvent/_loop/_menu/_screensaver/_handleButtonEvent/_handleEncoderEvent/_isr` for the 13 prefixes in `OC_apps.ino`, plus whatever else `.ino` files use before defining.
- `OC_ADC.cpp`: delete the DMA code. `Scan_DMA()` runs on every third call and feeds `update<channel>()` with `(code12 << 4)` for the four channels, where `code12` comes from the shim's current CV levels. Keep `update`, `value`, `pitch_value` exactly as upstream.
- `OC_DAC.cpp`: delete SPI. `Update()` keeps the history bookkeeping and leaves `values_[]` for the shim to read. Keep the scaling functions.
- `OC_digital_inputs.*`: delete pin interrupts. The shim calls `clock<N>()` for each latched rising edge just before `Scan()`. `read_immediate` returns the shim's gate level.
- `src/drivers/display.*`: no driver. `GRAPHICS_BEGIN_FRAME` always gets a frame to draw into; `display::Flush/Update` do nothing.
- `OC_calibration.ino`: keep `kCalibrationDefaults`, `calibration_reset()`, and a `calibration_load()` that only resets to defaults and sets `pitch_cv_scale` to `OC::ADC::kDefaultPitchCVScale`. Delete the calibration UI.
- `o_c_REV.ino`: replace `setup()`/`loop()` with `shoal_boot()` (the `setup()` sequence without delays, splash, calibration or timers, ending in `OC::apps::Init(false)` and `OC::CORE::app_isr_enabled = true`), `shoal_tick()` (the body of `CORE_timer_ISR`), and `shoal_main_pass()` (one pass of the `loop()` body without the draw).
- `Ui::AppSettings`, `Ui::ConfirmReset`, `Ui::DebugStats`, `Ui::Splashscreen`, `Ui::Calibrate`: stub out for now (Task 5 restores the app menu). `delay()` is a no-op. `millis()`/`micros()` derive from `OC::CORE::ticks × 60 µs`.
- `FreqMeasure`: a stub object whose `available()` returns 0. (Real meters are stage 4.)
- `__disable_irq`, `__enable_irq`, `__DMB`, `noInterrupts`, `interrupts`: no-ops. `__LDREXW`/`__STREXW`: plain load and store returning success.

`Core::tick()` in this task: apply inputs, `shoal_tick()`; every 1000 µs of accumulated tick time call `OC::ui.Poll()`; every 17th tick call `shoal_main_pass()`.

- [ ] **Step 3: Run**

Run: `make test`. Expected: `test_boot: all checks passed` with no sanitizer report. If the sanitizer stops on a firmware line, fix that line with a `// SHOAL:` guard that reproduces ARM's result (divide by zero yields 0; a shift by 32 or more yields 0) and log it in `CHANGES.md`.

- [ ] **Step 4: Build the plugin**

Run: `RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64 make`. Expected: links; warnings from firmware files are acceptable, errors are not.

- [ ] **Step 5: Commit** `Shoal: firmware compiles and boots on the host`

---

### Task 3: One context per instance

**Files:**
- Create: `src/shoal/shim/Context.hpp`, `src/shoal/FirmwareContext.inc`, `tools/check_shoal_globals.sh`, `src/shoal/test_instances.cpp`
- Modify: every firmware file with a file-scope or static mutable object; `src/shoal/CoreImpl.inc`; `Makefile`

**Interfaces:**
- Produces `shim/Context.hpp`:

```cpp
namespace shoal {
struct Context;                                   // defined in FirmwareContext.inc
Context*& current();                              // thread_local
struct Scope { explicit Scope(Context* c); ~Scope(); private: Context* prev_; };  // nests
template <class T> using Ref = T&;
}
#define SHOAL_STATE(TYPE, NAME) ::shoal::Ref<TYPE> NAME##_ref();
```

- Convention for each former global `T name;` in namespace `N`: replace the definition (and any `extern`) with `SHOAL_STATE(T, name)` followed by `#define name name##_ref()` written out by hand (`#define name name_ref()`), add member `T name;` to `Context` in `FirmwareContext.inc` in the original definition order, and define `N::name_ref()` there as `return shoal::current()->name;`. Qualified uses (`OC::ui.Poll()`) keep working because the macro replaces only the last token.
- Static data members of `OC::ADC`, `OC::DAC`, `OC::DigitalInputs`, `stmlib::Random`, `OC::CORE`, and the six function-local statics: move into `Context` as plain members, reached through a private static accessor in the owning class; do not use the macro for these (their names, such as `values_`, are common member names).
- If a macro name collides with another identifier, rename the global (prefix `g_`) rather than the other identifier, and log it.
- Every `Core` member function opens a `shoal::Scope` on its `Impl`'s context before touching firmware code.
- Read-only tables stay global and `const`.

- [ ] **Step 1: Write the failing test `src/shoal/test_instances.cpp`**

```cpp
// testIndependent
shoal::Core a, b;  a.seed(1);  b.seed(1);
a.setCv(0, 1.0f);  b.setCv(0, 3.0f);
for (int i = 0; i < 8; i++) { run(a, 0.01); run(b, 0.01); pulse(a, 0); pulse(b, 0); }
CHECK(std::fabs(a.out(0) - 1.0f) < 0.01f, "a sampled its own CV: %f", a.out(0));
CHECK(std::fabs(b.out(0) - 3.0f) < 0.01f, "b sampled its own CV: %f", b.out(0));
b.selectApp(5);
CHECK(a.app() == 0 && b.app() == 5, "apps are per instance");

// testDeterministic: two cores, same seed, same inputs for 2 s in each of the 13 apps:
//   outCode(ch) identical on every tick

// testThreads: two std::threads, each owning a core in app 10 (Viznutcracker) and app 5,
//   each running 5 s of ticks with its own Lcg inputs; then the same two runs done
//   sequentially on fresh cores with the same seeds give identical final outCode values

// testLifetimes (Review Focus 5):
//   std::vector<std::unique_ptr<shoal::Core>> v;  create 64; run each 0.01 s;
//   destroy even indices; run the odd ones 0.01 s; create 32 more; run all; destroy all.
//   Then: thread T ticks core x for 2 s while the main thread creates and destroys 200 cores.
//   CHECK(shoal_test_current_is_null(), "no dangling current pointer on this thread");
```

`shoal_test_current_is_null()` is a test-only function exported from `Firmware.cpp`.

- [ ] **Step 2: Write `tools/check_shoal_globals.sh`**

Takes an object file. Lists its symbols with `objdump -t` (use `llvm-objdump` or `objdump`, whichever exists) and fails, printing them, if any symbol sits in a writable data section: `.data`, `.bss`, `COMMON` on Linux; `__DATA,__data`, `__DATA,__bss`, `__DATA,__common` on macOS. Thread-local sections (`.tbss`, `.tdata`, `__thread_bss`, `__thread_data`, `__thread_vars`) and read-only relocated data (`.data.rel.ro`, `__DATA,__const`) are allowed. Sanitizer bookkeeping symbols (`__asan*`, `__ubsan*`, `___asan*`) are ignored.

Add a Makefile target and make `test` depend on it:

```make
.PHONY: shoal-globals
shoal-globals: build/test/shoal/Firmware.o
	tools/check_shoal_globals.sh $<
```

- [ ] **Step 3: Run both to see them fail**

Run: `make shoal-globals`. Expected: FAIL, listing roughly 60 to 80 symbols. That list is the work queue for step 4.

- [ ] **Step 4: Move the state**

Go through the list from step 3 file by file. After each file, rebuild and re-run `test_boot`. `Core::Impl` owns a `Context`; the constructor runs `shoal_boot()` inside a `Scope`.

- [ ] **Step 5: Run**

Run: `make test`. Expected: `shoal-globals` prints nothing and exits 0; `test_boot` and `test_instances` pass with no sanitizer report.

- [ ] **Step 6: Commit** `Shoal: per-instance firmware context`

---

### Task 4: Voltages, triggers and timing

**Files:**
- Create: `src/shoal/test_io.cpp`
- Modify: `src/shoal/CoreImpl.inc`, `src/shoal/firmware/OC_ADC.cpp`

**Interfaces:**
- Consumes: `voltsToAdc12`, `dacToVolts`, `Schmitt`, `TickClock` (Task 1); `Core` (Task 2).
- Produces: final behaviour of `Core::setCv`, `setGate`, `setInputRange`, `out`, `outCode`. `setInputRange` rewrites the four ADC offsets in the instance's calibration data (2730 or 2048) and takes effect on the next ADC update.

- [ ] **Step 1: Write the failing test `src/shoal/test_io.cpp`**

```cpp
// testAdcCadence: CV1 steps 0 V -> 2 V. Read the firmware's smoothed value through a
//   test-only accessor after each tick: it changes only on every third tick, and reaches
//   within 1 code of the target after 60 ticks (one-quarter smoothing).

// testPitchRoundTrip (app 12, References, or any app that copies pitch in to out is not
//   assumed): use test-only accessor shoal_test_pitch_in(core, ch) = OC::ADC::pitch_value(ch).
//   for v in {-3, -2, -1, 0, 1, 2, 3, 4, 5, 6}: setCv(0, v); run 0.02 s;
//   CHECK(abs(pitch_in - v * 12 * 128) <= 8, ...)        // 12 << 7 per octave, within 1 ADC code

// testInputRange:
//   Hardware: setCv(0, -5) reads the same as setCv(0, -3.5); setCv(0, 6.5) differs from 5.0
//   Bipolar5: 0 V -> pitch 0 ±8; +1 V -> 1536 ±8; -5 V and -4 V differ; +5 V and +6.5 V read the same
//   switching range mid-run does not fault and settles within 60 ticks

// testHostileVolts (Review Focus 1): for v in {NaN, +inf, -inf, 100, -100}:
//   setCv(all, v); setGate(all, v); run 0.1 s in each of the 13 apps; allFinite; NaN reads as 0 V

// testShortPulseLatched (Review Focus 2), app 0 with CV1 at 1 V then 2 V:
//   setGate(0, 10); setGate(0, 0);            // both before the next tick
//   run 0.005 s;  CHECK(abs(out(0) - 1.0) < 0.01)   // clocked once
//   setGate(0,10); setGate(0,0); setGate(0,10); setGate(0,0);   // two edges, one tick
//   run 0.005 s;  out B still at its previous value: exactly one shift happened

// testSchmittOnGate: gate ramps 0 -> 0.9 V: no clock; -> 1.1 V: one clock; wobble 1.1 <-> 0.5: no more

// testTickCountExact (Review Focus 3): for rate in {8000, 44100, 48000, 96000, 768000}:
//   TickClock k; k.setSampleRate(rate); count ticks via a test-only tick counter while
//   calling core.tick() k.advance() times per sample for 10 s: count is 166666 or 166667

// testOutputsHold: outCode(ch) is unchanged between tick() calls and out() == dacToVolts(outCode())
```

- [ ] **Step 2: Run to see it fail, implement, run to pass**

Run: `make test`. Expected: `test_io: all checks passed`.

- [ ] **Step 3: Commit** `Shoal: voltages, triggers, tick clock, ±5 V input range`

---

### Task 5: Controls, the two screens, the app menu

**Files:**
- Create: `src/shoal/test_ui.cpp`
- Modify: `src/shoal/CoreImpl.inc`, `src/shoal/shim/Pins.hpp`, firmware `OC_ui.h`, `OC_ui.cpp`, `OC_apps.ino`, `src/drivers/display.cpp`

**Interfaces:**
- Consumes: `EncoderPins` (Task 1), `Context` (Task 3).
- Produces: final behaviour of `setButton`, `turnEncoder`, `setEncoderAcceleration`, `drawMenu`, `drawScope`, `pixel`, `selectApp`, `appMenuOpen`.
- Decisions:
  - Buttons and encoders are driven through the firmware's own `UI::Button` and `UI::Encoder` classes by answering `digitalReadFast` from the context's pin levels (active low). `turnEncoder` queues detents; each detent takes two 1 ms polls. This keeps the firmware's debounce, long-press (1000 polls) and acceleration arithmetic.
  - Holding `BUTTON_L` while turning must produce encoder events whose `mask` includes the button, as on hardware; nothing extra is needed beyond leaving the pin low.
  - The firmware's screensaver state is never entered: `Ui::DispatchEvents` no longer sets `screensaver_` on idle or on UP long-press, and `APP_EVENT_SCREENSAVER_ON/OFF` are never sent. An UP long-press does nothing.
  - `drawMenu(dst)` runs `current_app->DrawMenu()` (or the app menu when open) into a private frame and copies it; `drawScope(dst)` runs `current_app->DrawScreensaver()` likewise. Neither touches the other's frame. Frame layout is weegfx's: document it in `Core.hpp` above `pixel()` after reading `weegfx.cpp`'s `setPixel`.
  - App menu: `Ui::AppSettings` becomes three functions, `AppMenuOpen()`, `AppMenuHandle(const UI::Event&)`, `AppMenuClose(bool change)`, with its cursor in the context. R long-press opens it; while open, R turn scrolls, R press switches app and closes, R long-press also switches and closes (saving is the host's job in Shoal), the 25 s idle timeout closes without change, UP toggles acceleration, L does nothing.
  - `selectApp(i)` performs what the menu's "change" path does: suspend event to the old app, `set_current_app`, `DigitalInputs::reInit` equivalent, resume event to the new app. It also closes the app menu if open.

- [ ] **Step 1: Write the failing test `src/shoal/test_ui.cpp`**

```cpp
// helpers: press(core, b, seconds) holds a button for the time then releases and runs 0.02 s;
//          frameHash(frame); lit(frame) = number of set pixels

// testFramesDraw: for each of the 13 apps: selectApp; run 0.1 s; drawMenu(m); drawScope(s);
//   CHECK(lit(m) > 50, ...);  CHECK(hash(m) != hash(s), "menu and scope differ");
//   (scope may legitimately be near-empty for some apps at rest: assert only that it draws
//    without fault and, after 1 s of random CV and clocks, lit(s) > 0)

// testEncoderMovesCursor (app 0): h0 = hash(menu); turnEncoder(ENCODER_R, 1); run 0.02 s;
//   hash(menu) != h0; turnEncoder(ENCODER_R, -1); run 0.02 s; hash(menu) == h0

// testEditValue (app 0): find index of setting named "octave"; v0 = setting(0, idx);
//   turn R until the menu cursor is on that row (compare against settingCount, bounded loop);
//   press(BUTTON_R, 0.05); turnEncoder(ENCODER_R, 2); run 0.05 s;
//   CHECK(setting(0, idx) == v0 + 2, ...)

// testAcceleration: with acceleration on, 40 detents queued at once move a 0..65535-range
//   value by more than 40; with it off, by exactly 40

// testLongPressOpensAppMenu: press(BUTTON_R, 1.2); CHECK(appMenuOpen());
//   turnEncoder(ENCODER_R, 3); run 0.05 s; press(BUTTON_R, 0.05);
//   CHECK(!appMenuOpen() && app() == 3, "Quantermain")
// testAppMenuTimeout: open it, run 26 s, CHECK(!appMenuOpen() && app() unchanged)

// testNoScreensaver: run 60 s with no input; turnEncoder(ENCODER_R, 1); run 0.02 s;
//   the menu hash changed (the first input after idle is not swallowed);
//   press(BUTTON_UP, 2.0) does not change that either

// testHeldButtonChord (app 3, Quantermain): open the scale editor as the manual describes
//   (L press on the scale row); setButton(BUTTON_L, true); turnEncoder(ENCODER_L, 1);
//   run 0.05 s; setButton(BUTTON_L, false); the menu frame differs from the same sequence
//   done without holding L

// testSwitchUnderLoad (Review Focus 6): for each ordered pair (i, j) of the 13 apps:
//   selectApp(i); gates high, random CV; run 0.02 s; selectApp(j); run 0.02 s;
//   allFinite; drawMenu; CHECK(lit(m) > 50)
```

- [ ] **Step 2: Run to see it fail, implement, run to pass**

Run: `make test`. Expected: `test_ui: all checks passed`; `shoal-globals` still clean.

- [ ] **Step 3: Commit** `Shoal: controls, menu and scope frames, non-blocking app menu`

---

### Task 6: Settings access and app behaviour

**Files:**
- Create: `src/shoal/SettingsAccess.inc`, `src/shoal/test_apps.cpp`
- Modify: `src/shoal/CoreImpl.inc`

**Interfaces:**
- Consumes: `Context` members for each app's state (Task 3).
- Produces: final behaviour of `channels`, `settingCount`, `settingInfo`, `setting`, `setSettingRaw`. Stage 3 builds its adapters on the same table.
- `SettingsAccess.inc` holds one entry per app, in `available_apps` order, built with a template helper over `settings::SettingsBase<T, N>` (`get_value`, `apply_value`, `value_attr`):

| # | App | Settings objects (confirm each by reading the file) | Channels |
|---|---|---|---|
| 0 | CopierMaschine | `asr` | 1 |
| 1 | Harrington 1200 | `h1200_settings` | 1 |
| 2 | Automatonnetz | `automatonnetz_state` grid settings (cells are stage 3) | 1 |
| 3 | Quantermain | `quantizer_channels[4]` | 4 |
| 4 | Meta-Q | `dq_quantizer_channels[2]` | 2 |
| 5 | Quadraturia | `poly_lfo` | 1 |
| 6 | Low-rents | `lorenz_generator` | 1 |
| 7 | Piqued | `envgen` envelopes ×4 | 4 |
| 8 | Sequins | `seq_channel[2]` | 2 |
| 9 | Dialectic Ping Pong | `bbgen` balls ×4 | 4 |
| 10 | Viznutcracker sweet | `bytebeatgen` ×4 | 4 |
| 11 | Acid Curds | `chords` | 1 |
| 12 | References | `references_app` channels ×4 | 4 |

- `setSettingRaw` returns false for an out-of-range channel or index, otherwise calls `apply_value` (which clamps) and returns true. It does not run app guards; that is stage 3.

- [ ] **Step 1: Write the failing test `src/shoal/test_apps.cpp`**

```cpp
// testTables: for each app: channels() matches the table above; settingCount() > 0;
//   every settingInfo(i) has a non-null name, min <= def <= max, and
//   setting(ch, i) == def on a fresh core for every channel;
//   setSettingRaw(ch, i, max + 1000) leaves setting == max; (min - 1000) leaves min;
//   setSettingRaw(channels(), 0, 0) and (0, settingCount(), 0) return false

// testCopierMaschineShifts (spec: "shifts sampled values down A, B, C, D on each clock"):
//   app 0, defaults. step(v): setCv(0, v); run 0.01 s; pulse(TR1); run 0.005 s
//   step(1.0): A≈1.0
//   step(2.0): A≈2.0, B≈1.0
//   step(3.0): A≈3.0, B≈2.0, C≈1.0
//   step(4.0): A≈4.0, B≈3.0, C≈2.0, D≈1.0          (each within 0.01 V)
//   TR2 held high (freeze), step(0.0): A..D are a rotation of the held values, 0.0 not among them

// testQuantermainInScale (spec: "in-scale 1 V/octave voltages for a sweep of inputs"):
//   app 3; set channel 0's scale setting to the entry whose value name is the semitone scale
//   (look it up through settingInfo().valueNames); for v from -3.0 to 6.0 in steps of 0.013:
//     setCv(0, v); run 0.01 s; pulse(TR1); run 0.005 s; s = out(0) * 12;
//     CHECK(abs(s - round(s)) < 0.02, "on a semitone");
//     CHECK(abs(s - v * 12) <= 0.6, "nearest note");
//   outputs are non-decreasing across the sweep
```

- [ ] **Step 2: Run to see it fail, implement, run to pass**

Run: `make test`. Expected: `test_apps: all checks passed`. If a CopierMaschine or Quantermain expectation fails, read the manual section for that app before changing either side: the test encodes the manual's description under default settings, and a default that differs (trigger delay, clock division, source) should be set explicitly in the test, with a comment naming the setting.

- [ ] **Step 3: Commit** `Shoal: raw settings access, CopierMaschine and Quantermain behaviour tests`

---

### Task 7: Save and restore

**Files:**
- Create: `src/shoal/test_state.cpp`
- Modify: `src/shoal/CoreImpl.inc`, `src/shoal/shim/EEPROM.h`, firmware `OC_apps.ino`

**Interfaces:**
- Consumes: settings access (Task 6).
- Produces: final behaviour of `save`, `restore`, `resetToDefaults`.
- The image is the instance's 2048-byte fake EEPROM. `save` runs the firmware's `save_global_settings()` and `save_app_data()` and copies the bytes out. `restore` copies the bytes in and re-runs the load half of `OC::apps::Init` (every app's `Init`, then global settings, then app data, then `set_current_app` and the resume event); it returns true only if the app-data storage loaded. On false the core is in firmware defaults, app 0. `resetToDefaults` zeroes the EEPROM and does the same.
- Not in the image, by design: input range, RNG seed, running state (sequence positions, shift-register contents). The host saves the first; the others reset.

- [ ] **Step 1: Write the failing test `src/shoal/test_state.cpp`**

```cpp
// testRoundTripEveryApp: core a. For each app: selectApp; for each channel and setting:
//   setSettingRaw(ch, i, info.def == info.max ? info.min : info.max).
//   selectApp(7); a.save(img);  core b; CHECK(b.restore(img));
//   CHECK(b.app() == 7); for each app, channel, setting: b's value == a's value.
//   Any setting the firmware legitimately alters on restore is listed in an
//   explicit exceptions array in the test, each with a one-line reason; the array
//   starts empty.

// testSaveIsStable: save twice with no change in between; restoring either image gives
//   the same settings (the two images may differ in bytes: the storage rotates pages)

// testFreshCoreSaves: a fresh core's image restores into another core as defaults, true

// testCorruptImage (Review Focus 4): restore of all-zero, all-0xFF, 100 random images
//   (Lcg seeds 1..100), and a valid image with each of 64 randomly chosen single bits flipped:
//   never faults; when it returns false, app() == 0 and every setting == def;
//   in every case run 0.5 s afterwards and allFinite

// testRunningStateResets: CopierMaschine clocked four times, save, restore into the same
//   core: outputs return to the fresh-core values after one tick

// testResetToDefaults: after the changes in testRoundTripEveryApp, resetToDefaults();
//   app() == 0 and every setting == def
```

- [ ] **Step 2: Run to see it fail, implement, run to pass**

Run: `make test`. Expected: `test_state: all checks passed`.

- [ ] **Step 3: Commit** `Shoal: save and restore through the firmware's own storage`

---

### Task 8: Fault sweeps, hardening, CPU

**Files:**
- Create: `src/shoal/test_faults.cpp`, `src/shoal/bench/bench_core.cpp`
- Modify: firmware files where the sweeps find faults (each marked and logged); `Makefile`; `README.md`; `CHANGELOG.md`

**Interfaces:**
- Consumes: everything above.
- Produces: Makefile target `shoal-bench`; the "Desktop-CPU guards" section of `CHANGES.md`.

- [ ] **Step 1: Write `src/shoal/test_faults.cpp`**

```cpp
// Inputs for every sweep: Lcg-driven. Each CV gets a new value in [-6, +8] V every 1..200
// ticks; each gate toggles every 1..400 ticks; plus the fixed extremes -3.33, 0, 6.67 V.

// testEveryAppRuns: for each app, for each InputRange: 3 s silent, 3 s random, 1 s extremes.
//   allFinite after every 1000 ticks.

// testEverySettingSwept (spec: "every setting swept across its full range while ticking"):
//   for each app, for channel 0 and channel channels()-1, for each setting i:
//     values = every value if (max - min) <= 64, else min, max, def and 32 evenly spaced;
//     for each value: setSettingRaw; run 200 ticks of random input; drawMenu; drawScope.
//   Restore the default before moving to the next setting.
//   Run the whole sweep once per InputRange (the Bipolar5 pass may use 8 spaced values).

// testPairsAtExtremes: for each app, set every setting to min, run 0.2 s; every setting to
//   max, run 0.2 s; alternate min/max by index parity, run 0.2 s.

// testControlsMash: for each app, 20 s of random button presses, holds and encoder turns
//   (Lcg-driven, including R long-presses that open the app menu) over random input.
```

- [ ] **Step 2: Run, and fix what it finds**

Run: `make test`. Each sanitizer stop is a firmware line that behaves differently on a desktop CPU. Fix it at that line so the result matches ARM's: integer divide or modulo by zero yields 0; a shift count of 32 or more yields 0 (left, logical right) or the sign fill (arithmetic right); an out-of-range table index is clamped. Mark with `// SHOAL:` and add a line to the "Desktop-CPU guards" section of `CHANGES.md` naming the app, the setting or input that reached it, and the fix. Repeat until the run is clean.

Expected at the end: `test_faults: all checks passed`, total `make test` time under five minutes on this laptop. If it is longer, reduce `testControlsMash` first, not the setting sweep.

- [ ] **Step 3: CPU measurement**

`bench/bench_core.cpp`: for each app, 10 s of ticks (166,667) with random input, built with `-O2` and no sanitizers; print microseconds per tick and the equivalent percentage of one core at real time; exit non-zero if any app exceeds 2 %.

```make
.PHONY: shoal-bench
shoal-bench:
	@mkdir -p build/test/shoal
	$(CXX) $(SHOAL_FW_FLAGS) -O2 -Isrc src/shoal/bench/bench_core.cpp src/shoal/Firmware.cpp -o build/test/shoal/bench_core
	build/test/shoal/bench_core
```

Run: `make shoal-bench`. Expected: 13 lines, each under 2 %. Paste the output into the commit message body.

- [ ] **Step 4: Whole-stage checks**

Run, in order, and report each result as it is:
- `make test` (all modules).
- `RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64 make`.
- `make smoke` (Shoal registers no module yet; this confirms the plugin still loads with the firmware linked in).
- `tools/cross_build.sh` if Colima is running (`colima status`); if it is not, do not start it: say the Windows and Linux builds were not checked.
- `git diff --stat <vendor commit>.. -- src/shoal/firmware | tail -1` and compare against `CHANGES.md`: every changed file is listed there.

- [ ] **Step 5: Docs**

`README.md`: add `src/shoal/` to the layout block with one line per top-level file, and `make shoal-bench`, `make shoal-globals` to the commands. `CHANGELOG.md`: an "Unreleased" entry, "Shoal: firmware core (no module yet)". `CLAUDE.md`: one bullet, "Shoal's core is the vendored O&C firmware in `src/shoal/firmware/`; change it only with a `// SHOAL:` mark and a `CHANGES.md` line; `make shoal-globals` must stay clean."

- [ ] **Step 6: Commit** `Shoal: fault sweeps, desktop-CPU guards, CPU bench, docs`

---

## Not in this stage

The Rack module, panel and widgets (stage 2); friendly-mode adapters, visible-settings lists, guards, note masks (stage 3); FreqMeasure-backed meters, right-click menu, manual (stage 4). The spec's test "a friendly-mode edit and the equivalent encoder turns give the same state" belongs to stage 3.
