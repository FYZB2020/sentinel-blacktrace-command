#pragma once

#include <cstdint>

// Deterministic, allocation-free primitives for the BLACKTRACE visual language.
// No timers, threads, assets or simulation ownership live here.
namespace TraceFX {

uint32_t Hash32(uint32_t value);
float Unit(uint32_t value);
float Cycle(double time, float rate, float offset);
wchar_t Glyph(uint32_t value);
unsigned char Byte(uint32_t seed, int channel, int index);

} // namespace TraceFX
