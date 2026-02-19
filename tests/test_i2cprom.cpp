/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for I2CProm — a generic I2C EEPROM device.

    Tests the I2C PROM protocol: subaddress positioning, sequential reads
    with auto-increment, wrap-around at ROM boundary, fill/set memory,
    and transaction reset.
*/

#include <devices/common/i2c/i2cprom.h>
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
             << #expr << " => " << (int)got_ \
             << ", expected " << (int)exp_ << endl; \
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

// Test that set_memory populates storage and receive_byte reads it back
static void test_set_and_read() {
    I2CProm prom(0xA0, 16);
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    prom.set_memory(0, data, 4);
    prom.start_transaction();

    uint8_t byte;
    for (int i = 0; i < 4; i++) {
        CHECK_TRUE(prom.receive_byte(&byte));
        CHECK_EQ(byte, data[i]);
    }
}

// Test that fill_memory writes a repeating byte
static void test_fill_and_read() {
    I2CProm prom(0xA0, 8);
    prom.fill_memory(0, 8, 0x42);
    prom.start_transaction();

    uint8_t byte;
    for (int i = 0; i < 8; i++) {
        CHECK_TRUE(prom.receive_byte(&byte));
        CHECK_EQ(byte, (uint8_t)0x42);
    }
}

// Test that send_subaddress positions the read pointer
static void test_subaddress_positioning() {
    I2CProm prom(0xA0, 8);
    const uint8_t data[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    prom.set_memory(0, data, 8);

    prom.start_transaction();
    prom.send_subaddress(5);  // skip to offset 5

    uint8_t byte;
    CHECK_TRUE(prom.receive_byte(&byte));
    CHECK_EQ(byte, (uint8_t)0x60);  // data[5]
    CHECK_TRUE(prom.receive_byte(&byte));
    CHECK_EQ(byte, (uint8_t)0x70);  // data[6]
}

// Test that sequential reads auto-increment and wrap around at ROM end
static void test_wrap_around() {
    I2CProm prom(0xA0, 4);
    const uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    prom.set_memory(0, data, 4);

    prom.start_transaction();
    prom.send_subaddress(2);  // start at offset 2

    uint8_t byte;
    CHECK_TRUE(prom.receive_byte(&byte));
    CHECK_EQ(byte, (uint8_t)0xCC);  // data[2]
    CHECK_TRUE(prom.receive_byte(&byte));
    CHECK_EQ(byte, (uint8_t)0xDD);  // data[3]
    // Next read should wrap around to 0
    CHECK_TRUE(prom.receive_byte(&byte));
    CHECK_EQ(byte, (uint8_t)0xAA);  // data[0] — wrapped
    CHECK_TRUE(prom.receive_byte(&byte));
    CHECK_EQ(byte, (uint8_t)0xBB);  // data[1]
}

// Test that start_transaction resets position to 0
static void test_transaction_reset() {
    I2CProm prom(0xA0, 4);
    const uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
    prom.set_memory(0, data, 4);

    prom.start_transaction();
    prom.send_subaddress(3);  // position at end

    uint8_t byte;
    prom.receive_byte(&byte);
    CHECK_EQ(byte, (uint8_t)0x44);

    // Reset via new transaction
    prom.start_transaction();
    prom.receive_byte(&byte);
    CHECK_EQ(byte, (uint8_t)0x11);  // back to data[0]
}

// Test partial fill — only fills the specified range
static void test_partial_fill() {
    I2CProm prom(0xA0, 8);
    prom.fill_memory(0, 8, 0x00);     // clear all
    prom.fill_memory(2, 3, 0xFF);     // fill bytes 2,3,4

    prom.start_transaction();
    uint8_t byte;
    prom.receive_byte(&byte); CHECK_EQ(byte, (uint8_t)0x00);  // [0]
    prom.receive_byte(&byte); CHECK_EQ(byte, (uint8_t)0x00);  // [1]
    prom.receive_byte(&byte); CHECK_EQ(byte, (uint8_t)0xFF);  // [2]
    prom.receive_byte(&byte); CHECK_EQ(byte, (uint8_t)0xFF);  // [3]
    prom.receive_byte(&byte); CHECK_EQ(byte, (uint8_t)0xFF);  // [4]
    prom.receive_byte(&byte); CHECK_EQ(byte, (uint8_t)0x00);  // [5]
}

int main() {
    cout << "Running I2CProm tests..." << endl;

    test_set_and_read();
    test_fill_and_read();
    test_subaddress_positioning();
    test_wrap_around();
    test_transaction_reset();
    test_partial_fill();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
