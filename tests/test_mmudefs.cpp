/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for cpu/ppc/ppcmmu.h — page alignment logic */

#include <cpu/ppc/ppcmmu.h>
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
             << #expr << " => 0x" << hex << (uint64_t)got_ \
             << ", expected 0x" << hex << (uint64_t)exp_ << endl; \
        nfailed++; \
    } \
} while(0)

#define CHECK_TRUE(expr) do { \
    ntested++; \
    if (!(expr)) { \
        cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " \
             << #expr << " is false" << endl; \
        nfailed++; \
    } \
} while(0)

static void test_page_mask_relationships() {
    // Verify derived constants are consistent with PPC_PAGE_SIZE_BITS
    CHECK_EQ(PPC_PAGE_SIZE, (1u << PPC_PAGE_SIZE_BITS));
    CHECK_EQ(PPC_PAGE_MASK, ~(PPC_PAGE_SIZE - 1));
}

static void test_page_alignment() {
    // Aligned addresses should survive masking unchanged
    CHECK_EQ(0x1000u & PPC_PAGE_MASK, 0x1000u);
    CHECK_EQ(0xFFFFF000u & PPC_PAGE_MASK, 0xFFFFF000u);

    // Unaligned addresses should be truncated to page boundary
    CHECK_EQ(0x1234u & PPC_PAGE_MASK, 0x1000u);
    CHECK_EQ(0xDEADBEEFu & PPC_PAGE_MASK, 0xDEADB000u);
    CHECK_EQ(0x00000001u & PPC_PAGE_MASK, 0x00000000u);
    CHECK_EQ(0xFFFFFFFFu & PPC_PAGE_MASK, 0xFFFFF000u);
}

static void test_tlb_vps_mask() {
    // TLB_VPS_MASK should select virtual page set bits (bits 12..27)
    CHECK_EQ(0x12345678u & TLB_VPS_MASK, 0x02345000u);
    CHECK_EQ(0xFFFFFFFFu & TLB_VPS_MASK, TLB_VPS_MASK);
    CHECK_EQ(0x00000FFFu & TLB_VPS_MASK, 0u);
}

int main() {
    cout << "Running mmudefs tests..." << endl;

    test_page_mask_relationships();
    test_page_alignment();
    test_tlb_vps_mask();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
