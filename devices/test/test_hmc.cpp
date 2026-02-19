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

static void test_hmc_install_ram() {
    // Exercise install_ram() with valid configurations
    {
        auto dev = make_unique<HMC>();
        // install_ram(mb_bank_size, bank_a_size, bank_b_size)
        // Valid mb_bank_size: BANK_SIZE_4MB or BANK_SIZE_8MB
        int ret = dev->install_ram(BANK_SIZE_4MB, 0, 0);
        ntested++;
        if (ret != 0) {
            cout << "  FAIL [hmc_install_ram_4mb]: returned " << ret << endl;
            nfailed++;
        }
    }

    {
        auto dev = make_unique<HMC>();
        int ret = dev->install_ram(BANK_SIZE_8MB, BANK_SIZE_8MB, 0);
        ntested++;
        if (ret != 0) {
            cout << "  FAIL [hmc_install_ram_8mb_a]: returned " << ret << endl;
            nfailed++;
        }
    }

    {
        auto dev = make_unique<HMC>();
        int ret = dev->install_ram(BANK_SIZE_8MB, BANK_SIZE_8MB, BANK_SIZE_8MB);
        ntested++;
        if (ret != 0) {
            cout << "  FAIL [hmc_install_ram_8mb_ab]: returned " << ret << endl;
            nfailed++;
        }
    }

    // Test with small bank A (< 8MB) to exercise alias creation path
    {
        auto dev = make_unique<HMC>();
        int ret = dev->install_ram(BANK_SIZE_4MB, BANK_SIZE_2MB, 0);
        ntested++;
        if (ret != 0) {
            cout << "  FAIL [hmc_install_ram_small_a]: returned " << ret << endl;
            nfailed++;
        }
    }

    // Test with small bank B (< 8MB) to exercise bank B alias path
    {
        auto dev = make_unique<HMC>();
        int ret = dev->install_ram(BANK_SIZE_4MB, BANK_SIZE_8MB, BANK_SIZE_2MB);
        ntested++;
        if (ret != 0) {
            cout << "  FAIL [hmc_install_ram_small_b]: returned " << ret << endl;
            nfailed++;
        }
    }

    // Error case: invalid mb_bank_size
    {
        auto dev = make_unique<HMC>();
        int ret = dev->install_ram(BANK_SIZE_2MB, 0, 0);  // invalid
        ntested++;
        if (ret != -1) {
            cout << "  FAIL [hmc_install_ram_invalid_mb]: expected -1 got " << ret << endl;
            nfailed++;
        }
    }

    // Error case: bank A empty but bank B not
    {
        auto dev = make_unique<HMC>();
        int ret = dev->install_ram(BANK_SIZE_4MB, 0, BANK_SIZE_8MB);  // invalid
        ntested++;
        if (ret != -1) {
            cout << "  FAIL [hmc_install_ram_empty_a]: expected -1 got " << ret << endl;
            nfailed++;
        }
    }

    // Exercise remap_ram_regions() by changing bank_config via serial interface
    // after installing RAM with bank B
    {
        auto dev = make_unique<HMC>();
        dev->install_ram(BANK_SIZE_8MB, BANK_SIZE_8MB, BANK_SIZE_8MB);

        // Write 35 bits to set BANK_CFG_2MB (bits 29=1, 30=0)
        dev->write(0, 8, 0, 1);  // reset bit_pos
        for (int i = 0; i < HMC_CTRL_BITS; i++) {
            uint32_t bit_val = (i == 29) ? 1 : 0;  // only bit 29 set
            dev->write(0, 0, bit_val, 1);
        }
        // bit_pos wrapped, remap_ram_regions called with BANK_CFG_2MB
        ntested++;

        // Now change to BANK_CFG_8MB (bits 29=0, 30=1)
        dev->write(0, 8, 0, 1);  // reset bit_pos
        for (int i = 0; i < HMC_CTRL_BITS; i++) {
            uint32_t bit_val = (i == 30) ? 1 : 0;  // only bit 30 set
            dev->write(0, 0, bit_val, 1);
        }
        ntested++;

        // Now change to BANK_CFG_32MB (bits 29=1, 30=1)
        dev->write(0, 8, 0, 1);  // reset bit_pos
        for (int i = 0; i < HMC_CTRL_BITS; i++) {
            uint32_t bit_val = (i == 29 || i == 30) ? 1 : 0;
            dev->write(0, 0, bit_val, 1);
        }
        ntested++;
    }

    // Exercise the >120MB bank A path (partial mirror setup)
    {
        auto dev = make_unique<HMC>();
        int ret = dev->install_ram(BANK_SIZE_8MB, 0x08000000, 0);  // 128MB bank A
        ntested++;
        if (ret != 0) {
            cout << "  FAIL [hmc_install_ram_large_a]: returned " << ret << endl;
            nfailed++;
        }
    }
}

void run_hmc_tests() {
    cout << "Running HMC tests..." << endl;

    // Direct C++ tests for install_ram
    test_hmc_install_ram();

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
