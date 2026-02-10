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

/** Device regression tests for memory controllers.

    Tests register read/write behavior of HammerheadCtrl, HMC, and AspenCtrl
    using CSV-driven test vectors.
*/

#include <devices/memctrl/hammerhead.h>
#include <devices/memctrl/hmc.h>
#include <devices/memctrl/aspen.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static int ntested = 0;
static int nfailed = 0;

static vector<string> tokenize_line(const string& line) {
    vector<string> tokens;
    istringstream stream(line);
    string token;

    while (getline(stream, token, ',')) {
        // trim leading/trailing whitespace
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start != string::npos && end != string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
        else
            tokens.push_back("");
    }

    return tokens;
}

static uint32_t parse_hex32(const string& s) {
    return (uint32_t)stoul(s, nullptr, 16);
}

static uint64_t parse_hex64(const string& s) {
    return (uint64_t)stoull(s, nullptr, 16);
}

// =============================================================================
// Hammerhead test runner
// =============================================================================
static void run_hammerhead_tests() {
    cout << "Running Hammerhead tests..." << endl;

    ifstream csv("hammerhead_tests.csv");
    if (!csv.is_open()) {
        cout << "  ERROR: Could not open hammerhead_tests.csv" << endl;
        nfailed++;
        return;
    }

    auto dev = make_unique<HammerheadCtrl>();
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
            dev = make_unique<HammerheadCtrl>();
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

// =============================================================================
// HMC test runner
// =============================================================================
static void run_hmc_tests() {
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
        if (tokens.size() < 2) {
            cout << "  Line " << lineno << ": too few fields, skipping" << endl;
            continue;
        }

        string op = tokens[0];

        if (op == "reset_pos") {
            dev->write(0, 8, 0, 1);
            continue;
        }

        if (op == "check_ctrl_reg") {
            uint64_t expected = parse_hex64(tokens[4]);
            uint64_t actual   = dev->get_control_reg();
            ntested++;
            if (actual != expected) {
                cout << "  FAIL line " << lineno
                     << " [check_ctrl_reg]: expected 0x" << hex << expected
                     << " got 0x" << actual << dec << endl;
                nfailed++;
            }
            continue;
        }

        if (op == "write_bit") {
            uint32_t offset = parse_hex32(tokens[1]);
            uint32_t value  = parse_hex32(tokens[2]);
            dev->write(0, offset, value, 1);
            continue;
        }

        if (op == "read_bit") {
            uint32_t offset  = parse_hex32(tokens[1]);
            uint32_t expected = parse_hex32(tokens[4]);
            uint32_t actual   = dev->read(0, offset, 1);
            ntested++;
            if (actual != expected) {
                string desc = (tokens.size() > 4) ? tokens[4] : "";
                cout << "  FAIL line " << lineno
                     << " [read_bit offset=0x" << hex << offset
                     << "]: expected 0x" << expected
                     << " got 0x" << actual << dec << endl;
                nfailed++;
            }
            continue;
        }

        cout << "  Line " << lineno << ": unknown op '" << op << "', skipping" << endl;
    }
}

// =============================================================================
// Aspen test runner
// =============================================================================
static void run_aspen_tests() {
    cout << "Running Aspen tests..." << endl;

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

// =============================================================================
// Main
// =============================================================================
int main() {
    cout << "Running DingusPPC device regression tests..." << endl << endl;

    ntested = 0;
    nfailed = 0;

    run_hammerhead_tests();
    run_hmc_tests();
    run_aspen_tests();

    cout << endl << "... completed." << endl;
    cout << "--> Tested: " << dec << ntested << endl;
    cout << "--> Failed: " << dec << nfailed << endl << endl;

    return (nfailed > 0) ? 1 : 0;
}
