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

/** SpdSdram168 EEPROM regression tests. */

#include "test_devices.h"
#include <devices/memctrl/spdram.h>

#include <iostream>
#include <memory>
#include <stdexcept>

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

static uint8_t read_eeprom_byte(SpdSdram168& dev, uint8_t addr) {
    dev.send_subaddress(addr);
    uint8_t data = 0;
    dev.receive_byte(&data);
    return data;
}

void run_spdsdram_tests() {
    cout << "Running SpdSdram168 tests..." << endl;

    // Default EEPROM values before set_capacity
    auto dev = make_unique<SpdSdram168>(0x50);

    // Byte 0: number of bytes present = 128
    check_byte_eq("spd_default_size", read_eeprom_byte(*dev, 0), 128);

    // Byte 1: log2(EEPROM size) = 8
    check_byte_eq("spd_default_log2", read_eeprom_byte(*dev, 1), 8);

    // Byte 2: memory type = SDRAM (4)
    check_byte_eq("spd_default_type", read_eeprom_byte(*dev, 2), 4);

    // Bytes 3-5: 0 (not configured yet)
    check_byte_eq("spd_default_rows", read_eeprom_byte(*dev, 3), 0);
    check_byte_eq("spd_default_cols", read_eeprom_byte(*dev, 4), 0);
    check_byte_eq("spd_default_banks", read_eeprom_byte(*dev, 5), 0);

    // Test all valid capacities
    struct CapTest {
        int megs;
        uint8_t rows, cols, banks;
    };
    CapTest caps[] = {
        {8,   12, 6,  1},
        {16,  12, 7,  1},
        {32,  12, 8,  1},
        {64,  12, 9,  1},
        {128, 12, 10, 1},
        {256, 12, 10, 2},
        {512, 12, 11, 2},
    };

    for (const auto& c : caps) {
        dev->set_capacity(c.megs);
        string prefix = "spd_" + to_string(c.megs) + "mb_";
        check_byte_eq((prefix + "rows").c_str(), read_eeprom_byte(*dev, 3), c.rows);
        check_byte_eq((prefix + "cols").c_str(), read_eeprom_byte(*dev, 4), c.cols);
        check_byte_eq((prefix + "banks").c_str(), read_eeprom_byte(*dev, 5), c.banks);
    }

    // Invalid capacity throws exception
    {
        bool threw = false;
        try {
            dev->set_capacity(7);  // unsupported
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        ntested++;
        if (!threw) {
            cout << "  FAIL [spd_invalid_capacity]: expected exception" << endl;
            nfailed++;
        }
    }

    // I2C protocol tests
    dev->start_transaction();  // resets pos to 0
    uint8_t data = 0;
    dev->receive_byte(&data);
    check_byte_eq("spd_start_pos0", data, 128);  // byte 0

    // send_byte returns true (just logs)
    check_true("spd_send_byte", dev->send_byte(0x42));

    // send_subaddress positions correctly
    check_true("spd_subaddr", dev->send_subaddress(2));
    dev->receive_byte(&data);
    check_byte_eq("spd_subaddr_read", data, 4);  // byte 2 = SDRAM type

    // Sequential reads advance position
    dev->send_subaddress(0);
    for (int i = 0; i < 6; i++) {
        dev->receive_byte(&data);
    }
    // After reading 6 bytes, pos is at 6 (byte 6 onward is 0)
    dev->receive_byte(&data);
    check_byte_eq("spd_sequential_6", data, 0);

    // Wrap-around: reading past eeprom_data[0] (128) wraps to 0
    dev->send_subaddress(127);
    dev->receive_byte(&data);  // reads byte 127
    // Next read should wrap to byte 0
    dev->receive_byte(&data);
    check_byte_eq("spd_wrap", data, 128);  // byte 0 = 128

    // Different I2C address
    auto dev2 = make_unique<SpdSdram168>(0x51);
    dev2->set_capacity(32);
    check_byte_eq("spd_addr51_cols", read_eeprom_byte(*dev2, 4), 8);
}
