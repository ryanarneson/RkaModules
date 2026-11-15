// SHOAL: not upstream. The Arduino build generates prototypes for the .ino
// files and concatenates them; Shoal's unity build (../Firmware.cpp) includes
// them one by one, so the functions used before their definition are declared
// here.
#ifndef SHOAL_PROTOTYPES_H_
#define SHOAL_PROTOTYPES_H_

#include <stddef.h>

#include "OC_apps.h" // OC::AppEvent, UI::Event

#define SHOAL_APP_PROTOTYPES(prefix) \
  void prefix##_init(); \
  size_t prefix##_storageSize(); \
  size_t prefix##_save(void *storage); \
  size_t prefix##_restore(const void *storage); \
  void prefix##_handleAppEvent(OC::AppEvent event); \
  void prefix##_loop(); \
  void prefix##_menu(); \
  void prefix##_screensaver(); \
  void prefix##_handleButtonEvent(const UI::Event &event); \
  void prefix##_handleEncoderEvent(const UI::Event &event); \
  void prefix##_isr();

SHOAL_APP_PROTOTYPES(ASR)
SHOAL_APP_PROTOTYPES(H1200)
SHOAL_APP_PROTOTYPES(Automatonnetz)
SHOAL_APP_PROTOTYPES(QQ)
SHOAL_APP_PROTOTYPES(DQ)
SHOAL_APP_PROTOTYPES(POLYLFO)
SHOAL_APP_PROTOTYPES(LORENZ)
SHOAL_APP_PROTOTYPES(ENVGEN)
SHOAL_APP_PROTOTYPES(SEQ)
SHOAL_APP_PROTOTYPES(BBGEN)
SHOAL_APP_PROTOTYPES(BYTEBEATGEN)
SHOAL_APP_PROTOTYPES(CHORDS)
SHOAL_APP_PROTOTYPES(REFS)

#undef SHOAL_APP_PROTOTYPES

// Button handlers the apps call from their _handleButtonEvent.
void ASR_topButton();
void ASR_lowerButton();
void ASR_rightButton();
void ASR_leftButton();
void ASR_leftButtonLong();
void ASR_downButtonLong();
void SEQ_upButton();
void SEQ_downButton();
void SEQ_rightButton();
void SEQ_leftButton();
void SEQ_leftButtonLong();
void SEQ_upButtonLong();
void SEQ_downButtonLong();
void QQ_topButton();
void QQ_lowerButton();
void QQ_rightButton();
void QQ_leftButton();
void QQ_leftButtonLong();
void QQ_downButtonLong();
void DQ_topButton();
void DQ_lowerButton();
void DQ_rightButton();
void DQ_leftButton();
void DQ_leftButtonLong();
void DQ_downButtonLong();
void CHORDS_topButton();
void CHORDS_lowerButton();
void CHORDS_rightButton();
void CHORDS_leftButton();
void CHORDS_leftButtonLong();
void CHORDS_downButtonLong();
void CHORDS_upButtonLong();

// OC_calibration.ino
void calibration_reset();
void calibration_load();

// o_c_REV.ino
extern const bool DUMMY;
bool shoal_boot();
void shoal_tick();
void shoal_main_pass();

#endif // SHOAL_PROTOTYPES_H_
