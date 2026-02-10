/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

(You may also contact divingkxt or powermax2286 on Discord)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef NUBUS_UTILS_H
#define NUBUS_UTILS_H

#include <cinttypes>
#include <string>

inline uint32_t calculate_rom_crc(uint8_t *data_ptr, int length)
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

int load_declaration_rom(const std::string rom_path, int slot_num);

#endif // NUBUS_UTILS_H
