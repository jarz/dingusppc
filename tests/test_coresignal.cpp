/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for core/coresignal.h */

#include <core/coresignal.h>
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

static void test_connect_and_emit() {
    CoreSignal<int> sig;
    int received = 0;

    sig.connect_func([&received](int val) {
        received = val;
    });

    sig.emit(42);
    CHECK_EQ(received, 42);

    sig.emit(100);
    CHECK_EQ(received, 100);
}

static void test_multiple_slots() {
    CoreSignal<int> sig;
    int sum = 0;

    sig.connect_func([&sum](int val) { sum += val; });
    sig.connect_func([&sum](int val) { sum += val * 10; });

    sig.emit(5);
    CHECK_EQ(sum, 55); // 5 + 50
}

static void test_disconnect() {
    CoreSignal<int> sig;
    int received = 0;

    int id = sig.connect_func([&received](int val) {
        received = val;
    });

    sig.emit(42);
    CHECK_EQ(received, 42);

    sig.disconnect(id);

    sig.emit(100);
    CHECK_EQ(received, 42); // should not have changed
}

static void test_disconnect_all() {
    CoreSignal<int> sig;
    int count = 0;

    sig.connect_func([&count](int) { count++; });
    sig.connect_func([&count](int) { count++; });
    sig.connect_func([&count](int) { count++; });

    sig.emit(0);
    CHECK_EQ(count, 3);

    sig.disconnect_all();

    sig.emit(0);
    CHECK_EQ(count, 3); // should not have changed
}

static void test_enable_disable() {
    CoreSignal<int> sig;
    int received = 0;

    sig.connect_func([&received](int val) {
        received = val;
    });

    CHECK_TRUE(sig.is_enabled());

    sig.emit(42);
    CHECK_EQ(received, 42);

    sig.disable();
    CHECK_FALSE(sig.is_enabled());

    sig.emit(100);
    CHECK_EQ(received, 42); // should not have changed

    sig.enable();
    CHECK_TRUE(sig.is_enabled());

    sig.emit(200);
    CHECK_EQ(received, 200);
}

static void test_no_args_signal() {
    CoreSignal<> sig;
    int count = 0;

    sig.connect_func([&count]() { count++; });

    sig.emit();
    CHECK_EQ(count, 1);

    sig.emit();
    CHECK_EQ(count, 2);
}

static void test_multi_arg_signal() {
    CoreSignal<int, string> sig;
    int got_int = 0;
    string got_str;

    sig.connect_func([&got_int, &got_str](int i, string s) {
        got_int = i;
        got_str = s;
    });

    sig.emit(42, "hello");
    CHECK_EQ(got_int, 42);
    CHECK_EQ(got_str, string("hello"));
}

class Receiver {
public:
    int last_value = 0;
    void on_signal(int val) { last_value = val; }
};

static void test_connect_method() {
    CoreSignal<int> sig;
    Receiver r;

    sig.connect_method(&r, &Receiver::on_signal);

    sig.emit(99);
    CHECK_EQ(r.last_value, 99);

    sig.emit(7);
    CHECK_EQ(r.last_value, 7);
}

static void test_unique_connection_ids() {
    CoreSignal<int> sig;

    int id1 = sig.connect_func([](int) {});
    int id2 = sig.connect_func([](int) {});
    int id3 = sig.connect_func([](int) {});

    CHECK_TRUE(id1 != id2);
    CHECK_TRUE(id2 != id3);
    CHECK_TRUE(id1 != id3);
}

int main() {
    cout << "Running coresignal tests..." << endl;

    test_connect_and_emit();
    test_multiple_slots();
    test_disconnect();
    test_disconnect_all();
    test_enable_disable();
    test_no_args_signal();
    test_multi_arg_signal();
    test_connect_method();
    test_unique_connection_ids();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
