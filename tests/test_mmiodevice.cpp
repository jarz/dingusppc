/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for devices/common/mmiodevice.h SIZE_ARG macro */

#include <devices/common/mmiodevice.h>
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
             << #expr << " => " << got_ \
             << ", expected " << exp_ << endl; \
        nfailed++; \
    } \
} while(0)

static void test_size_arg_standard() {
    // standard sizes map to named characters
    CHECK_EQ(SIZE_ARG(4), 'l');  // long = dword
    CHECK_EQ(SIZE_ARG(2), 'w');  // word
    CHECK_EQ(SIZE_ARG(1), 'b');  // byte
}

static void test_size_arg_nonstandard() {
    // non-standard sizes fall through to '0' + size
    CHECK_EQ(SIZE_ARG(3), '3');
    CHECK_EQ(SIZE_ARG(8), '8');
    CHECK_EQ(SIZE_ARG(0), '0');
}

int main() {
    cout << "Running mmiodevice tests..." << endl;

    test_size_arg_standard();
    test_size_arg_nonstandard();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
