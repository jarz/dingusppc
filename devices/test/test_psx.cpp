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

/** PSX memory controller regression tests. */

#include "test_devices.h"
#include <devices/memctrl/psx.h>
#include <devices/memctrl/memctrlbase.h>

#include <fstream>
#include <iostream>
#include <memory>

using namespace std;

static void test_psx_insert_ram_dimm() {
    auto dev = make_unique<PsxCtrl>(1, "PSX-PCI1");

    // Valid capacities: 0, 4MB, 8MB, 16MB, 32MB, 64MB (split)
    dev->insert_ram_dimm(0, 0);                // zero = no-op
    dev->insert_ram_dimm(0, DRAM_CAP_4MB);     // 4MB
    dev->insert_ram_dimm(1, DRAM_CAP_8MB);     // 8MB
    dev->insert_ram_dimm(2, DRAM_CAP_16MB);    // 16MB
    dev->insert_ram_dimm(3, DRAM_CAP_32MB);    // 32MB
    ntested++;

    // 64MB gets split across slot+0 and slot+1
    auto dev2 = make_unique<PsxCtrl>(1, "PSX-PCI1");
    dev2->insert_ram_dimm(0, DRAM_CAP_64MB);
    ntested++;

    // map_phys_ram exercises page mapping logic
    dev2->map_phys_ram();
    ntested++;
}

static void test_psx_page_mappings_write() {
    // Exercise the Page_Mappings write path via the device's write() method
    auto dev = make_unique<PsxCtrl>(1, "PSX-PCI1");
    dev->insert_ram_dimm(0, DRAM_CAP_32MB);

    // Writing to Page_Mappings_1 (offset 0x40, reg = 0x40>>3 = 8)
    dev->write(0, 0x40, 0x00000000, 4);
    ntested++;

    // Writing to Page_Mappings_2 (offset 0x48, reg = 9)
    dev->write(0, 0x48, 0x11111111, 4);
    ntested++;
}

void run_psx_tests() {
    cout << "Running PSX tests..." << endl;

    // Direct C++ tests for insert_ram_dimm / map_phys_ram / Page_Mappings
    test_psx_insert_ram_dimm();
    test_psx_page_mappings_write();

    ifstream csv("psx_tests.csv");
    if (!csv.is_open()) {
        cout << "  ERROR: Could not open psx_tests.csv" << endl;
        nfailed++;
        return;
    }

    auto dev = make_unique<PsxCtrl>(1, "PSX-PCI1");
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
            dev = make_unique<PsxCtrl>(1, "PSX-PCI1");
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
