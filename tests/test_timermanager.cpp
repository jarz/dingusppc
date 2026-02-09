/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/** Unit tests for my_priority_queue from core/timermanager.h */

#include <core/timermanager.h>
#include <cinttypes>
#include <iostream>
#include <memory>

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

static shared_ptr<TimerInfo> make_timer(uint32_t id, uint64_t timeout) {
    auto ti = make_shared<TimerInfo>();
    ti->id = id;
    ti->timeout_ns = timeout;
    ti->interval_ns = 0;
    ti->cb = []() {};
    return ti;
}

static void test_priority_queue_ordering() {
    my_priority_queue<shared_ptr<TimerInfo>, vector<shared_ptr<TimerInfo>>, MyGtComparator> pq;

    // push timers in non-sorted order
    pq.push(make_timer(1, 300));
    pq.push(make_timer(2, 100));
    pq.push(make_timer(3, 200));

    // pop should return in ascending timeout order (min-heap via MyGtComparator)
    auto t = pq.pop();
    CHECK_EQ(t->id, 2u);
    CHECK_EQ(t->timeout_ns, 100ULL);

    t = pq.pop();
    CHECK_EQ(t->id, 3u);
    CHECK_EQ(t->timeout_ns, 200ULL);

    t = pq.pop();
    CHECK_EQ(t->id, 1u);
    CHECK_EQ(t->timeout_ns, 300ULL);

    CHECK_TRUE(pq.empty());
}

static void test_priority_queue_remove_by_id() {
    my_priority_queue<shared_ptr<TimerInfo>, vector<shared_ptr<TimerInfo>>, MyGtComparator> pq;

    pq.push(make_timer(1, 100));
    pq.push(make_timer(2, 200));
    pq.push(make_timer(3, 300));

    // remove middle element
    CHECK_TRUE(pq.remove_by_id(2));
    CHECK_EQ(pq.size(), (size_t)2);

    // removed id should not be found
    CHECK_FALSE(pq.remove_by_id(2));

    // remaining elements in order
    auto t = pq.pop();
    CHECK_EQ(t->id, 1u);

    t = pq.pop();
    CHECK_EQ(t->id, 3u);

    CHECK_TRUE(pq.empty());
}

static void test_priority_queue_remove_top() {
    my_priority_queue<shared_ptr<TimerInfo>, vector<shared_ptr<TimerInfo>>, MyGtComparator> pq;

    pq.push(make_timer(1, 100));
    pq.push(make_timer(2, 200));

    // remove the top element by id
    CHECK_TRUE(pq.remove_by_id(1));
    CHECK_EQ(pq.size(), (size_t)1);

    auto t = pq.pop();
    CHECK_EQ(t->id, 2u);
}

static void test_priority_queue_remove_nonexistent() {
    my_priority_queue<shared_ptr<TimerInfo>, vector<shared_ptr<TimerInfo>>, MyGtComparator> pq;

    pq.push(make_timer(1, 100));

    // removing non-existent id returns false
    CHECK_FALSE(pq.remove_by_id(999));
    CHECK_EQ(pq.size(), (size_t)1);
}

static void test_priority_queue_single_element() {
    my_priority_queue<shared_ptr<TimerInfo>, vector<shared_ptr<TimerInfo>>, MyGtComparator> pq;

    pq.push(make_timer(42, 500));
    CHECK_EQ(pq.size(), (size_t)1);

    auto t = pq.pop();
    CHECK_EQ(t->id, 42u);
    CHECK_EQ(t->timeout_ns, 500ULL);
    CHECK_TRUE(pq.empty());
}

static void test_time_constants() {
    CHECK_EQ(NS_PER_SEC, 1000000000);
    CHECK_EQ(USEC_PER_SEC, 1000000);
    CHECK_EQ(NS_PER_USEC, 1000);
    CHECK_EQ(NS_PER_MSEC, 1000000);
    CHECK_EQ(ONE_BILLION_NS, 1000000000);
    CHECK_EQ(USECS_TO_NSECS(1), 1000);
    CHECK_EQ(USECS_TO_NSECS(1000), 1000000);
    CHECK_EQ(MSECS_TO_NSECS(1), 1000000);
    CHECK_EQ(MSECS_TO_NSECS(1000), 1000000000);
}

int main() {
    cout << "Running timermanager tests..." << endl;

    test_priority_queue_ordering();
    test_priority_queue_remove_by_id();
    test_priority_queue_remove_top();
    test_priority_queue_remove_nonexistent();
    test_priority_queue_single_element();
    test_time_constants();

    cout << "Tested: " << ntested << ", Failed: " << nfailed << endl;
    return nfailed ? 1 : 0;
}
