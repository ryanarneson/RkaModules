#ifndef OC_AUTOTUNE_H_
#define OC_AUTOTUNE_H_

#include "OC_autotune_presets.h"
#include "OC_DAC.h"

namespace OC {

typedef OC::Autotune_data Autotune_data;

class AUTOTUNE {
public:
  static const int NUM_DAC_CHANNELS;
  static void Init();
  static const Autotune_data &GetAutotune_data(int channel);
};

SHOAL_STATE(Autotune_data[DAC_CHANNEL_LAST], g_auto_calibration_data) // SHOAL: per-instance state (shoal::Context)
#define g_auto_calibration_data g_auto_calibration_data_ref()
};

#endif // OC_AUTOTUNE_H_