#include <Arduino.h>
#include <algorithm>
#include "OC_digital_inputs.h"
#include "OC_gpio.h"
#include "OC_options.h"

// SHOAL: clocked_mask_ and clocked_ live in shoal::Context.

// SHOAL: the TR1..TR4 pin-change ISRs are deleted. The shim calls
// clock<input>() for each rising trigger edge before the tick's Scan(), and
// drives the TR pins so read_immediate() sees the gate level.

/*static*/
void OC::DigitalInputs::Init() {

  // SHOAL: pin setup and attachInterrupt() deleted.

  clocked_mask_() = 0; // SHOAL: per-instance state
  std::fill(clocked_(), clocked_() + DIGITAL_INPUT_LAST, 0); // SHOAL: per-instance state

  // Assume the priority of pin change interrupts is lower or equal to the
  // thread where ::Scan function is called. Otherwise a safer mechanism is
  // required to avoid conflicts (LDREX/STREX or store ARM_DWT_CYCCNT in the
  // array to check for changes.
  //
  // A really nice approach would be to use the FTM timer mechanism and avoid
  // the ISR altogether, but this only works for one of the pins. Using more
  // explicit interrupt grouping might also improve the handling a bit (if
  // implemented on the DX)
  //
  // It's still not guaranteed that 4 simultaneous triggers will be handled
  // exactly simultaneously though, but that's micro-timing dependent even if
  // the pins have higher prio.

  //  NVIC_SET_PRIORITY(IRQ_PORTB, 0); // TR1 = 0 = PTB16
  // Defaults is 0, or set OC_GPIO_ISR_PRIO for all ports
}

void OC::DigitalInputs::reInit() {
  // SHOAL: re-attaching the TR4 interrupt (after FreqMeasure) deleted.
}

/*static*/
void OC::DigitalInputs::Scan() {
  clocked_mask_() = // SHOAL: per-instance state
    ScanInput<DIGITAL_INPUT_1>() |
    ScanInput<DIGITAL_INPUT_2>() |
    ScanInput<DIGITAL_INPUT_3>() |
    ScanInput<DIGITAL_INPUT_4>();
}
