#pragma once
// Stand-in for the Teensy/Arduino core headers. Task 2 extends this as the
// firmware compile demands. Pin functions are declared here and defined where
// the per-instance pin state lives.
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#define FASTRUN

typedef uint8_t byte;

#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

void pinMode(uint8_t pin, uint8_t mode);
int digitalReadFast(uint8_t pin);
void digitalWriteFast(uint8_t pin, uint8_t value);
