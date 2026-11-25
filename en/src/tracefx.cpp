#include "tracefx.h"

#include <cmath>

namespace TraceFX {

uint32_t Hash32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

float Unit(uint32_t value) {
    return (float)(Hash32(value) & 0x00FFFFFFu) / 16777215.0f;
}

float Cycle(double time, float rate, float offset) {
    double value = std::fmod(time * (double)rate + (double)offset, 1.0);
    if (value < 0.0) value += 1.0;
    return (float)value;
}

wchar_t Glyph(uint32_t value) {
    static const wchar_t glyphs[] = L"0123456789ABCDEF/\\|[]{}<>_+-*=:#";
    return glyphs[Hash32(value) % (sizeof(glyphs) / sizeof(glyphs[0]) - 1)];
}

unsigned char Byte(uint32_t seed, int channel, int index) {
    uint32_t value = seed ^ ((uint32_t)channel * 0x1F123BB5u) ^
                     ((uint32_t)index * 0x45D9F3Bu);
    return (unsigned char)(Hash32(value) & 0xFFu);
}

} // namespace TraceFX
