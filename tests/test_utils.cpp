/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for pure header-only utility functions.

Tests cover:
  - endianswap.h: BYTESWAP_16/32/64
  - core/bitops.h: ROTL_32, ROTR_32, extract_bits, insert_bits,
                   bit_changed, bit_set, extract_with_wrap_around
  - core/mathutils.h: _u32xu64, _u64xu64
  - memaccess.h: read_mem, write_mem (big-endian helpers)

No ROMs, SDL, or hardware required.
*/

#include <core/bitops.h>
#include <core/mathutils.h>
#include <endianswap.h>
#include <memaccess.h>

#include <cstring>
#include <cstdint>
#include <iostream>

using namespace std;

static int tests_run    = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << endl; \
        tests_failed++; \
    } \
} while(0)

// ============================================================
// endianswap tests
// ============================================================

static void test_byteswap_16() {
    TEST_ASSERT(BYTESWAP_16(0x1234U)  == 0x3412U, "BYTESWAP_16(0x1234)");
    TEST_ASSERT(BYTESWAP_16(0x0001U)  == 0x0100U, "BYTESWAP_16(0x0001)");
    TEST_ASSERT(BYTESWAP_16(0xFF00U)  == 0x00FFU, "BYTESWAP_16(0xFF00)");
    TEST_ASSERT(BYTESWAP_16(0x0000U)  == 0x0000U, "BYTESWAP_16(0x0000)");
    TEST_ASSERT(BYTESWAP_16(0xFFFFU)  == 0xFFFFU, "BYTESWAP_16(0xFFFF)");
}

static void test_byteswap_32() {
    TEST_ASSERT(BYTESWAP_32(0x12345678UL) == 0x78563412UL, "BYTESWAP_32(0x12345678)");
    TEST_ASSERT(BYTESWAP_32(0x00000001UL) == 0x01000000UL, "BYTESWAP_32(0x00000001)");
    TEST_ASSERT(BYTESWAP_32(0xFF000000UL) == 0x000000FFUL, "BYTESWAP_32(0xFF000000)");
    TEST_ASSERT(BYTESWAP_32(0x00000000UL) == 0x00000000UL, "BYTESWAP_32(0)");
    TEST_ASSERT(BYTESWAP_32(0xFFFFFFFFUL) == 0xFFFFFFFFUL, "BYTESWAP_32(0xFFFFFFFF)");
}

static void test_byteswap_64() {
    TEST_ASSERT(BYTESWAP_64(0x0102030405060708ULL) == 0x0807060504030201ULL,
        "BYTESWAP_64(0x0102030405060708)");
    TEST_ASSERT(BYTESWAP_64(0x0000000000000001ULL) == 0x0100000000000000ULL,
        "BYTESWAP_64(0x0000000000000001)");
    TEST_ASSERT(BYTESWAP_64(0x0000000000000000ULL) == 0x0000000000000000ULL,
        "BYTESWAP_64(0)");
    TEST_ASSERT(BYTESWAP_64(0xFFFFFFFFFFFFFFFFULL) == 0xFFFFFFFFFFFFFFFFULL,
        "BYTESWAP_64(0xFFFFFFFFFFFFFFFF)");
    // double-swap is identity
    uint64_t v = 0x0807060504030201ULL;
    TEST_ASSERT(BYTESWAP_64(BYTESWAP_64(v)) == v, "BYTESWAP_64 is its own inverse");
}

// ============================================================
// bitops tests
// ============================================================

static void test_rotl_32() {
    TEST_ASSERT(ROTL_32(0x00000001U, 1)  == 0x00000002U, "ROTL_32(1, 1)");
    TEST_ASSERT(ROTL_32(0x80000000U, 1)  == 0x00000001U, "ROTL_32(0x80000000, 1) wraps");
    TEST_ASSERT(ROTL_32(0x12345678U, 0)  == 0x12345678U, "ROTL_32 by 0 is identity");
    TEST_ASSERT(ROTL_32(0x12345678U, 4)  == 0x23456781U, "ROTL_32 by 4 nibble");
    TEST_ASSERT(ROTL_32(0x00000001U, 31) == 0x80000000U, "ROTL_32(1, 31)");
}

static void test_rotr_32() {
    TEST_ASSERT(ROTR_32(0x00000002U, 1)  == 0x00000001U, "ROTR_32(2, 1)");
    TEST_ASSERT(ROTR_32(0x00000001U, 1)  == 0x80000000U, "ROTR_32(1, 1) wraps");
    TEST_ASSERT(ROTR_32(0x12345678U, 0)  == 0x12345678U, "ROTR_32 by 0 is identity");
    TEST_ASSERT(ROTR_32(0x23456781U, 4)  == 0x12345678U, "ROTR_32 by 4 nibble");
    TEST_ASSERT(ROTR_32(0x80000000U, 31) == 0x00000001U, "ROTR_32(0x80000000, 31)");
}

static void test_rotl_rotr_inverse() {
    const uint32_t vals[]   = { 0x12345678U, 0xABCDEF01U, 0x00000001U, 0xFFFFFFFFU };
    const unsigned shifts[] = { 0, 1, 4, 7, 15, 16, 17, 31 };
    for (auto v : vals) {
        for (auto n : shifts) {
            TEST_ASSERT(ROTR_32(ROTL_32(v, n), n) == v,
                "ROTR_32(ROTL_32(v, n), n) == v");
        }
    }
}

static void test_extract_bits_uint32() {
    TEST_ASSERT(extract_bits<uint32_t>(0x000000FFU, 0, 8) == 0xFFU,
        "extract_bits: low byte");
    TEST_ASSERT(extract_bits<uint32_t>(0x0000FF00U, 8, 8) == 0xFFU,
        "extract_bits: second byte");
    TEST_ASSERT(extract_bits<uint32_t>(0x00000007U, 0, 3) == 0x7U,
        "extract_bits: 3 low bits");
    TEST_ASSERT(extract_bits<uint32_t>(0x000000FFU, 1, 3) == 0x7U,
        "extract_bits: bits [3:1] of 0xFF");
    TEST_ASSERT(extract_bits<uint32_t>(0xFFFFFFFFU, 0, 32) == 0xFFFFFFFFU,
        "extract_bits: all 32 bits");
    TEST_ASSERT(extract_bits<uint32_t>(0x00000000U, 0, 32) == 0x00000000U,
        "extract_bits: all-zero 32 bits");
}

static void test_insert_bits_uint32() {
    uint32_t val = 0;
    insert_bits<uint32_t>(val, 0xFFU, 0, 8);
    TEST_ASSERT(val == 0x000000FFU, "insert_bits: byte at pos 0");

    val = 0;
    insert_bits<uint32_t>(val, 0xFFU, 8, 8);
    TEST_ASSERT(val == 0x0000FF00U, "insert_bits: byte at pos 8");

    // Clears only the target field in an all-ones value
    val = 0xFFFFFFFFU;
    insert_bits<uint32_t>(val, 0x00U, 4, 8);
    TEST_ASSERT(val == 0xFFFFF00FU, "insert_bits: clear 8-bit field at pos 4");

    // insert-then-extract roundtrip
    val = 0;
    insert_bits<uint32_t>(val, 0xA5U, 8, 8);
    TEST_ASSERT(extract_bits<uint32_t>(val, 8, 8) == 0xA5U,
        "insert_bits / extract_bits roundtrip");
}

static void test_bit_changed() {
    TEST_ASSERT( bit_changed(0x00U, 0x01U, 0), "bit_changed: bit 0 set");
    TEST_ASSERT(!bit_changed(0x02U, 0x02U, 0), "bit_changed: bit 0 unchanged");
    TEST_ASSERT( bit_changed(0xFFU, 0x00U, 7), "bit_changed: bit 7 cleared");
    TEST_ASSERT(!bit_changed(0xAAU, 0xAAU, 3), "bit_changed: identical values bit 3");
}

static void test_bit_set() {
    TEST_ASSERT( bit_set(0x01ULL, 0), "bit_set: bit 0 in 0x01");
    TEST_ASSERT(!bit_set(0x01ULL, 1), "bit_set: bit 1 not in 0x01");
    TEST_ASSERT( bit_set(0x80ULL, 7), "bit_set: bit 7 in 0x80");
    TEST_ASSERT( bit_set(0xFFFFFFFFFFFFFFFFULL, 63), "bit_set: bit 63 in all-ones");
    TEST_ASSERT(!bit_set(0x00ULL, 0), "bit_set: no bit set in 0");
}

// ============================================================
// mathutils tests
// ============================================================

static void test_u32xu64_hi32_lo64() {
    uint32_t hi; uint64_t lo;

    _u32xu64(0U, 0xFFFFFFFFFFFFFFFFULL, hi, lo);
    TEST_ASSERT(lo == 0ULL && hi == 0U, "0 * MAX = 0");

    _u32xu64(1U, 1ULL, hi, lo);
    TEST_ASSERT(lo == 1ULL && hi == 0U, "1 * 1 = 1");

    _u32xu64(2U, 3ULL, hi, lo);
    TEST_ASSERT(lo == 6ULL && hi == 0U, "2 * 3 = 6");

    // 0xFFFFFFFF * 0xFFFFFFFF = 0xFFFFFFFE_00000001
    _u32xu64(0xFFFFFFFFU, 0xFFFFFFFFULL, hi, lo);
    TEST_ASSERT(lo == 0xFFFFFFFE00000001ULL && hi == 0U,
        "0xFFFFFFFF * 0xFFFFFFFF low 96 bits");

    // overflow: 0xFFFFFFFF * 2^33 = 0x1_FFFFFFFE_00000000 => hi=1, lo=0xFFFFFFFE00000000
    _u32xu64(0xFFFFFFFFU, 0x200000000ULL, hi, lo);
    TEST_ASSERT(hi == 1U && lo == 0xFFFFFFFE00000000ULL,
        "0xFFFFFFFF * 2^33 carries 1 into hi32");
}

static void test_u64xu64() {
    uint64_t hi, lo;

    _u64xu64(0ULL, 0xFFFFFFFFFFFFFFFFULL, hi, lo);
    TEST_ASSERT(lo == 0ULL && hi == 0ULL, "0 * MAX = 0");

    _u64xu64(1ULL, 1ULL, hi, lo);
    TEST_ASSERT(lo == 1ULL && hi == 0ULL, "1 * 1 = 1");

    _u64xu64(1ULL, 0xFFFFFFFFFFFFFFFFULL, hi, lo);
    TEST_ASSERT(lo == 0xFFFFFFFFFFFFFFFFULL && hi == 0ULL,
        "1 * MAX_U64 = MAX_U64");

    // 2^32 * 2^32 = 2^64 => hi=1, lo=0
    _u64xu64(0x100000000ULL, 0x100000000ULL, hi, lo);
    TEST_ASSERT(lo == 0ULL && hi == 1ULL, "2^32 * 2^32 = 2^64");
}

// ============================================================
// memaccess tests
// ============================================================

static void test_read_mem_be() {
    uint8_t buf4[4] = {0x12, 0x34, 0x56, 0x78};
    TEST_ASSERT(read_mem(buf4, 4) == 0x12345678U, "read_mem 4 bytes BE");

    uint8_t buf2[2] = {0xAB, 0xCD};
    TEST_ASSERT(read_mem(buf2, 2) == 0xABCDU, "read_mem 2 bytes BE");

    uint8_t buf1[1] = {0xEF};
    TEST_ASSERT(read_mem(buf1, 1) == 0xEFU, "read_mem 1 byte");
}

static void test_write_mem_be() {
    uint8_t buf4[4] = {};
    write_mem(buf4, 0x12345678U, 4);
    TEST_ASSERT(buf4[0] == 0x12 && buf4[1] == 0x34 &&
                buf4[2] == 0x56 && buf4[3] == 0x78,
        "write_mem 4 bytes BE");

    uint8_t buf2[2] = {};
    write_mem(buf2, 0xABCDU, 2);
    TEST_ASSERT(buf2[0] == 0xAB && buf2[1] == 0xCD, "write_mem 2 bytes BE");

    uint8_t buf1[1] = {};
    write_mem(buf1, 0xEFU, 1);
    TEST_ASSERT(buf1[0] == 0xEF, "write_mem 1 byte");
}

static void test_read_write_roundtrip() {
    uint8_t buf[4] = {};
    write_mem(buf, 0xDEADBEEFU, 4);
    TEST_ASSERT(read_mem(buf, 4) == 0xDEADBEEFU, "write_mem/read_mem 4-byte roundtrip");

    write_mem(buf, 0xCAFEU, 2);
    TEST_ASSERT(read_mem(buf, 2) == 0xCAFEU, "write_mem/read_mem 2-byte roundtrip");
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "Utility function tests" << endl;
    cout << "======================" << endl;

    cout << endl << "Endianswap tests:" << endl;
    test_byteswap_16();
    test_byteswap_32();
    test_byteswap_64();

    cout << endl << "Bitops tests:" << endl;
    test_rotl_32();
    test_rotr_32();
    test_rotl_rotr_inverse();
    test_extract_bits_uint32();
    test_insert_bits_uint32();
    test_bit_changed();
    test_bit_set();

    cout << endl << "Mathutils tests:" << endl;
    test_u32xu64_hi32_lo64();
    test_u64xu64();

    cout << endl << "Memaccess tests:" << endl;
    test_read_mem_be();
    test_write_mem_be();
    test_read_write_roundtrip();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;

    return tests_failed ? 1 : 0;
}
