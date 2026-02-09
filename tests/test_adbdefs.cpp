/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for devices/common/adb/adbbus.h definitions */

#include <devices/common/adb/adbbus.h>
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
             << #expr << " => 0x" << hex << (uint64_t)(uint32_t)got_ \
             << ", expected 0x" << hex << (uint64_t)(uint32_t)exp_ << endl; \
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

static void test_adb_constants() {
    CHECK_EQ(ADB_MAX_DATA_SIZE, 8);
}

static void test_adb_status_flags() {
    CHECK_EQ(ADB_STAT_OK, 0);
    CHECK_EQ(ADB_STAT_SRQ_ACTIVE, 1 << 0);
    CHECK_EQ(ADB_STAT_TIMEOUT, 1 << 1);
    CHECK_EQ(ADB_STAT_AUTOPOLL, 1 << 6);

    // Flags should be distinct
    int all = ADB_STAT_SRQ_ACTIVE | ADB_STAT_TIMEOUT | ADB_STAT_AUTOPOLL;
    int count = 0;
    for (int v = all; v; v &= v - 1) count++;
    CHECK_EQ(count, 3);

    // Composite flags should work
    int combined = ADB_STAT_SRQ_ACTIVE | ADB_STAT_TIMEOUT;
    CHECK_TRUE((combined & ADB_STAT_SRQ_ACTIVE) != 0);
    CHECK_TRUE((combined & ADB_STAT_TIMEOUT) != 0);
    CHECK_TRUE((combined & ADB_STAT_AUTOPOLL) == 0);
}

int main() {
    cout << "Running adbdefs tests..." << endl;

    test_adb_constants();
    test_adb_status_flags();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
