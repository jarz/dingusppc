/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for AudioProcessor (TDA7433) I2C protocol.

    Tests the I2C audio processor register protocol: subaddress validation,
    register write and readback, auto-increment mode, first-byte routing,
    and transaction reset.
*/

#include <devices/sound/awacs.h>
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

#define CHECK_FALSE(expr) do { \
    ntested++; \
    if ((expr)) { \
        cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " \
             << #expr << " is true (expected false)" << endl; \
        nfailed++; \
    } \
} while(0)

// Test that valid subaddresses (0-6) are accepted
static void test_valid_subaddresses() {
    AudioProcessor ap;
    for (uint8_t addr = 0; addr <= 6; addr++) {
        ap.start_transaction();
        CHECK_TRUE(ap.send_subaddress(addr));
    }
}

// Test that invalid subaddresses (>6) are rejected with NACK
static void test_invalid_subaddresses() {
    AudioProcessor ap;
    for (uint8_t addr = 7; addr <= 15; addr++) {
        ap.start_transaction();
        CHECK_FALSE(ap.send_subaddress(addr));
    }
}

// Test register write and readback via send_byte/receive_byte
static void test_register_write_readback() {
    AudioProcessor ap;
    ap.start_transaction();
    ap.send_byte(0x02);        // subaddress = 2 (no auto-inc)
    ap.send_byte(0x55);        // write 0x55 to register 2

    // Read it back
    ap.start_transaction();
    ap.send_byte(0x02);        // subaddress = 2
    uint8_t data;
    CHECK_TRUE(ap.receive_byte(&data));
    CHECK_EQ(data, (uint8_t)0x55);
}

// Test auto-increment mode: bit 4 of subaddress enables auto-inc
static void test_auto_increment() {
    AudioProcessor ap;
    ap.start_transaction();
    // subaddress 0x10 = subaddr 0 with auto-increment flag (bit 4)
    ap.send_byte(0x10);        // subaddr=0, auto_inc=1
    ap.send_byte(0xAA);        // write 0xAA to reg 0, auto-inc to reg 1
    ap.send_byte(0xBB);        // write 0xBB to reg 1, auto-inc to reg 2
    ap.send_byte(0xCC);        // write 0xCC to reg 2

    // Verify reg 0
    ap.start_transaction();
    ap.send_byte(0x00);
    uint8_t data;
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0xAA);

    // Verify reg 1
    ap.start_transaction();
    ap.send_byte(0x01);
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0xBB);

    // Verify reg 2
    ap.start_transaction();
    ap.send_byte(0x02);
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0xCC);
}

// Test that first byte after transaction start routes to subaddress
static void test_first_byte_is_subaddress() {
    AudioProcessor ap;
    ap.start_transaction();
    // First byte should set subaddress, not write a register
    CHECK_TRUE(ap.send_byte(0x03));   // subaddress = 3
    CHECK_TRUE(ap.send_byte(0x77));   // write 0x77 to reg 3

    // Read back reg 3
    ap.start_transaction();
    ap.send_byte(0x03);
    uint8_t data;
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0x77);
}

// Test that without auto-increment, writes go to the same register
static void test_no_auto_increment() {
    AudioProcessor ap;
    ap.start_transaction();
    ap.send_byte(0x04);        // subaddr=4, no auto-inc
    ap.send_byte(0x11);        // write 0x11 to reg 4
    ap.send_byte(0x22);        // overwrites reg 4 with 0x22

    // Verify reg 4 has the last value
    ap.start_transaction();
    ap.send_byte(0x04);
    uint8_t data;
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0x22);

    // Verify reg 5 is untouched (still 0)
    ap.start_transaction();
    ap.send_byte(0x05);
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0x00);
}

// Test auto-increment NACKs when incrementing past register 6
static void test_auto_increment_past_end() {
    AudioProcessor ap;
    ap.start_transaction();
    // Start at subaddr 5, auto-inc
    ap.send_byte(0x15);        // subaddr=5, auto_inc=1
    CHECK_TRUE(ap.send_byte(0xAA));   // reg 5 OK
    CHECK_TRUE(ap.send_byte(0xBB));   // reg 6 OK
    CHECK_FALSE(ap.send_byte(0xCC));  // reg 7 -> NACK (invalid)
}

// Test that start_transaction resets internal position
static void test_transaction_reset() {
    AudioProcessor ap;
    ap.start_transaction();
    ap.send_byte(0x00);       // subaddress 0
    ap.send_byte(0xFF);       // write 0xFF to reg 0

    // After new transaction, pos resets so first byte is subaddress again
    ap.start_transaction();
    CHECK_TRUE(ap.send_byte(0x01));   // this should be treated as subaddress, not data
    ap.send_byte(0xEE);              // write to reg 1

    // Verify reg 0 still has 0xFF (wasn't overwritten by "0x01")
    ap.start_transaction();
    ap.send_byte(0x00);
    uint8_t data;
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0xFF);

    // Verify reg 1 has 0xEE
    ap.start_transaction();
    ap.send_byte(0x01);
    ap.receive_byte(&data);
    CHECK_EQ(data, (uint8_t)0xEE);
}

int main() {
    cout << "Running AudioProcessor (TDA7433) tests..." << endl;

    test_valid_subaddresses();
    test_invalid_subaddresses();
    test_register_write_readback();
    test_auto_increment();
    test_first_byte_is_subaddress();
    test_no_auto_increment();
    test_auto_increment_past_end();
    test_transaction_reset();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
