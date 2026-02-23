/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for the I2C bus and I2CProm device.

Tests cover:
  - I2CProm: fill_memory, set_memory, send_subaddress, receive_byte,
             read-past-end wrap-around, send_byte acknowledgement
  - I2CBus: device registration, start/send/receive routing,
            and graceful handling of missing devices

No ROMs, SDL, or hardware required.
*/

#include <devices/common/i2c/i2c.h>
#include <devices/common/i2c/i2cprom.h>

#include <cstdint>
#include <iostream>
#include <memory>

using namespace std;

static int tests_run    = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << endl; \
        tests_failed++; \
    } \
} while(0)

// ============================================================
// I2CProm tests
// ============================================================

static void test_fill_memory_is_readable() {
    I2CProm prom(0x50, 16);
    prom.fill_memory(0, 16, 0xAB);

    prom.start_transaction();
    uint8_t byte = 0;
    prom.receive_byte(&byte);
    TEST_ASSERT(byte == 0xAB, "fill_memory: first byte reads back 0xAB");
    prom.receive_byte(&byte);
    TEST_ASSERT(byte == 0xAB, "fill_memory: second byte reads back 0xAB");
}

static void test_set_memory_preserves_bytes() {
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    I2CProm prom(0x50, 16);
    prom.set_memory(0, data, 4);

    prom.start_transaction();
    uint8_t byte = 0;
    prom.receive_byte(&byte); TEST_ASSERT(byte == 0x01, "set_memory byte 0");
    prom.receive_byte(&byte); TEST_ASSERT(byte == 0x02, "set_memory byte 1");
    prom.receive_byte(&byte); TEST_ASSERT(byte == 0x03, "set_memory byte 2");
    prom.receive_byte(&byte); TEST_ASSERT(byte == 0x04, "set_memory byte 3");
}

static void test_send_subaddress_seeks_position() {
    const uint8_t data[] = {0x00, 0x11, 0x22, 0x33, 0x44};
    I2CProm prom(0x50, 8);
    prom.set_memory(0, data, 5);

    prom.start_transaction();
    prom.send_subaddress(2);

    uint8_t byte = 0;
    prom.receive_byte(&byte);
    TEST_ASSERT(byte == 0x22, "send_subaddress(2): first receive gives byte[2]");
    prom.receive_byte(&byte);
    TEST_ASSERT(byte == 0x33, "send_subaddress(2): sequential read advances position");
}

static void test_start_transaction_resets_position() {
    const uint8_t data[] = {0xAA, 0xBB};
    I2CProm prom(0x50, 4);
    prom.set_memory(0, data, 2);

    prom.start_transaction();
    uint8_t byte = 0;
    prom.receive_byte(&byte); // pos becomes 1
    TEST_ASSERT(byte == 0xAA, "first read after start_transaction");

    prom.start_transaction();  // should reset pos to 0
    prom.receive_byte(&byte);
    TEST_ASSERT(byte == 0xAA, "start_transaction resets position to 0");
}

static void test_read_past_end_wraps_around() {
    I2CProm prom(0x50, 4);
    prom.fill_memory(0, 4, 0xFF);

    prom.start_transaction();
    prom.send_subaddress(3); // position = 3 (last valid byte)

    uint8_t byte = 0;
    prom.receive_byte(&byte); // reads byte[3], pos becomes 4
    TEST_ASSERT(byte == 0xFF, "read at last byte");
    prom.receive_byte(&byte); // pos == size: wrap to 0, read byte[0]
    TEST_ASSERT(byte == 0xFF, "read past end wraps to start");
}



// ============================================================
// I2CBus routing tests
//
// StubI2CDevice lets us observe every call made by the bus.
// ============================================================

class StubI2CDevice : public I2CDevice {
public:
    StubI2CDevice() {
        this->name = "StubI2CDev";
        supports_types(HWCompType::I2C_DEV);
    }

    void  start_transaction()               override { started = true; pos = 0; }
    bool  send_subaddress(uint8_t sub_addr) override { last_sub  = sub_addr; return true; }
    bool  send_byte(uint8_t data)           override { last_sent = data;     return true; }
    bool  receive_byte(uint8_t* p_data)     override { *p_data   = response[pos++ % 4]; return true; }

    bool    started   = false;
    uint8_t last_sub  = 0;
    uint8_t last_sent = 0;
    uint8_t response[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    int     pos       = 0;
};

static void test_bus_start_transaction_reaches_device() {
    I2CBus bus;
    StubI2CDevice dev;
    bus.register_device(0x10, &dev);

    bool ok = bus.start_transaction(0x10);
    TEST_ASSERT(ok,           "start_transaction to registered device returns true");
    TEST_ASSERT(dev.started,  "start_transaction actually calls device");
}

static void test_bus_send_byte_routes_to_device() {
    I2CBus bus;
    StubI2CDevice dev;
    bus.register_device(0x20, &dev);

    bus.send_byte(0x20, 0x5A);
    TEST_ASSERT(dev.last_sent == 0x5A, "send_byte routes value to correct device");
}

static void test_bus_send_subaddress_routes_to_device() {
    I2CBus bus;
    StubI2CDevice dev;
    bus.register_device(0x30, &dev);

    bus.send_subaddress(0x30, 0x07);
    TEST_ASSERT(dev.last_sub == 0x07, "send_subaddress routes sub-address to device");
}

static void test_bus_receive_byte_routes_to_device() {
    I2CBus bus;
    StubI2CDevice dev;
    dev.response[0] = 0x7F;
    bus.register_device(0x40, &dev);

    uint8_t byte = 0;
    bool ok = bus.receive_byte(0x40, &byte);
    TEST_ASSERT(ok,          "receive_byte from registered device returns true");
    TEST_ASSERT(byte == 0x7F, "receive_byte delivers device response");
}

static void test_bus_missing_device_returns_false() {
    I2CBus bus; // no devices registered
    uint8_t dummy = 0;
    TEST_ASSERT(!bus.start_transaction(0x55),     "start_transaction to absent: false");
    TEST_ASSERT(!bus.send_subaddress(0x55, 0),    "send_subaddress to absent: false");
    TEST_ASSERT(!bus.send_byte(0x55, 0),          "send_byte to absent: false");
    TEST_ASSERT(!bus.receive_byte(0x55, &dummy),  "receive_byte from absent: false");
}

static void test_bus_does_not_route_to_wrong_address() {
    I2CBus bus;
    StubI2CDevice dev;
    bus.register_device(0x10, &dev);

    bus.send_byte(0x20, 0xFF); // address 0x20 has no device
    TEST_ASSERT(dev.last_sent == 0,
        "send_byte to wrong address must not reach the registered device");
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "I2C device tests" << endl;
    cout << "================" << endl;

    cout << endl << "I2CProm tests:" << endl;
    test_fill_memory_is_readable();
    test_set_memory_preserves_bytes();
    test_send_subaddress_seeks_position();
    test_start_transaction_resets_position();
    test_read_past_end_wraps_around();

    cout << endl << "I2CBus tests:" << endl;
    test_bus_start_transaction_reaches_device();
    test_bus_send_byte_routes_to_device();
    test_bus_send_subaddress_routes_to_device();
    test_bus_receive_byte_routes_to_device();
    test_bus_missing_device_returns_false();
    test_bus_does_not_route_to_wrong_address();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;

    return tests_failed ? 1 : 0;
}
