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

// libFuzzer harness for MMIO device register read/write.
//
// Instantiates several concrete MMIO devices and feeds fuzzed
// offset / value / size combinations into their read() and write()
// methods, exercising register decode and state-machine logic.

#include <devices/memctrl/hmc.h>
#include <devices/memctrl/hammerhead.h>
#include <devices/common/mmiodevice.h>
#include "cpu/ppc/ppcemu.h"
#include <cstdint>
#include <cstring>
#include <memory>

// Stub: absorb exceptions without crashing the fuzzer.
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    power_on = false;
}

// Minimal struct describing one device under test.
struct DeviceUnderTest {
    MMIODevice* dev;
    uint32_t    rgn_start;
    uint32_t    rgn_size;
};

static bool g_initialized = false;
static DeviceUnderTest g_devices[2];
static int g_num_devices = 0;

static void fuzz_init() {
    if (g_initialized)
        return;

    // HMC: MMIO region 0x50F40000, size 0x10000
    static HMC hmc;
    g_devices[g_num_devices++] = { &hmc, 0x50F40000, 0x10000 };

    // HammerheadCtrl: MMIO region 0xF8000000, size 0x500
    static HammerheadCtrl hammerhead;
    g_devices[g_num_devices++] = { &hammerhead, 0xF8000000, 0x500 };

    g_initialized = true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Need: 1 byte device selector, 2 bytes offset, 4 bytes value, 1 byte flags
    if (size < 8)
        return 0;

    fuzz_init();

    // Parse fuzz input.
    uint8_t  dev_idx  = data[0] % g_num_devices;
    uint16_t raw_off  = (uint16_t(data[1]) << 8) | data[2];
    uint32_t value    = (uint32_t(data[3]) << 24) | (uint32_t(data[4]) << 16) |
                        (uint32_t(data[5]) << 8)  |  uint32_t(data[6]);
    uint8_t  flags    = data[7];

    DeviceUnderTest& dut = g_devices[dev_idx];

    // Constrain offset to device region.
    uint32_t offset = raw_off % dut.rgn_size;

    // Pick access size: 1, 2, or 4 bytes.
    static const int sizes[] = { 1, 2, 4 };
    int access_size = sizes[flags % 3];

    // Alternate between read and write based on a flag bit.
    if (flags & 0x80) {
        dut.dev->write(dut.rgn_start, offset, value, access_size);
    } else {
        dut.dev->read(dut.rgn_start, offset, access_size);
    }

    return 0;
}
