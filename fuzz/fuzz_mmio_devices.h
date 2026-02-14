/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

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

#ifndef FUZZ_MMIO_DEVICES_H
#define FUZZ_MMIO_DEVICES_H

#include <devices/common/mmiodevice.h>
#include <cstdint>

// Shared struct for describing devices under test.
struct DeviceUnderTest {
    MMIODevice* dev;
    uint32_t    rgn_start;
    uint32_t    rgn_size;
};

// Factory functions defined in fuzz_mmio_extras.cpp to avoid
// enum conflicts between hmc.h and aspen.h.
DeviceUnderTest make_aspen();
DeviceUnderTest make_psx();

#endif // FUZZ_MMIO_DEVICES_H
