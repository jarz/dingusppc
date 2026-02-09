/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for memaccess.h */

#include <memaccess.h>
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

/* ---- Aligned big-endian reads ---- */
static void test_read_word_be_a() {
    uint8_t buf[] = {0x12, 0x34};
    CHECK_EQ(READ_WORD_BE_A(buf), (uint16_t)0x1234);

    uint8_t buf2[] = {0xFF, 0x00};
    CHECK_EQ(READ_WORD_BE_A(buf2), (uint16_t)0xFF00);

    uint8_t buf3[] = {0x00, 0x00};
    CHECK_EQ(READ_WORD_BE_A(buf3), (uint16_t)0x0000);
}

static void test_read_dword_be_a() {
    uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK_EQ(READ_DWORD_BE_A(buf), 0xDEADBEEFu);

    uint8_t buf2[] = {0x00, 0x00, 0x00, 0x01};
    CHECK_EQ(READ_DWORD_BE_A(buf2), 0x00000001u);
}

static void test_read_qword_be_a() {
    uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK_EQ(READ_QWORD_BE_A(buf), 0x0102030405060708ULL);
}

/* ---- Aligned little-endian reads ---- */
static void test_read_word_le_a() {
    uint8_t buf[] = {0x34, 0x12};
    CHECK_EQ(READ_WORD_LE_A(buf), (uint16_t)0x1234);
}

static void test_read_dword_le_a() {
    uint8_t buf[] = {0xEF, 0xBE, 0xAD, 0xDE};
    CHECK_EQ(READ_DWORD_LE_A(buf), 0xDEADBEEFu);
}

static void test_read_qword_le_a() {
    uint8_t buf[] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    CHECK_EQ(READ_QWORD_LE_A(buf), 0x0102030405060708ULL);
}

/* ---- Unaligned big-endian reads ---- */
static void test_read_word_be_u() {
    uint8_t buf[] = {0xAB, 0xCD};
    CHECK_EQ((uint16_t)READ_WORD_BE_U(buf), (uint16_t)0xABCD);
}

static void test_read_dword_be_u() {
    uint8_t buf[] = {0xCA, 0xFE, 0xBA, 0xBE};
    CHECK_EQ((uint32_t)READ_DWORD_BE_U(buf), 0xCAFEBABEu);
}

static void test_read_qword_be_u() {
    uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    CHECK_EQ((uint64_t)READ_QWORD_BE_U(buf), 0xDEADBEEFCAFEBABEULL);
}

/* ---- Unaligned little-endian reads ---- */
static void test_read_word_le_u() {
    uint8_t buf[] = {0xCD, 0xAB};
    CHECK_EQ((uint16_t)READ_WORD_LE_U(buf), (uint16_t)0xABCD);
}

static void test_read_dword_le_u() {
    uint8_t buf[] = {0xBE, 0xBA, 0xFE, 0xCA};
    CHECK_EQ((uint32_t)READ_DWORD_LE_U(buf), 0xCAFEBABEu);
}

static void test_read_qword_le_u() {
    uint8_t buf[] = {0xBE, 0xBA, 0xFE, 0xCA, 0xEF, 0xBE, 0xAD, 0xDE};
    CHECK_EQ((uint64_t)READ_QWORD_LE_U(buf), 0xDEADBEEFCAFEBABEULL);
}

/* ---- Aligned big-endian writes ---- */
static void test_write_word_be_a() {
    uint8_t buf[2] = {};
    WRITE_WORD_BE_A(buf, 0x1234);
    CHECK_EQ(buf[0], 0x12);
    CHECK_EQ(buf[1], 0x34);
}

static void test_write_dword_be_a() {
    uint8_t buf[4] = {};
    WRITE_DWORD_BE_A(buf, 0xDEADBEEF);
    CHECK_EQ(buf[0], 0xDE);
    CHECK_EQ(buf[1], 0xAD);
    CHECK_EQ(buf[2], 0xBE);
    CHECK_EQ(buf[3], 0xEF);
}

static void test_write_qword_be_a() {
    uint8_t buf[8] = {};
    WRITE_QWORD_BE_A(buf, 0x0102030405060708ULL);
    CHECK_EQ(buf[0], 0x01);
    CHECK_EQ(buf[1], 0x02);
    CHECK_EQ(buf[2], 0x03);
    CHECK_EQ(buf[3], 0x04);
    CHECK_EQ(buf[4], 0x05);
    CHECK_EQ(buf[5], 0x06);
    CHECK_EQ(buf[6], 0x07);
    CHECK_EQ(buf[7], 0x08);
}

/* ---- Unaligned big-endian writes ---- */
static void test_write_word_be_u() {
    uint8_t buf[2] = {};
    WRITE_WORD_BE_U(buf, 0xABCD);
    CHECK_EQ(buf[0], 0xAB);
    CHECK_EQ(buf[1], 0xCD);
}

static void test_write_dword_be_u() {
    uint8_t buf[4] = {};
    WRITE_DWORD_BE_U(buf, 0xCAFEBABE);
    CHECK_EQ(buf[0], 0xCA);
    CHECK_EQ(buf[1], 0xFE);
    CHECK_EQ(buf[2], 0xBA);
    CHECK_EQ(buf[3], 0xBE);
}

static void test_write_qword_be_u() {
    uint8_t buf[8] = {};
    WRITE_QWORD_BE_U(buf, 0xDEADBEEFCAFEBABEULL);
    CHECK_EQ(buf[0], 0xDE);
    CHECK_EQ(buf[1], 0xAD);
    CHECK_EQ(buf[2], 0xBE);
    CHECK_EQ(buf[3], 0xEF);
    CHECK_EQ(buf[4], 0xCA);
    CHECK_EQ(buf[5], 0xFE);
    CHECK_EQ(buf[6], 0xBA);
    CHECK_EQ(buf[7], 0xBE);
}

/* ---- Aligned little-endian writes ---- */
static void test_write_word_le_a() {
    uint8_t buf[2] = {};
    WRITE_WORD_LE_A(buf, 0x1234);
    CHECK_EQ(buf[0], 0x34);
    CHECK_EQ(buf[1], 0x12);
}

static void test_write_dword_le_a() {
    uint8_t buf[4] = {};
    WRITE_DWORD_LE_A(buf, 0xDEADBEEF);
    CHECK_EQ(buf[0], 0xEF);
    CHECK_EQ(buf[1], 0xBE);
    CHECK_EQ(buf[2], 0xAD);
    CHECK_EQ(buf[3], 0xDE);
}

static void test_write_qword_le_a() {
    uint8_t buf[8] = {};
    WRITE_QWORD_LE_A(buf, 0x0102030405060708ULL);
    CHECK_EQ(buf[0], 0x08);
    CHECK_EQ(buf[1], 0x07);
    CHECK_EQ(buf[2], 0x06);
    CHECK_EQ(buf[3], 0x05);
    CHECK_EQ(buf[4], 0x04);
    CHECK_EQ(buf[5], 0x03);
    CHECK_EQ(buf[6], 0x02);
    CHECK_EQ(buf[7], 0x01);
}

/* ---- Unaligned little-endian writes ---- */
static void test_write_word_le_u() {
    uint8_t buf[2] = {};
    WRITE_WORD_LE_U(buf, 0xABCD);
    CHECK_EQ(buf[0], 0xCD);
    CHECK_EQ(buf[1], 0xAB);
}

static void test_write_dword_le_u() {
    uint8_t buf[4] = {};
    WRITE_DWORD_LE_U(buf, 0xCAFEBABE);
    CHECK_EQ(buf[0], 0xBE);
    CHECK_EQ(buf[1], 0xBA);
    CHECK_EQ(buf[2], 0xFE);
    CHECK_EQ(buf[3], 0xCA);
}

static void test_write_qword_le_u() {
    uint8_t buf[8] = {};
    WRITE_QWORD_LE_U(buf, 0xDEADBEEFCAFEBABEULL);
    CHECK_EQ(buf[0], 0xBE);
    CHECK_EQ(buf[1], 0xBA);
    CHECK_EQ(buf[2], 0xFE);
    CHECK_EQ(buf[3], 0xCA);
    CHECK_EQ(buf[4], 0xEF);
    CHECK_EQ(buf[5], 0xBE);
    CHECK_EQ(buf[6], 0xAD);
    CHECK_EQ(buf[7], 0xDE);
}

/* ---- Read/write round-trip tests ---- */
static void test_read_write_roundtrip_be() {
    uint8_t buf[8] = {};

    // 32-bit aligned BE round-trip
    WRITE_DWORD_BE_A(buf, 0x12345678u);
    CHECK_EQ(READ_DWORD_BE_A(buf), 0x12345678u);

    // 16-bit aligned BE round-trip
    WRITE_WORD_BE_A(buf, 0xABCD);
    CHECK_EQ(READ_WORD_BE_A(buf), (uint16_t)0xABCD);

    // 64-bit aligned BE round-trip
    WRITE_QWORD_BE_A(buf, 0xFEDCBA9876543210ULL);
    CHECK_EQ(READ_QWORD_BE_A(buf), 0xFEDCBA9876543210ULL);

    // 32-bit unaligned BE round-trip
    memset(buf, 0, sizeof(buf));
    WRITE_DWORD_BE_U(buf, 0xDEADBEEFu);
    CHECK_EQ((uint32_t)READ_DWORD_BE_U(buf), 0xDEADBEEFu);

    // 64-bit unaligned BE round-trip
    WRITE_QWORD_BE_U(buf, 0x0102030405060708ULL);
    CHECK_EQ((uint64_t)READ_QWORD_BE_U(buf), 0x0102030405060708ULL);
}

static void test_read_write_roundtrip_le() {
    uint8_t buf[8] = {};

    // 32-bit aligned LE round-trip
    WRITE_DWORD_LE_A(buf, 0x12345678u);
    CHECK_EQ(READ_DWORD_LE_A(buf), 0x12345678u);

    // 16-bit aligned LE round-trip
    WRITE_WORD_LE_A(buf, 0xABCD);
    CHECK_EQ(READ_WORD_LE_A(buf), (uint16_t)0xABCD);

    // 64-bit aligned LE round-trip
    WRITE_QWORD_LE_A(buf, 0xFEDCBA9876543210ULL);
    CHECK_EQ(READ_QWORD_LE_A(buf), 0xFEDCBA9876543210ULL);

    // 32-bit unaligned LE round-trip
    memset(buf, 0, sizeof(buf));
    WRITE_DWORD_LE_U(buf, 0xDEADBEEFu);
    CHECK_EQ((uint32_t)READ_DWORD_LE_U(buf), 0xDEADBEEFu);

    // 64-bit unaligned LE round-trip
    WRITE_QWORD_LE_U(buf, 0x0102030405060708ULL);
    CHECK_EQ((uint64_t)READ_QWORD_LE_U(buf), 0x0102030405060708ULL);
}

/* ---- Inline function tests (read_mem, write_mem, etc.) ---- */
static void test_read_mem() {
    uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};

    // read 4 bytes (big-endian)
    CHECK_EQ(read_mem(buf, 4), 0xDEADBEEFu);

    // read 2 bytes (big-endian)
    CHECK_EQ(read_mem(buf, 2), (uint32_t)0xDEADu);

    // read 1 byte
    CHECK_EQ(read_mem(buf, 1), (uint32_t)0xDEu);
}

static void test_read_mem_rev() {
    uint8_t buf[] = {0xEF, 0xBE, 0xAD, 0xDE};

    // read 4 bytes (little-endian on LE host = no swap)
    CHECK_EQ(read_mem_rev(buf, 4), 0xDEADBEEFu);

    // read 2 bytes
    CHECK_EQ(read_mem_rev(buf, 2), (uint32_t)0xBEEFu);

    // read 1 byte
    CHECK_EQ(read_mem_rev(buf, 1), (uint32_t)0xEFu);
}

static void test_write_mem() {
    uint8_t buf[4] = {};

    // write 4 bytes big-endian
    write_mem(buf, 0xCAFEBABE, 4);
    CHECK_EQ(buf[0], 0xCA);
    CHECK_EQ(buf[1], 0xFE);
    CHECK_EQ(buf[2], 0xBA);
    CHECK_EQ(buf[3], 0xBE);

    // write 2 bytes big-endian
    memset(buf, 0, 4);
    write_mem(buf, 0x1234, 2);
    CHECK_EQ(buf[0], 0x12);
    CHECK_EQ(buf[1], 0x34);

    // write 1 byte
    memset(buf, 0, 4);
    write_mem(buf, 0xAB, 1);
    CHECK_EQ(buf[0], 0xAB);
}

static void test_write_mem_rev() {
    uint8_t buf[4] = {};

    // write 4 bytes little-endian
    write_mem_rev(buf, 0xCAFEBABE, 4);
    CHECK_EQ(buf[0], 0xBE);
    CHECK_EQ(buf[1], 0xBA);
    CHECK_EQ(buf[2], 0xFE);
    CHECK_EQ(buf[3], 0xCA);

    // write 2 bytes little-endian
    memset(buf, 0, 4);
    write_mem_rev(buf, 0x1234, 2);
    CHECK_EQ(buf[0], 0x34);
    CHECK_EQ(buf[1], 0x12);

    // write 1 byte
    memset(buf, 0, 4);
    write_mem_rev(buf, 0xAB, 1);
    CHECK_EQ(buf[0], 0xAB);
}

int main() {
    cout << "Running memaccess tests..." << endl;

    // Aligned big-endian reads
    test_read_word_be_a();
    test_read_dword_be_a();
    test_read_qword_be_a();

    // Aligned little-endian reads
    test_read_word_le_a();
    test_read_dword_le_a();
    test_read_qword_le_a();

    // Unaligned big-endian reads
    test_read_word_be_u();
    test_read_dword_be_u();
    test_read_qword_be_u();

    // Unaligned little-endian reads
    test_read_word_le_u();
    test_read_dword_le_u();
    test_read_qword_le_u();

    // Aligned big-endian writes
    test_write_word_be_a();
    test_write_dword_be_a();
    test_write_qword_be_a();

    // Unaligned big-endian writes
    test_write_word_be_u();
    test_write_dword_be_u();
    test_write_qword_be_u();

    // Aligned little-endian writes
    test_write_word_le_a();
    test_write_dword_le_a();
    test_write_qword_le_a();

    // Unaligned little-endian writes
    test_write_word_le_u();
    test_write_dword_le_u();
    test_write_qword_le_u();

    // Round-trip tests
    test_read_write_roundtrip_be();
    test_read_write_roundtrip_le();

    // Inline function tests
    test_read_mem();
    test_read_mem_rev();
    test_write_mem();
    test_write_mem_rev();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
