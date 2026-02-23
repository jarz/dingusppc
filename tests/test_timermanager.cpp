/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for the TimerManager.

Tests cover:
  - One-shot timer fires when time advances past its deadline
  - One-shot timer does not fire before its deadline
  - One-shot timer does not fire again after expiry
  - Cyclic timer fires on each interval
  - cancel_timer prevents a pending timer from firing
  - Multiple timers fire in ascending expiry order
  - process_timers returns the correct ns remaining until the next timer

All tests use a fake virtual clock (fake_time_ns) so no real wall-clock
time is consumed and behaviour is fully deterministic.

No ROMs, SDL, or hardware required.
*/

#include <core/timermanager.h>

#include <cstdint>
#include <iostream>
#include <vector>

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

// Virtual clock advanced by each test.
static uint64_t fake_time_ns = 0;

static TimerManager* get_tm() {
    TimerManager* tm = TimerManager::get_instance();
    tm->set_time_now_cb([]() -> uint64_t { return fake_time_ns; });
    tm->set_notify_changes_cb([]() {});
    return tm;
}

// ============================================================
// Tests
// ============================================================

static void test_oneshot_fires_at_deadline() {
    fake_time_ns = 0;
    TimerManager* tm = get_tm();

    int fired = 0;
    tm->add_oneshot_timer(MSECS_TO_NSECS(10), [&]() { fired++; });

    // Must NOT fire before deadline
    fake_time_ns = MSECS_TO_NSECS(9);
    tm->process_timers();
    TEST_ASSERT(fired == 0, "one-shot: no fire before deadline (t=9ms)");

    // Must fire exactly at deadline
    fake_time_ns = MSECS_TO_NSECS(10);
    tm->process_timers();
    TEST_ASSERT(fired == 1, "one-shot: fires at deadline (t=10ms)");

    // Must NOT fire again
    fake_time_ns = MSECS_TO_NSECS(20);
    tm->process_timers();
    TEST_ASSERT(fired == 1, "one-shot: no second fire after expiry");
}

static void test_oneshot_does_not_fire_early() {
    fake_time_ns = 0;
    TimerManager* tm = get_tm();

    int fired = 0;
    uint32_t id = tm->add_oneshot_timer(MSECS_TO_NSECS(100), [&]() { fired++; });

    fake_time_ns = MSECS_TO_NSECS(50);
    tm->process_timers();
    TEST_ASSERT(fired == 0, "one-shot: no fire at t=50ms for 100ms timer");

    tm->cancel_timer(id);
}

static void test_cyclic_fires_on_each_interval() {
    fake_time_ns = 0;
    TimerManager* tm = get_tm();

    int count = 0;
    uint32_t id = tm->add_cyclic_timer(MSECS_TO_NSECS(10), [&]() { count++; });

    fake_time_ns = MSECS_TO_NSECS(10);
    tm->process_timers();
    TEST_ASSERT(count == 1, "cyclic: first fire at t=10ms");

    fake_time_ns = MSECS_TO_NSECS(20);
    tm->process_timers();
    TEST_ASSERT(count == 2, "cyclic: second fire at t=20ms");

    fake_time_ns = MSECS_TO_NSECS(30);
    tm->process_timers();
    TEST_ASSERT(count == 3, "cyclic: third fire at t=30ms");

    tm->cancel_timer(id);
}

static void test_cancel_prevents_firing() {
    fake_time_ns = 0;
    TimerManager* tm = get_tm();

    int fired = 0;
    uint32_t id = tm->add_oneshot_timer(MSECS_TO_NSECS(10), [&]() { fired++; });
    tm->cancel_timer(id);

    fake_time_ns = MSECS_TO_NSECS(20);
    tm->process_timers();
    TEST_ASSERT(fired == 0, "cancelled timer must not fire");
}

static void test_multiple_timers_fire_in_expiry_order() {
    fake_time_ns = 0;
    TimerManager* tm = get_tm();

    vector<int> order;
    // Register in reverse expiry order to verify priority-queue ordering.
    tm->add_oneshot_timer(MSECS_TO_NSECS(30), [&]() { order.push_back(3); });
    tm->add_oneshot_timer(MSECS_TO_NSECS(10), [&]() { order.push_back(1); });
    tm->add_oneshot_timer(MSECS_TO_NSECS(20), [&]() { order.push_back(2); });

    // Advance past all deadlines in one step.
    fake_time_ns = MSECS_TO_NSECS(30);
    tm->process_timers();

    TEST_ASSERT(order.size() == 3,
        "multiple timers: all three must fire");
    TEST_ASSERT(order[0] == 1 && order[1] == 2 && order[2] == 3,
        "multiple timers: must fire in ascending expiry order");
}

static void test_process_timers_returns_time_until_next() {
    fake_time_ns = 0;
    TimerManager* tm = get_tm();

    uint32_t id = tm->add_oneshot_timer(MSECS_TO_NSECS(50), []() {});
    uint64_t remaining = tm->process_timers();
    TEST_ASSERT(remaining == MSECS_TO_NSECS(50),
        "process_timers must return exact ns remaining until next timer");

    tm->cancel_timer(id);
}

static void test_cyclic_cancel_stops_future_fires() {
    fake_time_ns = 0;
    TimerManager* tm = get_tm();

    int count = 0;
    uint32_t id = tm->add_cyclic_timer(MSECS_TO_NSECS(10), [&]() { count++; });

    fake_time_ns = MSECS_TO_NSECS(10);
    tm->process_timers();
    TEST_ASSERT(count == 1, "cyclic: fired once before cancel");

    tm->cancel_timer(id);

    fake_time_ns = MSECS_TO_NSECS(20);
    tm->process_timers();
    TEST_ASSERT(count == 1, "cyclic: no fire after cancel");
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "TimerManager tests" << endl;
    cout << "==================" << endl;

    test_oneshot_fires_at_deadline();
    test_oneshot_does_not_fire_early();
    test_cyclic_fires_on_each_interval();
    test_cancel_prevents_firing();
    test_multiple_timers_fire_in_expiry_order();
    test_process_timers_returns_time_until_next();
    test_cyclic_cancel_stops_future_fires();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;

    return tests_failed ? 1 : 0;
}
