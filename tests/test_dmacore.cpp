/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for devices/common/dmacore.h */

#include <devices/common/dmacore.h>
#include <cinttypes>
#include <iostream>
#include <string>

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

// Concrete DmaOutChannel for testing (pure virtual pull_data needs impl)
class TestOutChannel : public DmaOutChannel {
public:
    TestOutChannel(string name) : DmaOutChannel(name) {}
    DmaPullResult pull_data(uint32_t req_len, uint32_t *avail_len, uint8_t **p_data) override {
        return DmaPullResult::NoMoreData;
    }
};

static void test_dma_out_channel() {
    TestOutChannel ch("AudioOut");
    CHECK_EQ(ch.get_name(), string("AudioOut"));
    CHECK_TRUE(ch.is_out_active());
    CHECK_EQ(ch.get_pull_data_remaining(), 1);
    // end_pull_data is a no-op by default — just shouldn't crash
    ch.end_pull_data();
}

// Concrete DmaInChannel for testing
class TestInChannel : public DmaInChannel {
public:
    TestInChannel(string name) : DmaInChannel(name) {}
    int push_data(const char* src_ptr, int len) override { return len; }
};

static void test_dma_in_channel() {
    TestInChannel ch("DiskIn");
    CHECK_EQ(ch.get_name(), string("DiskIn"));
    CHECK_TRUE(ch.is_in_active());
    CHECK_EQ(ch.get_push_data_remaining(), 1);
    ch.end_push_data(); // no-op
}

// Concrete DmaBidirChannel for testing
class TestBidirChannel : public DmaBidirChannel {
public:
    TestBidirChannel(string name) : DmaBidirChannel(name) {}
    DmaPullResult pull_data(uint32_t req_len, uint32_t *avail_len, uint8_t **p_data) override {
        return DmaPullResult::NoMoreData;
    }
    int push_data(const char* src_ptr, int len) override { return len; }
};

static void test_dma_bidir_channel() {
    TestBidirChannel ch("SCSI");
    // Bidirectional channel has its own name
    CHECK_EQ(ch.get_name(), string("SCSI"));
    // Parent out/in channels get suffixed names
    CHECK_EQ(ch.DmaOutChannel::get_name(), string("SCSI Out"));
    CHECK_EQ(ch.DmaInChannel::get_name(), string("SCSI In"));
}

static void test_dma_device_defaults() {
    DmaDevice dev;
    // Default implementations
    uint8_t buf[8] = {};
    CHECK_EQ(dev.xfer_from(buf, 4), 4);
    CHECK_EQ(dev.xfer_to(buf, 4), 4);
    CHECK_EQ(dev.tell_xfer_size(), 0);
    // notify is a no-op
    dev.notify(DmaMsg::CH_START);
}

static void test_dma_channel_defaults() {
    DmaChannel ch;
    CHECK_FALSE(ch.is_ready());
    // notify and xfer_retry are no-ops
    ch.notify(DmaMsg::DATA_AVAIL);
    ch.xfer_retry();
}

static void test_dma_connect() {
    DmaDevice dev;
    DmaChannel ch;

    // Connect device and channel
    dev.connect(&ch);
    ch.connect(&dev);
    // Should not crash — validates wiring works
}

int main() {
    cout << "Running dmacore tests..." << endl;

    test_dma_out_channel();
    test_dma_in_channel();
    test_dma_bidir_channel();
    test_dma_device_defaults();
    test_dma_channel_defaults();
    test_dma_connect();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
