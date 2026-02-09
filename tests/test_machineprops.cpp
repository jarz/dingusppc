/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for machines/machineproperties.h property classes */

#include <machines/machineproperties.h>
#include <cinttypes>
#include <iostream>
#include <string>
#include <vector>

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

/* ---- BinProperty ---- */

static void test_bin_property_construct() {
    BinProperty on_prop(1);
    CHECK_EQ(on_prop.get_val(), 1);
    CHECK_EQ(on_prop.get_string(), string("on"));
    CHECK_EQ(on_prop.get_type(), PROP_TYPE_BINARY);

    BinProperty off_prop(0);
    CHECK_EQ(off_prop.get_val(), 0);
    CHECK_EQ(off_prop.get_string(), string("off"));
}

static void test_bin_property_set_string() {
    BinProperty prop(0);

    // "on" variants
    prop.set_string("on");
    CHECK_EQ(prop.get_val(), 1);

    prop.set_string("OFF");
    CHECK_EQ(prop.get_val(), 0);

    prop.set_string("ON");
    CHECK_EQ(prop.get_val(), 1);

    prop.set_string("off");
    CHECK_EQ(prop.get_val(), 0);

    prop.set_string("1");
    CHECK_EQ(prop.get_val(), 1);

    prop.set_string("0");
    CHECK_EQ(prop.get_val(), 0);

    prop.set_string("yes");
    CHECK_EQ(prop.get_val(), 1);

    prop.set_string("no");
    CHECK_EQ(prop.get_val(), 0);
}

static void test_bin_property_invalid() {
    BinProperty prop(1);
    // invalid value should not change the property
    prop.set_string("maybe");
    CHECK_EQ(prop.get_val(), 1);
}

static void test_bin_property_clone() {
    BinProperty prop(1);
    BasicProperty* cloned = prop.clone();
    CHECK_EQ(dynamic_cast<BinProperty*>(cloned)->get_val(), 1);
    CHECK_EQ(cloned->get_type(), PROP_TYPE_BINARY);
    delete cloned;
}

static void test_bin_property_valid_values_str() {
    BinProperty prop(0);
    string valid = prop.get_valid_values_as_str();
    CHECK_TRUE(valid.find("on") != string::npos);
    CHECK_TRUE(valid.find("off") != string::npos);
}

/* ---- StrProperty ---- */

static void test_str_property_no_check() {
    StrProperty prop("hello");
    CHECK_EQ(prop.get_string(), string("hello"));
    CHECK_EQ(prop.get_type(), PROP_TYPE_STRING);

    // no check — any value accepted
    prop.set_string("world");
    CHECK_EQ(prop.get_string(), string("world"));
}

static void test_str_property_with_list() {
    vector<string> valid_vals = {"apple", "banana", "cherry"};
    StrProperty prop("apple", valid_vals);
    CHECK_EQ(prop.get_string(), string("apple"));

    // valid value
    prop.set_string("banana");
    CHECK_EQ(prop.get_string(), string("banana"));

    // invalid value — should not change
    prop.set_string("grape");
    CHECK_EQ(prop.get_string(), string("banana"));
}

static void test_str_property_clone() {
    StrProperty prop("test");
    BasicProperty* cloned = prop.clone();
    CHECK_EQ(cloned->get_string(), string("test"));
    CHECK_EQ(cloned->get_type(), PROP_TYPE_STRING);
    delete cloned;
}

/* ---- IntProperty ---- */

static void test_int_property_no_check() {
    IntProperty prop(42);
    CHECK_EQ(prop.get_int(), 42u);
    CHECK_EQ(prop.get_type(), PROP_TYPE_INTEGER);
    CHECK_EQ(prop.get_string(), string("42"));
}

static void test_int_property_with_range() {
    IntProperty prop(10, 5, 20);
    CHECK_EQ(prop.get_int(), 10u);

    // set to valid value within range
    prop.set_string("15");
    CHECK_EQ(prop.get_int(), 15u);

    // set to value outside range — should revert to last valid
    prop.set_string("25");
    CHECK_EQ(prop.get_int(), 15u);
}

static void test_int_property_with_list() {
    vector<uint32_t> valid_vals = {8, 16, 32, 64};
    IntProperty prop(16, valid_vals);
    CHECK_EQ(prop.get_int(), 16u);

    prop.set_string("32");
    CHECK_EQ(prop.get_int(), 32u);

    // invalid list value
    prop.set_string("24");
    CHECK_EQ(prop.get_int(), 32u);
}

static void test_int_property_clone() {
    IntProperty prop(100);
    BasicProperty* cloned = prop.clone();
    CHECK_EQ(dynamic_cast<IntProperty*>(cloned)->get_int(), 100u);
    delete cloned;
}

/* ---- get_valid_values_as_str ---- */

static void test_str_property_valid_values_list() {
    vector<string> valid_vals = {"red", "green", "blue"};
    StrProperty prop("red", valid_vals);
    string result = prop.get_valid_values_as_str();
    // Should contain each quoted value separated by commas
    CHECK_TRUE(result.find("red") != string::npos);
    CHECK_TRUE(result.find("green") != string::npos);
    CHECK_TRUE(result.find("blue") != string::npos);
}

static void test_str_property_valid_values_any() {
    StrProperty prop("anything");
    string result = prop.get_valid_values_as_str();
    CHECK_EQ(result, string("Any"));
}

static void test_int_property_valid_values_range() {
    IntProperty prop(10, 5, 20);
    string result = prop.get_valid_values_as_str();
    // Should produce "[5...20]"
    CHECK_TRUE(result.find("5") != string::npos);
    CHECK_TRUE(result.find("20") != string::npos);
    CHECK_TRUE(result.find("[") != string::npos);
    CHECK_TRUE(result.find("]") != string::npos);
}

static void test_int_property_valid_values_list() {
    vector<uint32_t> valid_vals = {8, 16, 32};
    IntProperty prop(16, valid_vals);
    string result = prop.get_valid_values_as_str();
    CHECK_TRUE(result.find("8") != string::npos);
    CHECK_TRUE(result.find("16") != string::npos);
    CHECK_TRUE(result.find("32") != string::npos);
}

static void test_int_property_valid_values_any() {
    IntProperty prop(42);
    string result = prop.get_valid_values_as_str();
    CHECK_EQ(result, string("Any"));
}

/* ---- parse_device_path ---- */

static void test_parse_device_path_basic() {
    string bus_id;
    uint32_t dev_num;

    parse_device_path("pci:0", bus_id, dev_num);
    CHECK_EQ(bus_id, string("pci"));
    CHECK_EQ(dev_num, 0u);
}

static void test_parse_device_path_with_number() {
    string bus_id;
    uint32_t dev_num;

    parse_device_path("ScsiMesh:3", bus_id, dev_num);
    CHECK_EQ(bus_id, string("ScsiMesh"));
    CHECK_EQ(dev_num, 3u);
}

static void test_parse_device_path_hex() {
    string bus_id;
    uint32_t dev_num;

    // strtoul with base 0 should handle hex prefix
    parse_device_path("bus:0x1F", bus_id, dev_num);
    CHECK_EQ(bus_id, string("bus"));
    CHECK_EQ(dev_num, 0x1Fu);
}

int main() {
    cout << "Running machineprops tests..." << endl;

    test_bin_property_construct();
    test_bin_property_set_string();
    test_bin_property_invalid();
    test_bin_property_clone();
    test_bin_property_valid_values_str();
    test_str_property_no_check();
    test_str_property_with_list();
    test_str_property_clone();
    test_int_property_no_check();
    test_int_property_with_range();
    test_int_property_with_list();
    test_int_property_clone();
    test_str_property_valid_values_list();
    test_str_property_valid_values_any();
    test_int_property_valid_values_range();
    test_int_property_valid_values_list();
    test_int_property_valid_values_any();
    test_parse_device_path_basic();
    test_parse_device_path_with_number();
    test_parse_device_path_hex();

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
