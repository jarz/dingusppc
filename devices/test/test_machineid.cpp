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

/** Machine ID register regression tests. */

#include "test_devices.h"
#include <devices/common/machineid.h>

#include <fstream>
#include <iostream>
#include <memory>

using namespace std;

void run_machineid_tests() {
    // --- NubusMacID tests (PDM = 0x3010) ---
    cout << "Running NubusMacID tests..." << endl;

    {
        ifstream csv("machineid_tests.csv");
        if (!csv.is_open()) {
            cout << "  ERROR: Could not open machineid_tests.csv" << endl;
            nfailed++;
            return;
        }

        auto dev = make_unique<NubusMacID>(0x3010);
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
            }
        }
    }

    // --- GossamerID tests (id=0x0030) ---
    cout << "Running GossamerID tests..." << endl;

    {
        ifstream csv("gossamerid_tests.csv");
        if (!csv.is_open()) {
            cout << "  ERROR: Could not open gossamerid_tests.csv" << endl;
            nfailed++;
            return;
        }

        auto dev = make_unique<GossamerID>(0x0030);
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
            }
        }
    }
}
