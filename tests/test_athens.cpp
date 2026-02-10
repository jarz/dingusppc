/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for AthensClocks PLL frequency computation.

    The Athens clock generator computes dot clock frequency from register values:
      dot_freq = xtal_freq * (N2 / (D2 * post_div))
    where post_div = 1 << (3 - (P2_MUX2 & 3)).

    This tests real PLL math, not just constants.
*/

#include <devices/common/clockgen/athens.h>
#include <cinttypes>
#include <cmath>
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

// Tolerance check for frequency values (within 1 Hz)
#define CHECK_FREQ_NEAR(expr, expected, tol) do { \
    ntested++; \
    int got_ = (expr); \
    int exp_ = (expected); \
    if (abs(got_ - exp_) > (tol)) { \
        cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " \
             << #expr << " => " << got_ \
             << ", expected " << exp_ << " (±" << (tol) << ")" << endl; \
        nfailed++; \
    } \
} while(0)

/* ---- Athens I2C register protocol ---- */

static void test_athens_i2c_protocol() {
    AthensClocks clk(0x28);

    // receive_byte should return the device ID
    uint8_t id = 0;
    clk.start_transaction();
    CHECK_TRUE(clk.receive_byte(&id));
    CHECK_EQ(id, (uint8_t)0x41);
}

static void test_athens_register_write() {
    AthensClocks clk(0x28);

    // Write register: first byte is reg number, second is data
    clk.start_transaction();
    CHECK_TRUE(clk.send_byte(AthensRegs::D2));   // select register D2
    CHECK_TRUE(clk.send_byte(14));                // write value 14

    clk.start_transaction();
    CHECK_TRUE(clk.send_byte(AthensRegs::N2));    // select register N2
    CHECK_TRUE(clk.send_byte(56));                // write value 56
}

static void test_athens_invalid_register() {
    AthensClocks clk(0x28);

    clk.start_transaction();
    CHECK_TRUE(clk.send_byte(ATHENS_NUM_REGS));   // invalid register number
    // Writing data to invalid register should return NACK (false)
    CHECK_TRUE(!clk.send_byte(0xFF));
}

/* ---- Dot clock frequency computation ---- */

static void test_athens_dot_freq_vco() {
    // Test the VCO mode (mux=0): freq = xtal * N2 / (D2 * post_div)
    // Use a known crystal frequency for easier math
    const float xtal = 31334400.0f;
    AthensClocks clk(0x28, xtal);

    // Set D2=14, N2=56, P2_MUX2=0x03 (mux=0 VCO, post_div = 1<<(3-3) = 1)
    clk.start_transaction();
    clk.send_byte(AthensRegs::D2);
    clk.send_byte(14);

    clk.start_transaction();
    clk.send_byte(AthensRegs::N2);
    clk.send_byte(56);

    clk.start_transaction();
    clk.send_byte(AthensRegs::P2_MUX2);
    clk.send_byte(0x03);  // mux=0 (bits 5:4 = 00), post_div bits = 3 -> post=1

    int freq = clk.get_dot_freq();
    // Expected: 31334400 * 56 / (14 * 1) = 31334400 * 4 = 125337600
    int expected = static_cast<int>(xtal * 56.0f / (14.0f * 1.0f) + 0.5f);
    CHECK_FREQ_NEAR(freq, expected, 1);
}

static void test_athens_dot_freq_vco_with_postdiv() {
    const float xtal = 31334400.0f;
    AthensClocks clk(0x28, xtal);

    // D2=7, N2=28, P2_MUX2=0x01 (mux=0 VCO, post_div = 1<<(3-1) = 4)
    clk.start_transaction();
    clk.send_byte(AthensRegs::D2);
    clk.send_byte(7);

    clk.start_transaction();
    clk.send_byte(AthensRegs::N2);
    clk.send_byte(28);

    clk.start_transaction();
    clk.send_byte(AthensRegs::P2_MUX2);
    clk.send_byte(0x01);  // mux=0, post_div = 1<<(3-1)=4

    int freq = clk.get_dot_freq();
    // Expected: 31334400 * 28 / (7 * 4) = 31334400
    int expected = static_cast<int>(xtal * 28.0f / (7.0f * 4.0f) + 0.5f);
    CHECK_FREQ_NEAR(freq, expected, 1);
}

static void test_athens_dot_freq_crystal_mode() {
    const float xtal = 31334400.0f;
    AthensClocks clk(0x28, xtal);

    // mux=2 (crystal mode): freq = xtal / post_div
    // P2_MUX2 = 0x22 -> mux = (0x22 >> 4) & 3 = 2, post_div = 1<<(3-(0x22&3)) = 1<<1 = 2
    clk.start_transaction();
    clk.send_byte(AthensRegs::P2_MUX2);
    clk.send_byte(0x22);

    int freq = clk.get_dot_freq();
    int expected = static_cast<int>(xtal / 2.0f + 0.5f);
    CHECK_FREQ_NEAR(freq, expected, 1);
}

static void test_athens_dot_freq_disabled() {
    AthensClocks clk(0x28);

    // When bit 7 of P2_MUX2 is set, dot clock is disabled (returns 0)
    clk.start_transaction();
    clk.send_byte(AthensRegs::P2_MUX2);
    clk.send_byte(0x82);  // bit 7 set = disabled

    CHECK_EQ(clk.get_dot_freq(), 0);
}

static void test_athens_custom_crystal() {
    // Test with a different crystal frequency
    const float xtal = 14318180.0f;
    AthensClocks clk(0x28, xtal);

    // Crystal mode, post_div=1 (P2_MUX2 bits 1:0 = 3)
    clk.start_transaction();
    clk.send_byte(AthensRegs::P2_MUX2);
    clk.send_byte(0x23);  // mux=2, post=1

    int freq = clk.get_dot_freq();
    int expected = static_cast<int>(xtal + 0.5f);
    CHECK_FREQ_NEAR(freq, expected, 1);
}

int main() {
    cout << "Running Athens clock tests..." << endl;

    test_athens_i2c_protocol();
    test_athens_register_write();
    test_athens_invalid_register();
    test_athens_dot_freq_vco();
    test_athens_dot_freq_vco_with_postdiv();
    test_athens_dot_freq_crystal_mode();
    test_athens_dot_freq_disabled();
    test_athens_custom_crystal();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
