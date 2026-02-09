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

/* ---- PropType / CheckType enums ---- */

static void test_prop_type_enums() {
    CHECK_EQ((int)PROP_TYPE_UNKNOWN, 0);
    CHECK_EQ((int)PROP_TYPE_STRING, 1);
    CHECK_EQ((int)PROP_TYPE_INTEGER, 2);
    CHECK_EQ((int)PROP_TYPE_BINARY, 3);

    CHECK_EQ((int)CHECK_TYPE_NONE, 0);
    CHECK_EQ((int)CHECK_TYPE_RANGE, 1);
    CHECK_EQ((int)CHECK_TYPE_LIST, 2);
}

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

int main() {
    cout << "Running machineprops tests..." << endl;

    test_prop_type_enums();
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

    cout << "Tested: " << dec << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
