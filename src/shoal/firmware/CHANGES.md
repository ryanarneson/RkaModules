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
- `OC_ui.h`, `OC_ui.cpp`, `OC_apps.ino`, `o_c_REV.ino` (stage 1, Task 5):
  the blocking `Ui::AppSettings()` loop became a non-blocking app menu,
  `Ui::AppMenuOpen()`, `AppMenuHandle(event)` and `AppMenuClose(change)`
  (flag `app_menu_open_` in `Ui`, cursor `OC::app_menu_cursor` in the
  context). `DispatchEvents` opens it on an R long press, routes every event
  to it while open and closes it unchanged after `APP_SELECTION_TIMEOUT_MS`
  idle. R press or long press switches app (no save: the host saves), UP
  toggles acceleration, L does nothing (`DebugStats` dropped);
  `app_isr_enabled` is not toggled (single-threaded). `shoal_main_pass()`
  skips the app's `loop()` while it is open (upstream's loop was blocked)
  and has no `UI_MODE_APP_SETTINGS` step. `draw_app_menu()` draws into the
  caller's frame (no `GRAPHICS_BEGIN_FRAME`/`END_FRAME`).
- `OC_ui.cpp`, `o_c_REV.ino` (Task 5): no screensaver state.
  `DispatchEvents` no longer sets `screensaver_` on idle or on an UP long
  press (which now does nothing), and `APP_EVENT_SCREENSAVER_ON/OFF` are
  never sent. Shoal draws the screensaver view on its own screen
  (`Core::drawScope`).
- `OC_apps.ino`: `Ui::ConfirmReset()` returns false. `::FOURCC` qualified
  in `GlobalSettings` and `AppData` (clang: the member name hides the
  template).
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

## Per-instance state (stage 1, Task 3)

Every mutable global lives in one `shoal::Context` per `shoal::Core`
(`../FirmwareContext.inc`, convention in `../shim/Context.hpp`). Each change
is marked `// SHOAL:`.

- Namespace-scope globals: the definition and any `extern` become
  `SHOAL_STATE(T, name)` plus `#define name name_ref()`, so uses are
  unchanged. Files: `OC_autotune.*`, `OC_chords.*`, `OC_menus.cpp`,
  `OC_patterns.*`, `OC_scales.*`, `OC_ui.*` (`MENU_REDRAW`'s `extern` too),
  `OC_apps.h`/`.ino`, `OC_calibration.h`/`.ino`, `o_c_REV.ino`,
  `src/drivers/display.*`, and every `APP_*.ino`.
- Renamed with a `g_` prefix because the macro would hit another identifier:
  `OC::user_scales`, `user_patterns`, `user_chords`, `auto_calibration_data`
  (`GlobalSettings` members), `OC::ui` (the apps' `ui` members),
  `OC::calibration_data` (`ADC::Init`/`DAC::Init` parameters),
  `APP_QQ.ino`'s `history` (`APP_REFS.ino` locals) and `APP_A_SEQ.ino`'s
  `copy_sequence` (`PatternEditor::copy_sequence()`). All uses renamed.
- Static data members of `OC::ADC`, `OC::DAC`, `OC::DigitalInputs` and
  `stmlib::Random` became private static accessors returning a reference
  (`values_()`, ...); uses call them. `OC::CORE::ticks` and
  `app_isr_enabled` (`OC_core.h`) are reached through `ticks_ref()` and
  `app_isr_enabled_ref()` (no macro: `ticks` is a common local name).
- Anonymous state structs named so `Context` can hold them:
  `OC::coords` (`OC_menus.cpp`), `LorenzGeneratorState` (`APP_LORENZ.ino`),
  `PolyLfoState` (`APP_POLYLFO.ino`). `display::FrameBufferType` typedef
  added (`SHOAL_STATE` takes one macro argument).
- Read-only tables made `const`: `string_table`, `lookup_table_table`,
  `lookup_table_hr_table`, `wt_table` (`frames_`, `peaks_`,
  `streams_resources.*`), `tonnetz::transform_names_str` and
  `transformations` (`tonnetz/tonnetz.h`), `clock_fraction_names` and
  `cell_event_masks` (`APP_AUTOMATONNETZ.ino`). `available_apps`
  (`OC_apps.ino`) is `constexpr` (so `DEFAULT_APP_ID` is a constant), and
  `apps::current_app`, `apps::find` and `Ui::DispatchEvents` use
  `const App*`.

## Shim-side substitutions (no firmware edit)

- `extern/dspinst.h` compiles to nothing: `shim/dspinst.h`, included first,
  defines its include guard.
- `arm_math.h` (for `util/util_sync.h`) and
  `src/drivers/FreqMeasure/OC_FreqMeasure.h` (for `APP_REFS.ino`) resolve to
  stand-ins under `shim/`. FreqMeasure never has a reading.
- `typeof` is `__typeof__`; `F_CPU`, `FASTRUN`, `DMAMEM`, interrupts,
  `__LDREXW`/`__STREXW`, `millis`/`micros` (from a per-instance 64-bit tick
  counter, so `millis()` wraps at 2^32 ms as on hardware), `delay`
  (no-op), `random`, `elapsedMillis`, `Serial` and EEPROM come from
  `shim/Arduino.h` and `shim/EEPROM.h`. A default-constructed
  `elapsedMillis` starts at 0 (`millis()` at boot) instead of reading
  `millis()`, because firmware objects are built with their context, before
  any context is current (`APP_REFS.ino`'s `ReferencesApp` holds one).

## Desktop-CPU guards

- Drawing changes no firmware state (Task 5 review). Upstream's menu draw
  code advanced two pieces of state that gate input, which would have tied
  behaviour to how often the host draws (or whether it draws at all):
  `OC_scale_edit.h`'s `ticks_` (Meta-Q's scale editor lets UP/DOWN switch
  slots only once it passes 250) and `OC_autotuner.h`'s error/completion
  status transitions (which the button handling reads). Both moved from
  `Draw()` into `Redrawn()`. `shoal_main_pass()` (`o_c_REV.ino`) keeps
  upstream's redraw bookkeeping (`MENU_REDRAW`, `LAST_REDRAW_TIME`) and, where
  upstream drew, calls `DQ_menuRedrawn()` (`APP_DQ.ino`) or
  `REFS_menuRedrawn()` (`APP_REFS.ino`) for the current app, at the same
  cadence (every UI event and at least every 2 ms). Every other `*_menu`,
  `*_screensaver` and editor `Draw()` writes only draw-only scratch (scope
  averaging, preview and history buffers).
- `APP_REFS.ino` (References screensaver view): with no TR4 frequency,
  `round(12000 * log2f(0))` converted -inf to `int32_t` (undefined on the
  host; ARM saturates). The value is printed only when a frequency is
  measured, so it is now 0 when the frequency is 0.
- `frames_poly_lfo.cpp` (Quadraturia, any settings): `PolyLfo::Render` read
  `dac_code_[i-1]` at `i == 0` (index -1). The value is multiplied by
  `am_depths[0]`, which is always 0, so it now reads 0 instead.
- `OC_apps.ino`, `OC_apps.h` (`apps::Init`): returns `bool`, true when the app
  data page loaded (`Core::restore`'s result); upstream returns `void`.
- `OC_apps.ino` (`save_app_data`): the chunks are written starting at app 0
  instead of a random app. Upstream's random start (so that no app is always
  the one dropped when they do not all fit) made every save differ in bytes
  and advanced the random sequence the apps share; all apps fit, which
  `test_state` checks.
- `util/util_pagestorage.h` (`PageStorage::checksum`): the length is
  `sizeof(DATA_TYPE)` instead of `sizeof(page_data) - sizeof(page_header)`.
  The same on the 32-bit Teensy; on a 64-bit host `AppData` (a `size_t`)
  aligns the data to 8, padding follows the 12-byte header, and the upstream
  length read the checksum four bytes past the page (an out-of-bounds read of
  whatever follows, which made `Load` reject valid pages at random).

- `OC_input_map.cpp` (`Input_Map::Process`): `q` is -1 when the input is 512
  or more from every code word, and `codebook_[q - 1]` / `codebook_[q + 1]`
  ran off either end of the 16-entry array (index -2, -1 and 16; reachable
  from in-range settings, e.g. Sequins or Acid Curds on a CV input). The
  neighbour reads are now the values the Teensy build read: indices 16 and -1
  alias the members next to `codebook_` (same layout on both), index -2 is 0.
- `OC_apps.ino` (`sanitize_global_settings`, new, called after a good global
  settings load): user scales (note count 4..16, as the scale editor keeps
  it), patterns and chords, the DAC scaling bytes
  are brought into the ranges their editors allow, since a patch file can
  carry any bytes with a valid checksum.
- `OC_apps.ino` (`restore_app_data`): `used` is clamped to the data size; the
  walk stops when less than a chunk header remains or a chunk is shorter than
  its header (a zero length looped forever); the "app not found" message no
  longer dereferences the null app.
- `util/util_turing.h` (`TuringShiftRegister::Clock`): `0xffffffff << length_`
  with `length_` 32 (in range for every app that uses it) shifts by the type's
  width, which is undefined; it now returns the whole register, as the
  Teensy's shift does.
- `bjorklund.cpp` (`EuclideanFilter`, `EuclideanPattern`, new `bjorklund_row`):
  only the table index is clamped (rows 1..31, beat columns 0..32); H1200's
  Euclidean length reaches 32 (in range), which read past the 1023-entry
  table. The cycle length `num_steps + 1` and the rotation keep the caller's
  value, and position 32 reads as a rest, as the Teensy's shift gives.
- `OC_input_maps.h` (`InputMaps::GetInputMap`): the index is clamped to the
  18 rows of the table; Sequins passes its sequence length (up to 32), which
  read past the table. Row 17 is the empty map.
- `OC_menus.h` (`menu::DrawChord`), `APP_CHORDS.ino` (the chord lookup in
  `Chords::Update`): the chord slot is clamped to the 32-entry user chord
  table, as `OC::Chords::GetChord` already does; the screensaver and the
  update counted slots past it.
- `peaks_bytebeat.cpp` (`ByteBeat::ProcessSingleSample`, equations 2, 3, 5, 6,
  8, 9, 10, 11, 13, 14, 15): divisions, remainders and variable shifts go through
  `bb_div`, `bb_mod`, `bb_shr`, which give the Teensy's results where C++ is
  undefined (division by zero is a crash on x86; x/0 = 0, x%0 = x, a shift by
  32 or more empties the register). All are reachable from in-range
  parameters (p0..p2, the pitch, the last sample).
- `OC_digital_inputs.h` (`DIGITAL_INPUT_MASK`): an index outside 0..31 gives an
  empty mask (Acid Curds, Sequins etc. compute `trigger_source - 1` with
  source 0 = "none", i.e. a shift by -1).
- `bjorklund.h` (`rotl32`), `bjorklund.cpp` (`EuclideanPattern`): shifts by 32
  or more guarded (a length of 31 with no rotation shifted by 32); the
  `num_steps` clamp of `EuclideanFilter` applies here too.
