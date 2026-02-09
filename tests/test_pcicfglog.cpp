/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for pci_cfg_log() from devices/common/pci/pcibase.h */

#include <devices/common/pci/pcibase.h>
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

static AccessDetails make_details(uint8_t size, uint8_t offset) {
    AccessDetails d;
    d.size = size;
    d.offset = offset;
    d.flags = 0;
    return d;
}

/* ---- Byte extraction (size=1) ---- */

static void test_cfg_log_bytes() {
    uint32_t val = 0xDDCCBBAA;
    AccessDetails d;

    d = make_details(1, 0);
    CHECK_EQ(pci_cfg_log(val, d), 0xAAu);

    d = make_details(1, 1);
    CHECK_EQ(pci_cfg_log(val, d), 0xBBu);

    d = make_details(1, 2);
    CHECK_EQ(pci_cfg_log(val, d), 0xCCu);

    d = make_details(1, 3);
    CHECK_EQ(pci_cfg_log(val, d), 0xDDu);
}

/* ---- Word extraction (size=2) ---- */

static void test_cfg_log_words() {
    uint32_t val = 0xDDCCBBAA;
    AccessDetails d;

    d = make_details(2, 0);
    CHECK_EQ(pci_cfg_log(val, d), (uint32_t)0xBBAAu);

    d = make_details(2, 1);
    CHECK_EQ(pci_cfg_log(val, d), (uint32_t)0xCCBBu);

    d = make_details(2, 2);
    CHECK_EQ(pci_cfg_log(val, d), (uint32_t)0xDDCCu);

    // offset 3: wraps around
    d = make_details(2, 3);
    uint32_t expected = (uint16_t)((val >> 24) | (val << 8));
    CHECK_EQ(pci_cfg_log(val, d), expected);
}

/* ---- Dword extraction (size=4) ---- */

static void test_cfg_log_dwords() {
    uint32_t val = 0xDDCCBBAA;
    AccessDetails d;

    d = make_details(4, 0);
    CHECK_EQ(pci_cfg_log(val, d), val);

    // offset 1: rotate right by 8
    d = make_details(4, 1);
    CHECK_EQ(pci_cfg_log(val, d), (val >> 8) | (val << 24));

    // offset 2: rotate right by 16
    d = make_details(4, 2);
    CHECK_EQ(pci_cfg_log(val, d), (val >> 16) | (val << 16));

    // offset 3: rotate right by 24
    d = make_details(4, 3);
    CHECK_EQ(pci_cfg_log(val, d), (val >> 24) | (val << 8));
}

/* ---- Default/invalid ---- */

static void test_cfg_log_default() {
    AccessDetails d = make_details(0, 0);
    CHECK_EQ(pci_cfg_log(0, d), 0xFFFFFFFFu);

    d = make_details(3, 0);
    CHECK_EQ(pci_cfg_log(0, d), 0xFFFFFFFFu);
}

int main() {
    cout << "Running pcicfglog tests..." << endl;

    test_cfg_log_bytes();
    test_cfg_log_words();
    test_cfg_log_dwords();
    test_cfg_log_default();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
