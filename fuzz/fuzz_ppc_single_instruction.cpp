/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// libFuzzer harness for single PPC instruction execution.
//
// Takes a 4-byte fuzz input, interprets it as a big-endian PPC opcode,
// resets CPU register state, and dispatches through the opcode table.
// This exercises every instruction handler's bit-field extraction and
// state-transition logic under sanitizer instrumentation.

#include "cpu/ppc/ppcemu.h"
#include "cpu/ppc/ppcmmu.h"
#include <core/timermanager.h>
#include <devices/memctrl/memctrlbase.h>
#include <cstdint>
#include <cstring>

// Stub: absorb exceptions without crashing the fuzzer.
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    power_on = false;
}

static bool g_initialized = false;

static void fuzz_init() {
    if (g_initialized)
        return;

    // Provide a minimal memory controller so load/store instructions
    // hit the "unmapped memory" path instead of crashing on nullptr.
    mem_ctrl_instance = new MemCtrlBase();

    // Set up the TimerManager so SPR writes that update the
    // decrementer / timebase don't crash on uninitialised callbacks.
    TimerManager::get_instance()->set_time_now_cb([]() -> uint64_t { return 0; });
    TimerManager::get_instance()->set_notify_changes_cb([]() {});

    is_601 = true;
    initialize_ppc_opcode_table();
    ppc_mmu_init();

    // Enable FP so floating-point opcodes are exercised.
    ppc_msr_did_change(ppc_state.msr, MSR::ME | MSR::IP | MSR::FP, false);

    g_initialized = true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4)
        return 0;

    fuzz_init();

    // Interpret the first 4 bytes as a big-endian PPC opcode.
    uint32_t opcode = (uint32_t(data[0]) << 24) |
                      (uint32_t(data[1]) << 16) |
                      (uint32_t(data[2]) <<  8) |
                      (uint32_t(data[3]));

    // Reset volatile CPU state between iterations so results are
    // deterministic and don't accumulate across runs.
    std::memset(ppc_state.gpr, 0, sizeof(ppc_state.gpr));
    std::memset(ppc_state.fpr, 0, sizeof(ppc_state.fpr));
    ppc_state.cr  = 0;
    ppc_state.fpscr = 0;
    ppc_state.spr[SPR::XER] = 0;
    ppc_state.pc  = 0;
    exec_flags    = 0;
    power_on      = true;

    // Reset MSR to prevent a previous iteration's mtmsr from enabling
    // address translation (IR/DR), which would abort in the MMU when
    // no page tables are set up.
    ppc_state.msr = MSR::ME | MSR::IP | MSR::FP;

    // When extra bytes are available, use them to seed CPU state so the
    // fuzzer can explore deeper paths (e.g., conditional branches taken
    // based on CR bits, supervisor instructions, XER-dependent logic).
    const uint8_t *extra = data + 4;
    size_t remaining = size - 4;

    if (remaining >= 1) {
        // Replicate the byte across all four CR fields (CR0-CR7 are 4 bits each).
        ppc_state.cr = uint32_t(extra[0]) * 0x01010101u;
    }
    if (remaining >= 2) {
        // Place fuzzed bits in the XER upper byte (SO, OV, CA at bits 31-29).
        ppc_state.spr[SPR::XER] = uint32_t(extra[1]) << 24;
    }
    if (remaining >= 6) {
        // Seed two source GPRs commonly used by instructions.
        ppc_state.gpr[3] = (uint32_t(extra[2]) << 24) | (uint32_t(extra[3]) << 16) |
                           (uint32_t(extra[4]) << 8)  |  uint32_t(extra[5]);
    }
    if (remaining >= 10) {
        ppc_state.gpr[4] = (uint32_t(extra[6]) << 24) | (uint32_t(extra[7]) << 16) |
                           (uint32_t(extra[8]) << 8)  |  uint32_t(extra[9]);
    }

    // Dispatch the fuzzed opcode.
    ppc_main_opcode(ppc_opcode_grabber, opcode);

    return 0;
}
