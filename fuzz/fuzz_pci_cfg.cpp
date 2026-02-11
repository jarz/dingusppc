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

// libFuzzer harness for PCI configuration space read/write.
//
// Instantiates a BanditPciDevice and feeds fuzzed register offset,
// value, and access size combinations into pci_cfg_read() and
// pci_cfg_write(). This exercises PCI config register decode, BAR
// sizing, and Bandit-specific register handling.

#include <devices/common/pci/bandit.h>
#include <devices/common/pci/pcihost.h>
#include "cpu/ppc/ppcemu.h"
#include <cstdint>
#include <cstring>

// Stub: absorb exceptions without crashing the fuzzer.
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    power_on = false;
}

static bool g_initialized = false;
static BanditPciDevice* g_dev = nullptr;

static void fuzz_init() {
    if (g_initialized)
        return;

    g_dev = new BanditPciDevice(1, "Bandit-PCI1", 1, 3);
    g_initialized = true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Need: 1 byte reg_offs, 4 bytes value, 1 byte flags
    if (size < 6)
        return 0;

    fuzz_init();

    // Parse fuzz input.
    uint8_t  reg_byte = data[0];
    uint32_t value    = (uint32_t(data[1]) << 24) | (uint32_t(data[2]) << 16) |
                        (uint32_t(data[3]) << 8)  |  uint32_t(data[4]);
    uint8_t  flags    = data[5];

    // PCI config space registers are dword-aligned.
    uint32_t reg_offs = (reg_byte & 0xFC);

    // Pick access size: 1, 2, or 4 bytes.
    static const uint8_t sizes[] = { 1, 2, 4 };
    uint8_t access_size = sizes[flags % 3];

    AccessDetails details;
    details.size   = access_size;
    details.offset = reg_byte & 3;
    details.flags  = 0;

    // Alternate between read and write based on a flag bit.
    if (flags & 0x80) {
        g_dev->pci_cfg_write(reg_offs, value, details);
    } else {
        g_dev->pci_cfg_read(reg_offs, details);
    }

    return 0;
}
