/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for the MachineBase and machine-settings accessor APIs.

Tests cover:
  - get_machine() / set_machine() / release_machine() lifecycle
  - Device registration and lookup through the accessor
  - get_machine_settings() reference semantics
  - GET_STR_PROP / GET_INT_PROP / GET_BIN_PROP macro correctness

No ROMs, SDL, or specialised hardware required.
*/

#include <devices/common/hwcomponent.h>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>

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
// Minimal stub HWComponent for device-registration tests
// ============================================================
class StubDevice : public HWComponent {
public:
    explicit StubDevice(const std::string& dev_name) {
        this->name = dev_name;
        supports_types(HWCompType::MMIO_DEV);
    }
    static std::unique_ptr<HWComponent> create(const std::string& dev_name) {
        return std::unique_ptr<StubDevice>(new StubDevice(dev_name));
    }
};

// ============================================================
// Tests: get_machine / set_machine / release_machine lifecycle
// ============================================================

static void test_get_machine_initially_null() {
    release_machine(); // ensure clean state
    TEST_ASSERT(get_machine() == nullptr,
        "get_machine() must return nullptr before any machine is set");
}

static void test_set_machine_makes_get_nonnull() {
    release_machine();
    set_machine(make_unique<MachineBase>("TestMachine"));
    TEST_ASSERT(get_machine() != nullptr,
        "get_machine() must return non-null after set_machine()");
    release_machine();
}

static void test_release_machine_resets_to_null() {
    set_machine(make_unique<MachineBase>("TestMachine"));
    release_machine();
    TEST_ASSERT(get_machine() == nullptr,
        "get_machine() must return nullptr after release_machine()");
}

static void test_set_machine_replaces_existing() {
    set_machine(make_unique<MachineBase>("First"));
    MachineBase* first = get_machine();
    set_machine(make_unique<MachineBase>("Second"));
    MachineBase* second = get_machine();
    TEST_ASSERT(second != nullptr,
        "get_machine() must return non-null after second set_machine()");
    TEST_ASSERT(first != second,
        "set_machine() must replace the previous machine with a new object");
    release_machine();
}

// ============================================================
// Tests: device registration and lookup via accessor
// ============================================================

static void test_add_and_get_device_by_name() {
    set_machine(make_unique<MachineBase>("TestMachine"));
    get_machine()->add_device("Dev1", StubDevice::create("Dev1"));
    HWComponent* comp = get_machine()->get_comp_by_name("Dev1");
    TEST_ASSERT(comp != nullptr,
        "get_comp_by_name() must find a device added through the accessor");
    release_machine();
}

static void test_get_missing_device_returns_null() {
    set_machine(make_unique<MachineBase>("TestMachine"));
    HWComponent* comp = get_machine()->get_comp_by_name_optional("NonExistent");
    TEST_ASSERT(comp == nullptr,
        "get_comp_by_name_optional() must return nullptr for an unknown device");
    release_machine();
}

static void test_get_comp_by_type() {
    set_machine(make_unique<MachineBase>("TestMachine"));
    get_machine()->add_device("StubDev", StubDevice::create("StubDev"));
    HWComponent* comp = get_machine()->get_comp_by_type(HWCompType::MMIO_DEV);
    TEST_ASSERT(comp != nullptr,
        "get_comp_by_type() must find a device registered with that type");
    release_machine();
}

static void test_devices_cleared_on_release() {
    set_machine(make_unique<MachineBase>("TestMachine"));
    get_machine()->add_device("Dev1", StubDevice::create("Dev1"));
    release_machine();
    // Recreate so we can test an empty machine
    set_machine(make_unique<MachineBase>("TestMachine2"));
    HWComponent* comp = get_machine()->get_comp_by_name_optional("Dev1");
    TEST_ASSERT(comp == nullptr,
        "A freshly set machine must not contain devices from the previous one");
    release_machine();
}

// ============================================================
// Tests: get_machine_settings() accessor
// ============================================================

static void test_settings_map_is_mutable_reference() {
    auto& s1 = get_machine_settings();
    auto& s2 = get_machine_settings();
    TEST_ASSERT(&s1 == &s2,
        "get_machine_settings() must return a reference to the same map object");
}

static void test_str_property_roundtrip() {
    auto& settings = get_machine_settings();
    settings["rom_path"] = std::unique_ptr<BasicProperty>(new StrProperty("test.bin"));
    TEST_ASSERT(GET_STR_PROP("rom_path") == "test.bin",
        "GET_STR_PROP must return the string value stored via get_machine_settings()");
    settings.erase("rom_path");
}

static void test_int_property_roundtrip() {
    auto& settings = get_machine_settings();
    settings["ram_size"] = std::unique_ptr<BasicProperty>(new IntProperty(64));
    TEST_ASSERT(GET_INT_PROP("ram_size") == 64,
        "GET_INT_PROP must return the integer value stored via get_machine_settings()");
    settings.erase("ram_size");
}

static void test_bin_property_on() {
    auto& settings = get_machine_settings();
    settings["emmo"] = std::unique_ptr<BasicProperty>(new BinProperty(1));
    TEST_ASSERT(GET_BIN_PROP("emmo") == 1,
        "GET_BIN_PROP must return 1 for a BinProperty set to on");
    settings.erase("emmo");
}

static void test_bin_property_off() {
    auto& settings = get_machine_settings();
    settings["emmo"] = std::unique_ptr<BasicProperty>(new BinProperty(0));
    TEST_ASSERT(GET_BIN_PROP("emmo") == 0,
        "GET_BIN_PROP must return 0 for a BinProperty set to off");
    settings.erase("emmo");
}

static void test_settings_survive_machine_release() {
    auto& settings = get_machine_settings();
    settings["persist_key"] = std::unique_ptr<BasicProperty>(new StrProperty("persist_value"));

    set_machine(make_unique<MachineBase>("TestMachine"));
    release_machine();

    // Settings are owned by the factory, not by the machine object
    TEST_ASSERT(settings.count("persist_key") == 1,
        "Machine settings must persist independently of machine object lifecycle");
    TEST_ASSERT(GET_STR_PROP("persist_key") == "persist_value",
        "Settings value must be unchanged after machine release and re-creation");

    settings.erase("persist_key");
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "MachineBase accessor API tests" << endl;
    cout << "==============================" << endl;

    cout << endl << "Lifecycle tests:" << endl;
    test_get_machine_initially_null();
    test_set_machine_makes_get_nonnull();
    test_release_machine_resets_to_null();
    test_set_machine_replaces_existing();

    cout << endl << "Device registration tests:" << endl;
    test_add_and_get_device_by_name();
    test_get_missing_device_returns_null();
    test_get_comp_by_type();
    test_devices_cleared_on_release();

    cout << endl << "Machine settings accessor tests:" << endl;
    test_settings_map_is_mutable_reference();
    test_str_property_roundtrip();
    test_int_property_roundtrip();
    test_bin_property_on();
    test_bin_property_off();
    test_settings_survive_machine_release();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;

    return tests_failed ? 1 : 0;
}
