#include "OC_ADC.h"
#include "OC_gpio.h" // SHOAL: DMAChannel.h include deleted (no DMA)
#include <algorithm>

namespace OC {

// SHOAL: calibration_data_, raw_, smoothed_ and scan_count_ (which replaces
// the DMA state) live in shoal::Context.

/*static*/ void ADC::Init(CalibrationData *calibration_data) {

  // SHOAL: ADC driver setup and DMA buffer deleted.
  calibration_data_() = calibration_data; // SHOAL: per-instance state
  std::fill(raw_(), raw_() + ADC_CHANNEL_LAST, 0); // SHOAL: per-instance state
  std::fill(smoothed_(), smoothed_() + ADC_CHANNEL_LAST, 0); // SHOAL: per-instance state
  scan_count_() = 0; // SHOAL: per-instance state
}

// SHOAL: DMA_ISR() and Init_DMA() deleted. Scan_DMA() takes new samples on
// every third call (the DMA delivered at about 5.55 kHz, every third 60 us
// tick), as 16-bit values from the shim's 12-bit CV codes.
/*static*/void FASTRUN ADC::Scan_DMA() {

  if (++scan_count_() >= 3) // SHOAL: per-instance state
  {
    scan_count_() = 0; // SHOAL: per-instance state
    update<ADC_CHANNEL_1>(shoal_adc_code12(ADC_CHANNEL_1) << 4);
    update<ADC_CHANNEL_2>(shoal_adc_code12(ADC_CHANNEL_2) << 4);
    update<ADC_CHANNEL_3>(shoal_adc_code12(ADC_CHANNEL_3) << 4);
    update<ADC_CHANNEL_4>(shoal_adc_code12(ADC_CHANNEL_4) << 4);
  }
}

/*static*/ void ADC::CalibratePitch(int32_t c2, int32_t c4) {
  // This is the method used by the Mutable Instruments calibration and
  // extrapolates from two octaves. I guess an alternative would be to get the
  // lowest (-3v) and highest (+6v) and interpolate between them
  // *vague handwaving*
  if (c2 < c4) {
    int32_t scale = (24 * 128 * 4096L) / (c4 - c2);
    calibration_data_()->pitch_cv_scale = scale; // SHOAL: per-instance state
  }
}

}; // namespace OC
