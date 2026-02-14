/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Benchmark specifically for instruction dispatch overhead
*/

#include <stdlib.h>
#include <chrono>
#include "cpu/ppc/ppcemu.h"
#include "cpu/ppc/ppcmmu.h"
#include "devices/memctrl/mpc106.h"
#include <thirdparty/loguru/loguru.hpp>

#if defined(PPC_BENCHMARKS)
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    power_on = false;
    power_off_reason = po_benchmark_exception;
}
#endif

// Tight loop with minimal ALU work - focuses on dispatch overhead
// addi r3, r3, 1    (increment register)
// cmpi cr0, r3, N   (compare with limit)
// bne 0             (branch back if not equal)
// blr               (return)
uint32_t tight_loop_code[] = {
    0x38630001,  // addi r3, r3, 1
    0x2C030000,  // cmpi cr0, r3, 0 (will be patched)
    0x4082FFF8,  // bne -8  (branch back 2 instructions)
    0x4E800020   // blr
};

// Branch prediction test - alternating taken/not-taken
// Uses conditional branches to stress branch prediction
uint32_t branch_test_code[] = {
    0x38000000,  // li r0, 0        (counter)
    0x38600000,  // li r3, 0        (result)
    0x70040001,  // andi. r4, r0, 1 (test if odd)
    0x4182000C,  // beq +12         (skip if even)
    0x38630001,  // addi r3, r3, 1  (increment if odd)
    0x42000008,  // bdnz +8         (decrement CTR and branch)
    0x38000001,  // addi r0, r0, 1  (increment counter)
    0x2C000000,  // cmpi cr0, r0, 0 (will be patched)
    0x4082FFE8,  // bne -24         (loop back)
    0x4E800020   // blr
};

constexpr uint32_t test_samples = 100;
constexpr uint32_t test_iterations = 10;

void run_benchmark(const char* name, uint32_t* code, size_t code_size, 
                   uint32_t iterations, uint32_t target_pc) {
    LOG_F(INFO, "\n=== %s ===", name);
    LOG_F(INFO, "Instructions per iteration: ~%u", iterations);
    
    // Load code
    for (size_t i = 0; i < code_size / 4; i++) {
        mmu_write_vmem<uint32_t>(0, i * 4, code[i]);
    }
    
    // Patch the comparison value
    uint32_t cmp_instr = code[1];
    cmp_instr = (cmp_instr & 0xFFFF0000) | (iterations & 0xFFFF);
    mmu_write_vmem<uint32_t>(0, 4, cmp_instr);
    
    // Run benchmark
    for (int i = 0; i < test_iterations; i++) {
        uint64_t best_sample = UINT64_MAX;
        for (uint32_t j = 0; j < test_samples; j++) {
            ppc_state.pc = 0;
            ppc_state.gpr[3] = 0;
            power_on = true;
            
            auto start_time = std::chrono::steady_clock::now();
            ppc_exec_until(target_pc);
            auto end_time = std::chrono::steady_clock::now();
            auto time_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end_time - start_time);
            
            if (time_elapsed.count() < best_sample)
                best_sample = time_elapsed.count();
        }
        
        double minsn_per_sec = (double)iterations * 1000.0 / best_sample;
        double ns_per_insn = (double)best_sample / iterations;
        LOG_F(INFO, "(%d) %llu ns total, %.2f ns/insn, %.2f Minsn/s", 
              i+1, best_sample, ns_per_insn, minsn_per_sec);
    }
}

int main(int argc, char** argv) {
    /* initialize logging */
    loguru::g_preamble_date    = false;
    loguru::g_preamble_time    = false;
    loguru::g_preamble_thread  = false;
    loguru::g_stderr_verbosity = 0;
    loguru::init(argc, argv);

    MPC106* grackle_obj = new MPC106;
    
    if (!grackle_obj->add_ram_region(0, 0x10000)) {
        LOG_F(ERROR, "Could not create RAM region");
        delete(grackle_obj);
        return -1;
    }

    constexpr uint64_t tbr_freq = 16705000;
    ppc_cpu_init(grackle_obj, PPC_VER::MPC750, false, tbr_freq);
    
    LOG_F(INFO, "PowerPC Dispatch Overhead Benchmark");
    LOG_F(INFO, "====================================");
    
    // Test 1: Very tight loop (1M iterations) - pure dispatch overhead
    LOG_F(INFO, "\nTest 1: Tight ALU loop (measures dispatch + minimal ALU work)");
    run_benchmark("1M iterations", tight_loop_code, sizeof(tight_loop_code), 
                  1000000, 0x0C);
    
    // Test 2: Medium loop (100K iterations)
    LOG_F(INFO, "\nTest 2: Medium loop");
    run_benchmark("100K iterations", tight_loop_code, sizeof(tight_loop_code), 
                  100000, 0x0C);
    
    // Test 3: Small loop (10K iterations)
    LOG_F(INFO, "\nTest 3: Small loop");
    run_benchmark("10K iterations", tight_loop_code, sizeof(tight_loop_code), 
                  10000, 0x0C);
    
    delete(grackle_obj);
    return 0;
}
