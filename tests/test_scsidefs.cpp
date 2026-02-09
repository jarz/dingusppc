/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for devices/common/scsi/scsi.h definitions */

#include <devices/common/scsi/scsi.h>
#include <cinttypes>
#include <iostream>
#include <set>

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

static void test_scsi_ctrl_signals() {
    // Verify control signal bits are distinct powers of 2
    CHECK_EQ(SCSI_CTRL_IO,  1 << 0);
    CHECK_EQ(SCSI_CTRL_CD,  1 << 1);
    CHECK_EQ(SCSI_CTRL_MSG, 1 << 2);
    CHECK_EQ(SCSI_CTRL_ATN, 1 << 3);
    CHECK_EQ(SCSI_CTRL_ACK, 1 << 4);
    CHECK_EQ(SCSI_CTRL_REQ, 1 << 5);
    CHECK_EQ(SCSI_CTRL_SEL, 1 << 13);
    CHECK_EQ(SCSI_CTRL_BSY, 1 << 14);
    CHECK_EQ(SCSI_CTRL_RST, 1 << 15);

    // Signals should not overlap
    int all_signals = SCSI_CTRL_IO | SCSI_CTRL_CD | SCSI_CTRL_MSG |
                      SCSI_CTRL_ATN | SCSI_CTRL_ACK | SCSI_CTRL_REQ |
                      SCSI_CTRL_SEL | SCSI_CTRL_BSY | SCSI_CTRL_RST;
    // Count bits set — should be 9
    int count = 0;
    for (int v = all_signals; v; v &= v - 1) count++;
    CHECK_EQ(count, 9);
}

static void test_scsi_phases() {
    CHECK_EQ(ScsiPhase::BUS_FREE, 0);
    CHECK_EQ(ScsiPhase::ARBITRATION, 1);
    CHECK_EQ(ScsiPhase::SELECTION, 2);
    CHECK_EQ(ScsiPhase::RESELECTION, 3);
    CHECK_EQ(ScsiPhase::COMMAND, 4);
    CHECK_EQ(ScsiPhase::DATA_IN, 5);
    CHECK_EQ(ScsiPhase::DATA_OUT, 6);
    CHECK_EQ(ScsiPhase::STATUS, 7);
    CHECK_EQ(ScsiPhase::MESSAGE_IN, 8);
    CHECK_EQ(ScsiPhase::MESSAGE_OUT, 9);
    CHECK_EQ(ScsiPhase::RESET, 10);
}

static void test_scsi_status_codes() {
    CHECK_EQ(ScsiStatus::GOOD, 0);
    CHECK_EQ(ScsiStatus::CHECK_CONDITION, 2);
}

static void test_scsi_commands() {
    CHECK_EQ((uint8_t)ScsiCommand::TEST_UNIT_READY, 0x00);
    CHECK_EQ((uint8_t)ScsiCommand::INQUIRY, 0x12);
    CHECK_EQ((uint8_t)ScsiCommand::READ_6, 0x08);
    CHECK_EQ((uint8_t)ScsiCommand::WRITE_6, 0x0A);
    CHECK_EQ((uint8_t)ScsiCommand::READ_10, 0x28);
    CHECK_EQ((uint8_t)ScsiCommand::WRITE_10, 0x2A);
    CHECK_EQ((uint8_t)ScsiCommand::READ_CAPACITY_10, 0x25);
    CHECK_EQ((uint8_t)ScsiCommand::MODE_SENSE_6, 0x1A);
    CHECK_EQ((uint8_t)ScsiCommand::READ_TOC, 0x43);
    CHECK_EQ((uint8_t)ScsiCommand::READ_12, 0xA8);
    CHECK_EQ((uint8_t)ScsiCommand::WRITE_12, 0xAA);
    CHECK_EQ((uint8_t)ScsiCommand::READ_CD, 0xBE);
}

static void test_scsi_sense_keys() {
    CHECK_EQ(ScsiSense::NO_SENSE, 0x0);
    CHECK_EQ(ScsiSense::RECOVERED, 0x1);
    CHECK_EQ(ScsiSense::NOT_READY, 0x2);
    CHECK_EQ(ScsiSense::MEDIUM_ERR, 0x3);
    CHECK_EQ(ScsiSense::HW_ERROR, 0x4);
    CHECK_EQ(ScsiSense::ILLEGAL_REQ, 0x5);
    CHECK_EQ(ScsiSense::UNIT_ATTENTION, 0x6);
    CHECK_EQ(ScsiSense::DATA_PROTECT, 0x7);
    CHECK_EQ(ScsiSense::BLANK_CHECK, 0x8);
    CHECK_EQ(ScsiSense::VOL_OVERFLOW, 0xD);
    CHECK_EQ(ScsiSense::MISCOMPARE, 0xE);
    CHECK_EQ(ScsiSense::COMPLETED, 0xF);
}

static void test_scsi_errors() {
    CHECK_EQ(ScsiError::NO_ERROR, 0x00);
    CHECK_EQ(ScsiError::NO_SECTOR, 0x01);
    CHECK_EQ(ScsiError::WRITE_FAULT, 0x03);
    CHECK_EQ(ScsiError::DEV_NOT_READY, 0x04);
    CHECK_EQ(ScsiError::INVALID_CMD, 0x20);
    CHECK_EQ(ScsiError::INVALID_LBA, 0x21);
    CHECK_EQ(ScsiError::INVALID_CDB, 0x24);
    CHECK_EQ(ScsiError::INVALID_LUN, 0x25);
    CHECK_EQ(ScsiError::WRITE_PROTECT, 0x27);
    CHECK_EQ(ScsiError::MEDIUM_NOT_PRESENT, 0x3A);
}

static void test_scsi_device_types() {
    CHECK_EQ(ScsiDevType::DIRECT_ACCESS, 0);
    CHECK_EQ(ScsiDevType::SEQ_ACCESS, 1);
    CHECK_EQ(ScsiDevType::CD_ROM, 5);
    CHECK_EQ(ScsiDevType::UNKNOWN, 0x1F);
}

static void test_scsi_timing_constants() {
    CHECK_EQ(BUS_SETTLE_DELAY, 400ULL);
    CHECK_EQ(BUS_FREE_DELAY, 800ULL);
    CHECK_EQ(BUS_CLEAR_DELAY, 800ULL);
    CHECK_EQ(ARB_DELAY, 2400ULL);
    CHECK_EQ(SEL_ABORT_TIME, 200000ULL);
    CHECK_EQ(SEL_TIME_OUT, 250000000ULL);
    CHECK_EQ(SCSI_MAX_DEVS, 8);
}

static void test_scsi_messages() {
    CHECK_EQ(ScsiMessage::COMMAND_COMPLETE, 0);
    CHECK_EQ(ScsiMessage::IDENTIFY, 0x80);
    CHECK_EQ(ScsiMessage::HAS_DISCONNECT_PRIVILEDGE, 0x40);

    CHECK_EQ(ScsiExtMessage::MODIFY_DATA_PTR, 0);
    CHECK_EQ(ScsiExtMessage::SYNCH_XFER_REQ, 1);
    CHECK_EQ(ScsiExtMessage::WIDE_XFER_REQ, 3);
}

int main() {
    cout << "Running scsidefs tests..." << endl;

    test_scsi_ctrl_signals();
    test_scsi_phases();
    test_scsi_status_codes();
    test_scsi_commands();
    test_scsi_sense_keys();
    test_scsi_errors();
    test_scsi_device_types();
    test_scsi_timing_constants();
    test_scsi_messages();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
