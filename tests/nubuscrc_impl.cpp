// Extracted from devices/common/nubus/nubusutils.cpp for standalone testing.
// This file contains only the calculate_rom_crc function.

#include <cinttypes>

uint32_t calculate_rom_crc(uint8_t *data_ptr, int length)
{
    uint32_t sum = 0;

    for (int i = 0; i < length; i++) {
        // rotate sum left by one bit
        if (sum & 0x80000000UL)
            sum = (sum << 1) | 1;
        else
            sum = (sum << 1) | 0;
        sum += data_ptr[i];
    }

    return sum;
}
