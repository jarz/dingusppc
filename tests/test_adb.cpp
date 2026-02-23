/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for the ADB (Apple Desktop Bus) bus and device layer.

Tests cover:
  - AdbBus::process_command — SendReset (0x00): dispatched to all devices
  - AdbBus::process_command — Talk to a registered device: data is returned,
    output_count is set, status == ADB_STAT_OK
  - AdbBus::process_command — Talk to a device with no pending data:
    status == ADB_STAT_TIMEOUT
  - AdbBus::process_command — Talk to an unregistered address:
    status == ADB_STAT_TIMEOUT
  - AdbBus::process_command — Listen: dispatched to matched address only;
    input_buf and input_count are populated correctly
  - AdbBus::poll: delegates to registered devices; returns the correct
    encoded talk command when a device has a pending SRQ and data;
    returns 0 when no device has data despite SRQ set
  - AdbDevice::poll: encodes the talk command as (0xC | addr<<4)

Only behaviors of the command-dispatch and poll logic are tested.
Trivial initial-state checks ("output_count starts at 0") are omitted.

No ROMs, SDL, or hardware required.
*/

#include <core/timermanager.h>
#include <devices/common/adb/adbbus.h>
#include <devices/common/adb/adbdevice.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

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
// Stub ADB device — bypasses device_postinit / machine dependency.
// ============================================================
class StubAdbDevice : public AdbDevice {
public:
    StubAdbDevice(const string& n, uint8_t addr) : AdbDevice(n) {
        this->my_addr        = addr;
        this->dev_handler_id = 0x01;
    }
    // Wire directly without going through device_postinit.
    void attach(AdbBus* bus) { this->host_obj = bus; bus->register_device(this); }

    void reset() override { reset_count++; srq_flag = 0; }

    bool get_register_0() override {
        if (!has_data) return false;
        host_obj->get_output_buf()[0] = data[0];
        host_obj->get_output_buf()[1] = data[1];
        host_obj->set_output_count(2);
        return true;
    }
    void set_register_0() override { listen_r0_count++; }
    void set_srq(uint8_t val)       { this->srq_flag = val; }

    bool    has_data        = false;
    uint8_t data[2]        = {};
    int     reset_count    = 0;
    int     listen_r0_count = 0;
};

// ============================================================
// SendReset
// ============================================================

static void test_send_reset_dispatches_to_all_devices() {
    AdbBus bus("ADB");
    StubAdbDevice dev1("D1", 2), dev2("D2", 3);
    dev1.attach(&bus); dev2.attach(&bus);

    uint8_t cmd[] = { 0x00 }; // SendReset: cmd_byte & 0xF == 0
    bus.process_command(cmd, 1);

    TEST_ASSERT(dev1.reset_count == 1, "SendReset: device at addr 2 was reset");
    TEST_ASSERT(dev2.reset_count == 1, "SendReset: device at addr 3 was reset");
}

// ============================================================
// Talk
// ============================================================

static void test_talk_delivers_device_data() {
    // Talk R0 to address 2: cmd = (2<<4)|0xC|0 = 0x2C
    AdbBus bus("ADB");
    StubAdbDevice dev("D", ADB_ADDR_KBD);
    dev.has_data = true; dev.data[0] = 0x11; dev.data[1] = 0x22;
    dev.attach(&bus);

    uint8_t cmd[] = { 0x2C };
    TEST_ASSERT(bus.process_command(cmd, 1) == ADB_STAT_OK, "Talk: status OK when device has data");
    TEST_ASSERT(bus.get_output_count() == 2,             "Talk: output_count set to 2");
    TEST_ASSERT(bus.get_output_buf()[0] == 0x11,         "Talk: output[0] = 0x11");
    TEST_ASSERT(bus.get_output_buf()[1] == 0x22,         "Talk: output[1] = 0x22");
}

static void test_talk_times_out_when_device_has_no_data() {
    AdbBus bus("ADB");
    StubAdbDevice dev("D", ADB_ADDR_KBD);
    dev.has_data = false; // will not respond
    dev.attach(&bus);

    uint8_t cmd[] = { 0x2C }; // Talk R0 addr 2
    TEST_ASSERT(bus.process_command(cmd, 1) == ADB_STAT_TIMEOUT,
        "Talk: timeout when registered device has no data");
}

static void test_talk_times_out_for_absent_address() {
    AdbBus bus("ADB"); // no devices registered

    uint8_t cmd[] = { 0x5C }; // Talk R0 addr 5
    TEST_ASSERT(bus.process_command(cmd, 1) == ADB_STAT_TIMEOUT,
        "Talk: timeout when no device exists at the addressed location");
}

// ============================================================
// Listen
// ============================================================

static void test_listen_dispatches_to_matched_address_only() {
    AdbBus bus("ADB");
    StubAdbDevice dev2("D2", ADB_ADDR_KBD);  // addr=2
    StubAdbDevice dev3("D3", ADB_ADDR_RELPOS); // addr=3
    dev2.attach(&bus); dev3.attach(&bus);

    // Listen R0 to address 2: cmd = (2<<4)|0x8|0 = 0x28, payload = 0xAB
    uint8_t cmd[] = { 0x28, 0xAB };
    uint8_t status = bus.process_command(cmd, 2);

    TEST_ASSERT(status == ADB_STAT_OK,           "Listen: status OK");
    TEST_ASSERT(bus.get_input_count() == 1,      "Listen: input_count populated");
    TEST_ASSERT(bus.get_input_buf()[0] == 0xAB,  "Listen: input byte propagated");
    TEST_ASSERT(dev2.listen_r0_count == 1,       "Listen: device at addr 2 received the command");
    TEST_ASSERT(dev3.listen_r0_count == 0,       "Listen: device at addr 3 was NOT called");
}

// ============================================================
// Poll
// ============================================================

static void setup_timer() {
    TimerManager::get_instance()->set_time_now_cb([]() -> uint64_t { return 0; });
    TimerManager::get_instance()->set_notify_changes_cb([]() {});
}

static void test_poll_returns_talk_cmd_when_srq_and_data() {
    setup_timer();
    AdbBus bus("ADB");
    StubAdbDevice dev("D", ADB_ADDR_KBD); // addr=2
    dev.has_data = true; dev.data[0] = 0x01; dev.set_srq(1);
    dev.attach(&bus);

    // Expected talk command: 0xC | (addr << 4) = 0xC | 0x20 = 0x2C
    TEST_ASSERT(dev.poll() == 0x2C,
        "AdbDevice::poll: encodes talk command as 0xC|(addr<<4) = 0x2C");
    TEST_ASSERT(bus.poll() == 0x2C,
        "AdbBus::poll: delegates to device and returns the encoded talk command");
}

static void test_poll_returns_zero_when_srq_set_but_no_data() {
    setup_timer();
    AdbBus bus("ADB");
    StubAdbDevice dev("D", ADB_ADDR_KBD);
    dev.has_data = false; dev.set_srq(1); // SRQ without data is a spurious SRQ
    dev.attach(&bus);

    TEST_ASSERT(dev.poll() == 0,  "AdbDevice::poll: returns 0 when SRQ set but get_register_0 returns false");
    TEST_ASSERT(bus.poll() == 0,  "AdbBus::poll: returns 0 when no device has both SRQ and data");
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "ADB bus and device tests" << endl;
    cout << "========================" << endl;

    cout << endl << "SendReset:" << endl;
    test_send_reset_dispatches_to_all_devices();

    cout << endl << "Talk:" << endl;
    test_talk_delivers_device_data();
    test_talk_times_out_when_device_has_no_data();
    test_talk_times_out_for_absent_address();

    cout << endl << "Listen:" << endl;
    test_listen_dispatches_to_matched_address_only();

    cout << endl << "Poll:" << endl;
    test_poll_returns_talk_cmd_when_srq_and_data();
    test_poll_returns_zero_when_srq_set_but_no_data();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;
    return tests_failed ? 1 : 0;
}
