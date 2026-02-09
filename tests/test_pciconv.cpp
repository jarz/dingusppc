/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for PCI config space data conversion functions in pcihost.h */

#include <devices/common/pci/pcihost.h>
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

/* ---- pci_conv_rd_data tests ---- */

static void test_pci_conv_rd_byte() {
    // Reading individual bytes from a little-endian dword 0x44332211
    uint32_t val = 0x44332211;
    AccessDetails d;

    d = make_details(1, 0);
    CHECK_EQ(pci_conv_rd_data(val, 0, d), 0x11u);

    d = make_details(1, 1);
    CHECK_EQ(pci_conv_rd_data(val, 0, d), 0x22u);

    d = make_details(1, 2);
    CHECK_EQ(pci_conv_rd_data(val, 0, d), 0x33u);

    d = make_details(1, 3);
    CHECK_EQ(pci_conv_rd_data(val, 0, d), 0x44u);
}

static void test_pci_conv_rd_word() {
    // Reading 16-bit words from a little-endian dword
    uint32_t val = 0x44332211;
    AccessDetails d;

    // offset 0: bytes 0,1 -> big-endian swap of 0x2211 -> 0x1122
    d = make_details(2, 0);
    CHECK_EQ(pci_conv_rd_data(val, 0, d), (uint32_t)BYTESWAP_16(0x2211));

    // offset 2: bytes 2,3 -> big-endian swap of 0x4433 -> 0x3344
    d = make_details(2, 2);
    CHECK_EQ(pci_conv_rd_data(val, 0, d), (uint32_t)BYTESWAP_16(0x4433));
}

static void test_pci_conv_rd_dword() {
    // Reading full 32-bit dword at offset 0
    uint32_t val = 0x44332211;
    AccessDetails d;

    d = make_details(4, 0);
    CHECK_EQ(pci_conv_rd_data(val, 0, d), BYTESWAP_32(0x44332211u));
}

static void test_pci_conv_rd_unaligned_word() {
    // Unaligned 16-bit read at offset 3 spans two dwords
    uint32_t val  = 0xDD000000; // byte 3 = 0xDD
    uint32_t val2 = 0x000000EE; // byte 0 of next = 0xEE
    AccessDetails d;

    d = make_details(2, 3);
    uint32_t result = pci_conv_rd_data(val, val2, d);
    // bytes 3,4 = 0xDD, 0xEE -> result = (0xDD << 8) | 0xEE = 0xDD00 | 0xEE
    CHECK_EQ(result, 0xDD00u | 0xEEu);
}

static void test_pci_conv_rd_unaligned_dword() {
    // Unaligned 32-bit read at offset 1 spans two dwords
    uint32_t val  = 0x44332211;
    uint32_t val2 = 0x88776655;
    AccessDetails d;

    d = make_details(4, 1);
    // bytes 1,2,3,4 = 0x22,0x33,0x44,0x55
    uint32_t combined = (uint32_t)((((uint64_t)val2 << 32) | val) >> 8);
    CHECK_EQ(pci_conv_rd_data(val, val2, d), BYTESWAP_32(combined));
}

static void test_pci_conv_rd_default() {
    // Invalid size should return 0xFFFFFFFF
    AccessDetails d = make_details(0, 0);
    CHECK_EQ(pci_conv_rd_data(0, 0, d), 0xFFFFFFFFu);

    d = make_details(3, 0);
    CHECK_EQ(pci_conv_rd_data(0, 0, d), 0xFFFFFFFFu);
}

/* ---- pci_conv_wr_data tests ---- */

static void test_pci_conv_wr_byte() {
    // Writing individual bytes into a dword
    uint32_t v1 = 0x44332211;
    AccessDetails d;

    // write byte 0xAA at offset 0
    d = make_details(1, 0);
    CHECK_EQ(pci_conv_wr_data(v1, 0xAA, d), 0x443322AAu);

    // write byte 0xBB at offset 1
    d = make_details(1, 1);
    CHECK_EQ(pci_conv_wr_data(v1, 0xBB, d), 0x4433BB11u);

    // write byte 0xCC at offset 2
    d = make_details(1, 2);
    CHECK_EQ(pci_conv_wr_data(v1, 0xCC, d), 0x44CC2211u);

    // write byte 0xDD at offset 3
    d = make_details(1, 3);
    CHECK_EQ(pci_conv_wr_data(v1, 0xDD, d), 0xDD332211u);
}

static void test_pci_conv_wr_word() {
    // Writing 16-bit words
    uint32_t v1 = 0x44332211;
    AccessDetails d;

    // write word at offset 0 — the value is big-endian from host
    d = make_details(2, 0);
    uint32_t result = pci_conv_wr_data(v1, 0xBBAA, d);
    // BYTESWAP_16(0xBBAA) = 0xAABB, placed in low 16 bits
    CHECK_EQ(result, (v1 & ~0xFFFFu) | BYTESWAP_16(0xBBAA));

    // write word at offset 2
    d = make_details(2, 2);
    result = pci_conv_wr_data(v1, 0xBBAA, d);
    CHECK_EQ(result, (v1 & 0x0000FFFFu) | ((uint32_t)BYTESWAP_16(0xBBAA) << 16));
}

static void test_pci_conv_wr_dword() {
    // Writing full 32-bit dword at offset 0
    AccessDetails d = make_details(4, 0);
    CHECK_EQ(pci_conv_wr_data(0, 0x44332211, d), BYTESWAP_32(0x44332211u));
}

static void test_pci_conv_wr_default() {
    // Invalid size should return 0xFFFFFFFF
    AccessDetails d = make_details(0, 0);
    CHECK_EQ(pci_conv_wr_data(0, 0, d), 0xFFFFFFFFu);
}

/* ---- DEV_FUN macro ---- */

static void test_dev_fun_macro() {
    CHECK_EQ(DEV_FUN(0, 0), 0);
    CHECK_EQ(DEV_FUN(1, 0), 8);
    CHECK_EQ(DEV_FUN(0, 1), 1);
    CHECK_EQ(DEV_FUN(1, 1), 9);
    CHECK_EQ(DEV_FUN(31, 7), 255);
}

int main() {
    cout << "Running pciconv tests..." << endl;

    test_pci_conv_rd_byte();
    test_pci_conv_rd_word();
    test_pci_conv_rd_dword();
    test_pci_conv_rd_unaligned_word();
    test_pci_conv_rd_unaligned_dword();
    test_pci_conv_rd_default();
    test_pci_conv_wr_byte();
    test_pci_conv_wr_word();
    test_pci_conv_wr_dword();
    test_pci_conv_wr_default();
    test_dev_fun_macro();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
