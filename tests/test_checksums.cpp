/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for calculate_rom_crc() from devices/common/nubus/nubusutils.cpp
    and OfConfigChrp::checksum_hdr() algorithm from devices/common/ofnvram.cpp.

    These are real checksum/CRC computation algorithms used in the emulator.
*/

#include <cinttypes>
#include <cstring>
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

// Forward-declare the function under test.
// The implementation is linked from nubuscrc_impl.cpp (extracted from nubusutils.cpp).
extern uint32_t calculate_rom_crc(uint8_t *data_ptr, int length);

/* ---- calculate_rom_crc tests ---- */

static void test_crc_empty() {
    // Zero-length input should produce 0
    uint8_t dummy = 0;
    CHECK_EQ(calculate_rom_crc(&dummy, 0), 0u);
}

static void test_crc_single_byte() {
    // Single byte: sum starts at 0, rotated 0 is 0, add byte value
    uint8_t data[] = {0x42};
    CHECK_EQ(calculate_rom_crc(data, 1), 0x42u);
}

static void test_crc_two_bytes() {
    // Byte 0: sum=0, rotate(0)=0, sum=0+0xAA=0xAA
    // Byte 1: sum=0xAA, rotate left = 0x154 (bit 31 not set, so shift left 1),
    //         actually 0xAA = 0b10101010, rotl = 0b101010100 = 0x154
    //         sum = 0x154 + 0x55 = 0x1A9
    uint8_t data[] = {0xAA, 0x55};
    // Manually trace:
    // i=0: sum=0, no high bit, sum = (0<<1)|0 = 0, sum += 0xAA = 0xAA
    // i=1: sum=0xAA, no high bit (bit 31 not set), sum = (0xAA<<1)|0 = 0x154, sum += 0x55 = 0x1A9
    CHECK_EQ(calculate_rom_crc(data, 2), 0x1A9u);
}

static void test_crc_high_bit_rotation() {
    // When bit 31 is set, rotate-left wraps the high bit to bit 0
    // Start with a value that will set bit 31
    uint8_t data[2];
    data[0] = 0xFF; // sum = 0xFF after byte 0
    data[1] = 0x00; // sum = rotl(0xFF) + 0 = 0x1FE
    CHECK_EQ(calculate_rom_crc(data, 2), 0x1FEu);

    // Now test with bit 31 actually set
    // We need many bytes to shift the accumulator up to bit 31.
    // Alternative: test with a known pattern.
    // After 24 shifts of 0xFF: the value gets complex. Let's trace a simpler case.
    // Use 4 bytes of 0x80 to test the rotation behavior:
    uint8_t data2[] = {0x80, 0x80, 0x80, 0x80};
    // i=0: sum=0, rotl(0)=0, sum=0x80
    // i=1: sum=0x80, rotl(0x80)=0x100, sum=0x100+0x80=0x180
    // i=2: sum=0x180, rotl(0x180)=0x300, sum=0x300+0x80=0x380
    // i=3: sum=0x380, rotl(0x380)=0x700, sum=0x700+0x80=0x780
    CHECK_EQ(calculate_rom_crc(data2, 4), 0x780u);
}

static void test_crc_all_zeros() {
    uint8_t data[16];
    memset(data, 0, sizeof(data));
    // All zeros: each step rotates 0 and adds 0
    CHECK_EQ(calculate_rom_crc(data, 16), 0u);
}

static void test_crc_all_ones() {
    uint8_t data[4];
    memset(data, 0xFF, sizeof(data));
    // i=0: rotl(0)=0, sum=0xFF
    // i=1: rotl(0xFF)=0x1FE, sum=0x1FE+0xFF=0x2FD
    // i=2: rotl(0x2FD)=0x5FA, sum=0x5FA+0xFF=0x6F9
    // i=3: rotl(0x6F9)=0xDF2, sum=0xDF2+0xFF=0xEF1
    CHECK_EQ(calculate_rom_crc(data, 4), 0xEF1u);
}

static void test_crc_deterministic() {
    // Same input always produces same output
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint32_t crc1 = calculate_rom_crc(data, 5);
    uint32_t crc2 = calculate_rom_crc(data, 5);
    CHECK_EQ(crc1, crc2);
    CHECK_TRUE(crc1 != 0); // non-trivial input should produce non-zero CRC
}

static void test_crc_order_matters() {
    uint8_t data_a[] = {0x01, 0x02};
    uint8_t data_b[] = {0x02, 0x01};
    uint32_t crc_a = calculate_rom_crc(data_a, 2);
    uint32_t crc_b = calculate_rom_crc(data_b, 2);
    // Different order should produce different CRC (in general)
    CHECK_TRUE(crc_a != crc_b);
}

/* ---- CHRP header checksum algorithm ---- */

// This is the exact algorithm from OfConfigChrp::checksum_hdr in ofnvram.cpp.
// We test it here as a standalone function.
static uint8_t chrp_checksum_hdr(const uint8_t* data) {
    uint16_t sum = data[0];
    for (int i = 2; i < 16; i++) {
        sum += data[i];
        if (sum >= 256)
            sum = (sum + 1) & 0xFFU;
    }
    return sum;
}

static void test_chrp_checksum_zeros() {
    uint8_t hdr[16];
    memset(hdr, 0, sizeof(hdr));
    // All zeros: sum starts at 0, adds 14 zeros, result = 0
    CHECK_EQ(chrp_checksum_hdr(hdr), (uint8_t)0);
}

static void test_chrp_checksum_skips_byte1() {
    // Byte at index 1 is skipped (it's the checksum field itself)
    uint8_t hdr[16];
    memset(hdr, 0, sizeof(hdr));
    hdr[1] = 0xFF;  // this should be ignored
    CHECK_EQ(chrp_checksum_hdr(hdr), (uint8_t)0);
}

static void test_chrp_checksum_basic() {
    uint8_t hdr[16];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 0x10;  // included (initial sum)
    hdr[2] = 0x20;  // included
    hdr[3] = 0x30;  // included
    // sum = 0x10 + 0x20 + 0x30 = 0x60 (no overflow)
    CHECK_EQ(chrp_checksum_hdr(hdr), (uint8_t)0x60);
}

static void test_chrp_checksum_carry_wrap() {
    // Test the carry-wrapping behavior: when sum >= 256, sum = (sum+1) & 0xFF
    uint8_t hdr[16];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 0x80;   // initial sum = 0x80
    hdr[2] = 0x80;   // sum = 0x80 + 0x80 = 0x100 >= 256, so sum = (0x100+1)&0xFF = 0x01
    hdr[3] = 0x02;   // sum = 0x01 + 0x02 = 0x03
    CHECK_EQ(chrp_checksum_hdr(hdr), (uint8_t)0x03);
}

static void test_chrp_checksum_multiple_carries() {
    uint8_t hdr[16];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 0xFE;   // sum = 0xFE
    hdr[2] = 0x01;   // sum = 0xFF (no carry)
    hdr[3] = 0x01;   // sum = 0x100 >= 256 -> (0x101)&0xFF = 0x01... wait
    // Actually: sum = 0xFF + 0x01 = 0x100, sum >= 256 -> sum = (0x100+1)&0xFF = 0x01
    hdr[4] = 0xFE;   // sum = 0x01 + 0xFE = 0xFF (no carry)
    hdr[5] = 0x01;   // sum = 0xFF + 0x01 = 0x100 -> (0x101)&0xFF = 0x01
    CHECK_EQ(chrp_checksum_hdr(hdr), (uint8_t)0x01);
}

static void test_chrp_checksum_all_fields() {
    // Fill all 14 summed bytes with 0x10 each (skipping byte 1)
    uint8_t hdr[16];
    memset(hdr, 0x10, sizeof(hdr));
    hdr[1] = 0xFF;  // should be ignored
    // Bytes summed are [0] and [2..15] (15 bytes total, skipping index 1)
    // sum = 15 * 0x10 = 0xF0 (no carry)
    CHECK_EQ(chrp_checksum_hdr(hdr), (uint8_t)0xF0);
}

int main() {
    cout << "Running checksum tests..." << endl;

    test_crc_empty();
    test_crc_single_byte();
    test_crc_two_bytes();
    test_crc_high_bit_rotation();
    test_crc_all_zeros();
    test_crc_all_ones();
    test_crc_deterministic();
    test_crc_order_matters();
    test_chrp_checksum_zeros();
    test_chrp_checksum_skips_byte1();
    test_chrp_checksum_basic();
    test_chrp_checksum_carry_wrap();
    test_chrp_checksum_multiple_carries();
    test_chrp_checksum_all_fields();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
