/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for machine property classes and constraint validation.

Tests cover:
  - StrProperty: unconstrained round-trip, list-constrained acceptance,
                 rejection of values not in the allowed list
  - IntProperty: unconstrained round-trip, range constraint (valid &
                 out-of-range), list constraint
  - BinProperty: construction from int (0/1), set_string with all
                 accepted string forms ("on","off","ON","OFF","1","0")
  - BasicProperty::clone(): derived type is faithfully copied

No ROMs, SDL, or hardware required.
*/

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
// StrProperty tests
// ============================================================

static void test_str_property_unconstrained_roundtrip() {
    StrProperty p("hello");
    TEST_ASSERT(p.get_string() == "hello",  "StrProperty: initial value is stored");
    p.set_string("world");
    TEST_ASSERT(p.get_string() == "world",  "StrProperty: set_string updates value");
}

static void test_str_property_list_accepts_valid_value() {
    StrProperty p("a", {"a", "b", "c"});
    TEST_ASSERT(p.get_string() == "a", "StrProperty list: initial valid value stored");
    p.set_string("b");
    TEST_ASSERT(p.get_string() == "b", "StrProperty list: accepts value in list");
    p.set_string("c");
    TEST_ASSERT(p.get_string() == "c", "StrProperty list: accepts another value in list");
}

static void test_str_property_list_rejects_invalid_value() {
    StrProperty p("a", {"a", "b", "c"});
    p.set_string("z"); // not in list — implementation logs an error and keeps old value
    TEST_ASSERT(p.get_string() == "a",
        "StrProperty list: rejects value not in list and retains previous value");
}


static void test_str_property_clone() {
    StrProperty orig("clone_me");
    unique_ptr<BasicProperty> copy(orig.clone());
    TEST_ASSERT(copy->get_string() == "clone_me",
        "StrProperty::clone() produces an independent copy with same value");
    // Mutating the copy must not change the original.
    copy->set_string("mutated");
    TEST_ASSERT(orig.get_string() == "clone_me",
        "StrProperty::clone() is independent — mutating copy leaves original intact");
}

// ============================================================
// IntProperty tests
// ============================================================

static void test_int_property_unconstrained_roundtrip() {
    IntProperty p(42U);
    TEST_ASSERT(p.get_int() == 42U,  "IntProperty: initial value is stored");
    p.set_string("100");
    TEST_ASSERT(p.get_int() == 100U, "IntProperty: set_string updates integer value");
}

static void test_int_property_range_accepts_valid() {
    IntProperty p(8U, 1U, 128U);
    TEST_ASSERT(p.get_int() == 8U, "IntProperty range: initial value in range");
    p.set_string("64");
    TEST_ASSERT(p.get_int() == 64U, "IntProperty range: accepts value within range");
}

static void test_int_property_range_rejects_out_of_range() {
    IntProperty p(8U, 1U, 128U);
    p.set_string("200"); // out of range — implementation logs error and keeps old value
    TEST_ASSERT(p.get_int() == 8U,
        "IntProperty range: out-of-range value retains previous value");
}

static void test_int_property_list_accepts_valid() {
    IntProperty p(1U, {1U, 2U, 4U, 8U});
    TEST_ASSERT(p.get_int() == 1U, "IntProperty list: initial value accepted");
    p.set_string("4");
    TEST_ASSERT(p.get_int() == 4U, "IntProperty list: accepts value in list");
}

static void test_int_property_list_rejects_invalid() {
    IntProperty p(1U, {1U, 2U, 4U, 8U});
    p.set_string("3"); // not in list
    TEST_ASSERT(p.get_int() == 1U,
        "IntProperty list: value not in list retains previous value");
}


static void test_int_property_clone() {
    IntProperty orig(77U);
    unique_ptr<BasicProperty> copy(orig.clone());
    IntProperty* icp = dynamic_cast<IntProperty*>(copy.get());
    TEST_ASSERT(icp != nullptr, "IntProperty::clone() returns IntProperty*");
    TEST_ASSERT(icp->get_int() == 77U,
        "IntProperty::clone() preserves value");
    icp->set_string("99");
    TEST_ASSERT(orig.get_int() == 77U,
        "IntProperty::clone() is independent");
}

// ============================================================
// BinProperty tests
// ============================================================


static void test_bin_property_set_string_forms() {
    BinProperty p(0);

    p.set_string("on");
    TEST_ASSERT(p.get_val() == 1, "BinProperty: \"on\" sets to 1");
    p.set_string("off");
    TEST_ASSERT(p.get_val() == 0, "BinProperty: \"off\" sets to 0");

    p.set_string("ON");
    TEST_ASSERT(p.get_val() == 1, "BinProperty: \"ON\" sets to 1");
    p.set_string("OFF");
    TEST_ASSERT(p.get_val() == 0, "BinProperty: \"OFF\" sets to 0");

    p.set_string("1");
    TEST_ASSERT(p.get_val() == 1, "BinProperty: \"1\" sets to 1");
    p.set_string("0");
    TEST_ASSERT(p.get_val() == 0, "BinProperty: \"0\" sets to 0");
}


static void test_bin_property_clone() {
    BinProperty orig(1);
    unique_ptr<BasicProperty> copy(orig.clone());
    BinProperty* bcp = dynamic_cast<BinProperty*>(copy.get());
    TEST_ASSERT(bcp != nullptr, "BinProperty::clone() returns BinProperty*");
    TEST_ASSERT(bcp->get_val() == 1, "BinProperty::clone() preserves value");
    bcp->set_string("off");
    TEST_ASSERT(orig.get_val() == 1, "BinProperty::clone() is independent");
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "Machine property tests" << endl;
    cout << "======================" << endl;

    cout << endl << "StrProperty tests:" << endl;
    test_str_property_unconstrained_roundtrip();
    test_str_property_list_accepts_valid_value();
    test_str_property_list_rejects_invalid_value();
    test_str_property_clone();

    cout << endl << "IntProperty tests:" << endl;
    test_int_property_unconstrained_roundtrip();
    test_int_property_range_accepts_valid();
    test_int_property_range_rejects_out_of_range();
    test_int_property_list_accepts_valid();
    test_int_property_list_rejects_invalid();
    test_int_property_clone();

    cout << endl << "BinProperty tests:" << endl;
    test_bin_property_set_string_forms();
    test_bin_property_clone();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;

    return tests_failed ? 1 : 0;
}
