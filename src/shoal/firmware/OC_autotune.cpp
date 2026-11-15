#include "OC_autotune.h"
#include "OC_autotune_presets.h"

namespace OC {

    // SHOAL: g_auto_calibration_data lives in shoal::Context (g_ prefix: the name clashes with a GlobalSettings member).

    /*static*/
    const int AUTOTUNE::NUM_DAC_CHANNELS = DAC_CHANNEL_LAST;

    /*static*/
    void AUTOTUNE::Init() {
      for (size_t i = 0; i < DAC_CHANNEL_LAST; i++)
        memcpy(&g_auto_calibration_data[i], &OC::autotune_data_default[0], sizeof(Autotune_data)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
    }
    /*static*/
    const Autotune_data &AUTOTUNE::GetAutotune_data(int channel) {
        return g_auto_calibration_data[channel]; // SHOAL: g_ prefix (name clashes with GlobalSettings members)
    }
} // namespace OC