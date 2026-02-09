/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for endianswap.h */

#include <endianswap.h>
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

static void test_byteswap_16() {
    CHECK_EQ(BYTESWAP_16(0x0000), (uint16_t)0x0000);
    CHECK_EQ(BYTESWAP_16(0x0102), (uint16_t)0x0201);
    CHECK_EQ(BYTESWAP_16(0xAABB), (uint16_t)0xBBAA);
    CHECK_EQ(BYTESWAP_16(0xFFFF), (uint16_t)0xFFFF);
    CHECK_EQ(BYTESWAP_16(0xFF00), (uint16_t)0x00FF);
    // double swap is identity
    CHECK_EQ(BYTESWAP_16(BYTESWAP_16(0x1234)), (uint16_t)0x1234);
}

static void test_byteswap_32() {
    CHECK_EQ(BYTESWAP_32(0x00000000u), 0x00000000u);
    CHECK_EQ(BYTESWAP_32(0x01020304u), 0x04030201u);
    CHECK_EQ(BYTESWAP_32(0xDEADBEEFu), 0xEFBEADDEu);
    CHECK_EQ(BYTESWAP_32(0xFFFFFFFFu), 0xFFFFFFFFu);
    CHECK_EQ(BYTESWAP_32(0xFF000000u), 0x000000FFu);
    // double swap is identity
    CHECK_EQ(BYTESWAP_32(BYTESWAP_32(0x12345678u)), 0x12345678u);
}

static void test_byteswap_64() {
    CHECK_EQ(BYTESWAP_64(0x0000000000000000ULL), 0x0000000000000000ULL);
    CHECK_EQ(BYTESWAP_64(0x0102030405060708ULL), 0x0807060504030201ULL);
    CHECK_EQ(BYTESWAP_64(0xDEADBEEFCAFEBABEULL), 0xBEBAFECAEFBEADDEULL);
    CHECK_EQ(BYTESWAP_64(0xFFFFFFFFFFFFFFFFULL), 0xFFFFFFFFFFFFFFFFULL);
    CHECK_EQ(BYTESWAP_64(0xFF00000000000000ULL), 0x00000000000000FFULL);
    // double swap is identity
    CHECK_EQ(BYTESWAP_64(BYTESWAP_64(0x123456789ABCDEF0ULL)), 0x123456789ABCDEF0ULL);
}

static void test_byteswap_sized() {
    CHECK_EQ(BYTESWAP_SIZED(0x0102, 2), (uint64_t)0x0201);
    CHECK_EQ(BYTESWAP_SIZED(0x01020304u, 4), (uint64_t)0x04030201u);
    CHECK_EQ(BYTESWAP_SIZED(0xAB, 1), (uint64_t)0xAB);
}

int main() {
    cout << "Running endianswap tests..." << endl;

    test_byteswap_16();
    test_byteswap_32();
    test_byteswap_64();
    test_byteswap_sized();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
