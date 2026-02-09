/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for core/bitops.h */

#include <core/bitops.h>
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

static void test_extract_bits() {
    CHECK_EQ(extract_bits<uint32_t>(0xDEADBEEF, 0, 8), 0xEFu);
    CHECK_EQ(extract_bits<uint32_t>(0xDEADBEEF, 8, 8), 0xBEu);
    CHECK_EQ(extract_bits<uint32_t>(0xDEADBEEF, 16, 8), 0xADu);
    CHECK_EQ(extract_bits<uint32_t>(0xDEADBEEF, 24, 8), 0xDEu);
    CHECK_EQ(extract_bits<uint32_t>(0x80000000, 31, 1), 1u);
    CHECK_EQ(extract_bits<uint32_t>(0x80000000, 30, 1), 0u);
    CHECK_EQ(extract_bits<uint32_t>(0xDEADBEEF, 0, 32), 0xDEADBEEFu);
    CHECK_EQ(extract_bits<uint32_t>(0, 0, 16), 0u);
    CHECK_EQ(extract_bits<uint16_t>(0xABCD, 0, 8), (uint16_t)0xCD);
    CHECK_EQ(extract_bits<uint16_t>(0xABCD, 8, 8), (uint16_t)0xAB);
}

static void test_insert_bits() {
    uint32_t val;

    val = 0;
    insert_bits<uint32_t>(val, 0xFFu, 0, 8);
    CHECK_EQ(val, 0x000000FFu);

    val = 0;
    insert_bits<uint32_t>(val, 0xABu, 8, 8);
    CHECK_EQ(val, 0x0000AB00u);

    val = 0xFFFF0000u;
    insert_bits<uint32_t>(val, 0x12u, 0, 8);
    CHECK_EQ(val, 0xFFFF0012u);

    val = 0x12345678u;
    insert_bits<uint32_t>(val, 0xDEADBEEFu, 0, 32);
    CHECK_EQ(val, 0xDEADBEEFu);

    val = 0;
    insert_bits<uint32_t>(val, 1u, 31, 1);
    CHECK_EQ(val, 0x80000000u);
}

static void test_bit_set() {
    CHECK_TRUE(bit_set(1ULL, 0));
    CHECK_FALSE(bit_set(1ULL, 1));
    CHECK_TRUE(bit_set(0x8000000000000000ULL, 63));
    CHECK_FALSE(bit_set(0, 0));
    CHECK_TRUE(bit_set(0xFFFFFFFFFFFFFFFFULL, 32));
}

static void test_bit_changed() {
    CHECK_TRUE(bit_changed(0, 1, 0));
    CHECK_FALSE(bit_changed(0, 0, 0));
    CHECK_FALSE(bit_changed(1, 1, 0));
    CHECK_TRUE(bit_changed(0, 0x80000000, 31));
    CHECK_FALSE(bit_changed(0xFF, 0xFF, 7));
}

static void test_set_clear_bit() {
    uint32_t val = 0;
    set_bit<uint32_t>(val, 0);
    CHECK_EQ(val, 1u);

    set_bit<uint32_t>(val, 31);
    CHECK_EQ(val, 0x80000001u);

    clear_bit<uint32_t>(val, 0);
    CHECK_EQ(val, 0x80000000u);

    clear_bit<uint32_t>(val, 31);
    CHECK_EQ(val, 0u);

    uint64_t val64 = 0;
    set_bit<uint64_t>(val64, 63);
    CHECK_EQ(val64, 0x8000000000000000ULL);
    clear_bit<uint64_t>(val64, 63);
    CHECK_EQ(val64, 0ULL);
}

static void test_rotl_rotr() {
    CHECK_EQ(ROTL_32(0xDEADBEEF, 0), 0xDEADBEEFu);
    CHECK_EQ(ROTL_32(0xDEADBEEF, 4), 0xEADBEEFDu);
    CHECK_EQ(ROTL_32(0xDEADBEEF, 8), 0xADBEEFDEu);
    CHECK_EQ(ROTL_32(0xDEADBEEF, 16), 0xBEEFDEADu);

    CHECK_EQ(ROTR_32(0xDEADBEEF, 0), 0xDEADBEEFu);
    CHECK_EQ(ROTR_32(0xDEADBEEF, 4), 0xFDEADBEEu);
    CHECK_EQ(ROTR_32(0xDEADBEEF, 16), 0xBEEFDEADu);

    // ROTL and ROTR are inverses
    CHECK_EQ(ROTR_32(ROTL_32(0x12345678, 13), 13), 0x12345678u);
}

static void test_extract_with_wrap_around() {
    CHECK_EQ(extract_with_wrap_around(0xAABBCCDD, 0, 1), 0xAAu);
    CHECK_EQ(extract_with_wrap_around(0xAABBCCDD, 0, 2), 0xAABBu);
    CHECK_EQ(extract_with_wrap_around(0xAABBCCDD, 0, 4), 0xAABBCCDDu);
    CHECK_EQ(extract_with_wrap_around(0xAABBCCDD, 3, 2), 0xDDAAu);
}

int main() {
    cout << "Running bitops tests..." << endl;

    test_extract_bits();
    test_insert_bits();
    test_bit_set();
    test_bit_changed();
    test_set_clear_bit();
    test_rotl_rotr();
    test_extract_with_wrap_around();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
