/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for cpu/ppc/ppcmmu.h definitions */

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

static void test_page_constants() {
    CHECK_EQ(PPC_PAGE_SIZE_BITS, 12u);
    CHECK_EQ(PPC_PAGE_SIZE, 4096u);
    // page mask should clear the low 12 bits
    CHECK_EQ(PPC_PAGE_MASK, 0xFFFFF000u);
    // verify relationship
    CHECK_EQ(PPC_PAGE_SIZE, (1u << PPC_PAGE_SIZE_BITS));
    CHECK_EQ(PPC_PAGE_MASK, ~(PPC_PAGE_SIZE - 1));
}

static void test_tlb_constants() {
    CHECK_EQ(TLB_SIZE, 4096u);
    CHECK_EQ(TLB2_WAYS, 4u);
    CHECK_EQ(TLB_INVALID_TAG, 0xFFFFFFFFu);
    CHECK_EQ(TLB_VPS_MASK, 0x0FFFF000u);
}

static void test_bat_type_enum() {
    CHECK_EQ((int)BATType::IBAT, 0);
    CHECK_EQ((int)BATType::DBAT, 1);
}

static void test_tlb_type_enum() {
    CHECK_EQ((int)TLBType::ITLB, 0);
    CHECK_EQ((int)TLBType::DTLB, 1);
}

static void test_tlb_flags() {
    // Verify TLB flags are distinct bit positions
    CHECK_EQ((uint16_t)TLBFlags::PAGE_MEM, 1 << 0);
    CHECK_EQ((uint16_t)TLBFlags::PAGE_IO, 1 << 1);
    CHECK_EQ((uint16_t)TLBFlags::PAGE_NOPHYS, 1 << 2);
    CHECK_EQ((uint16_t)TLBFlags::TLBE_FROM_BAT, 1 << 3);
    CHECK_EQ((uint16_t)TLBFlags::TLBE_FROM_PAT, 1 << 4);
    CHECK_EQ((uint16_t)TLBFlags::PAGE_WRITABLE, 1 << 5);
    CHECK_EQ((uint16_t)TLBFlags::PTE_SET_C, 1 << 6);

    // Flags should not overlap
    uint16_t all = TLBFlags::PAGE_MEM | TLBFlags::PAGE_IO | TLBFlags::PAGE_NOPHYS |
                   TLBFlags::TLBE_FROM_BAT | TLBFlags::TLBE_FROM_PAT |
                   TLBFlags::PAGE_WRITABLE | TLBFlags::PTE_SET_C;
    CHECK_EQ(all, 0x7Fu);
}

static void test_page_alignment() {
    // Any aligned address AND'd with PAGE_MASK should be unchanged
    CHECK_EQ(0x1000u & PPC_PAGE_MASK, 0x1000u);
    CHECK_EQ(0xFFFFF000u & PPC_PAGE_MASK, 0xFFFFF000u);

    // Unaligned address should be truncated
    CHECK_EQ(0x1234u & PPC_PAGE_MASK, 0x1000u);
    CHECK_EQ(0xDEADBEEFu & PPC_PAGE_MASK, 0xDEADB000u);
}

int main() {
    cout << "Running mmudefs tests..." << endl;

    test_page_constants();
    test_tlb_constants();
    test_bat_type_enum();
    test_tlb_type_enum();
    test_tlb_flags();
    test_page_alignment();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
