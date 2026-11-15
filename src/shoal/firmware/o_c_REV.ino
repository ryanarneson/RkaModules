// Copyright (c) 2015, 2016 Max Stadler, Patrick Dowling
//
// Original Author : Max Stadler
// Heavily modified: Patrick Dowling (pld@gurkenkiste.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Main startup/loop for O&C firmware

// SHOAL: this file is restructured for the host. The timers, ISRs and the
// endless loop() are replaced by three functions the shim calls:
//   shoal_boot()       the setup() sequence without delays, splash screen,
//                      calibration UI or timers; returns whether app data
//                      loaded from the EEPROM;
//   shoal_tick()       the body of CORE_timer_ISR (called every 60 us);
//   shoal_main_pass()  one pass of the loop() body, without the draw
//                      (Core::drawMenu/drawScope draw on request).
// The UI timer ISR is OC::g_ui.Poll(), called by the shim every 1 ms.

#include <EEPROM.h>

#include "OC_apps.h"
#include "OC_core.h"
#include "OC_DAC.h" // SHOAL: OC_debug.h include deleted
#include "OC_gpio.h"
#include "OC_ADC.h"
#include "OC_calibration.h"
#include "OC_digital_inputs.h"
#include "OC_menus.h"
#include "OC_ui.h"
#include "OC_version.h"
#include "OC_options.h"
#include "src/drivers/display.h" // SHOAL: src/drivers/ADC/OC_util_ADC.h include deleted
#include "util/util_debugpins.h" // SHOAL: VBiasManager.h include deleted

SHOAL_STATE(unsigned long, LAST_REDRAW_TIME) // SHOAL: per-instance state (shoal::Context)
#define LAST_REDRAW_TIME LAST_REDRAW_TIME_ref()
// SHOAL: MENU_REDRAW lives in shoal::Context (initially true).
SHOAL_STATE(OC::UiMode, ui_mode) // SHOAL: per-instance state (shoal::Context)
#define ui_mode ui_mode_ref()
const bool DUMMY = false;

/*  ------------------------ core timer ISR ---------------------------   */
// SHOAL: OC::CORE::app_isr_enabled (initially false) and OC::CORE::ticks
// (initially 0) live in shoal::Context.

void FASTRUN shoal_tick() {
  // DAC and display share SPI. By first updating the DAC values, then starting
  // a DMA transfer to the display things are fairly nicely interleaved. In the
  // next ISR, the display transfer is finalized (CS update).

  display::Flush();
  OC::DAC::Update();
  display::Update();

  // see OC_ADC.h for details; empirically (with current parameters), Scan_DMA() picks up new samples @ 5.55kHz
  OC::ADC::Scan_DMA();

  // Pin changes are tracked in separate ISRs, so depending on prio it might
  // need extra precautions.
  OC::DigitalInputs::Scan();

  ++OC::CORE::ticks_ref(); // SHOAL: per-instance state
  if (OC::CORE::app_isr_enabled_ref()) // SHOAL: per-instance state
    OC::apps::ISR();
}

/*       ---------------------------------------------------------         */

bool shoal_boot() { // SHOAL: returns apps::Init's result
  OC::DigitalInputs::Init();
  OC::ADC::Init(&OC::g_calibration_data.adc); // Yes, it's using the calibration_data before it's loaded... // SHOAL: g_ prefix (name clashes with Init parameters)
  OC::DAC::Init(&OC::g_calibration_data.dac); // SHOAL: g_ prefix (name clashes with Init parameters)

  display::Init();

  GRAPHICS_BEGIN_FRAME(true);
  GRAPHICS_END_FRAME();

  calibration_load();

  OC::menu::Init();
  OC::g_ui.Init(); // SHOAL: g_ prefix (name clashes with app ui members)
  OC::g_ui.configure_encoders(OC::g_calibration_data.encoder_config()); // SHOAL: g_ prefix (names clash with app ui members, Init parameters)

  ui_mode = OC::UI_MODE_MENU;
  OC::g_ui.set_screensaver_timeout(OC::g_calibration_data.screensaver_timeout); // SHOAL: g_ prefix (names clash with app ui members, Init parameters)

  // initialize apps
  const bool app_data_loaded = OC::apps::Init(false); // SHOAL: result kept for the caller

  // from the start of loop()
  OC::CORE::app_isr_enabled_ref() = true; // SHOAL: per-instance state
  return app_data_loaded; // SHOAL
}

/*  ---------    main loop  --------  */

// SHOAL: firmware state that upstream's menu drawing advanced, advanced at
// upstream's redraw cadence instead (drawing is the host's, at any rate).
// Found by reading every *_menu/*_screensaver and editor Draw(): only the
// Meta-Q scale editor's ticks_ and the References autotuner's status gate
// behaviour; the rest write draw-only scratch (scope averaging, preview and
// history buffers).
static void shoal_menu_redrawn() {
  if (OC::apps::current_app->DrawMenu == DQ_menu)
    DQ_menuRedrawn();
  else if (OC::apps::current_app->DrawMenu == REFS_menu)
    REFS_menuRedrawn();
}

void FASTRUN shoal_main_pass() {

    // SHOAL: upstream drew the menu here whenever MENU_REDRAW was set (on
    // every UI event, app updates, and at least every REDRAW_TIMEOUT_MS + 1
    // ms). The host draws instead; the redraw bookkeeping stays so the
    // draw-paced state advances at the same cadence.
    if (MENU_REDRAW && !OC::g_ui.app_menu_open()) {
      shoal_menu_redrawn();
      MENU_REDRAW = 0;
      LAST_REDRAW_TIME = millis();
    }

    // SHOAL: the app menu is non-blocking (Ui::DispatchEvents opens, runs and
    // closes it), so there is no UI_MODE_APP_SETTINGS step here. While it is
    // open the current app's loop() does not run, as upstream's blocking
    // AppSettings() kept it from running.

    // Run current app
    if (!OC::g_ui.app_menu_open()) // SHOAL: app suspended while the app menu is open
      OC::apps::current_app->loop();

    // UI events
    // SHOAL: no screensaver state, so no APP_EVENT_SCREENSAVER_ON/OFF
    // transitions; the mode is MENU or APP_SETTINGS (app menu open).
    ui_mode = OC::g_ui.DispatchEvents(OC::apps::current_app); // SHOAL: g_ prefix (name clashes with app ui members)

    if (millis() - LAST_REDRAW_TIME > REDRAW_TIMEOUT_MS)
      MENU_REDRAW = 1;
}
