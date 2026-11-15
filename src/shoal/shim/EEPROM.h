#pragma once
// Stand-in for the Teensy EEPROM library over a 2048-byte image owned by the
// shim's hardware state (../CoreImpl.inc defines shoal_eeprom_data()).
// Indices are taken modulo the length, so no access leaves the image.
#include <stdint.h>
#include <stddef.h>

uint8_t* shoal_eeprom_data();

struct EEPROMClass {
	static constexpr size_t kLength = 2048;
	static size_t wrap(int idx) { return (size_t) idx % kLength; }
	size_t length() const { return kLength; }
	uint8_t read(int idx) const { return shoal_eeprom_data()[wrap(idx)]; }
	void write(int idx, uint8_t val) { shoal_eeprom_data()[wrap(idx)] = val; }
	void update(int idx, uint8_t val) { if (read(idx) != val) write(idx, val); }
};

[[maybe_unused]] static EEPROMClass EEPROM;  // unused in most includers (GCC warns)

// Arduino's EEPROM reference and pointer helpers (used by util/EEPROMStorage.h).
struct EERef {
	explicit EERef(int idx) : index(idx) { }
	operator uint8_t() const { return EEPROMClass().read(index); }
	EERef& operator=(uint8_t val) { EEPROMClass().write(index, val); return *this; }
	EERef& update(uint8_t val) { EEPROMClass().update(index, val); return *this; }
	int index;
};

struct EEPtr {
	EEPtr(int idx) : index(idx) { }
	EERef operator*() const { return EERef(index); }
	EEPtr& operator++() { ++index; return *this; }
	EEPtr operator++(int) { EEPtr old = *this; ++index; return old; }
	int index;
};
