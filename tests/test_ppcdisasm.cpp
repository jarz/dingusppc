/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for cpu/ppc/ppcdisasm.h — SIGNEXT macro and disassemble_single() */

#include <cpu/ppc/ppcdisasm.h>
#include <cinttypes>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Stub for get_reg() referenced by ppcdisasm.cpp but defined in ppcexec.cpp
uint64_t get_reg(std::string reg_name) { return 0; }

using namespace std;

static int nfailed = 0;
static int ntested = 0;

#define CHECK_EQ(expr, expected) do { \
    ntested++; \
    auto got_ = (expr); \
    auto exp_ = (expected); \
    if (got_ != exp_) { \
        cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " \
             << #expr << " => 0x" << hex << (uint64_t)(uint32_t)got_ \
             << ", expected 0x" << hex << (uint64_t)(uint32_t)exp_ << endl; \
        nfailed++; \
    } \
} while(0)

#define CHECK_STR_EQ(expr, expected) do { \
    ntested++; \
    auto got_ = (expr); \
    string exp_(expected); \
    if (got_ != exp_) { \
        cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " \
             << #expr << " => \"" << got_ \
             << "\", expected \"" << exp_ << "\"" << endl; \
        nfailed++; \
    } \
} while(0)

static void test_signext_no_extension_needed() {
    // positive value, sign bit not set — should remain unchanged
    CHECK_EQ(SIGNEXT(0x0F, 7), 0x0F);     // 4-bit positive in 8-bit field
    CHECK_EQ(SIGNEXT(0x00, 7), 0x00);     // zero
    CHECK_EQ(SIGNEXT(0x7F, 7), 0x7F);     // max positive 8-bit signed
    CHECK_EQ(SIGNEXT(0x01, 15), 0x01);    // small positive in 16-bit field
    CHECK_EQ(SIGNEXT(0x7FFF, 15), 0x7FFF); // max positive 16-bit signed
}

static void test_signext_extension_needed() {
    // sign bit set — should extend with 1s
    // 8-bit sign extension (sign bit = bit 7)
    CHECK_EQ((uint32_t)SIGNEXT(0x80, 7), 0xFFFFFF80u);   // -128 as 8-bit
    CHECK_EQ((uint32_t)SIGNEXT(0xFF, 7), 0xFFFFFFFFu);   // -1 as 8-bit

    // 16-bit sign extension (sign bit = bit 15)
    CHECK_EQ((uint32_t)SIGNEXT(0x8000, 15), 0xFFFF8000u); // -32768 as 16-bit
    CHECK_EQ((uint32_t)SIGNEXT(0xFFFF, 15), 0xFFFFFFFFu); // -1 as 16-bit

    // 4-bit sign extension (sign bit = bit 3)
    CHECK_EQ((uint32_t)SIGNEXT(0x8, 3), 0xFFFFFFF8u);     // -8 as 4-bit
    CHECK_EQ((uint32_t)SIGNEXT(0xF, 3), 0xFFFFFFFFu);     // -1 as 4-bit
}

static void test_signext_boundary_values() {
    // exactly at the sign bit boundary
    // 1-bit sign extension (sign bit = bit 0)
    CHECK_EQ((uint32_t)SIGNEXT(0x1, 0), 0xFFFFFFFFu);     // -1 as 1-bit
    CHECK_EQ((uint32_t)SIGNEXT(0x0, 0), 0x00000000u);     // 0 as 1-bit

    // 24-bit sign extension (sign bit = bit 23)
    CHECK_EQ((uint32_t)SIGNEXT(0x800000, 23), 0xFF800000u);
    CHECK_EQ((uint32_t)SIGNEXT(0x7FFFFF, 23), 0x007FFFFFu);
}

/* ---- disassemble_single() tests driven by ppcdisasmtest.csv ---- */

// Helper: disassemble an instruction at a given address with simplified mnemonics
static string disasm(uint32_t addr, uint32_t opcode) {
    PPCDisasmContext ctx = {};
    ctx.instr_addr = addr;
    ctx.instr_code = opcode;
    ctx.simplified = true;
    return disassemble_single(&ctx);
}

// Build the expected disassembly string from CSV fields.
// CSV format: address,opcode,mnemonic[,operand1[,operand2[,...]]]
// Disassembler output: "%-7s operand1, operand2, ..."
static string build_expected(const vector<string>& fields) {
    // fields[2] is the mnemonic, fields[3..] are operands
    string mnemonic = fields[2];

    // Pad mnemonic to at least 7 chars (matching %-7s in disassembler)
    while (mnemonic.size() < 7)
        mnemonic += ' ';
    mnemonic += ' ';

    if (fields.size() <= 3)
        return mnemonic;

    string operands;
    for (size_t i = 3; i < fields.size(); i++) {
        if (i > 3) operands += ", ";
        operands += fields[i];
    }
    return mnemonic + operands;
}

// Split a CSV line into fields (simple comma split, no quoting)
static vector<string> split_csv(const string& line) {
    vector<string> fields;
    istringstream ss(line);
    string field;
    while (getline(ss, field, ','))
        fields.push_back(field);
    return fields;
}

static void test_disasm_csv(const string& csv_path) {
    ifstream file(csv_path);
    if (!file.is_open()) {
        cerr << "FAIL: cannot open " << csv_path << endl;
        nfailed++;
        ntested++;
        return;
    }

    string line;
    int line_num = 0;
    while (getline(file, line)) {
        line_num++;
        // Skip comments and blank lines
        if (line.empty() || line[0] == '#')
            continue;

        auto fields = split_csv(line);
        if (fields.size() < 3) {
            cerr << "WARN: skipping malformed line " << line_num << ": " << line << endl;
            continue;
        }

        char *end;
        uint32_t addr = (uint32_t)strtoul(fields[0].c_str(), &end, 16);
        if (*end != '\0') {
            cerr << "WARN: bad address on line " << line_num << endl;
            continue;
        }
        uint32_t opcode = (uint32_t)strtoul(fields[1].c_str(), &end, 16);
        if (*end != '\0') {
            cerr << "WARN: bad opcode on line " << line_num << endl;
            continue;
        }
        string expected = build_expected(fields);
        string got = disasm(addr, opcode);

        ntested++;
        if (got != expected) {
            cerr << "FAIL line " << line_num << ": "
                 << "disasm(0x" << hex << addr << ", 0x" << opcode << ") => \""
                 << got << "\", expected \"" << expected << "\"" << endl;
            nfailed++;
        }
    }
}

int main(int argc, char* argv[]) {
    cout << "Running ppcdisasm tests..." << endl;

    test_signext_no_extension_needed();
    test_signext_extension_needed();
    test_signext_boundary_values();

    // Run CSV-driven disassembly tests
    // Try multiple paths to find the test data
    string csv_path;
    const char* candidates[] = {
        "../cpu/ppc/test/ppcdisasmtest.csv",         // when run from tests/build/
        "cpu/ppc/test/ppcdisasmtest.csv",             // when run from repo root
        "../../cpu/ppc/test/ppcdisasmtest.csv",       // fallback
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        ifstream f(candidates[i]);
        if (f.good()) {
            csv_path = candidates[i];
            break;
        }
    }
    if (argc > 1)
        csv_path = argv[1];

    if (!csv_path.empty()) {
        test_disasm_csv(csv_path);
    } else {
        cerr << "WARN: ppcdisasmtest.csv not found, skipping CSV tests" << endl;
    }

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
