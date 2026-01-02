#pragma once

// Make Waveshare AVR-style code work on ESP32
#include <stdint.h>

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_byte
static inline uint8_t pgm_read_byte(const void* addr) {
  return *(const uint8_t*)addr;  // RAM/flash both ok on ESP32
}
#endif
