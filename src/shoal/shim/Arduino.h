#pragma once
// Stand-in for the Teensy/Arduino core headers: just what the vendored
// firmware uses. Functions declared here are defined in ../CoreImpl.inc,
// backed by the shim's hardware state.
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "Context.hpp" // per-instance state: SHOAL_STATE, shoal::Ref

// OC_config.h refuses any other CPU speed.
#define F_CPU 120000000
#define F_BUS 60000000

// Teensy memory-placement attributes: meaningless on the host.
#define FASTRUN
#define DMAMEM
#define PROGMEM

typedef uint8_t byte;

// GNU spelling used by the firmware (Teensy builds with gnu++ dialects).
#define typeof __typeof__

#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// Pins. Every pin reads HIGH (released button, idle encoder, low trigger
// input: the inputs are active low) until the shim drives it.
void pinMode(uint8_t pin, uint8_t mode);
int digitalReadFast(uint8_t pin);
void digitalWriteFast(uint8_t pin, uint8_t value);

// Time, derived from the firmware's core tick counter (60 us per tick).
uint32_t millis();
uint32_t micros();
inline void delay(uint32_t) { }
inline void delayMicroseconds(uint32_t) { }

class elapsedMillis {
public:
	// Starts at 0, which is millis() at boot: firmware objects holding one are
	// built with their shoal::Context, before any context is current.
	elapsedMillis() : ms_(0) { }
	elapsedMillis(uint32_t val) : ms_(millis() - val) { }
	operator uint32_t() const { return millis() - ms_; }
	elapsedMillis& operator=(uint32_t val) { ms_ = millis() - val; return *this; }
private:
	uint32_t ms_;
};

// Arduino random(): one generator, seeded by randomSeed() (and Core::seed).
void randomSeed(uint32_t seed);
int32_t random(int32_t howbig);
int32_t random(int32_t howsmall, int32_t howbig);

// Serial output is dropped.
struct ShoalSerial {
	void print(const char*) const { }
	void println(const char*) const { }
};
extern const ShoalSerial Serial;

// Interrupts and exclusive access: firmware code only ever runs on one
// thread at a time here, so these are no-ops / plain loads and stores.
inline void __disable_irq() { }
inline void __enable_irq() { }
inline void __DMB() { }
inline void __CLREX() { }
inline void noInterrupts() { }
inline void interrupts() { }
inline uint32_t __LDREXW(volatile uint32_t* addr) { return *addr; }
inline uint32_t __STREXW(uint32_t value, volatile uint32_t* addr) { *addr = value; return 0; }

// Shoal hardware hooks used by the firmware's peripheral classes.
// 12-bit ADC code currently on CV input `channel` (0..3).
uint16_t shoal_adc_code12(int channel);

// FreqMeasure on TR4: never has a reading (real meters are a later stage).
struct FreqMeasureClass {
	static void begin() { }
	static uint8_t available() { return 0; }
	static uint32_t read() { return 0; }
	static float countToFrequency(uint32_t) { return 0.f; }
	static void end() { }
};
extern const FreqMeasureClass FreqMeasure;
