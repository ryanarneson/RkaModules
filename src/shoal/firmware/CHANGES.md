# Vendored Ornament & Crime firmware: change log

- Upstream: https://github.com/mxmxmx/O_C/ , directory `software/o_c_REV/`
- Version: v1.3.6
- Commit: cfbffab4e2a1b162480264d4ef57c2bcb41580c6

Every local change to a file here carries a `// SHOAL:` comment and a line
in this file.

## Files dropped from the copy

- `src/drivers/ADC/`
- `src/drivers/FreqMeasure/`
- `src/drivers/SH1106_128x64_driver.cpp`
- `OC_debug.h`, `OC_debug.cpp`
- `util/util_SPIFIFO.h`
- `util/util_profiling.h`
- `VBiasManager.h`
- `resources/`
- `.DS_Store`

## Added files

- `shoal_prototypes.h`: prototypes for the functions the `.ino` files use
  before defining them (Arduino generates these; Shoal's unity build,
  `../Firmware.cpp`, includes the files one by one).

## Modified files

Each change is marked `// SHOAL:` in the file.

- `OC_ADC.h`, `OC_ADC.cpp`: ADC driver and DMA code deleted (`adc_`,
  `ready_`, `scan_channel_`, `SCA_CHANNEL_ID`, `DMA_BUF_SIZE`, `DMA_NUM_CH`,
  `Init_DMA`, `DMA_ISR`, driver speed settings). `Scan_DMA()` now updates the
  four channels on every third call with `shoal_adc_code12(ch) << 4`;
  `update`, `value` and `pitch_value` are unchanged.
- `OC_DAC.h`, `OC_DAC.cpp`: SPI and DAC8565 code deleted (`set8565_CH*`,
  `SPI_init`, `SPIFIFO` setup, DAC pin setup, VOR `init_Vbias`/`set_Vbias`).
  `Update()` keeps the history bookkeeping; the shim reads `values_`.
- `OC_digital_inputs.cpp`: TR1..TR4 pin-change ISRs, `attachInterrupt` and
  pin setup deleted; `reInit()` is empty. The shim calls `clock<N>()` per
  rising edge and drives the TR pins (active low) for `read_immediate()`.
- `OC_gpio.h`: `OC::pinMode()` (Teensy port registers) deleted.
- `OC_ui.h`: `OC_debug.h` include and the `OC_UI_DEBUG` event counters in
  `PushEvent` deleted (`OC_debug.*` is dropped).
- `OC_ui.cpp`: `Ui::Splashscreen()` deleted.
- `OC_apps.ino`: `Ui::AppSettings()` stubbed to do nothing and
  `Ui::ConfirmReset()` to return false, until the non-blocking app menu
  (stage 1, Task 5). `::FOURCC` qualified in `GlobalSettings` and `AppData`
  (clang: the member name hides the template).
- `OC_calibration.h`: `::FOURCC` qualified, as above.
- `OC_calibration.ino`: calibration UI deleted (`Ui::Calibrate`, steps,
  drawing, `calibration_save`, `calibration_storage`,
  `calibration_data_loaded`). `calibration_load()` only resets to the
  defaults and sets `pitch_cv_scale` to `kDefaultPitchCVScale`.
  `_ADC_OFFSET` uses `1 << n` for `pow(2, n)` (constexpr; same value 2730).
- `o_c_REV.ino`: `setup()`/`loop()`/timer ISRs replaced by `shoal_boot()`
  (the setup sequence without delays, splash, calibration or timers),
  `shoal_tick()` (the body of `CORE_timer_ISR`) and `shoal_main_pass()` (one
  pass of the loop body without the draw). Debug-profiling and VOR includes
  removed.
- `src/drivers/display.h`, `display.cpp`: SH1106 paged driver and
  `AdjustOffset` deleted; `Flush`/`Update` do nothing; `GRAPHICS_END_FRAME`
  also marks the frame read, so a frame is always available to draw into.
- `src/drivers/weegfx.cpp`: `print(uint32_t, size_t)` definition takes
  `unsigned`, matching its declaration (size_t is 64-bit on the host).
- `util/util_math.h`: `USAT16`, `multiply_u32xu32_rshift24` and
  `multiply_u32xu32_rshift` rewritten from ARM assembly to portable C with
  ARM's results (usat reads its operand as signed; register shifts of 32 or
  more give 0). Tested in `../test_shim.cpp`.
- `braids_quantizer.h`: `Scale::num_notes` is `uint32_t` (was `size_t`, 32-bit
  on ARM), so `GlobalSettings` fits its EEPROM pages on a 64-bit host.
- `APP_AUTOMATONNETZ.ino`: `CellGrid`'s `bool epsilon` argument is
  `(bool)GRID_EPSILON` (6 -> true, as ARM GCC converted it; clang rejects
  the narrowing).
- `extern/stmlib_utils_random.cpp`: include path fixed
  (`utils/stmlib_utils_random.h` does not exist).

## Shim-side substitutions (no firmware edit)

- `extern/dspinst.h` compiles to nothing: `shim/dspinst.h`, included first,
  defines its include guard.
- `arm_math.h` (for `util/util_sync.h`) and
  `src/drivers/FreqMeasure/OC_FreqMeasure.h` (for `APP_REFS.ino`) resolve to
  stand-ins under `shim/`. FreqMeasure never has a reading.
- `typeof` is `__typeof__`; `F_CPU`, `FASTRUN`, `DMAMEM`, interrupts,
  `__LDREXW`/`__STREXW`, `millis`/`micros` (from the tick counter), `delay`
  (no-op), `random`, `elapsedMillis`, `Serial` and EEPROM come from
  `shim/Arduino.h` and `shim/EEPROM.h`.

## Desktop-CPU guards

- `frames_poly_lfo.cpp` (Quadraturia, any settings): `PolyLfo::Render` read
  `dac_code_[i-1]` at `i == 0` (index -1). The value is multiplied by
  `am_depths[0]`, which is always 0, so it now reads 0 instead.
