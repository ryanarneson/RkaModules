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
//                      calibration UI or timers;
//   shoal_tick()       the body of CORE_timer_ISR (called every 60 us);
//   shoal_main_pass()  one pass of the loop() body, without the draw.
// The UI timer ISR is OC::ui.Poll(), called by the shim every 1 ms.

#include <EEPROM.h>

#include "OC_apps.h"
#include "OC_core.h"
#include "OC_DAC.h"
#include "OC_gpio.h"
#include "OC_ADC.h"
#include "OC_calibration.h"
#include "OC_digital_inputs.h"
#include "OC_menus.h"
#include "OC_ui.h"
#include "OC_version.h"
#include "OC_options.h"
#include "src/drivers/display.h"
#include "util/util_debugpins.h"

unsigned long LAST_REDRAW_TIME = 0;
uint_fast8_t MENU_REDRAW = true;
OC::UiMode ui_mode = OC::UI_MODE_MENU;
const bool DUMMY = false;

/*  ------------------------ core timer ISR ---------------------------   */
volatile bool OC::CORE::app_isr_enabled = false;
volatile uint32_t OC::CORE::ticks = 0;

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

  ++OC::CORE::ticks;
  if (OC::CORE::app_isr_enabled)
    OC::apps::ISR();
}

/*       ---------------------------------------------------------         */

void shoal_boot() {
  OC::DigitalInputs::Init();
  OC::ADC::Init(&OC::calibration_data.adc); // Yes, it's using the calibration_data before it's loaded...
  OC::DAC::Init(&OC::calibration_data.dac);

  display::Init();

  GRAPHICS_BEGIN_FRAME(true);
  GRAPHICS_END_FRAME();

  calibration_load();

  OC::menu::Init();
  OC::ui.Init();
  OC::ui.configure_encoders(OC::calibration_data.encoder_config());

  ui_mode = OC::UI_MODE_MENU;
  OC::ui.set_screensaver_timeout(OC::calibration_data.screensaver_timeout);

  // initialize apps
  OC::apps::Init(false);

  // from the start of loop()
  OC::CORE::app_isr_enabled = true;
}

/*  ---------    main loop  --------  */

void FASTRUN shoal_main_pass() {

    // don't change current_app while it's running
    if (OC::UI_MODE_APP_SETTINGS == ui_mode) {
      OC::ui.AppSettings();
      ui_mode = OC::UI_MODE_MENU;
    }

    // Run current app
    OC::apps::current_app->loop();

    // UI events
    OC::UiMode mode = OC::ui.DispatchEvents(OC::apps::current_app);

    // State transition for app
    if (mode != ui_mode) {
      if (OC::UI_MODE_SCREENSAVER == mode)
        OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_SCREENSAVER_ON);
      else if (OC::UI_MODE_SCREENSAVER == ui_mode)
        OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_SCREENSAVER_OFF);
      ui_mode = mode;
    }

    if (millis() - LAST_REDRAW_TIME > REDRAW_TIMEOUT_MS)
      MENU_REDRAW = 1;
}
