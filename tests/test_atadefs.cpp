/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for devices/common/ata/atadefs.h */

#include <devices/common/ata/atadefs.h>
#include <cinttypes>
#include <iostream>

using namespace std;
using namespace ata_interface;

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

static void test_device_ids() {
    CHECK_EQ(DEVICE_ID_INVALID, -1);
    CHECK_EQ(DEVICE_ID_ZERO, 0);
    CHECK_EQ(DEVICE_ID_ONE, 1);
}

static void test_device_types() {
    CHECK_EQ(DEVICE_TYPE_UNKNOWN, -1);
    CHECK_EQ(DEVICE_TYPE_ATA, 0);
    CHECK_EQ(DEVICE_TYPE_ATAPI, 1);
}

static void test_ata_register_offsets() {
    CHECK_EQ(ATA_Reg::DATA, 0x00);
    CHECK_EQ(ATA_Reg::ERROR, 0x01);
    CHECK_EQ(ATA_Reg::FEATURES, 0x01);
    CHECK_EQ(ATA_Reg::SEC_COUNT, 0x02);
    CHECK_EQ(ATA_Reg::SEC_NUM, 0x03);
    CHECK_EQ(ATA_Reg::CYL_LOW, 0x04);
    CHECK_EQ(ATA_Reg::CYL_HIGH, 0x05);
    CHECK_EQ(ATA_Reg::DEVICE_HEAD, 0x06);
    CHECK_EQ(ATA_Reg::STATUS, 0x07);
    CHECK_EQ(ATA_Reg::COMMAND, 0x07);
    CHECK_EQ(ATA_Reg::ALT_STATUS, 0x16);
    CHECK_EQ(ATA_Reg::DEV_CTRL, 0x16);
}

static void test_ata_status_bits() {
    // Status register bits should be individual bit flags
    CHECK_EQ(ATA_Status::ERR, 0x01);
    CHECK_EQ(ATA_Status::IDX, 0x02);
    CHECK_EQ(ATA_Status::CORR, 0x04);
    CHECK_EQ(ATA_Status::DRQ, 0x08);
    CHECK_EQ(ATA_Status::DSC, 0x10);
    CHECK_EQ(ATA_Status::DWF, 0x20);
    CHECK_EQ(ATA_Status::DRDY, 0x40);
    CHECK_EQ(ATA_Status::BSY, 0x80);

    // Verify they don't overlap
    uint8_t all = ATA_Status::ERR | ATA_Status::IDX | ATA_Status::CORR |
                  ATA_Status::DRQ | ATA_Status::DSC | ATA_Status::DWF |
                  ATA_Status::DRDY | ATA_Status::BSY;
    CHECK_EQ(all, 0xFF);
}

static void test_ata_error_bits() {
    CHECK_EQ(ATA_Error::ANMF, 0x01);
    CHECK_EQ(ATA_Error::TK0NF, 0x02);
    CHECK_EQ(ATA_Error::ABRT, 0x04);
    CHECK_EQ(ATA_Error::MCR, 0x08);
    CHECK_EQ(ATA_Error::IDNF, 0x10);
    CHECK_EQ(ATA_Error::MC, 0x20);
    CHECK_EQ(ATA_Error::UNC, 0x40);
    CHECK_EQ(ATA_Error::BBK, 0x80);
}

static void test_ata_commands() {
    CHECK_EQ(ATA_Cmd::NOP, 0x00);
    CHECK_EQ(ATA_Cmd::READ_SECTOR, 0x20);
    CHECK_EQ(ATA_Cmd::WRITE_SECTOR, 0x30);
    CHECK_EQ(ATA_Cmd::IDENTIFY_DEVICE, 0xEC);
    CHECK_EQ(ATA_Cmd::SET_FEATURES, 0xEF);
    CHECK_EQ(ATA_Cmd::ATAPI_PACKET, 0xA0);
    CHECK_EQ(ATA_Cmd::ATAPI_IDFY_DEV, 0xA1);
    CHECK_EQ(ATA_Cmd::READ_DMA, 0xC8);
    CHECK_EQ(ATA_Cmd::WRITE_DMA, 0xCA);
}

static void test_ata_null_device() {
    AtaNullDevice dev;

    // null device returns all ones except BSY (DD7)
    CHECK_EQ(dev.read(0), 0xFF7Fu);
    CHECK_EQ(dev.read(ATA_Reg::STATUS), 0xFF7Fu);
    CHECK_EQ(dev.read(ATA_Reg::DATA), 0xFF7Fu);

    // null device has invalid device ID
    CHECK_EQ(dev.get_device_id(), DEVICE_ID_INVALID);

    // write should be a no-op (no crash)
    dev.write(0, 0x1234);

    // pull/push data default to returning 0
    uint8_t buf[4] = {};
    CHECK_EQ(dev.pull_data(buf, 4), 0);
    CHECK_EQ(dev.push_data(buf, 4), 0);
}

static void test_atapi_int_reason() {
    CHECK_EQ(ATAPI_Int_Reason::CoD, 1);
    CHECK_EQ(ATAPI_Int_Reason::IO, 2);
    CHECK_EQ(ATAPI_Int_Reason::RELEASE, 4);
}

static void test_ata_ctrl_bits() {
    CHECK_EQ(ATA_CTRL::IEN, 0x02);
    CHECK_EQ(ATA_CTRL::SRST, 0x04);
    CHECK_EQ(ATA_CTRL::HOB, 0x80);
}

int main() {
    cout << "Running atadefs tests..." << endl;

    test_device_ids();
    test_device_types();
    test_ata_register_offsets();
    test_ata_status_bits();
    test_ata_error_bits();
    test_ata_commands();
    test_ata_null_device();
    test_atapi_int_reason();
    test_ata_ctrl_bits();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
