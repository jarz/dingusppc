/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for core/mathutils.h */

#include <core/mathutils.h>
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

static void test_u32xu64_variant1() {
    uint32_t hi;
    uint64_t lo;

    // 0 * 0 = 0
    _u32xu64(0u, 0ULL, hi, lo);
    CHECK_EQ(hi, 0u);
    CHECK_EQ(lo, 0ULL);

    // 1 * 1 = 1
    _u32xu64(1u, 1ULL, hi, lo);
    CHECK_EQ(hi, 0u);
    CHECK_EQ(lo, 1ULL);

    // 3 * 7 = 21
    _u32xu64(3u, 7ULL, hi, lo);
    CHECK_EQ(hi, 0u);
    CHECK_EQ(lo, 21ULL);

    // 0xFFFFFFFF * 2 = 0x1FFFFFFFE
    _u32xu64(0xFFFFFFFFu, 2ULL, hi, lo);
    CHECK_EQ(hi, 0u);
    CHECK_EQ(lo, 0x1FFFFFFFEULL);

    // max * max overflow
    _u32xu64(0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFULL, hi, lo);
    CHECK_EQ(hi, 0xFFFFFFFEu);
    CHECK_EQ(lo, 0xFFFFFFFF00000001ULL);
}

static void test_u32xu64_variant2() {
    uint64_t hi;
    uint32_t lo;

    // 0 * 0 = 0
    _u32xu64(0u, 0ULL, hi, lo);
    CHECK_EQ(hi, 0ULL);
    CHECK_EQ(lo, 0u);

    // 1 * 1 = 1
    _u32xu64(1u, 1ULL, hi, lo);
    CHECK_EQ(hi, 0ULL);
    CHECK_EQ(lo, 1u);

    // 5 * 10 = 50
    _u32xu64(5u, 10ULL, hi, lo);
    CHECK_EQ(hi, 0ULL);
    CHECK_EQ(lo, 50u);

    // overflow lo: 0xFFFFFFFF * 0x100000002
    _u32xu64(0xFFFFFFFFu, 0x100000002ULL, hi, lo);
    CHECK_EQ(lo, 0xFFFFFFFEu);
    CHECK_EQ(hi, 0x100000000ULL);
}

static void test_u64xu64() {
    uint64_t hi, lo;

    // 0 * 0 = 0
    _u64xu64(0ULL, 0ULL, hi, lo);
    CHECK_EQ(hi, 0ULL);
    CHECK_EQ(lo, 0ULL);

    // 1 * 1 = 1
    _u64xu64(1ULL, 1ULL, hi, lo);
    CHECK_EQ(hi, 0ULL);
    CHECK_EQ(lo, 1ULL);

    // 2^32 * 2^32 = 2^64
    _u64xu64(0x100000000ULL, 0x100000000ULL, hi, lo);
    CHECK_EQ(hi, 1ULL);
    CHECK_EQ(lo, 0ULL);

    // max * 1 = max
    _u64xu64(0xFFFFFFFFFFFFFFFFULL, 1ULL, hi, lo);
    CHECK_EQ(hi, 0ULL);
    CHECK_EQ(lo, 0xFFFFFFFFFFFFFFFFULL);

    // max * max
    _u64xu64(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, hi, lo);
    CHECK_EQ(hi, 0xFFFFFFFFFFFFFFFEULL);
    CHECK_EQ(lo, 0x0000000000000001ULL);

    // 100 * 200 = 20000
    _u64xu64(100ULL, 200ULL, hi, lo);
    CHECK_EQ(hi, 0ULL);
    CHECK_EQ(lo, 20000ULL);
}

int main() {
    cout << "Running mathutils tests..." << endl;

    test_u32xu64_variant1();
    test_u32xu64_variant2();
    test_u64xu64();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
