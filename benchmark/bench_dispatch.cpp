/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Benchmark specifically for instruction dispatch overhead
*/

#include <stdlib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
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
// lis   r4, HI(iter)   (load loop bound upper)
// ori   r4, r4, LO(iter)
// addi  r3, r3, 1      (increment register)
// cmpw  r3, r4         (compare with full 32-bit bound)
// bne   -12            (branch back to addi)
// blr                  (return)
uint32_t tight_loop_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x38630001,  // addi r3, r3, 1
    0x7C032000,  // cmpw r3, r4
    0x4082FFF4,  // bne -12 (back to addi)
    0x4E800020   // blr
};

// Branch prediction test - alternating taken/not-taken using CTR
// lis   r4, HI(iter)
// ori   r4, r4, LO(iter)
// mtctr r4             (loop bound)
// li    r0, 0          (counter)
// li    r3, 0          (result)
// andi. r5, r0, 1      (odd check)
// beq   +8             (skip add if even)
// addi  r3, r3, 1      (increment on odd)
// addi  r0, r0, 1      (increment counter)
// bdnz  -16            (loop)
// blr
uint32_t branch_test_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x7C8903A6,  // mtctr r4
    0x38000000,  // li r0, 0
    0x38600000,  // li r3, 0
    0x70050001,  // andi. r5, r0, 1
    0x41820008,  // beq +8
    0x38630001,  // addi r3, r3, 1
    0x38000001,  // addi r0, r0, 1
    0x4200FFF0,  // bdnz -16
    0x4E800020   // blr
};

// Load/store stress: walk a small buffer and touch memory each iteration
// lis   r4, HI(iter)
// ori   r4, r4, LO(iter)
// lis   r5, HI(base)
// ori   r5, r5, LO(base)
// mtctr r4
// lwz   r6, 0(r5)
// addi  r6, r6, 1
// stw   r6, 0(r5)
// addi  r5, r5, 4
// bdnz  -16
// blr
uint32_t load_store_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x3CA00000,  // lis r5, 0       (HI base)
    0x60A52000,  // ori r5, r5, 0x2000 (LO base)
    0x7C8903A6,  // mtctr r4
    0x80C50000,  // lwz r6, 0(r5)
    0x38C60001,  // addi r6, r6, 1
    0x90C50000,  // stw r6, 0(r5)
    0x60A50000,  // ori r5, r5, 0 (keep address fixed to stay in-bounds)
    0x4200FFF0,  // bdnz -16
    0x4E800020   // blr
};

constexpr uint32_t test_samples = 100;
constexpr uint32_t test_iterations = 10;

static uint64_t percentile(const std::vector<uint64_t>& sorted, double pct) {
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(std::ceil(sorted.size() * pct)) - 1;
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

void run_benchmark(const char* name, uint32_t* code, size_t code_size, 
                   uint32_t iterations, uint32_t target_pc) {
    LOG_F(INFO, "\n=== %s ===", name);
    LOG_F(INFO, "Instructions per iteration: ~%u", iterations);
    
    // Load code
    for (size_t i = 0; i < code_size / 4; i++) {
        mmu_write_vmem<uint32_t>(0, i * 4, code[i]);
    }
    
    // Patch the loop bound (full 32-bit) into lis/ori pair
    uint32_t hi = (iterations >> 16) & 0xFFFF;
    uint32_t lo = iterations & 0xFFFF;
    mmu_write_vmem<uint32_t>(0, 0, (code[0] & 0xFFFF0000) | hi);
    mmu_write_vmem<uint32_t>(0, 4, (code[1] & 0xFFFF0000) | lo);
    
    // Warm-up run (not measured) to populate tables/cache
    ppc_state.pc = 0;
    ppc_state.gpr[3] = 0;
    power_on = true;
    ppc_exec_until(target_pc);
    
    // Run benchmark
    for (int i = 0; i < test_iterations; i++) {
        std::vector<uint64_t> samples;
        samples.reserve(test_samples);
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
            
            uint64_t sample = time_elapsed.count();
            samples.push_back(sample);
            if (sample < best_sample)
                best_sample = sample;
        }
        std::sort(samples.begin(), samples.end());
        uint64_t med_sample = samples[samples.size() / 2];
        uint64_t p95_sample = percentile(samples, 0.95);
        
        double minsn_per_sec = (double)iterations * 1000.0 / best_sample;
        double ns_per_insn = (double)best_sample / iterations;
        LOG_F(INFO, "(%d) best %llu ns, median %llu ns, p95 %llu ns, %.2f ns/insn (best), %.2f Minsn/s", 
              i+1, best_sample, med_sample, p95_sample, ns_per_insn, minsn_per_sec);
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
    constexpr uint32_t tight_loop_target_pc = sizeof(tight_loop_code) - 4; // blr
    run_benchmark("1M iterations", tight_loop_code, sizeof(tight_loop_code), 
                  1000000, tight_loop_target_pc);
    
    // Test 2: Medium loop (100K iterations)
    LOG_F(INFO, "\nTest 2: Medium loop");
    run_benchmark("100K iterations", tight_loop_code, sizeof(tight_loop_code), 
                  100000, tight_loop_target_pc);
    
    // Test 3: Small loop (10K iterations)
    LOG_F(INFO, "\nTest 3: Small loop");
    run_benchmark("10K iterations", tight_loop_code, sizeof(tight_loop_code), 
                  10000, tight_loop_target_pc);

    // Test 4: Branch-prediction stress (1M iterations)
    LOG_F(INFO, "\nTest 4: Branch predictor (alternating taken/not-taken)");
    constexpr uint32_t branch_loop_target_pc = sizeof(branch_test_code) - 4; // blr
    run_benchmark("Branch alt 1M", branch_test_code, sizeof(branch_test_code),
                  1000000, branch_loop_target_pc);

    // Test 5: Load/store stress (1M iterations)
    LOG_F(INFO, "\nTest 5: Load/store stress");
    constexpr uint32_t load_store_target_pc = sizeof(load_store_code) - 4; // blr
    run_benchmark("Load/store 1M", load_store_code, sizeof(load_store_code),
                  1000000, load_store_target_pc);
    
    delete(grackle_obj);
    return 0;
}
