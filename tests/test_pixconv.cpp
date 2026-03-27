/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for the pixel format conversion math used in videoctrl.cpp.

    These test the pure conversion formulas that map packed pixel values
    to ARGB8888.  The formulas are extracted directly from the conversion
    loops in VideoCtrlBase — what we're testing is that the bit manipulation
    correctly expands lower-bit-depth channels to 8-bit with proper rounding.

    Conversions tested:
      - RGB332 → ARGB (3-bit R replicated, 3-bit G replicated, 2-bit B replicated)
      - RGB555 → ARGB (5-bit channels expanded to 8-bit)
      - RGB565 → ARGB (5/6/5 channels expanded to 8-bit)
*/

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
             << #expr << " => 0x" << hex << (unsigned)got_ \
             << ", expected 0x" << hex << (unsigned)exp_ << endl; \
        nfailed++; \
    } \
} while(0)

// --- Extracted conversion formulas from videoctrl.cpp ---

// RGB332 → ARGB8888  (from convert_frame_8bpp)
// 8-bit pixel: RRRGGBB0
// R: 3 bits [7:5] replicated to fill 8 bits
// G: 3 bits [4:2] replicated to fill 8 bits
// B: 2 bits [1:0] replicated to fill 8 bits
static uint32_t rgb332_to_argb(uint8_t c) {
    uint32_t r = ((c << 16) & 0x00E00000) | ((c << 13) & 0x001C0000) | ((c << 10) & 0x00030000);
    uint32_t g = ((c << 11) & 0x0000E000) | ((c <<  8) & 0x00001C00) | ((c <<  5) & 0x00000300);
    uint32_t b = ((c <<  6) & 0x000000C0) | ((c <<  4) & 0x00000030) | ((c <<  2) & 0x0000000C) | (c & 0x00000003);
    return r | g | b;
}

// RGB555 → ARGB8888  (from convert_frame_15bpp)
// 16-bit pixel: 0RRRRRGGGGGBBBBB
// Each 5-bit channel expanded to 8 bits (top 5 + top 3 as low bits)
static uint32_t rgb555_to_argb(uint16_t c) {
    uint32_t r = ((c << 9) & 0x00F80000) | ((c << 4) & 0x00070000);
    uint32_t g = ((c << 6) & 0x0000F800) | ((c << 1) & 0x00000700);
    uint32_t b = ((c << 3) & 0x000000F8) | ((c >> 2) & 0x00000007);
    return r | g | b;
}

// RGB565 → ARGB8888  (from convert_frame_16bpp)
// 16-bit pixel: RRRRRGGGGGGBBBBB
// R: 5-bit expanded to 8, G: 6-bit expanded to 8, B: 5-bit expanded to 8
static uint32_t rgb565_to_argb(uint16_t c) {
    uint32_t r = ((c << 8) & 0x00F80000) | ((c << 3) & 0x00070000);
    uint32_t g = ((c << 5) & 0x0000FC00) | ((c >> 1) & 0x00000300);
    uint32_t b = ((c << 3) & 0x000000F8) | ((c >> 2) & 0x00000007);
    return r | g | b;
}

// --- Tests ---

static void test_rgb332_black() {
    // All zeros → black
    CHECK_EQ(rgb332_to_argb(0x00), (uint32_t)0x00000000);
}

static void test_rgb332_white() {
    // All ones (0xFF) → R=0xE0|0x1C|0x02=0xFE (not exactly 0xFF due to 3-bit R)
    // Actually: 0xFF = R=111, G=111, B=11
    uint32_t result = rgb332_to_argb(0xFF);
    // R channel: 111 → 11111111 would need full replication
    // Let's verify the exact formulas:
    // r = (0xFF << 16) & 0x00E00000 = 0x00E00000
    //   | (0xFF << 13) & 0x001C0000 = 0x001C0000
    //   | (0xFF << 10) & 0x00030000 = 0x00030000
    //   = 0x00FF0000
    // g = (0xFF << 11) & 0x0000E000 = 0x0000E000
    //   | (0xFF <<  8) & 0x00001C00 = 0x00001C00
    //   | (0xFF <<  5) & 0x00000300 = 0x00000300  (0x1FE0 & 0x300 = 0x0100... wait)
    // Let me just verify with the actual formula
    CHECK_EQ(result, (uint32_t)0x00FFFFFF);
}

static void test_rgb332_pure_red() {
    // 0xE0 = R=111, G=000, B=00
    uint32_t result = rgb332_to_argb(0xE0);
    // r = (0xE0 << 16) & 0x00E00000 = 0x00E00000
    //   | (0xE0 << 13) & 0x001C0000 = 0x001C0000
    //   | (0xE0 << 10) & 0x00030000 = 0x00020000   (0x38000 & 0x30000 = 0x30000... let me recalc)
    // 0xE0 << 10 = 0x38000, & 0x30000 = 0x30000? No, 0x00030000 mask: 0x38000 & 0x30000 = 0x30000
    // Wait: 0x38000 = 0b0011_1000_0000_0000_0000, 0x00030000 = 0b0000_0011_0000_0000_0000_0000
    // 0x38000 = 0x00038000, & 0x00030000 = 0x00030000
    // Total: 0x00E00000 | 0x001C0000 | 0x00030000 = 0x00FF0000
    CHECK_EQ(result, (uint32_t)0x00FF0000);
}

static void test_rgb332_pure_green() {
    // 0x1C = R=000, G=111, B=00
    uint32_t result = rgb332_to_argb(0x1C);
    // g = (0x1C << 11) & 0x0000E000 = (0xE000) & 0xE000 = 0xE000
    //   | (0x1C <<  8) & 0x00001C00 = (0x1C00) & 0x1C00 = 0x1C00
    //   | (0x1C <<  5) & 0x00000300 = (0x0380) & 0x0300 = 0x0300
    //   = 0xFF00
    CHECK_EQ(result, (uint32_t)0x0000FF00);
}

static void test_rgb332_pure_blue() {
    // 0x03 = R=000, G=000, B=11
    uint32_t result = rgb332_to_argb(0x03);
    // b = (0x03 <<  6) & 0xC0 = 0xC0
    //   | (0x03 <<  4) & 0x30 = 0x30
    //   | (0x03 <<  2) & 0x0C = 0x0C
    //   | (0x03 <<  0) & 0x03 = 0x03
    //   = 0xFF
    CHECK_EQ(result, (uint32_t)0x000000FF);
}

static void test_rgb332_mid_gray() {
    // 0x92 = R=100, G=100, B=10 → approximately 50% gray
    uint32_t result = rgb332_to_argb(0x92);
    // R = 100 binary = 4/7 of max
    // r = (0x92 << 16) & 0x00E00000 = (0x920000) & 0xE00000 = 0x800000
    //   | (0x92 << 13) & 0x001C0000 = (0x490000...) hmm let me be precise
    // 0x92 = 146 = 0b10010010
    // 0x92 << 16 = 0x00920000, & 0x00E00000 = 0x00800000
    // 0x92 << 13 = 0x00490000, & 0x001C0000 = 0x00080000
    // 0x92 << 10 = 0x00024800, & 0x00030000 = 0x00020000
    // r = 0x008A0000... wait 0x800000|0x080000|0x020000 = 0x8A0000? No: 0x800000+0x080000 = 0x880000 + 0x020000 = 0x8A0000. Hmm.
    // Actually: R bits are [7:5] = 100 = 4
    // The replication pattern for R=100 is: 100_100_10 = 0x92 → 0x920000
    // So R channel value = 0x92
    // Let me just verify the result
    uint32_t r_val = (result >> 16) & 0xFF;
    uint32_t g_val = (result >>  8) & 0xFF;
    uint32_t b_val =  result        & 0xFF;
    // R=100 → 10010010 = 0x92
    CHECK_EQ(r_val, (uint32_t)0x92);
    // G=100 → 10010010 = 0x92
    CHECK_EQ(g_val, (uint32_t)0x92);
    // B=10 → 10101010 = 0xAA
    CHECK_EQ(b_val, (uint32_t)0xAA);
}

// --- RGB555 tests ---

static void test_rgb555_black() {
    CHECK_EQ(rgb555_to_argb(0x0000), (uint32_t)0x00000000);
}

static void test_rgb555_white() {
    // 0x7FFF = 0_11111_11111_11111
    CHECK_EQ(rgb555_to_argb(0x7FFF), (uint32_t)0x00FFFFFF);
}

static void test_rgb555_pure_red() {
    // R=11111, G=00000, B=00000 → 0_11111_00000_00000 = 0x7C00
    uint32_t result = rgb555_to_argb(0x7C00);
    CHECK_EQ(result, (uint32_t)0x00FF0000);
}

static void test_rgb555_pure_green() {
    // R=00000, G=11111, B=00000 → 0_00000_11111_00000 = 0x03E0
    uint32_t result = rgb555_to_argb(0x03E0);
    CHECK_EQ(result, (uint32_t)0x0000FF00);
}

static void test_rgb555_pure_blue() {
    // R=00000, G=00000, B=11111 → 0x001F
    uint32_t result = rgb555_to_argb(0x001F);
    CHECK_EQ(result, (uint32_t)0x000000FF);
}

static void test_rgb555_mid_values() {
    // R=10000, G=10000, B=10000 → ~50% per channel
    // 0_10000_10000_10000 = 0x4210
    uint32_t result = rgb555_to_argb(0x4210);
    // R: 10000 → 10000_100 = 0x84
    uint32_t r_val = (result >> 16) & 0xFF;
    CHECK_EQ(r_val, (uint32_t)0x84);
    // G: 10000 → 10000_100 = 0x84
    uint32_t g_val = (result >> 8) & 0xFF;
    CHECK_EQ(g_val, (uint32_t)0x84);
    // B: 10000 → 10000_100 = 0x84
    uint32_t b_val = result & 0xFF;
    CHECK_EQ(b_val, (uint32_t)0x84);
}

static void test_rgb555_one_per_channel() {
    // R=00001, G=00001, B=00001 → minimum nonzero
    // 0_00001_00001_00001 = 0x0421
    uint32_t result = rgb555_to_argb(0x0421);
    // R: 00001 → 00001_000 = 0x08
    CHECK_EQ((result >> 16) & 0xFF, (uint32_t)0x08);
    CHECK_EQ((result >>  8) & 0xFF, (uint32_t)0x08);
    CHECK_EQ( result        & 0xFF, (uint32_t)0x08);
}

// --- RGB565 tests ---

static void test_rgb565_black() {
    CHECK_EQ(rgb565_to_argb(0x0000), (uint32_t)0x00000000);
}

static void test_rgb565_white() {
    // R=11111, G=111111, B=11111 → 11111_111111_11111 = 0xFFFF
    CHECK_EQ(rgb565_to_argb(0xFFFF), (uint32_t)0x00FFFFFF);
}

static void test_rgb565_pure_red() {
    // R=11111, G=000000, B=00000 → 11111_000000_00000 = 0xF800
    CHECK_EQ(rgb565_to_argb(0xF800), (uint32_t)0x00FF0000);
}

static void test_rgb565_pure_green() {
    // R=00000, G=111111, B=00000 → 00000_111111_00000 = 0x07E0
    CHECK_EQ(rgb565_to_argb(0x07E0), (uint32_t)0x0000FF00);
}

static void test_rgb565_pure_blue() {
    // R=00000, G=000000, B=11111 → 0x001F
    CHECK_EQ(rgb565_to_argb(0x001F), (uint32_t)0x000000FF);
}

static void test_rgb565_green_6bit_precision() {
    // Green has 6 bits in 565 — test mid-value
    // G=100000 → 100000_10 = 0x82
    uint16_t pixel = (0 << 11) | (0b100000 << 5) | 0;  // 0x0400
    uint32_t result = rgb565_to_argb(pixel);
    uint32_t g_val = (result >> 8) & 0xFF;
    CHECK_EQ(g_val, (uint32_t)0x82);
}

static void test_rgb565_vs_rgb555_green() {
    // In 565, green has 6 bits vs 5 in 555
    // This means 565 can represent more green values
    // G=000001 in 565 → 000001_00 = 0x04
    uint16_t px_565 = (0b000001 << 5);  // only lowest green bit set
    uint32_t g_565 = (rgb565_to_argb(px_565) >> 8) & 0xFF;
    CHECK_EQ(g_565, (uint32_t)0x04);

    // G=00001 in 555 → 00001_000 = 0x08
    uint16_t px_555 = (0b00001 << 5);  // only lowest green bit set
    uint32_t g_555 = (rgb555_to_argb(px_555) >> 8) & 0xFF;
    CHECK_EQ(g_555, (uint32_t)0x08);
}

// Verify that the formulas produce values that cover the full 0-255 range
static void test_full_range_coverage() {
    // RGB332: max R (3 bits) → 0xFF
    CHECK_EQ((rgb332_to_argb(0xE0) >> 16) & 0xFF, (uint32_t)0xFF);
    // RGB332: max G (3 bits) → 0xFF
    CHECK_EQ((rgb332_to_argb(0x1C) >> 8) & 0xFF, (uint32_t)0xFF);
    // RGB332: max B (2 bits) → 0xFF
    CHECK_EQ(rgb332_to_argb(0x03) & 0xFF, (uint32_t)0xFF);

    // RGB555: max channel (5 bits) → 0xFF
    CHECK_EQ((rgb555_to_argb(0x7C00) >> 16) & 0xFF, (uint32_t)0xFF);
    CHECK_EQ((rgb555_to_argb(0x03E0) >>  8) & 0xFF, (uint32_t)0xFF);
    CHECK_EQ(rgb555_to_argb(0x001F) & 0xFF, (uint32_t)0xFF);

    // RGB565: max per channel → 0xFF
    CHECK_EQ((rgb565_to_argb(0xF800) >> 16) & 0xFF, (uint32_t)0xFF);
    CHECK_EQ((rgb565_to_argb(0x07E0) >>  8) & 0xFF, (uint32_t)0xFF);
    CHECK_EQ(rgb565_to_argb(0x001F) & 0xFF, (uint32_t)0xFF);
}

int main() {
    cout << "Running pixel format conversion tests..." << endl;

    test_rgb332_black();
    test_rgb332_white();
    test_rgb332_pure_red();
    test_rgb332_pure_green();
    test_rgb332_pure_blue();
    test_rgb332_mid_gray();

    test_rgb555_black();
    test_rgb555_white();
    test_rgb555_pure_red();
    test_rgb555_pure_green();
    test_rgb555_pure_blue();
    test_rgb555_mid_values();
    test_rgb555_one_per_channel();

    test_rgb565_black();
    test_rgb565_white();
    test_rgb565_pure_red();
    test_rgb565_pure_green();
    test_rgb565_pure_blue();
    test_rgb565_green_6bit_precision();
    test_rgb565_vs_rgb555_green();

    test_full_range_coverage();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
