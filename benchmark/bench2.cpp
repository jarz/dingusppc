/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

(You may also contact divingkxt or powermax2286 on Discord)

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

/** @file Interpreter dispatch micro-benchmark.

    Two PPC kernels to stress-test the interpreter:
      1. ALU-heavy loop with Rc=1 (record) instructions — measures
         ppc_changecrf0 inlining benefit (cross-TU for poweropcodes.cpp).
      2. Tight branch loop — measures exec_flags / branch-path overhead.
*/

#include <stdlib.h>
#include <chrono>
#include "cpu/ppc/ppcemu.h"
#include "cpu/ppc/ppcmmu.h"
#include "devices/memctrl/mpc106.h"
#include <thirdparty/loguru/loguru.hpp>
#include <debugger/debugger.h>

#if defined(PPC_BENCHMARKS)
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    power_on = false;
    power_off_reason = po_benchmark_exception;
}
#endif

/* Kernel 1: ALU-heavy with Rc=1 instructions.
   PPC assembly:
     li r3, 0              # 0x00: r3 = 0 (accumulator)
     lis r4, 0x0010        # 0x04: r4 = 0x00100000 (1M iterations)
     mtctr r4              # 0x08: CTR = r4
   loop:
     addi  r3, r3, 7       # 0x0C: r3 += 7
     addic. r3, r3, 1      # 0x10: r3 += 1 (sets CR0 via Rc=1)
     rlwinm. r5, r3, 2, 0, 29  # 0x14: r5 = (r3 << 2) & mask, sets CR0
     add   r3, r3, r5      # 0x18: r3 += r5
     bdnz  loop            # 0x1C: --CTR, branch if CTR != 0
     .long 0x00005AF0      # 0x20: invalid opcode — stops ppc_exec
*/
uint32_t alu_rc1_code[] = {
    0x38600000,  /* li    r3, 0          */
    0x3C800010,  /* lis   r4, 0x0010     */
    0x7C8903A6,  /* mtctr r4             */
    0x38630007,  /* addi  r3, r3, 7      */
    0x34630001,  /* addic. r3, r3, 1     */
    0x5465103B,  /* rlwinm. r5, r3, 2, 0, 29 */
    0x7C632A14,  /* add   r3, r3, r5     */
    0x4200FFF0,  /* bdnz  -16 (to 0x0C)  */
    0x00005AF0,  /* stop                 */
};

/* Kernel 2: Tight branch-only loop (measures branch dispatch overhead).
   PPC assembly:
     li  r4, 0x2000        # 0x00: r4 = 8192 iterations
     mtctr r4              # 0x04: CTR = r4
   loop:
     bdnz  loop            # 0x08: --CTR, branch if CTR != 0 (self-loop)
     .long 0x00005AF0      # 0x0C: stop
*/
uint32_t branch_loop_code[] = {
    0x38802000,  /* li    r4, 0x2000     */
    0x7C8903A6,  /* mtctr r4             */
    0x42000000,  /* bdnz  +0 (to 0x08)   */
    0x00005AF0,  /* stop                 */
};

constexpr uint32_t branch_loop_iters = 0x2000; /* 8K */

constexpr uint32_t test_samples    = 50;
constexpr uint32_t test_iterations = 5;

struct BenchKernel {
    const char*     name;
    uint32_t*       code;
    size_t          code_words;
    uint32_t        stop_addr;
    uint32_t        loop_iters;  /* iterations in the PPC loop */
};

static void run_bench(MPC106* /*grackle_obj*/, const BenchKernel& kern) {
    LOG_F(INFO, "=== %s ===", kern.name);

    // Timing overhead calibration
    uint64_t overhead = -1ULL;
    for (int j = 0; j < test_samples; j++) {
        auto s = std::chrono::steady_clock::now();
        auto e = std::chrono::steady_clock::now();
        auto t = std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
        if ((uint64_t)t < overhead) overhead = t;
    }

    // Warm-up run
    ppc_state.pc = 0;
    ppc_state.gpr[3] = 0;
    ppc_state.gpr[4] = 0;
    ppc_state.gpr[5] = 0;
    power_on = true;
    ppc_exec_until(kern.stop_addr);

    for (int i = 0; i < test_iterations; i++) {
        uint64_t best = -1ULL;
        for (int j = 0; j < test_samples; j++) {
            ppc_state.pc = 0;
            ppc_state.gpr[3] = 0;
            ppc_state.gpr[4] = 0;
            ppc_state.gpr[5] = 0;
            power_on = true;

            auto s = std::chrono::steady_clock::now();
            ppc_exec_until(kern.stop_addr);
            auto e = std::chrono::steady_clock::now();
            auto t = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            if (t < best) best = t;
        }
        best -= overhead;
        double mips = 1e9 * (double)kern.loop_iters / best / 1e6;
        LOG_F(INFO, "(%d) %llu ns, %.2f Minsn/s", i + 1,
              (unsigned long long)best, mips);
    }
}

int main(int argc, char** argv) {
    loguru::g_preamble_date   = false;
    loguru::g_preamble_time   = false;
    loguru::g_preamble_thread = false;
    loguru::g_stderr_verbosity = 0;
    loguru::init(argc, argv);

    MPC106* grackle_obj = new MPC106;
    if (!grackle_obj->add_ram_region(0, 0x10000)) {
        LOG_F(ERROR, "Could not create RAM region");
        delete grackle_obj;
        return -1;
    }

    constexpr uint64_t tbr_freq = 16705000;
    ppc_cpu_init(grackle_obj, PPC_VER::MPC750, false, tbr_freq);

    BenchKernel kernels[] = {
        {
            "ALU Rc=1 loop (addic. + rlwinm. — 1M x 5 insns)",
            alu_rc1_code,
            sizeof(alu_rc1_code) / sizeof(alu_rc1_code[0]),
            0x20, /* stop_addr */
            0x100000, /* 1M iterations */
        },
        {
            "Tight branch loop (bdnz — 64K x 1 insn)",
            branch_loop_code,
            sizeof(branch_loop_code) / sizeof(branch_loop_code[0]),
            0x0C, /* stop_addr */
            branch_loop_iters,
        },
    };

    for (auto& kern : kernels) {
        // Load code
        for (size_t i = 0; i < kern.code_words; i++)
            mmu_write_vmem<uint32_t>(0, (uint32_t)(i * 4), kern.code[i]);
        run_bench(grackle_obj, kern);
    }

    delete grackle_obj;
    return 0;
}
