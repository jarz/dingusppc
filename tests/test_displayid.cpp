/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for DisplayID Apple Monitor Sense identification.

    Tests read_monitor_sense() which decodes Apple's monitor sense protocol:
    - Standard sense: returns the 3-bit std_sense_code when no line is pulled low
    - Extended sense: when one sense line is pulled low, returns encoded bits
      from the 6-bit ext_sense_code that identify the specific monitor type

    The protocol works by driving one of 3 sense lines low and reading back
    the other two, allowing 6 bits of information to be encoded.
*/

#include <devices/video/displayid.h>
#include <machines/machineproperties.h>
#include <cinttypes>
#include <iostream>

// Provide the global that displayid.cpp's default constructor references
std::map<std::string, std::unique_ptr<BasicProperty>> gMachineSettings;

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

/* Helper to construct sense line drive args.
   dirs: bitmask of which lines are outputs (active low drivers)
     bit 2 = sense line 2 direction
     bit 1 = sense line 1 direction
     bit 0 = sense line 0 direction
   levels: pin levels for output lines (0 = pulled low, 1 = floating high)
     bit 2 = sense line 2 level
     bit 1 = sense line 1 level
     bit 0 = sense line 0 level
*/

/* ---- Standard sense code ---- */

// When no specific line is pulled low (default state), returns std_sense_code
static void test_standard_sense_code() {
    // 21" RGB: std=0, ext=0x00
    DisplayID disp_21(0, 0x00);
    // Default read: dirs=0 (all inputs), levels=0b111
    CHECK_EQ(disp_21.read_monitor_sense(0b111, 0b000), (uint8_t)0);

    // Portrait Monochrome: std=1, ext=0x14
    DisplayID disp_portrait(1, 0x14);
    CHECK_EQ(disp_portrait.read_monitor_sense(0b111, 0b000), (uint8_t)1);

    // 12" RGB: std=2, ext=0x21
    DisplayID disp_12(2, 0x21);
    CHECK_EQ(disp_12.read_monitor_sense(0b111, 0b000), (uint8_t)2);

    // Hi-Res 12-14in: std=6, ext=0x2B
    DisplayID disp_hires(6, 0x2B);
    CHECK_EQ(disp_hires.read_monitor_sense(0b111, 0b000), (uint8_t)6);

    // Not Connected: std=7, ext=0x3F
    DisplayID disp_nc(7, 0x3F);
    CHECK_EQ(disp_nc.read_monitor_sense(0b111, 0b000), (uint8_t)7);
}

/* ---- Extended sense code probing ---- */

// Pull sense line 2 low: dirs=0b100, levels=0b011 -> returns ext bits [5:4]
// Pull sense line 1 low: dirs=0b010, levels=0b101 -> returns ext bits [3:2] rearranged
// Pull sense line 0 low: dirs=0b001, levels=0b110 -> returns ext bits [1:0] shifted

static void test_ext_sense_21inch_rgb() {
    // 21" RGB: std=0, ext=0x00 (0b00'00'00)
    DisplayID disp(0, 0x00);

    // Pull line 2 low: returns (ext >> 4) & 0x3 = 0
    CHECK_EQ(disp.read_monitor_sense(0b011, 0b100), (uint8_t)0x00);
    // Pull line 1 low: returns bits 3,2 rearranged = 0
    CHECK_EQ(disp.read_monitor_sense(0b101, 0b010), (uint8_t)0x00);
    // Pull line 0 low: returns (ext & 0x3) << 1 = 0
    CHECK_EQ(disp.read_monitor_sense(0b110, 0b001), (uint8_t)0x00);
}

static void test_ext_sense_hires_12_14() {
    // HiRes12-14in: std=6, ext=0x2B (0b10'10'11)
    DisplayID disp(6, 0x2B);

    // Pull line 2 low: (dirs=0b100, levels=0b011)
    // returns (ext_sense_code & 0b110000) >> 4 = (0x2B >> 4) & 3 = 0x2
    CHECK_EQ(disp.read_monitor_sense(0b011, 0b100), (uint8_t)0x02);

    // Pull line 1 low: (dirs=0b010, levels=0b101)
    // bit 3 contribution: ((ext & 0b001000) >> 1) = (0x2B & 0x08) >> 1 = 0x04
    // bit 2 contribution: ((ext & 0b000100) >> 2) = (0x2B & 0x04) >> 2 = 0x00
    // result = 0x04 | 0x00 = 0x04
    CHECK_EQ(disp.read_monitor_sense(0b101, 0b010), (uint8_t)0x04);

    // Pull line 0 low: (dirs=0b001, levels=0b110)
    // returns (ext & 0b000011) << 1 = (0x2B & 0x03) << 1 = 3 << 1 = 0x06
    CHECK_EQ(disp.read_monitor_sense(0b110, 0b001), (uint8_t)0x06);
}

static void test_ext_sense_portrait_mono() {
    // Portrait Monochrome: std=1, ext=0x14 (0b01'01'00)
    DisplayID disp(1, 0x14);

    // Pull line 2 low: (ext >> 4) & 3 = (0x14 >> 4) & 3 = 1
    CHECK_EQ(disp.read_monitor_sense(0b011, 0b100), (uint8_t)0x01);

    // Pull line 1 low: 
    // ((ext & 0x08) >> 1) | ((ext & 0x04) >> 2) = (0x00) | (0x04>>2) = 0x01
    CHECK_EQ(disp.read_monitor_sense(0b101, 0b010), (uint8_t)0x01);

    // Pull line 0 low: (ext & 0x03) << 1 = (0x00) << 1 = 0
    CHECK_EQ(disp.read_monitor_sense(0b110, 0b001), (uint8_t)0x00);
}

static void test_ext_sense_not_connected() {
    // Not Connected: std=7, ext=0x3F (0b11'11'11) — all bits set
    DisplayID disp(7, 0x3F);

    // Pull line 2 low: (0x3F >> 4) & 3 = 3
    CHECK_EQ(disp.read_monitor_sense(0b011, 0b100), (uint8_t)0x03);

    // Pull line 1 low: ((0x3F & 0x08) >> 1) | ((0x3F & 0x04) >> 2) = 4 | 1 = 5
    CHECK_EQ(disp.read_monitor_sense(0b101, 0b010), (uint8_t)0x05);

    // Pull line 0 low: (0x3F & 0x03) << 1 = 3 << 1 = 6
    CHECK_EQ(disp.read_monitor_sense(0b110, 0b001), (uint8_t)0x06);
}

static void test_ext_sense_vga() {
    // VGA-SVGA: std=7, ext=0x17 (0b01'01'11)
    DisplayID disp(7, 0x17);

    // Pull line 2 low: (0x17 >> 4) & 3 = 1
    CHECK_EQ(disp.read_monitor_sense(0b011, 0b100), (uint8_t)0x01);

    // Pull line 1 low: ((0x17 & 0x08) >> 1) | ((0x17 & 0x04) >> 2) = 0 | 1 = 1
    CHECK_EQ(disp.read_monitor_sense(0b101, 0b010), (uint8_t)0x01);    // Pull line 0 low: (0x17 & 0x03) << 1 = 3 << 1 = 6
    CHECK_EQ(disp.read_monitor_sense(0b110, 0b001), (uint8_t)0x06);
}

/* Verify that all defined monitors have distinct ext sense code responses */
static void test_ext_sense_uniqueness() {
    // Test a few monitors to make sure different ext codes produce different results
    DisplayID disp_a(6, 0x2B); // HiRes12-14in
    DisplayID disp_b(6, 0x03); // Multiscan15in
    DisplayID disp_c(6, 0x0B); // Multiscan17in

    // All have std=6 but different ext codes
    // Pull line 2 low — should differ
    uint8_t a = disp_a.read_monitor_sense(0b011, 0b100);
    uint8_t b = disp_b.read_monitor_sense(0b011, 0b100);
    uint8_t c = disp_c.read_monitor_sense(0b011, 0b100);

    // At least two of these three should be distinguishable
    // (they might not all differ on a single probe, but the ext code is different)
    // Full identification requires probing all 3 lines and combining
    // Let's check the combined 6-bit ext code is reconstructable
    auto get_ext = [](DisplayID& d) -> uint8_t {
        uint8_t p2 = d.read_monitor_sense(0b011, 0b100);  // bits [5:4]
        uint8_t p1 = d.read_monitor_sense(0b101, 0b010);  // rearranged [3:2]
        uint8_t p0 = d.read_monitor_sense(0b110, 0b001);  // shifted [1:0]
        // Reconstruct ext_sense_code:
        // p2 = (ext >> 4) & 3 -> gives ext bits 5:4 directly
        // p1 = ((ext & 0x08) >> 1) | ((ext & 0x04) >> 2)
        //      -> p1 bit 2 carries ext bit 3, p1 bit 0 carries ext bit 2
        // p0 = (ext & 0x03) << 1 -> ext bits 1:0 shifted left by 1
        uint8_t b54 = p2 << 4;
        uint8_t b3 = (p1 & 0x04) << 1;  // p1 bit 2 -> ext bit 3
        uint8_t b2 = (p1 & 0x01) << 2;  // p1 bit 0 -> ext bit 2
        uint8_t b10 = p0 >> 1;
        return b54 | b3 | b2 | b10;
    };

    CHECK_EQ(get_ext(disp_a), (uint8_t)0x2B);
    CHECK_EQ(get_ext(disp_b), (uint8_t)0x03);
    CHECK_EQ(get_ext(disp_c), (uint8_t)0x0B);
}

/* Verify full round-trip: construct with code, probe all 3 lines, reconstruct code */
static void test_ext_sense_roundtrip() {
    // Test with several different ext codes
    uint8_t test_codes[] = {0x00, 0x14, 0x21, 0x31, 0x1E, 0x03, 0x2B, 0x3F, 0x17, 0x2D};

    for (uint8_t ext : test_codes) {
        DisplayID disp(0, ext);

        uint8_t p2 = disp.read_monitor_sense(0b011, 0b100);
        uint8_t p1 = disp.read_monitor_sense(0b101, 0b010);
        uint8_t p0 = disp.read_monitor_sense(0b110, 0b001);

        // Reconstruct ext code from probe results
        uint8_t reconstructed = (p2 << 4) | ((p1 & 0x04) << 1) | ((p1 & 0x01) << 2) | (p0 >> 1);
        CHECK_EQ(reconstructed, ext);
    }
}

int main() {
    cout << "Running DisplayID tests..." << endl;

    test_standard_sense_code();
    test_ext_sense_21inch_rgb();
    test_ext_sense_hires_12_14();
    test_ext_sense_portrait_mono();
    test_ext_sense_not_connected();
    test_ext_sense_vga();
    test_ext_sense_uniqueness();
    test_ext_sense_roundtrip();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
