#include <Arduino.h>
#include <algorithm>

#include "OC_apps.h"
#include "OC_bitmaps.h"
#include "OC_calibration.h"
#include "OC_config.h"
#include "OC_core.h"
#include "OC_gpio.h"
#include "OC_menus.h"
#include "OC_strings.h"
#include "OC_ui.h"
#include "OC_version.h"
#include "OC_options.h"
#include "src/drivers/display.h"

#ifdef VOR
#include "VBiasManager.h"
VBiasManager *VBiasManager::instance = 0;
#endif

SHOAL_STATE(uint_fast8_t, MENU_REDRAW) // SHOAL: per-instance state (shoal::Context)
#define MENU_REDRAW MENU_REDRAW_ref()

namespace OC {

// SHOAL: g_ui lives in shoal::Context (g_ prefix: the name clashes with the apps' ui members).

void Ui::Init() {
  ticks_ = 0;
  set_screensaver_timeout(SCREENSAVER_TIMEOUT_S);

  #ifdef VOR
  static const int button_pins[] = { but_top, but_bot, butL, butR, but_mid };
  #else
  static const int button_pins[] = { but_top, but_bot, butL, butR };
  #endif
  
  for (size_t i = 0; i < CONTROL_BUTTON_LAST; ++i) {
    buttons_[i].Init(button_pins[i], OC_GPIO_BUTTON_PINMODE);
  }
  std::fill(button_press_time_, button_press_time_ + 4, 0);
  button_state_ = 0;
  button_ignore_mask_ = 0;
  screensaver_ = false;
  preempt_screensaver_ = false;
  app_menu_open_ = false; // SHOAL: non-blocking app menu

  encoder_right_.Init(OC_GPIO_ENC_PINMODE);
  encoder_left_.Init(OC_GPIO_ENC_PINMODE);

  event_queue_.Init();
}

void Ui::configure_encoders(EncoderConfig encoder_config) {
  SERIAL_PRINTLN("Configuring encoders: %s (%x)", OC::Strings::encoder_config_strings[encoder_config], encoder_config);

  encoder_right_.reverse(encoder_config & ENCODER_CONFIG_R_REVERSED);
  encoder_left_.reverse(encoder_config & ENCODER_CONFIG_L_REVERSED);
}

void Ui::set_screensaver_timeout(uint32_t seconds) {
  uint32_t timeout = seconds * 1000U;
  if (timeout < kLongPressTicks * 2)
    timeout = kLongPressTicks * 2;

  screensaver_timeout_ = timeout;
  SERIAL_PRINTLN("Set screensaver timeout to %lu", timeout);
  event_queue_.Poke();
}

void FASTRUN Ui::_Poke() {
  event_queue_.Poke();
}

void Ui::_preemptScreensaver(bool v) {
  preempt_screensaver_ = v;
}

void FASTRUN Ui::Poll() {

  uint32_t now = ++ticks_;
  uint16_t button_state = 0;

  for (size_t i = 0; i < CONTROL_BUTTON_LAST; ++i) {
    if (buttons_[i].Poll())
      button_state |= control_mask(i);
  }

  for (size_t i = 0; i < CONTROL_BUTTON_LAST; ++i) {
    auto &button = buttons_[i];
    if (button.just_pressed()) {
      button_press_time_[i] = now;
    } else if (button.released()) {
      if (now - button_press_time_[i] < kLongPressTicks)
        PushEvent(UI::EVENT_BUTTON_PRESS, control_mask(i), 0, button_state);
      else
        PushEvent(UI::EVENT_BUTTON_LONG_PRESS, control_mask(i), 0, button_state);
    }
  }

  encoder_right_.Poll();
  encoder_left_.Poll();

  int32_t increment;
  increment = encoder_right_.Read();
  if (increment)
    PushEvent(UI::EVENT_ENCODER, CONTROL_ENCODER_R, increment, button_state);

  increment = encoder_left_.Read();
  if (increment)
    PushEvent(UI::EVENT_ENCODER, CONTROL_ENCODER_L, increment, button_state);

  button_state_ = button_state;
}

UiMode Ui::DispatchEvents(const App *app) { // SHOAL: const (available_apps is a read-only table)

  // SHOAL: while the non-blocking app menu is open it takes every event
  // (as upstream's AppSettings() loop did) and closes, unchanged, after
  // APP_SELECTION_TIMEOUT_MS without input.
  if (app_menu_open_) {
    while (app_menu_open_ && event_queue_.available()) {
      const UI::Event event = event_queue_.PullEvent();
      if (IgnoreEvent(event))
        continue;
      AppMenuHandle(event);
    }
    if (app_menu_open_ && idle_time() >= APP_SELECTION_TIMEOUT_MS)
      AppMenuClose(false);
    return app_menu_open_ ? UI_MODE_APP_SETTINGS : UI_MODE_MENU;
  }

  while (event_queue_.available()) {
    const UI::Event event = event_queue_.PullEvent();
    if (IgnoreEvent(event))
      continue;

    switch (event.type) {
      case UI::EVENT_BUTTON_PRESS:
        #ifdef VOR
          #ifdef VOR_NO_RANGE_BUTTON
        if (OC::CONTROL_BUTTON_UP == event.control) {
            VBiasManager *vbias_m = vbias_m->get();
            if (vbias_m->IsEditing()) vbias_m->AdvanceBias();
            else app->HandleButtonEvent(event);
        } else app->HandleButtonEvent(event);
          #else
        if (OC::CONTROL_BUTTON_M == event.control) {
            VBiasManager *vbias_m = vbias_m->get();
            vbias_m->AdvanceBias();
        } else app->HandleButtonEvent(event);
        #endif
        #else
        app->HandleButtonEvent(event);
        #endif
        break;
      case UI::EVENT_BUTTON_LONG_PRESS:
        if (OC::CONTROL_BUTTON_UP == event.control) {
        #ifdef VOR_NO_RANGE_BUTTON
            VBiasManager *vbias_m = vbias_m->get();
            vbias_m->AdvanceBias();
        #else
            // SHOAL: no screensaver state: an UP long press does nothing
            // (Shoal shows the screensaver view on its own screen).
        #endif
        }
        else if (OC::CONTROL_BUTTON_R == event.control) {
          AppMenuOpen(); // SHOAL: non-blocking app menu; queued events go to it
          return UI_MODE_APP_SETTINGS;
        }
        else
          app->HandleButtonEvent(event);
        break;
      case UI::EVENT_ENCODER:
        app->HandleEncoderEvent(event);
        break;
      default:
        break;
    }
    MENU_REDRAW = 1;
  }

  // SHOAL: no screensaver state: idling does not enter it, so the first
  // input after a pause is never swallowed.
  return UI_MODE_MENU;
}

// SHOAL: Ui::Splashscreen() deleted; Shoal boots straight into the saved app.

} // namespace OC
