/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Link-time replacement for mmu_map_dma_mem: provides a flat 64 KB
test memory so networking tests can exercise DMA without a full MMU.
Also bootstraps TimerManager for test use.
*/

#include <cpu/ppc/ppcmmu.h>
#include <core/timermanager.h>

#include <array>
#include <cstdint>

std::array<uint8_t, 65536> g_test_mem{};

MapDmaResult mmu_map_dma_mem(uint32_t addr, uint32_t size, bool /*allow_mmio*/) {
    MapDmaResult r{};
    if (addr + size <= g_test_mem.size()) {
        r.type = 0;
        r.is_writable = true;
        r.host_va = &g_test_mem[addr];
    }
    return r;
}

static uint64_t g_fake_ns = 0;

void init_timer_manager() {
    auto* tm = TimerManager::get_instance();
    tm->set_time_now_cb([]() -> uint64_t { return g_fake_ns; });
    tm->set_notify_changes_cb([]() {});
}
