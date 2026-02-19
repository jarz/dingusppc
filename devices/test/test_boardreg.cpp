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

/** BoardRegister regression tests. */

#include "test_devices.h"
#include <devices/common/machineid.h>

#include <iostream>
#include <memory>

using namespace std;

static void check_eq16(const char* name, uint16_t actual, uint16_t expected) {
    ntested++;
    if (actual != expected) {
        cout << "  FAIL [" << name << "]: expected 0x" << hex << expected
             << " got 0x" << actual << dec << endl;
        nfailed++;
    }
}

void run_boardreg_tests() {
    cout << "Running BoardRegister tests..." << endl;

    auto reg = make_unique<BoardRegister>("TestBoardReg", (uint16_t)0xABCD);

    // iodev_read returns stored data regardless of address
    check_eq16("breg_read_0", reg->iodev_read(0x00000000), 0xABCD);
    check_eq16("breg_read_4", reg->iodev_read(0x00000004), 0xABCD);
    check_eq16("breg_read_ff", reg->iodev_read(0xFFFFFFFF), 0xABCD);

    // iodev_write is a no-op (read-only to guest)
    reg->iodev_write(0x00000000, 0xFFFF);
    check_eq16("breg_write_noop", reg->iodev_read(0), 0xABCD);

    reg->iodev_write(0x00000000, 0x0000);
    check_eq16("breg_write_noop2", reg->iodev_read(0), 0xABCD);

    // update_bits: data = (data & ~mask) | (val & mask)
    // 0xABCD & ~0x00FF = 0xAB00; 0x00FF & 0x00FF = 0x00FF; result = 0xABFF
    reg->update_bits(0x00FF, 0x00FF);
    check_eq16("breg_update_low", reg->iodev_read(0), 0xABFF);

    // Reset to known value
    reg->update_bits(0xABCD, 0xFFFF);  // force all bits
    check_eq16("breg_reset", reg->iodev_read(0), 0xABCD);

    // Nibble mask: 0xABCD & ~0x0F0F = 0xA0C0; 0x1234 & 0x0F0F = 0x0204; result = 0xA2C4
    reg->update_bits(0x1234, 0x0F0F);
    check_eq16("breg_update_nibbles", reg->iodev_read(0), 0xA2C4);

    // Full mask: replaces entirely
    reg->update_bits(0xFFFF, 0xFFFF);
    check_eq16("breg_update_all", reg->iodev_read(0), 0xFFFF);

    // Clear all bits
    reg->update_bits(0x0000, 0xFFFF);
    check_eq16("breg_clear_all", reg->iodev_read(0), 0x0000);

    // Zero mask: no change
    reg->update_bits(0xFFFF, 0x0000);
    check_eq16("breg_zero_mask", reg->iodev_read(0), 0x0000);

    // Different initial value
    auto reg2 = make_unique<BoardRegister>("TestReg2", (uint16_t)0x0000);
    check_eq16("breg2_read", reg2->iodev_read(0), 0x0000);
    reg2->update_bits(0x1234, 0xFFFF);
    check_eq16("breg2_update", reg2->iodev_read(0), 0x1234);
}
