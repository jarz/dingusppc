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

static void test_hwcomptype_values() {
    // Verify enum values are distinct powers of 2
    CHECK_EQ((uint64_t)HWCompType::UNKNOWN, 0ULL);
    CHECK_EQ((uint64_t)HWCompType::MEM_CTRL, 1ULL << 0);
    CHECK_EQ((uint64_t)HWCompType::NVRAM, 1ULL << 1);
    CHECK_EQ((uint64_t)HWCompType::ROM, 1ULL << 2);
    CHECK_EQ((uint64_t)HWCompType::RAM, 1ULL << 3);
    CHECK_EQ((uint64_t)HWCompType::MMIO_DEV, 1ULL << 4);
    CHECK_EQ((uint64_t)HWCompType::PCI_HOST, 1ULL << 5);
    CHECK_EQ((uint64_t)HWCompType::PCI_DEV, 1ULL << 6);
    CHECK_EQ((uint64_t)HWCompType::INT_CTRL, 1ULL << 16);
    CHECK_EQ((uint64_t)HWCompType::SND_CODEC, 1ULL << 30);
    CHECK_EQ((uint64_t)HWCompType::FLOPPY_CTRL, 1ULL << 32);
    CHECK_EQ((uint64_t)HWCompType::ETHER_MAC, 1ULL << 40);
    CHECK_EQ((uint64_t)HWCompType::MACHINE, 1ULL << 41);
}

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

    test_hwcomptype_values();
    test_hwcomponent_name();
    test_hwcomponent_supports_type();
    test_hwcomponent_device_postinit();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
