#ifndef OC_ADC_H_
#define OC_ADC_H_

// SHOAL: no ADC driver; Scan_DMA() reads the shim's CV codes.
#include "OC_config.h"
#include "OC_options.h"

#include <stdint.h>
#include <string.h>

//#define ENABLE_ADC_DEBUG

enum ADC_CHANNEL {
  ADC_CHANNEL_1,
  ADC_CHANNEL_2,
  ADC_CHANNEL_3,
  ADC_CHANNEL_4,
  ADC_CHANNEL_LAST,
};

// SHOAL: DMA_BUF_SIZE and DMA_NUM_CH deleted with the DMA code.

namespace OC {

class ADC {
public:

  static constexpr uint8_t kAdcResolution = 12;
  static constexpr uint32_t kAdcSmoothing = 4;
  static constexpr uint32_t kAdcSmoothBits = 8; // fractional bits for smoothing
  static constexpr uint16_t kDefaultPitchCVScale = SEMITONES << 7;

  // These values should be tweaked so startSingleRead/readSingle run in main ISR update time
  // 16 bit has best-case 13 bits useable, but we only want 12 so we discard 4 anyway
  static constexpr uint8_t kAdcScanResolution = 16;
  // SHOAL: driver settings (averages, sampling/conversion speed) deleted.
  static constexpr uint32_t kAdcValueShift = kAdcSmoothBits;


  struct CalibrationData {
    uint16_t offset[ADC_CHANNEL_LAST];
    uint16_t pitch_cv_scale;
    int16_t pitch_cv_offset;
  };

  static void Init(CalibrationData *calibration_data);
  // SHOAL: Init_DMA() and DMA_ISR() deleted. Scan_DMA() picks up new codes on
  // every third call, as the DMA did (5.55 kHz at the 60 us tick).
  static void Scan_DMA();

  template <ADC_CHANNEL channel>
  static int32_t value() {
    return calibration_data_()->offset[channel] - (smoothed_()[channel] >> kAdcValueShift); // SHOAL: per-instance state
  }

  static int32_t value(ADC_CHANNEL channel) {
    return calibration_data_()->offset[channel] - (smoothed_()[channel] >> kAdcValueShift); // SHOAL: per-instance state
  }

  static uint32_t raw_value(ADC_CHANNEL channel) {
    return raw_()[channel] >> kAdcValueShift; // SHOAL: per-instance state
  }

  static uint32_t smoothed_raw_value(ADC_CHANNEL channel) {
    return smoothed_()[channel] >> kAdcValueShift; // SHOAL: per-instance state
  }

  static int32_t pitch_value(ADC_CHANNEL channel) {
    return (value(channel) * calibration_data_()->pitch_cv_scale) >> 12; // SHOAL: per-instance state
  }

  static int32_t raw_pitch_value(ADC_CHANNEL channel) {
    int32_t value = calibration_data_()->offset[channel] - raw_value(channel); // SHOAL: per-instance state
    return (value * calibration_data_()->pitch_cv_scale) >> 12; // SHOAL: per-instance state
  }

  static void CalibratePitch(int32_t c2, int32_t c4);

private:

  template <ADC_CHANNEL channel>
  static void update(uint32_t value) {
    value = (value  >> (kAdcScanResolution - kAdcResolution)) << kAdcSmoothBits;
    raw_()[channel] = value; // SHOAL: per-instance state
    // division should be shift if kAdcSmoothing is power-of-two
    value = (smoothed_()[channel] * (kAdcSmoothing - 1) + value) / kAdcSmoothing; // SHOAL: per-instance state
    smoothed_()[channel] = value; // SHOAL: per-instance state
  }

  // SHOAL: adc_, ready_ and scan_channel_ deleted; scan_count_ paces Scan_DMA().
  // SHOAL: the static data members live in the per-instance shoal::Context;
  // these accessors return the current instance's (FirmwareContext.inc).
  static shoal::Ref<uint32_t> scan_count_();
  static shoal::Ref<CalibrationData *> calibration_data_();

  static shoal::Ref<uint32_t[ADC_CHANNEL_LAST]> raw_(); // SHOAL: per-instance state
  static shoal::Ref<uint32_t[ADC_CHANNEL_LAST]> smoothed_(); // SHOAL: per-instance state

  // SHOAL: SCA_CHANNEL_ID (ADC mux ids for the DMA) deleted.
};

};

#endif // OC_ADC_H_
