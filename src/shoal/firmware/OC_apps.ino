// Copyright (c) 2016 Patrick Dowling
//
// Author: Patrick Dowling (pld@gurkenkiste.com)
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

#include "OC_apps.h"
#include "OC_digital_inputs.h"
#include "OC_autotune.h"

#define DECLARE_APP(a, b, name, prefix, isr) \
{ TWOCC<a,b>::value, name, \
  prefix ## _init, prefix ## _storageSize, prefix ## _save, prefix ## _restore, \
  prefix ## _handleAppEvent, \
  prefix ## _loop, prefix ## _menu, prefix ## _screensaver, \
  prefix ## _handleButtonEvent, \
  prefix ## _handleEncoderEvent, \
  isr \
}

#ifdef BORING_APP_NAMES
constexpr OC::App available_apps[] = { // SHOAL: read-only table (constexpr, so DEFAULT_APP_ID is a constant)
  DECLARE_APP('A','S', "ASR", ASR, ASR_isr),
  DECLARE_APP('H','A', "Triads", H1200, H1200_isr),
  DECLARE_APP('A','T', "Vectors", Automatonnetz, Automatonnetz_isr),
  DECLARE_APP('Q','Q', "4x Quantizer", QQ, QQ_isr),
  DECLARE_APP('D','Q', "2x Quantizer", DQ, DQ_isr),
  DECLARE_APP('P','L', "Quadrature LFO", POLYLFO, POLYLFO_isr),
  DECLARE_APP('L','R', "Lorenz", LORENZ, LORENZ_isr),
  DECLARE_APP('E','G', "4x EG", ENVGEN, ENVGEN_isr),
  DECLARE_APP('S','Q', "2x Sequencer", SEQ, SEQ_isr),
  DECLARE_APP('B','B', "Balls", BBGEN, BBGEN_isr),
  DECLARE_APP('B','Y', "Bytebeats", BYTEBEATGEN, BYTEBEATGEN_isr),
  DECLARE_APP('C','Q', "Chords", CHORDS, CHORDS_isr),
  DECLARE_APP('R','F', "Voltages", REFS, REFS_isr)
};
#else 
constexpr OC::App available_apps[] = { // SHOAL: read-only table (constexpr, so DEFAULT_APP_ID is a constant)
  DECLARE_APP('A','S', "CopierMaschine", ASR, ASR_isr),
  DECLARE_APP('H','A', "Harrington 1200", H1200, H1200_isr),
  DECLARE_APP('A','T', "Automatonnetz", Automatonnetz, Automatonnetz_isr),
  DECLARE_APP('Q','Q', "Quantermain", QQ, QQ_isr),
  DECLARE_APP('D','Q', "Meta-Q", DQ, DQ_isr),
  DECLARE_APP('P','L', "Quadraturia", POLYLFO, POLYLFO_isr),
  DECLARE_APP('L','R', "Low-rents", LORENZ, LORENZ_isr),
  DECLARE_APP('E','G', "Piqued", ENVGEN, ENVGEN_isr),
  DECLARE_APP('S','Q', "Sequins", SEQ, SEQ_isr),
  DECLARE_APP('B','B', "Dialectic Ping Pong", BBGEN, BBGEN_isr),
  DECLARE_APP('B','Y', "Viznutcracker sweet", BYTEBEATGEN, BYTEBEATGEN_isr),
  DECLARE_APP('C','Q', "Acid Curds", CHORDS, CHORDS_isr),
  DECLARE_APP('R','F', "References", REFS, REFS_isr)
};
#endif

static constexpr int NUM_AVAILABLE_APPS = ARRAY_SIZE(available_apps);

namespace OC {

// Global settings are stored separately to actual app setings.
// The theory is that they might not change as often.
struct GlobalSettings {
  static constexpr uint32_t FOURCC = ::FOURCC<'O','C','S',2>::value; // SHOAL: '::' for clang (member name hides the template)

  bool encoders_enable_acceleration;
  bool reserved0;
  bool reserved1;
  uint32_t DAC_scaling;
  uint16_t current_app_id;
  
  OC::Scale user_scales[OC::Scales::SCALE_USER_LAST];
  OC::Pattern user_patterns[OC::Patterns::PATTERN_USER_ALL];
  OC::Chord user_chords[OC::Chords::CHORDS_USER_LAST];
  OC::Autotune_data auto_calibration_data[DAC_CHANNEL_LAST];
};

// App settings are packed into a single blob of binary data; each app's chunk
// gets its own header with id and the length of the entire chunk. This makes
// this a bit more flexible during development.
// Chunks are aligned on 2-byte boundaries for arbitrary reasons (thankfully M4
// allows unaligned access...)
struct AppChunkHeader {
  uint16_t id;
  uint16_t length;
} __attribute__((packed));

struct AppData {
  static constexpr uint32_t FOURCC = ::FOURCC<'O','C','A',4>::value; // SHOAL: '::' for clang (member name hides the template)

  static constexpr size_t kAppDataSize = EEPROM_APPDATA_BINARY_SIZE;
  char data[kAppDataSize];
  size_t used;
};

typedef PageStorage<EEPROMStorage, EEPROM_GLOBALSETTINGS_START, EEPROM_GLOBALSETTINGS_END, GlobalSettings> GlobalSettingsStorage;
typedef PageStorage<EEPROMStorage, EEPROM_APPDATA_START, EEPROM_APPDATA_END, AppData> AppDataStorage;

SHOAL_STATE(GlobalSettings, global_settings) // SHOAL: per-instance state (shoal::Context)
#define global_settings global_settings_ref()
SHOAL_STATE(GlobalSettingsStorage, global_settings_storage) // SHOAL: per-instance state (shoal::Context)
#define global_settings_storage global_settings_storage_ref()

SHOAL_STATE(AppData, app_settings) // SHOAL: per-instance state (shoal::Context)
#define app_settings app_settings_ref()
SHOAL_STATE(AppDataStorage, app_data_storage) // SHOAL: per-instance state (shoal::Context)
#define app_data_storage app_data_storage_ref()

static constexpr int DEFAULT_APP_INDEX = 0;
static const uint16_t DEFAULT_APP_ID = available_apps[DEFAULT_APP_INDEX].id;

void save_global_settings() {
  SERIAL_PRINTLN("Save global settings");

  memcpy(global_settings.user_scales, OC::g_user_scales, sizeof(OC::g_user_scales)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
  memcpy(global_settings.user_patterns, OC::g_user_patterns, sizeof(OC::g_user_patterns)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
  memcpy(global_settings.user_chords, OC::g_user_chords, sizeof(OC::g_user_chords)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
  memcpy(global_settings.auto_calibration_data, OC::g_auto_calibration_data, sizeof(OC::g_auto_calibration_data)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
  // scaling settings:
  global_settings.DAC_scaling = OC::DAC::store_scaling();
  
  global_settings_storage.Save(global_settings);
  SERIAL_PRINTLN("Saved global settings: page_index %d", global_settings_storage.page_index());
}

void save_app_data() {
  SERIAL_PRINTLN("Save app data... (%u bytes available)", OC::AppData::kAppDataSize);

  app_settings.used = 0;
  char *data = app_settings.data;
  char *data_end = data + OC::AppData::kAppDataSize;

  // SHOAL: upstream starts at a random app, so that if the apps ever outgrew
  // the page it would not always be the same ones that were dropped. They all
  // fit (test_state checks every app round-trips), and a random start made
  // every save differ in bytes and advanced the random sequence the apps use.
  size_t start_app = 0;
  for (size_t i = 0; i < NUM_AVAILABLE_APPS; ++i) {
    const auto &app = available_apps[(start_app + i) % NUM_AVAILABLE_APPS];
    size_t storage_size = app.storageSize() + sizeof(AppChunkHeader);
    if (storage_size & 1) ++storage_size; // Align chunks on 2-byte boundaries
    if (storage_size > sizeof(AppChunkHeader) && app.Save) {
      if (data + storage_size > data_end) {
        SERIAL_PRINTLN("%s: ERROR: %u BYTES NEEDED, %u BYTES AVAILABLE OF %u BYTES TOTAL", app.name, storage_size, data_end - data, AppData::kAppDataSize);
        continue;
      }

      AppChunkHeader *chunk = reinterpret_cast<AppChunkHeader *>(data);
      chunk->id = app.id;
      chunk->length = storage_size;
      #ifdef PRINT_DEBUG
        SERIAL_PRINTLN("* %s (%02x) : Saved %u bytes... (%u)", app.name, app.id, app.Save(chunk + 1), storage_size);
      #else
        app.Save(chunk + 1);
      #endif
      app_settings.used += chunk->length;
      data += chunk->length;
    }
  }
  SERIAL_PRINTLN("App settings used: %u/%u", app_settings.used, EEPROM_APPDATA_BINARY_SIZE);
  app_data_storage.Save(app_settings);
  SERIAL_PRINTLN("Saved app settings in page_index %d", app_data_storage.page_index());
}

void restore_app_data() {
  SERIAL_PRINTLN("Restoring app data from page_index %d, used=%u", app_data_storage.page_index(), app_settings.used);

  const char *data = app_settings.data;
  const char *data_end = data + app_settings.used;
  size_t restored_bytes = 0;

  while (data < data_end) {
    const AppChunkHeader *chunk = reinterpret_cast<const AppChunkHeader *>(data);
    if (data + chunk->length > data_end) {
      SERIAL_PRINTLN("App chunk length %u exceeds available space (%u)", chunk->length, data_end - data);
      break;
    }

    const App *app = apps::find(chunk->id); // SHOAL: const (available_apps is a read-only table)
    if (!app) {
      SERIAL_PRINTLN("App %02x not found, ignoring chunk...", app->id);
      if (!chunk->length)
        break;
      data += chunk->length;
      continue;
    }
    size_t expected_length = app->storageSize() + sizeof(AppChunkHeader);
    if (expected_length & 0x1) ++expected_length;
    if (chunk->length != expected_length) {
      SERIAL_PRINTLN("* %s (%02x): chunk length %u != %u (storageSize=%u), skipping...", app->name, chunk->id, chunk->length, expected_length, app->storageSize());
      data += chunk->length;
      continue;
    }

    if (app->Restore) {
      #ifdef PRINT_DEBUG
        SERIAL_PRINTLN("* %s (%02x): Restored %u from %u (chunk length %u)...", app->name, chunk->id, app->Restore(chunk + 1), chunk->length - sizeof(AppChunkHeader), chunk->length);
      #else
        app->Restore(chunk + 1);
      #endif
    }
    restored_bytes += chunk->length;
    data += chunk->length;
    
#ifdef VOR
    VBiasManager *vbias_m = vbias_m->get();
    vbias_m->SetStateForApp(apps::index_of(global_settings.current_app_id));
#endif
  }

  SERIAL_PRINTLN("App data restored: %u, expected %u", restored_bytes, app_settings.used);
}

namespace apps {

void set_current_app(int index) {
  current_app = &available_apps[index];
  global_settings.current_app_id = current_app->id;
}

// SHOAL: current_app (initially &available_apps[DEFAULT_APP_INDEX]) lives in shoal::Context.

const App *find(uint16_t id) { // SHOAL: const (available_apps is a read-only table)
  for (auto &app : available_apps)
    if (app.id == id) return &app;
  return nullptr;
}

int index_of(uint16_t id) {
  int i = 0;
  for (const auto &app : available_apps) {
    if (app.id == id) return i;
    ++i;
  }
  return i;
}

// SHOAL: returns whether the app data page loaded (Core::restore's result);
// upstream's is void.
bool Init(bool reset_settings) {
  bool app_data_loaded = false; // SHOAL

  Scales::Init();
  AUTOTUNE::Init();
  for (auto &app : available_apps)
    app.Init();

  global_settings.current_app_id = DEFAULT_APP_ID;
  global_settings.encoders_enable_acceleration = OC_ENCODERS_ENABLE_ACCELERATION_DEFAULT;
  global_settings.reserved0 = false;
  global_settings.reserved1 = false;
  global_settings.DAC_scaling = VOLTAGE_SCALING_1V_PER_OCT; 

  if (reset_settings) {
    if (g_ui.ConfirmReset()) { // SHOAL: g_ prefix (name clashes with app ui members)
      SERIAL_PRINTLN("Erase EEPROM ...");
      EEPtr d = EEPROM_GLOBALSETTINGS_START;
      size_t len = EEPROMStorage::LENGTH - EEPROM_GLOBALSETTINGS_START;
      while (len--)
        *d++ = 0;
      SERIAL_PRINTLN("...done");
      SERIAL_PRINTLN("Skip settings, using defaults...");
      global_settings_storage.Init();
      app_data_storage.Init();
    } else {
      reset_settings = false;
    }
  }

  if (!reset_settings) {
    SERIAL_PRINTLN("Load global settings: size: %u, PAGESIZE=%u, PAGES=%u, LENGTH=%u",
                  sizeof(GlobalSettings),
                  GlobalSettingsStorage::PAGESIZE,
                  GlobalSettingsStorage::PAGES,
                  GlobalSettingsStorage::LENGTH);

    if (!global_settings_storage.Load(global_settings)) {
      SERIAL_PRINTLN("Settings invalid, using defaults!");
    } else {
      SERIAL_PRINTLN("Loaded settings from page_index %d, current_app_id is %02x",
                    global_settings_storage.page_index(),global_settings.current_app_id);
      memcpy(g_user_scales, global_settings.user_scales, sizeof(g_user_scales)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
      memcpy(g_user_patterns, global_settings.user_patterns, sizeof(g_user_patterns)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
      memcpy(g_user_chords, global_settings.user_chords, sizeof(g_user_chords)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
      memcpy(g_auto_calibration_data, global_settings.auto_calibration_data, sizeof(g_auto_calibration_data)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
      DAC::choose_calibration_data(); // either use default data, or g_auto_calibration_data // SHOAL: g_ prefix (name clashes with GlobalSettings members)
      DAC::restore_scaling(global_settings.DAC_scaling); // recover output scaling settings
    }

    SERIAL_PRINTLN("Load app data: size is %u, PAGESIZE=%u, PAGES=%u, LENGTH=%u",
                  sizeof(AppData),
                  AppDataStorage::PAGESIZE,
                  AppDataStorage::PAGES,
                  AppDataStorage::LENGTH);

    if (!app_data_storage.Load(app_settings)) {
      SERIAL_PRINTLN("Data not loaded, using defaults!");
    } else {
      app_data_loaded = true; // SHOAL
      restore_app_data();
    }
  }

  int current_app_index = apps::index_of(global_settings.current_app_id);
  if (current_app_index < 0 || current_app_index >= NUM_AVAILABLE_APPS) {
    SERIAL_PRINTLN("App id %02x not found, using default!", global_settings.current_app_id);
    global_settings.current_app_id = DEFAULT_APP_INDEX;
    current_app_index = DEFAULT_APP_INDEX;
  }

  SERIAL_PRINTLN("Encoder acceleration: %s", global_settings.encoders_enable_acceleration ? "enabled" : "disabled");
  g_ui.encoders_enable_acceleration(global_settings.encoders_enable_acceleration); // SHOAL: g_ prefix (name clashes with app ui members)

  set_current_app(current_app_index);
  current_app->HandleAppEvent(APP_EVENT_RESUME);

  delay(100);
  return app_data_loaded; // SHOAL
}

}; // namespace apps

// SHOAL: draws into the frame the caller has begun (Core::drawMenu), so
// GRAPHICS_BEGIN_FRAME/GRAPHICS_END_FRAME are removed here.
void draw_app_menu(const menu::ScreenCursor<5> &cursor) {

  menu::SettingsListItem item;
  item.x = menu::kIndentDx;
  item.y = (64 - (5 * menu::kMenuLineH)) / 2;

  for (int current = cursor.first_visible();
       current <= cursor.last_visible();
       ++current, item.y += menu::kMenuLineH) {
    item.selected = current == cursor.cursor_pos();
    item.SetPrintPos();
    graphics.movePrintPos(weegfx::Graphics::kFixedFontW, 0);
    graphics.print(available_apps[current].name);
    if (global_settings.current_app_id == available_apps[current].id)
       graphics.drawBitmap8(item.x + 2, item.y + 1, 4, bitmap_indicator_4x8);
    item.DrawCustom();
  }
}

void draw_save_message(uint8_t c) {
  GRAPHICS_BEGIN_FRAME(true);
  uint8_t _size = c % 120;
  graphics.drawRect(63 - (_size >> 1), 31 - (_size >> 2), _size, _size >> 1);  
  GRAPHICS_END_FRAME();
}

SHOAL_STATE(menu::ScreenCursor<5>, app_menu_cursor) // SHOAL: per-instance state (shoal::Context)
#define app_menu_cursor app_menu_cursor_ref()

// SHOAL: upstream's blocking AppSettings() loop, split into a non-blocking
// app menu driven by Ui::DispatchEvents. Open suspends the current app,
// events scroll and choose, and close (with or without a change) resumes the
// current app. Differences: an R long press switches without saving (saving
// is the host's job in Shoal), L does nothing (DebugStats is dropped), and
// app_isr_enabled is not toggled around the switch (the shim runs ticks and
// the main pass on one thread, so no ISR can fire in between).
void Ui::AppMenuOpen() {
  SetButtonIgnoreMask();

  apps::current_app->HandleAppEvent(APP_EVENT_SUSPEND);

  app_menu_cursor.Init(0, NUM_AVAILABLE_APPS - 1);
  app_menu_cursor.Scroll(apps::index_of(global_settings.current_app_id));
  app_menu_open_ = true;
}

void Ui::AppMenuHandle(const UI::Event &event) {
  if (UI::EVENT_ENCODER == event.type && CONTROL_ENCODER_R == event.control) {
    app_menu_cursor.Scroll(event.value);
  } else if (CONTROL_BUTTON_R == event.control) {
    AppMenuClose(true);
  } else if (CONTROL_BUTTON_UP == event.control) {
    bool enabled = !global_settings.encoders_enable_acceleration;
    SERIAL_PRINTLN("Encoder acceleration: %s", enabled ? "enabled" : "disabled");
    encoders_enable_acceleration(enabled);
    global_settings.encoders_enable_acceleration = enabled;
  }
}

void Ui::AppMenuClose(bool change) {
  event_queue_.Flush();
  event_queue_.Poke();
  app_menu_open_ = false;

  if (change) {
    apps::set_current_app(app_menu_cursor.cursor_pos());
    FreqMeasure.end();
    OC::DigitalInputs::reInit();
  }

  encoders_enable_acceleration(global_settings.encoders_enable_acceleration);

  // Restore state
  apps::current_app->HandleAppEvent(APP_EVENT_RESUME);
}

// SHOAL: the reset confirmation is stubbed: it declines.
bool Ui::ConfirmReset() {
  return false;
}

}; // namespace OC
