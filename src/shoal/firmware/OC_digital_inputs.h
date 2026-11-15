#ifndef OC_DIGITAL_INPUTS_H_
#define OC_DIGITAL_INPUTS_H_

#include <stdint.h>
#include "OC_config.h"
#include "OC_core.h"
#include "OC_gpio.h"

namespace OC {

enum DigitalInput {
  DIGITAL_INPUT_1,
  DIGITAL_INPUT_2,
  DIGITAL_INPUT_3,
  DIGITAL_INPUT_4,
  DIGITAL_INPUT_LAST
};

// SHOAL: an index outside 0..31 (-1: "no input", from a trigger source of 0
// minus one) gives an empty mask; upstream shifted by it, which the Teensy's
// shift turns into 0.
#define DIGITAL_INPUT_MASK(x) (static_cast<unsigned>(x) < 32u ? (0x1 << static_cast<unsigned>(x)) : 0)

static constexpr uint32_t DIGITAL_INPUT_1_MASK = DIGITAL_INPUT_MASK(DIGITAL_INPUT_1);
static constexpr uint32_t DIGITAL_INPUT_2_MASK = DIGITAL_INPUT_MASK(DIGITAL_INPUT_2);
static constexpr uint32_t DIGITAL_INPUT_3_MASK = DIGITAL_INPUT_MASK(DIGITAL_INPUT_3);
static constexpr uint32_t DIGITAL_INPUT_4_MASK = DIGITAL_INPUT_MASK(DIGITAL_INPUT_4);

template <DigitalInput> struct InputPinDesc { };
template <> struct InputPinDesc<DIGITAL_INPUT_1> { static constexpr int PIN = TR1; };
template <> struct InputPinDesc<DIGITAL_INPUT_2> { static constexpr int PIN = TR2; };
template <> struct InputPinDesc<DIGITAL_INPUT_3> { static constexpr int PIN = TR3; };
template <> struct InputPinDesc<DIGITAL_INPUT_4> { static constexpr int PIN = TR4; };

class DigitalInputs {
public:

  static void Init();

  static void reInit();

  static void Scan();

  // @return mask of all pins cloked since last call, reset state
  static inline uint32_t clocked() {
    return clocked_mask_(); // SHOAL: per-instance state
  }

  // @return mask if pin clocked since last call and reset state
  template <DigitalInput input> static inline uint32_t clocked() {
    return clocked_mask_() & (0x1 << input); // SHOAL: per-instance state
  }

  // @return mask if pin clocked since last call, reset state
  static inline uint32_t clocked(DigitalInput input) {
    return clocked_mask_() & (0x1 << input); // SHOAL: per-instance state
  }

  template <DigitalInput input> static inline bool read_immediate() {
    return !digitalReadFast(InputPinDesc<input>::PIN);
  }

  static inline bool read_immediate(DigitalInput input) {
    return !digitalReadFast(InputPinMap(input));
  }

  template <DigitalInput input> static inline void clock() {
    clocked_()[input] = 1; // SHOAL: per-instance state
  }

private:

  inline static int InputPinMap(DigitalInput input) {
    switch (input) {
      case DIGITAL_INPUT_1: return InputPinDesc<DIGITAL_INPUT_1>::PIN;
      case DIGITAL_INPUT_2: return InputPinDesc<DIGITAL_INPUT_2>::PIN;
      case DIGITAL_INPUT_3: return InputPinDesc<DIGITAL_INPUT_3>::PIN;
      case DIGITAL_INPUT_4: return InputPinDesc<DIGITAL_INPUT_4>::PIN;
      default: break;
    }
    return 0;
  }

  // SHOAL: the static data members live in the per-instance shoal::Context;
  // these accessors return the current instance's (FirmwareContext.inc).
  static shoal::Ref<uint32_t> clocked_mask_();
  static shoal::Ref<volatile uint32_t[DIGITAL_INPUT_LAST]> clocked_();

  template <DigitalInput input>
  static uint32_t ScanInput() {
    if (clocked_()[input]) { // SHOAL: per-instance state
      clocked_()[input] = 0; // SHOAL: per-instance state
      return DIGITAL_INPUT_MASK(input);
    } else {
      return 0;
    }
  }
};

// Helper class for visualizing digital inputs with decay
// Uses 4 bits for decay
class DigitalInputDisplay {
public:
  static constexpr uint32_t kDisplayTime = OC_CORE_ISR_FREQ / 8;
  static constexpr uint32_t kPhaseInc = (0xf << 28) / kDisplayTime;

  void Init() {
    phase_ = 0;
  }

  void Update(uint32_t ticks, bool clocked) {
    uint32_t phase_inc = ticks * kPhaseInc;
    if (clocked) {
      phase_ = 0xffffffff;
    } else {
      uint32_t phase = phase_;
      if (phase) {
        if (phase < phase_inc)
          phase_ = 0;
        else
          phase_ = phase - phase_inc;
      }
    }
  }

  uint8_t getState() const {
    return phase_ >> 28;
  }

private:
  uint32_t phase_;
};

};

#endif // OC_DIGITAL_INPUTS_H_
