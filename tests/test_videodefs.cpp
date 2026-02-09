/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for video subsystem definitions */

#include <devices/video/displayid.h>
#include <devices/video/rgb514defs.h>
#include <devices/video/appleramdac.h>
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

/* ---- displayid.h enums ---- */

static void test_disp_id_kind() {
    CHECK_TRUE(Disp_Id_Kind::AppleSense != Disp_Id_Kind::DDC2B);
}

static void test_i2c_state() {
    CHECK_EQ((uint8_t)I2CState::STOP, 0);
    CHECK_EQ((uint8_t)I2CState::START, 1);
    CHECK_EQ((uint8_t)I2CState::DEV_ADDR, 2);
    CHECK_EQ((uint8_t)I2CState::REG_ADDR, 3);
    CHECK_EQ((uint8_t)I2CState::DATA, 4);
    CHECK_EQ((uint8_t)I2CState::ACK, 5);
    CHECK_EQ((uint8_t)I2CState::NACK, 6);
}

/* ---- rgb514defs.h ---- */

static void test_rgb514_constants() {
    CHECK_EQ(PLL_ENAB, 1);
}

static void test_rgb514_direct_regs() {
    CHECK_EQ(Rgb514::CLUT_ADDR_WR, 0);
    CHECK_EQ(Rgb514::CLUT_DATA, 1);
    CHECK_EQ(Rgb514::CLUT_MASK, 2);
    CHECK_EQ(Rgb514::CLUT_ADDR_RD, 3);
    CHECK_EQ(Rgb514::INDEX_LOW, 4);
    CHECK_EQ(Rgb514::INDEX_HIGH, 5);
    CHECK_EQ(Rgb514::INDEX_DATA, 6);
    CHECK_EQ(Rgb514::INDEX_CNTL, 7);
}

static void test_rgb514_indirect_regs() {
    CHECK_EQ(Rgb514::MISC_CLK_CNTL, 0x0002);
    CHECK_EQ(Rgb514::HOR_SYNC_POS, 0x0004);
    CHECK_EQ(Rgb514::PWR_MNMGMT, 0x0005);
    CHECK_EQ(Rgb514::PIX_FORMAT, 0x000A);
    CHECK_EQ(Rgb514::PLL_CTL_1, 0x0010);
    CHECK_EQ(Rgb514::F0_M0, 0x0020);
    CHECK_EQ(Rgb514::F1_N0, 0x0021);
    CHECK_EQ(Rgb514::MISC_CNTL_1, 0x0070);
    CHECK_EQ(Rgb514::MISC_CNTL_2, 0x0071);
    CHECK_EQ(Rgb514::VRAM_MASK_LO, 0x0090);
    CHECK_EQ(Rgb514::VRAM_MASK_HI, 0x0091);
}

/* ---- appleramdac.h ---- */

static void test_dac_flavour() {
    CHECK_TRUE(DacFlavour::RADACAL != DacFlavour::DACULA);
}

static void test_dac_constants() {
    CHECK_EQ(DACULA_VENDOR_SIERRA, 0x3C);
    CHECK_EQ(DACULA_VENDOR_ATT, 0x84);
    CHECK_TRUE(VIDEO_XTAL > 14000000.0f);
    CHECK_TRUE(VIDEO_XTAL < 15000000.0f);
}

static void test_ramdac_regs() {
    CHECK_EQ(RamdacRegs::ADDRESS, 0);
    CHECK_EQ(RamdacRegs::CURSOR_CLUT, 1);
    CHECK_EQ(RamdacRegs::MULTI, 2);
    CHECK_EQ(RamdacRegs::CLUT_DATA, 3);
    CHECK_EQ(RamdacRegs::CURSOR_POS_HI, 0x10);
    CHECK_EQ(RamdacRegs::CURSOR_POS_LO, 0x11);
    CHECK_EQ(RamdacRegs::MISC_CTRL, 0x20);
    CHECK_EQ(RamdacRegs::DBL_BUF_CTRL, 0x21);
    CHECK_EQ(RamdacRegs::TEST_CTRL, 0x22);
    CHECK_EQ(RamdacRegs::PLL_CTRL, 0x23);
    CHECK_EQ(RamdacRegs::VENDOR_ID, 0x40);
}

int main() {
    cout << "Running videodefs tests..." << endl;

    test_disp_id_kind();
    test_i2c_state();
    test_rgb514_constants();
    test_rgb514_direct_regs();
    test_rgb514_indirect_regs();
    test_dac_flavour();
    test_dac_constants();
    test_ramdac_regs();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
