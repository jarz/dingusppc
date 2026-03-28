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

/** I2CProm regression tests. */

#include "test_devices.h"
#include <devices/common/i2c/i2cprom.h>

#include <iostream>
#include <memory>

using namespace std;

static void check_byte_eq(const char* name, uint8_t actual, uint8_t expected) {
    ntested++;
    if (actual != expected) {
        cout << "  FAIL [" << name << "]: expected 0x" << hex << (int)expected
             << " got 0x" << (int)actual << dec << endl;
        nfailed++;
    }
}

static void check_true(const char* name, bool actual) {
    ntested++;
    if (!actual) {
        cout << "  FAIL [" << name << "]: expected true got false" << endl;
        nfailed++;
    }
}

void run_i2cprom_tests() {
    cout << "Running I2CProm tests..." << endl;

    auto dev = make_unique<I2CProm>(0xA0, 256);

    // fill_memory to establish known state (memory is uninitialized by default)
    dev->fill_memory(0, 256, 0x00);

    // Default state after fill: all zeros
    dev->start_transaction();
    uint8_t data = 0xFF;
    check_true("prom_read_default", dev->receive_byte(&data));
    check_byte_eq("prom_default_0", data, 0x00);

    // fill_memory: fill bytes 0-9 with 0xAA
    dev->fill_memory(0, 10, 0xAA);
    dev->start_transaction();
    dev->receive_byte(&data);
    check_byte_eq("prom_filled_0", data, 0xAA);
    dev->receive_byte(&data);
    check_byte_eq("prom_filled_1", data, 0xAA);

    // set_memory: write specific bytes
    const uint8_t test_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    dev->set_memory(4, test_data, 4);

    // Read back via subaddress positioning
    dev->send_subaddress(4);
    dev->receive_byte(&data);
    check_byte_eq("prom_set_4", data, 0xDE);
    dev->receive_byte(&data);
    check_byte_eq("prom_set_5", data, 0xAD);
    dev->receive_byte(&data);
    check_byte_eq("prom_set_6", data, 0xBE);
    dev->receive_byte(&data);
    check_byte_eq("prom_set_7", data, 0xEF);

    // Byte 8 should still be 0xAA (from fill_memory)
    dev->receive_byte(&data);
    check_byte_eq("prom_after_set", data, 0xAA);

    // Byte 10+ should be 0x00 (unfilled)
    dev->send_subaddress(10);
    dev->receive_byte(&data);
    check_byte_eq("prom_unfilled", data, 0x00);

    // Wrap-around: read past rom_size wraps to 0
    dev->send_subaddress(255);
    dev->receive_byte(&data);  // reads byte 255 (0x00)
    check_byte_eq("prom_last_byte", data, 0x00);
    dev->receive_byte(&data);  // wraps to pos 0, reads 0xAA
    check_byte_eq("prom_wrap_to_0", data, 0xAA);

    // send_byte always returns true (just logs)
    check_true("prom_send_byte", dev->send_byte(0x42));

    // send_subaddress always returns true
    check_true("prom_subaddr", dev->send_subaddress(0x80));

    // fill_memory boundary: exactly at rom_size (should succeed)
    dev->fill_memory(0, 256, 0xBB);
    dev->send_subaddress(255);
    dev->receive_byte(&data);
    check_byte_eq("prom_fill_full", data, 0xBB);

    // fill_memory boundary: beyond rom_size (should be no-op)
    dev->fill_memory(250, 10, 0xCC);  // 250+10=260 > 256, no-op
    dev->send_subaddress(250);
    dev->receive_byte(&data);
    check_byte_eq("prom_fill_beyond", data, 0xBB);  // unchanged

    // set_memory boundary: beyond rom_size (should be no-op)
    const uint8_t over[] = {0xDD};
    dev->set_memory(256, over, 1);  // 256+1=257 > 256, no-op
    dev->send_subaddress(0);
    dev->receive_byte(&data);
    check_byte_eq("prom_set_beyond", data, 0xBB);  // unchanged

    // Small prom: size=4
    auto small = make_unique<I2CProm>(0xA2, 4);
    const uint8_t small_data[] = {0x01, 0x02, 0x03, 0x04};
    small->set_memory(0, small_data, 4);

    small->start_transaction();
    for (int i = 0; i < 4; i++) {
        small->receive_byte(&data);
        check_byte_eq(("prom_small_" + to_string(i)).c_str(), data, i + 1);
    }
    // Wrap around after 4 bytes
    small->receive_byte(&data);
    check_byte_eq("prom_small_wrap", data, 0x01);
}
