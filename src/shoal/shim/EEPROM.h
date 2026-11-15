#pragma once
// Stand-in for the Teensy EEPROM library over a 2048-byte image owned by the
// current core instance. shoal_eeprom_data() is defined with the instance
// context (Task 3); nothing here owns storage.
#include <stdint.h>
#include <stddef.h>

uint8_t* shoal_eeprom_data();

struct EEPROMClass {
	static constexpr size_t kLength = 2048;
	size_t length() const { return kLength; }
	uint8_t read(int idx) const { return shoal_eeprom_data()[idx]; }
	void write(int idx, uint8_t val) { shoal_eeprom_data()[idx] = val; }
	void update(int idx, uint8_t val) { if (read(idx) != val) write(idx, val); }
};
