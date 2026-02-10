/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for devices/common/hwcomponent.h */

#include <devices/common/hwcomponent.h>
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

static void test_hwcomponent_name() {
    HWComponent comp;

    // default name is empty
    CHECK_EQ(comp.get_name(), string(""));

    // set and get name
    comp.set_name("TestDevice");
    CHECK_EQ(comp.get_name(), string("TestDevice"));

    // change name
    comp.set_name("AnotherDevice");
    CHECK_EQ(comp.get_name(), string("AnotherDevice"));
}

static void test_hwcomponent_supports_type() {
    HWComponent comp;

    // default supports nothing (UNKNOWN = 0)
    CHECK_FALSE(comp.supports_type(HWCompType::MEM_CTRL));
    CHECK_FALSE(comp.supports_type(HWCompType::PCI_DEV));

    // set a single type
    comp.supports_types(HWCompType::PCI_DEV);
    CHECK_TRUE(comp.supports_type(HWCompType::PCI_DEV));
    CHECK_FALSE(comp.supports_type(HWCompType::MEM_CTRL));

    // set multiple types via bitwise OR
    comp.supports_types(HWCompType::PCI_DEV | HWCompType::MMIO_DEV);
    CHECK_TRUE(comp.supports_type(HWCompType::PCI_DEV));
    CHECK_TRUE(comp.supports_type(HWCompType::MMIO_DEV));
    CHECK_FALSE(comp.supports_type(HWCompType::RAM));
}

static void test_hwcomponent_device_postinit() {
    HWComponent comp;

    // default postinit returns 0
    CHECK_EQ(comp.device_postinit(), 0);
}

int main() {
    cout << "Running hwcomponent tests..." << endl;

    test_hwcomponent_name();
    test_hwcomponent_supports_type();
    test_hwcomponent_device_postinit();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
