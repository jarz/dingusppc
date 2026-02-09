/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for floppy image and superdrive definitions */

#include <devices/floppy/floppyimg.h>
#include <devices/floppy/superdrive.h>
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
             << #expr << " => " << (uint64_t)got_ \
             << ", expected " << (uint64_t)exp_ << endl; \
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

/* ---- floppyimg.h constants ---- */

static void test_floppy_constants() {
    CHECK_EQ(BLOCK_SIZE, 512);
    CHECK_EQ(MFM_HD_SIZE, 512 * 2880);  // 1,474,560 bytes = 1.44 MB
}

/* ---- FlopImgType enum ---- */

static void test_flop_img_types() {
    // Verify enum values are distinct
    CHECK_TRUE(FlopImgType::RAW != FlopImgType::DC42);
    CHECK_TRUE(FlopImgType::DC42 != FlopImgType::WOZ1);
    CHECK_TRUE(FlopImgType::WOZ1 != FlopImgType::WOZ2);
    CHECK_TRUE(FlopImgType::WOZ2 != FlopImgType::UNKNOWN);
    CHECK_TRUE(FlopImgType::RAW != FlopImgType::UNKNOWN);
}

/* ---- superdrive.h MFM timing macro ---- */

static void test_mfm_bytes_to_disk_time() {
    // MFM_BYTES_TO_DISK_TIME(bytes) = USECS_TO_NSECS(bytes * 8 * 2)
    //                               = bytes * 8 * 2 * NS_PER_USEC
    //                               = bytes * 16 * 1000
    //                               = bytes * 16000
    CHECK_EQ(MFM_BYTES_TO_DISK_TIME(1), (uint32_t)(1 * 8 * 2 * NS_PER_USEC));
    CHECK_EQ(MFM_BYTES_TO_DISK_TIME(512), (uint32_t)(512 * 8 * 2 * NS_PER_USEC));
    CHECK_EQ(MFM_BYTES_TO_DISK_TIME(0), 0u);
}

/* ---- superdrive.h MFM timing constants ---- */

static void test_mfm_timing_constants() {
    // Constants should be positive and non-zero
    CHECK_TRUE(MFM_INDX_MARK_DELAY > 0);
    CHECK_TRUE(MFM_ADR_MARK_DELAY > 0);
    CHECK_TRUE(MFM_SECT_DATA_DELAY > 0);
    CHECK_TRUE(MFM_DD_SECTOR_DELAY > 0);
    CHECK_TRUE(MFM_HD_SECTOR_DELAY > 0);
    CHECK_TRUE(MFM_DD_EOT_DELAY > 0);
    CHECK_TRUE(MFM_HD_EOT_DELAY > 0);

    // HD delays should be greater than DD delays
    CHECK_TRUE(MFM_HD_SECTOR_DELAY > MFM_DD_SECTOR_DELAY);
    CHECK_TRUE(MFM_HD_EOT_DELAY > MFM_DD_EOT_DELAY);

    // Verify expected relationships
    CHECK_EQ(MFM_INDX_MARK_DELAY, MFM_BYTES_TO_DISK_TIME(146));
    CHECK_EQ(MFM_ADR_MARK_DELAY, MFM_BYTES_TO_DISK_TIME(22));
    CHECK_EQ(MFM_SECT_DATA_DELAY, MFM_BYTES_TO_DISK_TIME(514));
}

/* ---- superdrive.h enums ---- */

static void test_superdrive_status_addr() {
    using namespace MacSuperdrive;
    CHECK_EQ((uint8_t)StatusAddr::Step_Status, 1);
    CHECK_EQ((uint8_t)StatusAddr::Motor_Status, 2);
    CHECK_EQ((uint8_t)StatusAddr::Eject_Latch, 3);
    CHECK_EQ((uint8_t)StatusAddr::Drive_Exists, 7);
    CHECK_EQ((uint8_t)StatusAddr::Disk_In_Drive, 8);
    CHECK_EQ((uint8_t)StatusAddr::Write_Protect, 9);
    CHECK_EQ((uint8_t)StatusAddr::Track_Zero, 0xA);
    CHECK_EQ((uint8_t)StatusAddr::Media_Kind, 0xF);
}

static void test_superdrive_command_addr() {
    using namespace MacSuperdrive;
    CHECK_EQ((uint8_t)CommandAddr::Step_Direction, 0);
    CHECK_EQ((uint8_t)CommandAddr::Do_Step, 1);
    CHECK_EQ((uint8_t)CommandAddr::Motor_On_Off, 2);
    CHECK_EQ((uint8_t)CommandAddr::Eject_Disk, 3);
    CHECK_EQ((uint8_t)CommandAddr::Reset_Eject_Latch, 4);
    CHECK_EQ((uint8_t)CommandAddr::Switch_Drive_Mode, 5);
}

static void test_superdrive_media_and_rec() {
    using namespace MacSuperdrive;
    CHECK_EQ((uint8_t)MediaKind::low_density, 0);
    CHECK_EQ((uint8_t)MediaKind::high_density, 1);
    CHECK_EQ((int)RecMethod::GCR, 0);
    CHECK_EQ((int)RecMethod::MFM, 1);
}

int main() {
    cout << "Running floppydefs tests..." << endl;

    test_floppy_constants();
    test_flop_img_types();
    test_mfm_bytes_to_disk_time();
    test_mfm_timing_constants();
    test_superdrive_status_addr();
    test_superdrive_command_addr();
    test_superdrive_media_and_rec();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
