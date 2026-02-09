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
#include <iostream>
#include <string>

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

/* ---- disassemble_single() tests ---- */

// Helper: disassemble an instruction at a given address with simplified mnemonics
static string disasm(uint32_t addr, uint32_t opcode) {
    PPCDisasmContext ctx = {};
    ctx.instr_addr = addr;
    ctx.instr_code = opcode;
    ctx.simplified = true;
    return disassemble_single(&ctx);
}

// Test vectors derived from cpu/ppc/test/ppcdisasmtest.csv

static void test_disasm_branches() {
    // bl 0xFFF0335C
    CHECK_STR_EQ(disasm(0xFFF03008, 0x48000355), "bl      0xFFF0335C");
    // b 0xFFF0335C
    CHECK_STR_EQ(disasm(0xFFF03000, 0x4280035C), "b       0xFFF0335C");
    // ba 0x00000800
    CHECK_STR_EQ(disasm(0xFFF03000, 0x48000802), "ba      0x00000800");
    // bla 0x00000800
    CHECK_STR_EQ(disasm(0xFFF03000, 0x48000803), "bla     0x00000800");
}

static void test_disasm_branch_ctr() {
    // bctr (with trailing padding)
    CHECK_STR_EQ(disasm(0xFFF03000, 0x4E800420), "bctr    ");
    // bctrl
    CHECK_STR_EQ(disasm(0xFFF03000, 0x4E800421), "bctrl   ");
}

static void test_disasm_branch_lr() {
    // blr
    CHECK_STR_EQ(disasm(0xFFF03000, 0x4E800020), "blr     ");
    // blrl
    CHECK_STR_EQ(disasm(0xFFF03000, 0x4E800021), "blrl    ");
}

static void test_disasm_arithmetic_imm() {
    // addi r1, r1, -0x20
    CHECK_STR_EQ(disasm(0, 0x3821FFE0), "addi    r1, r1, -0x20");

    // li r3, 0x0 (simplified addi r3,r0,0)
    CHECK_STR_EQ(disasm(0, 0x38600000), "li      r3, 0x0");

    // li r0, 0x1
    CHECK_STR_EQ(disasm(0, 0x38000001), "li      r0, 0x1");
}

static void test_disasm_logical_imm() {
    // ori r0,r0,0 is the canonical nop
    CHECK_STR_EQ(disasm(0, 0x60000000), "nop     ");
}

static void test_disasm_load_store() {
    // lwz r0, 0x0(r1)
    CHECK_STR_EQ(disasm(0, 0x80010000), "lwz     r0, 0x0(r1)");

    // stw r0, 0x4(r1)
    CHECK_STR_EQ(disasm(0, 0x90010004), "stw     r0, 0x4(r1)");

    // lbz r3, 0x0(r4)
    CHECK_STR_EQ(disasm(0, 0x88640000), "lbz     r3, 0x0(r4)");
}

static void test_disasm_system() {
    // sc (system call)
    CHECK_STR_EQ(disasm(0, 0x44000002), "sc      ");
}

int main() {
    cout << "Running ppcdisasm tests..." << endl;

    test_signext_no_extension_needed();
    test_signext_extension_needed();
    test_signext_boundary_values();
    test_disasm_branches();
    test_disasm_branch_ctr();
    test_disasm_branch_lr();
    test_disasm_arithmetic_imm();
    test_disasm_logical_imm();
    test_disasm_load_store();
    test_disasm_system();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
