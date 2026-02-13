// libFuzzer harness for short PPC instruction sequences
#include "cpu/ppc/ppcemu.h"
#include "cpu/ppc/ppcmmu.h"
#include "devices/memctrl/memctrlbase.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <setjmp.h>
#include <loguru.hpp>
#include <core/timermanager.h>

extern bool power_on;
extern unsigned exec_flags;
extern uint32_t ppc_next_instruction_address;
extern uint64_t g_icycles;

static constexpr uint32_t RAM_SIZE = 16 * 1024 * 1024; // 16MB
static std::unique_ptr<MemCtrlBase> g_memctrl;
static std::vector<uint8_t> g_ram(RAM_SIZE);
static bool g_initialized = false;

static inline uint32_t be32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
static inline uint32_t be32_looping(const uint8_t* data, size_t size, size_t off) {
    uint32_t v = 0;
    for (int j = 0; j < 4; ++j) {
        v = (v << 8) | data[(off + j) % size];
    }
    return v;
}

static void ppc_illegalop_safe(uint32_t) {}
static void scrub_illegal_opcodes() {
    constexpr size_t OPCODE_TABLE_SIZE = 64 * 2048;
    for (size_t i = 0; i < OPCODE_TABLE_SIZE; ++i) {
        if (ppc_opcode_grabber[i] == ppc_illegalop) {
            ppc_opcode_grabber[i] = ppc_illegalop_safe;
        }
    }
}

static void init_once() {
    if (g_initialized) return;
    loguru::g_stderr_verbosity = -9;
    is_deterministic = true;
    g_memctrl = std::make_unique<MemCtrlBase>();
    g_memctrl->add_ram_region(0, RAM_SIZE, g_ram.data());
    ppc_cpu_init(g_memctrl.get(), PPC_VER::MPC750, false, 62500000 /*tb_freq*/);
    scrub_illegal_opcodes();
    power_on = true;
    g_initialized = true;
}

static void reset_state(const uint8_t *data, size_t size) {
    ppc_cpu_init(g_memctrl.get(), PPC_VER::MPC750, false, 62500000);
    TimerManager::get_instance()->reset();
    scrub_illegal_opcodes();
    power_on = true;
    exec_flags = 0;
    g_icycles = 0;
    ppc_state.pc = 0;

    // Seed registers from input to exercise interactions
    for (int i = 0; i < 32; ++i) {
        uint32_t v = 0;
        if (size > 0) {
            size_t off = (i * 4u) % size;
            bool can_read4 = size >= 4 && off + 4 <= size;
            v = can_read4 ? be32(data + off) : be32_looping(data, size, off);
        }
        ppc_state.gpr[i] = v;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    init_once();
    if (size < 4) return 0;
    reset_state(data, size);

    // Copy up to 32 instructions into RAM at 0
    const size_t max_insns = 32;
    size_t insns = std::min<size_t>(max_insns, size / 4);
    if (insns == 0) return 0;
    std::memcpy(g_ram.data(), data, insns * 4);

    // Execute insns with safety bounds
    if (setjmp(exc_env)) {
        power_on = false;
        return 0;
    }
    for (size_t i = 0; i < insns; ++i) {
        ppc_exec_single();
        if (!power_on) break;
        if (ppc_state.pc >= insns * 4) break;
    }
    power_on = false;
    return 0;
}
