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

/** HMC (Highspeed Memory Controller) regression tests. */

#include "test_devices.h"
#include <devices/memctrl/hmc.h>

#include <fstream>
#include <iostream>
#include <memory>

using namespace std;

void run_hmc_tests() {
    cout << "Running HMC tests..." << endl;

    ifstream csv("hmc_tests.csv");
    if (!csv.is_open()) {
        cout << "  ERROR: Could not open hmc_tests.csv" << endl;
        nfailed++;
        return;
    }

    auto dev = make_unique<HMC>();
    string line;
    int lineno = 0;

    while (getline(csv, line)) {
        lineno++;
        if (line.empty() || line[0] == '#')
            continue;

        auto tokens = tokenize_line(line);
        if (tokens.size() < 3) {
            cout << "  Line " << lineno << ": too few fields, skipping" << endl;
            continue;
        }

        string test_name = tokens[0];
        string op        = tokens[1];

        if (op == "reset_pos") {
            dev->write(0, 8, 0, 1);
            continue;
        }

        if (op == "check_ctrl_reg") {
            uint64_t expected = parse_hex64(tokens[4]);
            uint64_t actual   = dev->get_control_reg();
            ntested++;
            if (actual != expected) {
                cout << "  FAIL line " << lineno << " [" << test_name
                     << "]: check_ctrl_reg expected 0x" << hex << expected
                     << " got 0x" << actual << dec << endl;
                nfailed++;
            }
            continue;
        }

        if (op == "write_bit") {
            uint32_t offset = parse_hex32(tokens[2]);
            uint32_t value  = parse_hex32(tokens[3]);
            dev->write(0, offset, value, 1);
            continue;
        }

        if (op == "read_bit") {
            uint32_t offset  = parse_hex32(tokens[2]);
            uint32_t expected = parse_hex32(tokens[4]);
            uint32_t actual   = dev->read(0, offset, 1);
            ntested++;
            if (actual != expected) {
                cout << "  FAIL line " << lineno << " [" << test_name
                     << "]: read_bit(0x" << hex << offset
                     << ") expected 0x" << expected
                     << " got 0x" << actual << dec << endl;
                nfailed++;
            }
            continue;
        }

        cout << "  Line " << lineno << ": unknown op '" << op << "', skipping" << endl;
    }
}
