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

/** Aspen memory controller regression tests. */

#include "test_devices.h"
#include <devices/memctrl/aspen.h>
#include <devices/memctrl/memctrlbase.h>

#include <fstream>
#include <iostream>
#include <memory>

using namespace std;

static void test_aspen_insert_ram_dimm() {
    auto dev = make_unique<AspenCtrl>();

    // insert_ram_dimm takes (bank_num, capacity_in_MB) where capacity
    // is shifted left by 20 internally. Valid bank_num: 0-3.
    // Valid resulting sizes: DRAM_CAP_1MB, _4MB, _8MB, _16MB
    dev->insert_ram_dimm(0, 1);   // 1MB
    dev->insert_ram_dimm(1, 4);   // 4MB
    dev->insert_ram_dimm(2, 8);   // 8MB
    dev->insert_ram_dimm(3, 16);  // 16MB
    ntested++;

    // Invalid bank_num (out of range)
    dev->insert_ram_dimm(-1, 4);  // invalid - should be silently ignored
    dev->insert_ram_dimm(4, 4);   // invalid - should be silently ignored
    ntested++;

    // Unsupported capacity (not a recognized size)
    dev->insert_ram_dimm(0, 3);   // 3MB → default: break (no-op)
    ntested++;

    // device_postinit() calls map_phys_ram()
    int ret = dev->device_postinit();
    ntested++;
    if (ret != 0) {
        cout << "  FAIL [aspen_postinit]: returned " << ret << endl;
        nfailed++;
    }
}

void run_aspen_tests() {
    cout << "Running Aspen tests..." << endl;

    // Direct C++ tests for insert_ram_dimm / map_phys_ram
    test_aspen_insert_ram_dimm();

    ifstream csv("aspen_tests.csv");
    if (!csv.is_open()) {
        cout << "  ERROR: Could not open aspen_tests.csv" << endl;
        nfailed++;
        return;
    }

    auto dev = make_unique<AspenCtrl>();
    string line;
    int lineno = 0;

    while (getline(csv, line)) {
        lineno++;
        if (line.empty() || line[0] == '#')
            continue;

        auto tokens = tokenize_line(line);
        if (tokens.size() < 4) {
            cout << "  Line " << lineno << ": too few fields, skipping" << endl;
            continue;
        }

        string test_name = tokens[0];
        string op        = tokens[1];
        uint32_t offset  = parse_hex32(tokens[2]);
        int size         = stoi(tokens[3]);

        if (op == "reset") {
            dev = make_unique<AspenCtrl>();
            continue;
        }

        if (op == "read") {
            uint32_t expected = parse_hex32(tokens[5]);
            uint32_t actual   = dev->read(0, offset, size);
            ntested++;
            if (actual != expected) {
                cout << "  FAIL line " << lineno << " [" << test_name
                     << "]: read(0x" << hex << offset << ", " << dec << size
                     << ") expected 0x" << hex << expected
                     << " got 0x" << actual << dec << endl;
                nfailed++;
            }
        } else if (op == "write") {
            uint32_t wval = parse_hex32(tokens[4]);
            dev->write(0, offset, wval, size);
        } else if (op == "write_read") {
            uint32_t wval    = parse_hex32(tokens[4]);
            uint32_t expected = parse_hex32(tokens[5]);
            dev->write(0, offset, wval, size);
            uint32_t actual = dev->read(0, offset, size);
            ntested++;
            if (actual != expected) {
                cout << "  FAIL line " << lineno << " [" << test_name
                     << "]: write_read(0x" << hex << offset << ", " << dec << size
                     << ", 0x" << hex << wval
                     << ") expected 0x" << expected
                     << " got 0x" << actual << dec << endl;
                nfailed++;
            }
        }
    }
}
