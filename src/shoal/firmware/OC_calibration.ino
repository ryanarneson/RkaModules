/*
*
* calibration menu:
*
* enter by pressing left encoder button during start up; use encoder switches to navigate.
*
*/

#include "OC_calibration.h"

using OC::DAC;

#if defined(BUCHLA_cOC) || defined(VOR)
static constexpr uint16_t DAC_OFFSET = 0;  // DAC offset, initial approx., ish (Easel card)
#else
static constexpr uint16_t DAC_OFFSET = 4890; // DAC offset, initial approx., ish --> -3.5V to 6V
#endif

#ifdef BUCHLA_4U
  static constexpr uint16_t _ADC_OFFSET = (uint16_t)((float)pow(2,OC::ADC::kAdcResolution)*1.0f);       // ADC offset @3.3V
#else
  // SHOAL: 1 << n for pow(2, n), which is not constexpr outside GCC (same value, 2730).
  static constexpr uint16_t _ADC_OFFSET = (uint16_t)((float)(1 << OC::ADC::kAdcResolution)*0.6666667f); // ADC offset @2.2V
#endif

namespace OC {
// SHOAL: calibration_storage deleted; Shoal never stores calibration.
CalibrationData calibration_data;
};

// SHOAL: kCalibrationAdcSmoothing and calibration_data_loaded (calibration UI) deleted.

const OC::CalibrationData kCalibrationDefaults = {
  // DAC
  { {
    #ifdef BUCHLA_cOC 
    {197, 6634, 13083, 19517, 25966, 32417, 38850, 45301, 51733, 58180, 64400},
    {197, 6634, 13083, 19517, 25966, 32417, 38850, 45301, 51733, 58180, 64400},
    {197, 6634, 13083, 19517, 25966, 32417, 38850, 45301, 51733, 58180, 64400},
    {197, 6634, 13083, 19517, 25966, 32417, 38850, 45301, 51733, 58180, 64400}
    #elif defined(VOR)
    {880, 7190, 13510, 19830, 26300, 32460, 38770, 45090, 51410, 57720, 64040},
    {880, 7190, 13510, 19830, 26300, 32460, 38770, 45090, 51410, 57720, 64040},
    {880, 7190, 13510, 19830, 26300, 32460, 38770, 45090, 51410, 57720, 64040},
    {880, 7190, 13510, 19830, 26300, 32460, 38770, 45090, 51410, 57720, 64040}
    #else
    {0, 6553, 13107, 19661, 26214, 32768, 39321, 45875, 52428, 58981, 65535},
    {0, 6553, 13107, 19661, 26214, 32768, 39321, 45875, 52428, 58981, 65535},
    {0, 6553, 13107, 19661, 26214, 32768, 39321, 45875, 52428, 58981, 65535},
    {0, 6553, 13107, 19661, 26214, 32768, 39321, 45875, 52428, 58981, 65535} 
    #endif
    },
  },
  // ADC
  { { _ADC_OFFSET, _ADC_OFFSET, _ADC_OFFSET, _ADC_OFFSET },
    0,  // pitch_cv_scale
    0   // pitch_cv_offset : unused
  },
  // display_offset
  SH1106_128x64_Driver::kDefaultOffset,
  OC_CALIBRATION_DEFAULT_FLAGS,
  SCREENSAVER_TIMEOUT_S, 
  { 0, 0, 0 }, // reserved0
  #ifdef VOR
  DAC::VBiasBipolar | (DAC::VBiasAsymmetric << 16) // default v_bias values
  #else
  0 // reserved1
  #endif
};

void calibration_reset() {
  memcpy(&OC::calibration_data, &kCalibrationDefaults, sizeof(OC::calibration_data));
  for (int i = 0; i < OCTAVES; ++i) {
    OC::calibration_data.dac.calibrated_octaves[0][i] += DAC_OFFSET;
    OC::calibration_data.dac.calibrated_octaves[1][i] += DAC_OFFSET;
    OC::calibration_data.dac.calibrated_octaves[2][i] += DAC_OFFSET;
    OC::calibration_data.dac.calibrated_octaves[3][i] += DAC_OFFSET;
  }
}

// SHOAL: the shim's hardware is ideal, so the defaults are the calibration:
// nothing is loaded from EEPROM, and the pitch CV scale gets its default.
void calibration_load() {
  calibration_reset();
  OC::calibration_data.adc.pitch_cv_scale = OC::ADC::kDefaultPitchCVScale;
}

// SHOAL: calibration_save() and the calibration UI (OC::Ui::Calibrate and its
// steps, drawing and ADC averaging) deleted.
