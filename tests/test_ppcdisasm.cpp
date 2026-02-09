/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for cpu/ppc/ppcdisasm.h SIGNEXT macro */

#include <cpu/ppc/ppcdisasm.h>
#include <cinttypes>
#include <iostream>

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

int main() {
    cout << "Running ppcdisasm SIGNEXT tests..." << endl;

    test_signext_no_extension_needed();
    test_signext_extension_needed();
    test_signext_boundary_values();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
