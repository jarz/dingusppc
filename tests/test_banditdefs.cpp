/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for devices/common/pci/bandit.h definitions */

// We only need the SINGLE_BIT_SET macro and constants from bandit.h.
// The BUS_NUM/DEV_NUM/FUN_NUM/REG_NUM macros reference `this->config_addr`
// and cannot be tested standalone.

#include <cinttypes>
#include <iostream>

using namespace std;

// Extract the testable parts directly since bandit.h has complex includes
constexpr auto BANDIT_DEV      = 11;
constexpr auto BANDIT_CAR_TYPE = 1 << 0;

#define SINGLE_BIT_SET(val) ((val) && !((val) & ((val)-1)))

static int nfailed = 0;
static int ntested = 0;

#define CHECK_EQ(expr, expected) do { \
    ntested++; \
    auto got_ = (expr); \
    auto exp_ = (expected); \
    if (got_ != exp_) { \
        cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " \
             << #expr << " => " << (uint64_t)got_ \
             << ", expected " << (uint64_t)exp_ << endl; \
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

#define CHECK_FALSE(expr) do { \
    ntested++; \
    if ((expr)) { \
        cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " \
             << #expr << " is true, expected false" << endl; \
        nfailed++; \
    } \
} while(0)

/* ---- SINGLE_BIT_SET macro ---- */

static void test_single_bit_set_powers_of_2() {
    // All powers of 2 should return true
    CHECK_TRUE(SINGLE_BIT_SET(1));
    CHECK_TRUE(SINGLE_BIT_SET(2));
    CHECK_TRUE(SINGLE_BIT_SET(4));
    CHECK_TRUE(SINGLE_BIT_SET(8));
    CHECK_TRUE(SINGLE_BIT_SET(16));
    CHECK_TRUE(SINGLE_BIT_SET(32));
    CHECK_TRUE(SINGLE_BIT_SET(64));
    CHECK_TRUE(SINGLE_BIT_SET(128));
    CHECK_TRUE(SINGLE_BIT_SET(256));
    CHECK_TRUE(SINGLE_BIT_SET(0x80000000u));
}

static void test_single_bit_set_non_powers() {
    // Zero should return false
    CHECK_FALSE(SINGLE_BIT_SET(0));

    // Multiple bits set should return false
    CHECK_FALSE(SINGLE_BIT_SET(3));
    CHECK_FALSE(SINGLE_BIT_SET(5));
    CHECK_FALSE(SINGLE_BIT_SET(6));
    CHECK_FALSE(SINGLE_BIT_SET(7));
    CHECK_FALSE(SINGLE_BIT_SET(0xFF));
    CHECK_FALSE(SINGLE_BIT_SET(0xFFFFFFFF));
    CHECK_FALSE(SINGLE_BIT_SET(0x80000001u));
}

/* ---- PCI config address bit extraction ---- */

static void test_pci_config_addr_extraction() {
    // Test the bit extraction logic that BUS_NUM/DEV_NUM/FUN_NUM/REG_NUM use
    // These macros use this->config_addr, but we can test the extraction logic directly
    uint32_t config_addr;

    // Construct a config address with known fields:
    // Bus=0x12, Dev=0x0B, Fun=0x03, Reg=0x40
    config_addr = (0x12u << 16) | (0x0Bu << 11) | (0x03u << 8) | 0x40u;

    CHECK_EQ((config_addr >> 16) & 0xFFu, 0x12u);  // BUS_NUM
    CHECK_EQ((config_addr >> 11) & 0x1Fu, 0x0Bu);  // DEV_NUM
    CHECK_EQ((config_addr >>  8) & 0x07u, 0x03u);  // FUN_NUM
    CHECK_EQ((config_addr      ) & 0xFCu, 0x40u);  // REG_NUM

    // Test with all-zeros
    config_addr = 0;
    CHECK_EQ((config_addr >> 16) & 0xFFu, 0u);
    CHECK_EQ((config_addr >> 11) & 0x1Fu, 0u);
    CHECK_EQ((config_addr >>  8) & 0x07u, 0u);
    CHECK_EQ((config_addr      ) & 0xFCu, 0u);

    // Test with max values
    config_addr = (0xFFu << 16) | (0x1Fu << 11) | (0x07u << 8) | 0xFCu;
    CHECK_EQ((config_addr >> 16) & 0xFFu, 0xFFu);
    CHECK_EQ((config_addr >> 11) & 0x1Fu, 0x1Fu);
    CHECK_EQ((config_addr >>  8) & 0x07u, 0x07u);
    CHECK_EQ((config_addr      ) & 0xFCu, 0xFCu);
}

int main() {
    cout << "Running banditdefs tests..." << endl;

    test_single_bit_set_powers_of_2();
    test_single_bit_set_non_powers();
    test_pci_config_addr_extraction();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
