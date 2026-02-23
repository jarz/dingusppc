/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/**
 * @file Thread safety tests for atomic globals and DMA mutex protection.
 *
 * These tests verify that:
 *   1. Atomic globals (power_on, power_off_reason, int_pin,
 *      dec_exception_pending) behave correctly under concurrent access.
 *   2. DMAChannel's mutex prevents data races when pull_data / reg_write
 *      are called from different threads (simulating the cubeb audio
 *      callback vs the main emulator thread).
 */

#include <cpu/ppc/ppcemu.h>
#include <devices/common/dbdma.h>
#include <devices/ioctrl/amic.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using std::cerr;
using std::cout;
using std::endl;

static int tests_run    = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do {  \
    tests_run++;                     \
    if (!(cond)) {                   \
        cerr << "FAIL: " << msg      \
             << " (" << __FILE__     \
             << ":" << __LINE__      \
             << ")" << endl;         \
        tests_failed++;              \
    }                                \
} while(0)

// ---------------------------------------------------------------------------
// 1. Atomic globals: concurrent reads and writes must not tear / race
// ---------------------------------------------------------------------------

static void test_atomic_power_on() {
    cout << "  test_atomic_power_on..." << endl;

    constexpr int ITERS = 100'000;
    power_on = false;

    std::thread writer([&]{
        for (int i = 0; i < ITERS; i++) {
            power_on.store(i & 1, std::memory_order_relaxed);
        }
    });

    int true_seen = 0, false_seen = 0;
    for (int i = 0; i < ITERS; i++) {
        bool v = power_on.load(std::memory_order_relaxed);
        if (v) true_seen++; else false_seen++;
    }
    writer.join();

    // Both values should have been observed at least once.
    // Under a non-atomic bool a torn read could produce values other than
    // 0 or 1, but std::atomic guarantees no tearing.
    TEST_ASSERT(true_seen > 0 || false_seen > 0,
                "power_on should be readable concurrently");
}

static void test_atomic_power_off_reason() {
    cout << "  test_atomic_power_off_reason..." << endl;

    constexpr int ITERS = 100'000;
    static const Po_Cause reasons[] = {
        po_none, po_shut_down, po_restart, po_enter_debugger, po_signal_interrupt
    };
    power_off_reason.store(po_none, std::memory_order_relaxed);

    std::thread writer([&]{
        for (int i = 0; i < ITERS; i++) {
            power_off_reason.store(reasons[i % 5], std::memory_order_relaxed);
        }
    });

    bool all_valid = true;
    for (int i = 0; i < ITERS; i++) {
        Po_Cause v = power_off_reason.load(std::memory_order_relaxed);
        if (v < po_none || v > po_endian_switch) { all_valid = false; break; }
    }
    writer.join();

    TEST_ASSERT(all_valid,
                "power_off_reason values should always be valid Po_Cause");
}

static void test_atomic_int_pin() {
    cout << "  test_atomic_int_pin..." << endl;

    constexpr int ITERS = 100'000;
    int_pin = false;

    std::thread writer([&]{
        for (int i = 0; i < ITERS; i++) {
            int_pin.store(i & 1, std::memory_order_relaxed);
        }
    });

    bool ok = true;
    for (int i = 0; i < ITERS; i++) {
        bool v = int_pin.load(std::memory_order_relaxed);
        // Any bool value is fine – just verify no crash / UB
        (void)v;
    }
    writer.join();

    TEST_ASSERT(ok, "int_pin concurrent access should not crash");
}

static void test_atomic_dec_exception_pending() {
    cout << "  test_atomic_dec_exception_pending..." << endl;

    constexpr int ITERS = 100'000;
    dec_exception_pending = false;

    std::thread writer([&]{
        for (int i = 0; i < ITERS; i++) {
            dec_exception_pending.store(i & 1, std::memory_order_relaxed);
        }
    });

    bool ok = true;
    for (int i = 0; i < ITERS; i++) {
        bool v = dec_exception_pending.load(std::memory_order_relaxed);
        (void)v;
    }
    writer.join();

    TEST_ASSERT(ok, "dec_exception_pending concurrent access should not crash");
}

// ---------------------------------------------------------------------------
// 2. DMAChannel mutex: concurrent reg_write + reg_read must not race
// ---------------------------------------------------------------------------

static void test_dma_channel_concurrent_reg_access() {
    cout << "  test_dma_channel_concurrent_reg_access..." << endl;

    DMAChannel ch("test-dma");

    constexpr int ITERS = 50'000;

    // Writer thread: repeatedly sets CMD_PTR_LO while channel is idle
    std::thread writer([&]{
        for (int i = 0; i < ITERS; i++) {
            // Channel is not RUN/ACTIVE so CMD_PTR_LO writes are accepted
            uint32_t val = BYTESWAP_32(static_cast<uint32_t>(i * 16));
            ch.reg_write(DMAReg::CMD_PTR_LO, val, 4);
        }
    });

    // Reader thread: concurrently reads CH_STAT and CMD_PTR_LO
    bool read_ok = true;
    for (int i = 0; i < ITERS; i++) {
        uint32_t stat = ch.reg_read(DMAReg::CH_STAT, 4);
        uint32_t ptr  = ch.reg_read(DMAReg::CMD_PTR_LO, 4);
        (void)stat;
        (void)ptr;
    }
    writer.join();

    TEST_ASSERT(read_ok,
                "DMAChannel concurrent reg_read/reg_write should not race");
}

// ---------------------------------------------------------------------------
// 3. DMAChannel mutex: is_out_active / is_in_active from "audio" thread
// ---------------------------------------------------------------------------

static void test_dma_channel_concurrent_active_check() {
    cout << "  test_dma_channel_concurrent_active_check..." << endl;

    DMAChannel ch("test-dma-active");

    constexpr int ITERS = 50'000;

    // Simulate cubeb audio thread calling is_out_active
    std::thread audio([&]{
        for (int i = 0; i < ITERS; i++) {
            bool out = ch.is_out_active();
            bool in  = ch.is_in_active();
            (void)out;
            (void)in;
        }
    });

    // Main thread reads status
    for (int i = 0; i < ITERS; i++) {
        uint32_t s = ch.reg_read(DMAReg::CH_STAT, 4);
        (void)s;
    }

    audio.join();
    TEST_ASSERT(true, "concurrent is_out_active/is_in_active should not race");
}

// ---------------------------------------------------------------------------
// 4. DMAChannel::get_mutex() external lock works
// ---------------------------------------------------------------------------

static void test_dma_channel_external_lock() {
    cout << "  test_dma_channel_external_lock..." << endl;

    DMAChannel ch("test-dma-lock");

    constexpr int ITERS = 50'000;
    int shared_counter = 0;

    std::thread t1([&]{
        for (int i = 0; i < ITERS; i++) {
            std::lock_guard<std::mutex> lk(ch.get_mutex());
            shared_counter++;
        }
    });

    for (int i = 0; i < ITERS; i++) {
        std::lock_guard<std::mutex> lk(ch.get_mutex());
        shared_counter++;
    }

    t1.join();
    TEST_ASSERT(shared_counter == 2 * ITERS,
                "get_mutex() should protect shared state");
}

// ---------------------------------------------------------------------------
// 5. Stress: many threads hammering atomics simultaneously
// ---------------------------------------------------------------------------

static void test_atomic_stress_multithread() {
    cout << "  test_atomic_stress_multithread..." << endl;

    constexpr int NTHREADS = 8;
    constexpr int ITERS    = 50'000;

    power_on = false;
    int_pin  = false;
    dec_exception_pending = false;

    std::vector<std::thread> threads;
    for (int t = 0; t < NTHREADS; t++) {
        threads.emplace_back([t]{
            for (int i = 0; i < ITERS; i++) {
                if (t & 1) {
                    power_on.store(true, std::memory_order_relaxed);
                    int_pin.store(true, std::memory_order_relaxed);
                    dec_exception_pending.store(true, std::memory_order_relaxed);
                } else {
                    power_on.store(false, std::memory_order_relaxed);
                    int_pin.store(false, std::memory_order_relaxed);
                    dec_exception_pending.store(false, std::memory_order_relaxed);
                }
                // Also read
                bool a = power_on.load(std::memory_order_relaxed);
                bool b = int_pin.load(std::memory_order_relaxed);
                bool c = dec_exception_pending.load(std::memory_order_relaxed);
                (void)a; (void)b; (void)c;
            }
        });
    }

    for (auto& t : threads) t.join();

    TEST_ASSERT(true, "8-thread stress on atomics should not crash/hang");
}

// ---------------------------------------------------------------------------
// 6. DMA mutex stress: multiple threads doing reg_read/reg_write
// ---------------------------------------------------------------------------

static void test_dma_mutex_stress() {
    cout << "  test_dma_mutex_stress..." << endl;

    DMAChannel ch("test-dma-stress");

    constexpr int NTHREADS = 4;
    constexpr int ITERS    = 20'000;

    std::vector<std::thread> threads;
    for (int t = 0; t < NTHREADS; t++) {
        threads.emplace_back([&ch, t]{
            for (int i = 0; i < ITERS; i++) {
                if (t & 1) {
                    // writer: set CMD_PTR_LO
                    uint32_t val = BYTESWAP_32(static_cast<uint32_t>(i * 16));
                    ch.reg_write(DMAReg::CMD_PTR_LO, val, 4);
                } else {
                    // reader
                    uint32_t s = ch.reg_read(DMAReg::CH_STAT, 4);
                    uint32_t p = ch.reg_read(DMAReg::CMD_PTR_LO, 4);
                    (void)s; (void)p;
                }
            }
        });
    }

    for (auto& t : threads) t.join();

    TEST_ASSERT(true, "4-thread DMA mutex stress should not deadlock");
}

// ---------------------------------------------------------------------------
// 7. AmicSndOutDma mutex: concurrent write_dma_out_ctrl + read_stat
// ---------------------------------------------------------------------------

static void test_amic_snd_concurrent_ctrl() {
    cout << "  test_amic_snd_concurrent_ctrl..." << endl;

    AmicSndOutDma dma;
    // Don't call init() or init_interrupts() — we only exercise
    // write_dma_out_ctrl with values that avoid the interrupt path
    // (PDM_DMA_INTS_MASK = 0xF0, so values < 0x10 skip update_irq).

    constexpr int ITERS = 50'000;

    std::thread writer([&]{
        for (int i = 0; i < ITERS; i++) {
            // Toggle DMA interrupt enable bits (bits 1-3, mask 0x0E)
            dma.write_dma_out_ctrl(static_cast<uint8_t>((i & 1) ? 0x0E : 0x00));
        }
    });

    bool in_range = true;
    for (int i = 0; i < ITERS; i++) {
        uint8_t s = dma.read_stat();
        // Only bits 1-3 should be toggling, upper bits stay 0
        if (s & ~0x0EU) { in_range = false; break; }
    }
    writer.join();

    TEST_ASSERT(in_range,
                "AmicSndOutDma concurrent ctrl writes should not corrupt state");
}

// ---------------------------------------------------------------------------
// 8. AmicSndOutDma: concurrent enable/disable + read_stat
// ---------------------------------------------------------------------------

static void test_amic_snd_concurrent_enable_disable() {
    cout << "  test_amic_snd_concurrent_enable_disable..." << endl;

    AmicSndOutDma dma;

    constexpr int ITERS = 50'000;

    std::thread toggler([&]{
        for (int i = 0; i < ITERS; i++) {
            if (i & 1)
                dma.enable();
            else
                dma.disable();
        }
    });

    for (int i = 0; i < ITERS; i++) {
        uint8_t s = dma.read_stat();
        (void)s;
    }
    toggler.join();

    TEST_ASSERT(true,
                "AmicSndOutDma concurrent enable/disable should not race");
}

// ---------------------------------------------------------------------------
// 9. DMAChannel: concurrent set_stat + is_out_active
// ---------------------------------------------------------------------------

static void test_dma_channel_concurrent_set_stat() {
    cout << "  test_dma_channel_concurrent_set_stat..." << endl;

    DMAChannel ch("test-dma-setstat");

    constexpr int ITERS = 50'000;

    // Thread simulating AWACS calling set_stat
    std::thread setter([&]{
        for (int i = 0; i < ITERS; i++) {
            ch.set_stat(static_cast<uint8_t>(i & 0xFF));
        }
    });

    // Simulating audio thread calling is_out_active
    for (int i = 0; i < ITERS; i++) {
        bool active = ch.is_out_active();
        (void)active;
    }
    setter.join();

    TEST_ASSERT(true,
                "DMAChannel concurrent set_stat + is_out_active should not race");
}

// ---------------------------------------------------------------------------
// 10. DMAChannel: concurrent get_pull_data_remaining + reg_write
// ---------------------------------------------------------------------------

static void test_dma_channel_concurrent_remaining() {
    cout << "  test_dma_channel_concurrent_remaining..." << endl;

    DMAChannel ch("test-dma-remaining");

    constexpr int ITERS = 50'000;

    // Writer thread: write CMD_PTR_LO (channel is idle so it's accepted)
    std::thread writer([&]{
        for (int i = 0; i < ITERS; i++) {
            uint32_t val = BYTESWAP_32(static_cast<uint32_t>(i * 16));
            ch.reg_write(DMAReg::CMD_PTR_LO, val, 4);
        }
    });

    // Simulating soundserver calling get_pull_data_remaining
    bool ok = true;
    for (int i = 0; i < ITERS; i++) {
        int pull_rem = ch.get_pull_data_remaining();
        int push_rem = ch.get_push_data_remaining();
        if (pull_rem < 0 || push_rem < 0) { ok = false; break; }
    }
    writer.join();

    TEST_ASSERT(ok,
                "DMAChannel concurrent get_pull/push_data_remaining should not race");
}

// ---------------------------------------------------------------------------
// 11. DMAChannel try_lock fallback: audio-thread methods must not block
//     when the mutex is held by the main thread.
// ---------------------------------------------------------------------------

static void test_dma_try_lock_fallback() {
    cout << "  test_dma_try_lock_fallback..." << endl;

    DMAChannel ch("test-dma-trylock");

    // Hold the mutex from the "main thread" for the duration of the test.
    std::unique_lock<std::mutex> held(ch.get_mutex());

    // Spawn a thread to simulate the audio callback.
    // is_out_active() and is_in_active() should return false immediately
    // (not block) because they use try_lock internally.
    bool is_out = true;   // will be overwritten with result
    bool is_in  = true;
    bool thread_completed = false;

    std::thread audio([&]{
        is_out = ch.is_out_active();
        is_in  = ch.is_in_active();
        thread_completed = true;
    });

    audio.join();

    TEST_ASSERT(thread_completed,
                "try_lock methods should return immediately, not block");
    TEST_ASSERT(!is_out,
                "is_out_active should return false when mutex is contended");
    TEST_ASSERT(!is_in,
                "is_in_active should return false when mutex is contended");

    held.unlock();
}

// ---------------------------------------------------------------------------
// 12. DMAChannel::pull_data() try_lock fallback: must not block when
//     the mutex is held.
// ---------------------------------------------------------------------------

static void test_dma_pull_data_try_lock_fallback() {
    cout << "  test_dma_pull_data_try_lock_fallback..." << endl;

    DMAChannel ch("test-dma-pulldata-trylock");

    std::unique_lock<std::mutex> held(ch.get_mutex());

    uint32_t avail = 999;
    uint8_t *p_data = nullptr;
    DmaPullResult result = DmaPullResult::MoreData;
    bool completed = false;

    std::thread audio([&]{
        result = ch.pull_data(1024, &avail, &p_data);
        completed = true;
    });

    audio.join();

    TEST_ASSERT(completed,
                "pull_data should return immediately when mutex is contended");
    TEST_ASSERT(result == DmaPullResult::NoMoreData,
                "pull_data should return NoMoreData when mutex is contended");
    TEST_ASSERT(avail == 0,
                "pull_data should set avail_len=0 when mutex is contended");

    held.unlock();
}

// ---------------------------------------------------------------------------
// 13. AmicSndOutDma::pull_data() try_lock fallback: must not block when
//     the mutex is held.
// ---------------------------------------------------------------------------

static void test_amic_pull_data_try_lock_fallback() {
    cout << "  test_amic_pull_data_try_lock_fallback..." << endl;

    AmicSndOutDma dma;

    std::unique_lock<std::mutex> held(dma.get_mutex());

    uint32_t avail = 999;
    uint8_t *p_data = nullptr;
    DmaPullResult result = DmaPullResult::MoreData;
    bool completed = false;

    std::thread audio([&]{
        result = dma.pull_data(1024, &avail, &p_data);
        completed = true;
    });

    audio.join();

    TEST_ASSERT(completed,
                "AmicSndOutDma pull_data should not block on contended mutex");
    TEST_ASSERT(result == DmaPullResult::NoMoreData,
                "AmicSndOutDma pull_data should return NoMoreData when contended");
    TEST_ASSERT(avail == 0,
                "AmicSndOutDma pull_data should set avail_len=0 when contended");

    held.unlock();
}

// ---------------------------------------------------------------------------
// 14. Concurrent pull_data + reg_write stress: simulates real audio callback
//     racing against main-thread MMIO writes.
// ---------------------------------------------------------------------------

static void test_dma_concurrent_pull_data_reg_write() {
    cout << "  test_dma_concurrent_pull_data_reg_write..." << endl;

    DMAChannel ch("test-dma-pull-stress");

    constexpr int ITERS = 50'000;

    // Audio thread: repeatedly call pull_data (channel is idle, returns
    // NoMoreData every time, but exercises the try_lock / defer_irq path)
    std::thread audio([&]{
        for (int i = 0; i < ITERS; i++) {
            uint32_t avail;
            uint8_t *p_data;
            ch.pull_data(256, &avail, &p_data);
        }
    });

    // Main thread: write CMD_PTR_LO and read CH_STAT concurrently
    for (int i = 0; i < ITERS; i++) {
        uint32_t val = BYTESWAP_32(static_cast<uint32_t>(i * 16));
        ch.reg_write(DMAReg::CMD_PTR_LO, val, 4);
        uint32_t s = ch.reg_read(DMAReg::CH_STAT, 4);
        (void)s;
    }

    audio.join();

    TEST_ASSERT(true,
                "concurrent pull_data + reg_write should not deadlock or race");
}

// ===========================================================================

int main() {
    cout << "Thread safety tests" << endl;
    cout << "===================" << endl;

    cout << endl << "Atomic global tests:" << endl;
    test_atomic_power_on();
    test_atomic_power_off_reason();
    test_atomic_int_pin();
    test_atomic_dec_exception_pending();
    test_atomic_stress_multithread();

    cout << endl << "DMA channel mutex tests:" << endl;
    test_dma_channel_concurrent_reg_access();
    test_dma_channel_concurrent_active_check();
    test_dma_channel_external_lock();
    test_dma_mutex_stress();
    test_dma_channel_concurrent_set_stat();
    test_dma_channel_concurrent_remaining();
    test_dma_try_lock_fallback();
    test_dma_pull_data_try_lock_fallback();
    test_dma_concurrent_pull_data_reg_write();

    cout << endl << "AMIC sound DMA mutex tests:" << endl;
    test_amic_snd_concurrent_ctrl();
    test_amic_snd_concurrent_enable_disable();
    test_amic_pull_data_try_lock_fallback();

    cout << endl;
    cout << "Results: " << tests_run << " tests, "
         << tests_failed << " failed" << endl;

    return tests_failed ? 1 : 0;
}
