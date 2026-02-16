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

/*
Benchmark specifically for instruction dispatch overhead
*/

#include <stdlib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include <atomic>
#include <thread>
#include <cctype>
#include <filesystem>
#include <cinttypes>
#include "benchmark/bench_api.h"
#include "benchmark/bench_common.h"
#include "cpu/ppc/ppcemu.h"
#include "cpu/ppc/ppcmmu.h"
#include "devices/memctrl/mpc106.h"
#include <thirdparty/loguru/loguru.hpp>

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
// li    r5, 0          (counter)
// li    r3, 0          (result)
// andi. r6, r5, 1      (odd check)
// beq   +8             (skip add if even)
// addi  r3, r3, 1      (increment on odd)
// addi  r5, r5, 1      (increment counter)
// bdnz  -16            (loop)
// blr
uint32_t branch_test_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x7C8903A6,  // mtctr r4
    0x38A00000,  // li r5, 0
    0x38600000,  // li r3, 0
    0x70C50001,  // andi. r6, r5, 1
    0x41820008,  // beq +8
    0x38630001,  // addi r3, r3, 1
    0x38A50001,  // addi r5, r5, 1 (proper increment; rA != 0)
    0x4200FFF0,  // bdnz -16
    0x4E800020   // blr
};

// Predictable taken: pure CTR loop, always taken until final fall-through
uint32_t branch_taken_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x7C8903A6,  // mtctr r4
    0x60000000,  // nop
    0x4200FFFC,  // bdnz -4 (taken until final)
    0x4E800020   // blr
};

// Strided load/store with wrap: walks a 64 KB window to stress caches/TLB
// lis   r4, HI(iter)
// ori   r4, r4, LO(iter)
// lis   r5, HI(base)
// ori   r5, r5, LO(base)   (base = 0)
// lis   r6, HI(mask)
// ori   r6, r6, LO(mask)   (mask = 0xFFFC for 64 KB window)
// mtctr r4
// lwz   r7, 0(r5)
// addi  r7, r7, 1
// stw   r7, 0(r5)
// addi  r5, r5, 64       (stride)
// and   r5, r5, r6       (wrap inside 16 KB window)
// bdnz  -16
// blr
uint32_t stride_load_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x3CA00000,  // lis r5, 0       (HI base)
    0x60A00000,  // ori r5, r5, 0x0 (LO base)
    0x3CC00000,  // lis r6, 0       (HI mask)
    0x60C0FFFC,  // ori r6, r6, 0xFFFC (wrap mask)
    0x7C8903A6,  // mtctr r4
    0x80E50000,  // lwz r7, 0(r5)
    0x38E70001,  // addi r7, r7, 1
    0x90E50000,  // stw r7, 0(r5)
    0x38A50040,  // addi r5, r5, 64
    0x7CA53038,  // and r5, r5, r6 (wrap stride)
    0x4200FFF0,  // bdnz -16
    0x4E800020   // blr
};

// Strided load/store with wrap: 4 KB window for higher locality
uint32_t stride_load_small_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x3CA00000,  // lis r5, 0       (HI base)
    0x60A00000,  // ori r5, r5, 0x0 (LO base)
    0x3CC00000,  // lis r6, 0       (HI mask)
    0x60C00FFC,  // ori r6, r6, 0x0FFC (wrap mask for 4 KB window)
    0x7C8903A6,  // mtctr r4
    0x80E50000,  // lwz r7, 0(r5)
    0x38E70001,  // addi r7, r7, 1
    0x90E50000,  // stw r7, 0(r5)
    0x38A50040,  // addi r5, r5, 64
    0x7CA53038,  // and r5, r5, r6 (wrap stride)
    0x4200FFF0,  // bdnz -16
    0x4E800020   // blr
};

// Streaming load/store with 4-byte stride over 4 KB window (sequential touch)
uint32_t stream_load_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x3CA00000,  // lis r5, 0       (HI base)
    0x60A03000,  // ori r5, r5, 0x3000 (LO base)
    0x3CC00000,  // lis r6, 0       (HI mask)
    0x60C00FFC,  // ori r6, r6, 0x0FFC (wrap mask for 4 KB window)
    0x7C8903A6,  // mtctr r4
    0x80E50000,  // lwz r7, 0(r5)
    0x38E70001,  // addi r7, r7, 1
    0x90E50000,  // stw r7, 0(r5)
    0x38A50004,  // addi r5, r5, 4 (sequential stride)
    0x7CA53038,  // and r5, r5, r6 (wrap inside 4 KB)
    0x4200FFF0,  // bdnz -16
    0x4E800020   // blr
};

// Write-only fill with 4-byte stride over 4 KB window (no reads)
uint32_t stream_store_code[] = {
    0x3C800000,  // lis r4, 0       (HI patched)
    0x60840000,  // ori r4, r4, 0   (LO patched)
    0x3CA00000,  // lis r5, 0       (HI base)
    0x60A03000,  // ori r5, r5, 0x3000 (LO base)
    0x3CC00000,  // lis r6, 0       (HI mask)
    0x60C00FFC,  // ori r6, r6, 0x0FFC (wrap mask for 4 KB window)
    0x3CE00000,  // lis r7, 0       (init counter high)
    0x60E70001,  // ori r7, r7, 1   (init counter low)
    0x7C8903A6,  // mtctr r4
    0x90E50000,  // stw r7, 0(r5)
    0x38E70001,  // addi r7, r7, 1
    0x38A50004,  // addi r5, r5, 4 (sequential stride)
    0x7CA53038,  // and r5, r5, r6 (wrap inside 4 KB)
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

// Lightweight helpers to encode a few D-form and XL/A-form opcodes so we can build
// new microbenchmarks without hand-calculating hex encodings.
constexpr uint32_t encode_addis(uint8_t rt, uint8_t ra, uint16_t imm) {
    return (15u << 26) | (static_cast<uint32_t>(rt) << 21) | (static_cast<uint32_t>(ra) << 16) | imm;
}

constexpr uint32_t encode_addi(uint8_t rt, uint8_t ra, int16_t imm) {
    return (14u << 26) | (static_cast<uint32_t>(rt) << 21) | (static_cast<uint32_t>(ra) << 16) |
           static_cast<uint16_t>(imm);
}

constexpr uint32_t encode_ori(uint8_t rs, uint8_t ra, uint16_t imm) {
    return (24u << 26) | (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(ra) << 16) | imm;
}

constexpr uint32_t encode_andi_dot(uint8_t rs, uint8_t ra, uint16_t imm) {
    return (28u << 26) | (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(ra) << 16) | imm;
}

constexpr uint32_t encode_rlwinm(uint8_t ra, uint8_t rs, uint8_t sh, uint8_t mb, uint8_t me) {
    return (21u << 26) | (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(ra) << 16) |
           (static_cast<uint32_t>(sh) << 11) | (static_cast<uint32_t>(mb) << 6) |
           (static_cast<uint32_t>(me) << 1);
}

constexpr uint32_t encode_xor(uint8_t ra, uint8_t rs, uint8_t rb) {
    return (31u << 26) | (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(ra) << 16) |
           (static_cast<uint32_t>(rb) << 11) | (316u << 1);
}

constexpr uint32_t encode_and(uint8_t ra, uint8_t rs, uint8_t rb) {
    return (31u << 26) | (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(ra) << 16) |
           (static_cast<uint32_t>(rb) << 11) | (28u << 1);
}

constexpr uint32_t encode_or(uint8_t ra, uint8_t rs, uint8_t rb) {
    return (31u << 26) | (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(ra) << 16) |
           (static_cast<uint32_t>(rb) << 11) | (444u << 1);
}

constexpr uint32_t encode_bc(uint8_t bo, uint8_t bi, int16_t bd, bool aa = false, bool lk = false) {
    // BD is a signed 14-bit immediate (shifted left by 2). Mask to 14 bits to avoid overflow
    // when bd is negative and passed in as int16_t.
    uint32_t bd14 = static_cast<uint32_t>(bd) & 0x3FFF; // two's-complement in 14 bits
    return (16u << 26) | (static_cast<uint32_t>(bo) << 21) | (static_cast<uint32_t>(bi) << 16) |
           (bd14 << 2) | (aa ? 2u : 0u) | (lk ? 1u : 0u);
}

constexpr uint32_t encode_lfs(uint8_t frt, uint8_t ra, int16_t d) {
    return (48u << 26) | (static_cast<uint32_t>(frt) << 21) | (static_cast<uint32_t>(ra) << 16) |
           static_cast<uint16_t>(d);
}

constexpr uint32_t encode_stfs(uint8_t frs, uint8_t ra, int16_t d) {
    return (52u << 26) | (static_cast<uint32_t>(frs) << 21) | (static_cast<uint32_t>(ra) << 16) |
           static_cast<uint16_t>(d);
}

constexpr uint32_t encode_fadds(uint8_t frd, uint8_t fra, uint8_t frb) {
    return (59u << 26) | (static_cast<uint32_t>(frd) << 21) | (static_cast<uint32_t>(fra) << 16) |
           (static_cast<uint32_t>(frb) << 11) | (21u << 1);
}

static int16_t branch_disp(size_t from_index, size_t to_index) {
    // Returns branch displacement in *words* (BD field), not bytes.
    int32_t from_bytes = static_cast<int32_t>(from_index * 4);
    int32_t to_bytes = static_cast<int32_t>(to_index * 4);
    int32_t offset_bytes = to_bytes - (from_bytes + 4);
    return static_cast<int16_t>(offset_bytes / 4);
}

static std::vector<uint32_t> build_branch_not_taken_code() {
    // Simple loop with a conditional branch that is never taken; CTR controls exit.
    // Uses known-good encodings to avoid mis-patched branch offsets.
    return std::vector<uint32_t>{
        0x3C800000,                     // lis r4, 0 (HI patched)
        0x60840000,                     // ori r4, r4, 0 (LO patched)
        0x38C00001,                     // li r6, 1 (non-zero predicate)
        0x7C8903A6,                     // mtctr r4
        encode_andi_dot(6, 6, 1),       // andi. r6, r6, 1 (CR0.eq=0; ensures beq never taken)
        0x41820008,                     // beq +8 (never taken)
        0x38630001,                     // addi r3, r3, 1
        0x4200FFF0,                     // bdnz -16
        0x4E800020                      // blr
    };
}

static std::vector<uint32_t> build_branch_random_code(uint32_t table_base) {
    // Simpler, more deterministic implementation using a precomputed pattern table.
    // This avoids any surprises with branch immediates and ensures CTR exit works with the stepper.
    std::vector<uint32_t> code;
    code.reserve(20);
    code.push_back(encode_addis(4, 0, 0));          // patched HI(iter)
    code.push_back(encode_ori(4, 4, 0));            // patched LO(iter)
    code.push_back(encode_addis(5, 0, table_base >> 16)); // base hi
    code.push_back(encode_ori(5, 5, table_base));   // base lo
    code.push_back(0x7C8903A6);                     // mtctr r4

    size_t loop_start = code.size();
    code.push_back(0x80C50000);                     // lwz r6, 0(r5)
    code.push_back(0x38A50004);                     // addi r5, r5, 4
    code.push_back(encode_andi_dot(7, 6, 1));       // andi. r7, r6, 1
    size_t beq_index = code.size();
    code.push_back(0x00000000);                     // beq placeholder (skip add when bit==0)
    code.push_back(0x38630001);                     // addi r3, r3, 1
    size_t bdnz_index = code.size();
    code.push_back(0x00000000);                     // bdnz placeholder
    code.push_back(0x4E800020);                     // blr (not relied on for stepper; CTR exit is used)

    code[beq_index] = encode_bc(12, 2, branch_disp(beq_index, bdnz_index));
    code[bdnz_index] = encode_bc(16, 0, branch_disp(bdnz_index, loop_start));
    return code;
}

static std::vector<uint32_t> build_stride_sweep_code(uint32_t stride_bytes, uint16_t mask) {
    std::vector<uint32_t> code = {
        encode_addis(4, 0, 0),          // patched HI(iter)
        encode_ori(4, 4, 0),            // patched LO(iter)
        encode_addis(5, 0, 0),          // base hi
        encode_ori(5, 5, 0),            // base lo (0)
        encode_addis(6, 0, mask >> 16), // mask hi
        encode_ori(6, 6, mask),         // mask lo
        0x7C8903A6,                     // mtctr r4
        0x80E50000,                     // lwz r7, 0(r5)
        0x38E70001,                     // addi r7, r7, 1
        0x90E50000,                     // stw r7, 0(r5)
        encode_addi(5, 5, static_cast<int16_t>(stride_bytes)),
        encode_and(5, 5, 6),            // wrap
        0,                              // bdnz placeholder
        0x4E800020                      // blr
    };

    size_t loop_start = 7; // lwz r7...
    size_t bdnz_index = 12;
    code[bdnz_index] = encode_bc(16, 0, branch_disp(bdnz_index, loop_start));
    return code;
}

static std::vector<uint32_t> build_mixed_mix_code(uint16_t mask) {
    std::vector<uint32_t> code;
    code.reserve(40);
    code.push_back(encode_addis(4, 0, 0));          // patched HI(iter)
    code.push_back(encode_ori(4, 4, 0));            // patched LO(iter)
    code.push_back(encode_addis(5, 0, 0));          // base hi
    code.push_back(encode_ori(5, 5, 0x4000));       // base lo
    code.push_back(encode_addis(6, 0, mask >> 16));
    code.push_back(encode_ori(6, 6, mask));         // wrap mask (4 KB)
    code.push_back(0x7C8903A6);                     // mtctr r4

    size_t loop_start = code.size();
    code.push_back(0x80E50000);                     // lwz r7, 0(r5)
    code.push_back(0x81250004);                     // lwz r9, 4(r5)
    code.push_back(0x81450008);                     // lwz r10, 8(r5)
    code.push_back(0x8165000C);                     // lwz r11, 12(r5)
    code.push_back(0x81850010);                     // lwz r12, 16(r5)
    code.push_back(0x38E70001);                     // addi r7, r7, 1
    code.push_back(0x7D095214);                     // add r8, r9, r10
    code.push_back(0x7D2A6214);                     // add r9, r10, r12
    code.push_back(0x7D4B3A14);                     // add r10, r11, r7
    code.push_back(0x398C0003);                     // addi r12, r12, 3
    code.push_back(0x91050000);                     // stw r8, 0(r5)
    code.push_back(0x91250004);                     // stw r9, 4(r5)
    code.push_back(0x91450008);                     // stw r10, 8(r5)
    code.push_back(encode_or(13, 8, 10));
    code.push_back(encode_and(14, 13, 12));
    code.push_back(0x38630001);                     // addi r3, r3, 1
    code.push_back(0x38A50014);                     // addi r5, r5, 20
    code.push_back(encode_and(5, 5, 6));            // wrap within window
    code.push_back(encode_andi_dot(15, 3, 3));      // andi. r15, r3, 3
    size_t beq_index = code.size();
    code.push_back(0);                              // beq placeholder
    code.push_back(0x38630001);                     // addi r3, r3, 1 (occasionally)
    size_t bdnz_index = code.size();
    code.push_back(0);                              // bdnz placeholder
    code.push_back(0x4E800020);                     // blr

    code[beq_index] = encode_bc(12, 2, branch_disp(beq_index, bdnz_index));
    code[bdnz_index] = encode_bc(16, 0, branch_disp(bdnz_index, loop_start));
    return code;
}

static std::vector<uint32_t> build_fpu_loop_code(uint16_t mask) {
    std::vector<uint32_t> code;
    code.reserve(24);
    code.push_back(encode_addis(4, 0, 0));          // patched HI(iter)
    code.push_back(encode_ori(4, 4, 0));            // patched LO(iter)
    code.push_back(encode_addis(5, 0, 0));          // base hi
    code.push_back(encode_ori(5, 5, 0x6000));       // base lo
    code.push_back(encode_addis(6, 0, mask >> 16));
    code.push_back(encode_ori(6, 6, mask));         // wrap mask (4 KB)
    code.push_back(0x7C8903A6);                     // mtctr r4

    size_t loop_start = code.size();
    code.push_back(encode_lfs(1, 5, 0));            // lfs f1, 0(r5)
    code.push_back(encode_lfs(2, 5, 4));            // lfs f2, 4(r5)
    code.push_back(encode_fadds(3, 1, 2));          // fadds f3, f1, f2
    code.push_back(encode_stfs(3, 5, 8));           // stfs f3, 8(r5)
    code.push_back(encode_addi(5, 5, 12));          // bump pointer
    code.push_back(encode_and(5, 5, 6));            // wrap
    size_t bdnz_index = code.size();
    code.push_back(0);                              // bdnz placeholder
    code.push_back(0x4E800020);                     // blr

    code[bdnz_index] = encode_bc(16, 0, branch_disp(bdnz_index, loop_start));
    return code;
}

static std::vector<uint32_t> build_memcpy_guest_code(uint32_t src, uint32_t dst) {
    std::vector<uint32_t> code;
    code.reserve(20);
    code.push_back(encode_addis(4, 0, 0));          // patched HI(iter)
    code.push_back(encode_ori(4, 4, 0));            // patched LO(iter)
    code.push_back(encode_addis(5, 0, src >> 16));  // src hi
    code.push_back(encode_ori(5, 5, src));          // src lo
    code.push_back(encode_addis(6, 0, dst >> 16));  // dst hi
    code.push_back(encode_ori(6, 6, dst));          // dst lo
    code.push_back(0x7C8903A6);                     // mtctr r4

    size_t loop_start = code.size();
    code.push_back(0x80E50000);                     // lwz r7, 0(r5)
    code.push_back(0x90E60000);                     // stw r7, 0(r6)
    code.push_back(0x38A50004);                     // addi r5, r5, 4
    code.push_back(0x38C60004);                     // addi r6, r6, 4
    size_t bdnz_index = code.size();
    code.push_back(0);                              // bdnz placeholder
    code.push_back(0x4E800020);                     // blr

    code[bdnz_index] = encode_bc(16, 0, branch_disp(bdnz_index, loop_start));
    return code;
}

static std::vector<uint32_t> build_mmio_poll_code(uint32_t mmio_addr) {
    std::vector<uint32_t> code;
    code.reserve(16);
    code.push_back(encode_addis(4, 0, 0));          // patched HI(iter)
    code.push_back(encode_ori(4, 4, 0));            // patched LO(iter)
    code.push_back(encode_addis(5, 0, mmio_addr >> 16));
    code.push_back(encode_ori(5, 5, mmio_addr));    // MMIO base
    code.push_back(0x7C8903A6);                     // mtctr r4

    size_t loop_start = code.size();
    code.push_back(0x80C50000);                     // lwz r6, 0(r5)
    code.push_back(0x38C60001);                     // addi r6, r6, 1 (touch value)
    code.push_back(0x90C50000);                     // stw r6, 0(r5) (exercise write handler)
    size_t bdnz_index = code.size();
    code.push_back(0);                              // bdnz placeholder
    code.push_back(0x4E800020);                     // blr

    code[bdnz_index] = encode_bc(16, 0, branch_disp(bdnz_index, loop_start));
    return code;
}

constexpr uint32_t kDefaultSamples = 100;
constexpr uint32_t kDefaultRuns = 10;

static uint64_t percentile(const std::vector<uint64_t>& sorted, double pct) {
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(std::ceil(sorted.size() * pct)) - 1;
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

static bool matches_filter(const std::string& test_name, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    };
    std::string tn = lower(test_name);
    std::string f = lower(filter);
    return tn.find(f) != std::string::npos;
}

// Watchdog that can flip `power_on` to false if we overrun
struct BenchWatchdog {
    std::atomic<bool> triggered{false};
    std::atomic<bool> stop{false};
    std::thread t;
    BenchWatchdog(uint64_t watchdog_ms) {
        if (!watchdog_ms) return;
        t = std::thread([this, watchdog_ms]() {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(watchdog_ms);
            while (!stop.load(std::memory_order_relaxed)) {
                if (std::chrono::steady_clock::now() > deadline) {
                    triggered.store(true, std::memory_order_relaxed);
                    // Avoid data race on global `power_on` by using atomic_ref shim.
                    std::atomic_ref<bool> power_on_atomic(power_on);
                    power_on_atomic.store(false, std::memory_order_relaxed);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    ~BenchWatchdog() {
        stop.store(true, std::memory_order_relaxed);
        if (t.joinable()) t.join();
    }
};

static void run_benchmark(const char* name, uint32_t* code, size_t code_size,
                          uint32_t iterations, uint32_t target_pc,
                          uint32_t runs, uint32_t samples_per_run,
                          const BenchOptions& options,
                          bool use_ppc_exec = false,
                          uint32_t loop_pc_hint = 0) {
    const uint32_t code_insns = static_cast<uint32_t>(code_size / 4);
    LOG_F(INFO, "\n=== %s ===", name);
    LOG_F(INFO, "Loop iterations: %u, code length (insns): %u, runs=%u, samples/run=%u, ppc_exec=%d",
          iterations, code_insns, runs, static_cast<unsigned>(samples_per_run), (use_ppc_exec || options.force_ppc_exec));
    
    // Load code
    for (size_t i = 0; i < code_size / 4; i++) {
        mmu_write_vmem<uint32_t>(0, i * 4, code[i]);
    }
    if (options.verbose_dump) {
        LOG_F(1, "%s code dump:", name);
        for (size_t i = 0; i < code_size / 4; i++) {
            LOG_F(1, "  [%02zu] 0x%08X", i, code[i]);
        }
    }
    
    // Patch the loop bound (full 32-bit) into lis/ori pair
    uint32_t hi = (iterations >> 16) & 0xFFFF;
    uint32_t lo = iterations & 0xFFFF;
    mmu_write_vmem<uint32_t>(0, 0, (code[0] & 0xFFFF0000) | hi);
    mmu_write_vmem<uint32_t>(0, 4, (code[1] & 0xFFFF0000) | lo);
    
    auto run_stepper = [&](uint32_t target_pc_local, uint64_t iterations_local, BenchWatchdog& watchdog) -> bool {
        // Allow generous step budget: iterations * code_insns * 2 (but clamp to a sane minimum)
        uint64_t max_steps = std::max<uint64_t>(iterations_local * std::max<uint64_t>(code_insns, 8ull) * 2ull, 5'000ull);
        bool hit_target = false;
        bool ctr_supported_exit = false;
        uint64_t loop_iters_observed = 0;
        // Heuristic: assume bdnz sits immediately before blr
        const uint32_t bdnz_pc = (code_insns >= 2) ? (target_pc_local - 4) : target_pc_local;
        const uint32_t loop_pc = loop_pc_hint ? loop_pc_hint : 0;
        uint32_t last_ctr = static_cast<uint32_t>(ppc_state.spr[SPR::CTR]);
        for (uint64_t step = 0; step < max_steps; ++step) {
            ppc_exec_single();
            if (options.verbose_dump && step < 8) {
                LOG_F(1, "step %" PRIu64 " pc=0x%08X ctr=0x%08X", static_cast<uint64_t>(step), ppc_state.pc, static_cast<uint32_t>(ppc_state.spr[SPR::CTR]));
            }
            // Count loop iterations when we see the loop head (pc after branch) or bdnz pc,
            // or when CTR decreases (covers cases where branch landing differs slightly).
            bool saw_loop_pc = (loop_pc && ppc_state.pc == loop_pc) || (ppc_state.pc == bdnz_pc);
            bool ctr_decremented = (static_cast<uint32_t>(ppc_state.spr[SPR::CTR]) != last_ctr) && (static_cast<uint32_t>(ppc_state.spr[SPR::CTR]) < last_ctr);
            if (ctr_decremented || saw_loop_pc) {
                ++loop_iters_observed;
                last_ctr = static_cast<uint32_t>(ppc_state.spr[SPR::CTR]);
                if (loop_iters_observed >= iterations_local) {
                    ctr_supported_exit = true;
                    hit_target = true;
                    break;
                }
            }
            // Accept completion if we hit the target PC (either pre- or post-increment).
            if (ppc_state.pc == target_pc_local || ppc_state.pc == (target_pc_local + 4)) {
                hit_target = true;
                break;
            }
            // Once we've stepped a few instructions, allow CTR-based exit (covers blr back to LR=0 cases)
            if (step > code_insns && ppc_state.spr[SPR::CTR] == 0) {
                ctr_supported_exit = true;
                hit_target = true;
                break;
            }
            if (watchdog.triggered.load(std::memory_order_relaxed)) {
                if (options.verbose_dump) {
                    LOG_F(ERROR, "run_stepper abort: watchdog tripped at step=%" PRIu64 " pc=0x%08X ctr=0x%08X",
                          static_cast<uint64_t>(step), ppc_state.pc, static_cast<uint32_t>(ppc_state.spr[SPR::CTR]));
                }
                return false;
            }
        }
        if (!hit_target) {
            LOG_F(ERROR, "Stepper did not hit target pc 0x%08X within %" PRIu64 " steps (pc=0x%08X ctr=0x%08X iters_obs=%" PRIu64 " loop_pc=0x%08X)",
                  target_pc_local, static_cast<uint64_t>(max_steps),
                  ppc_state.pc, static_cast<uint32_t>(ppc_state.spr[SPR::CTR]),
                  static_cast<uint64_t>(loop_iters_observed), loop_pc);
        } else if (ctr_supported_exit) {
            LOG_F(1, "Stepper exited via CTR/loop counter (pc=0x%08X, iters=%" PRIu64 ")",
                  ppc_state.pc, static_cast<uint64_t>(loop_iters_observed));
        }
        return hit_target;
    };

    auto reset_guest_state = [&](uint32_t iter_val) {
        ppc_state.pc = 0;
        ppc_state.gpr[0] = 0;
        ppc_state.gpr[3] = 0;
        ppc_state.gpr[4] = 0;
        ppc_state.gpr[5] = 0;
        ppc_state.gpr[6] = 0;
        ppc_state.gpr[7] = 0;
        // Clear MSR to disable address translation; this keeps effective == physical and avoids
        // unmapped warnings under stepper runs.
        ppc_state.msr = 0;
        ppc_state.spr[SPR::CTR] = iter_val; // ensure non-zero so stepper doesn't exit early
        power_on = true;
    };

    // Warm-up run (not measured) to populate tables/cache
    {
        reset_guest_state(iterations);
        BenchWatchdog watchdog(options.watchdog_ms);
        if (use_ppc_exec || options.force_ppc_exec) {
            if (!run_stepper(target_pc, iterations, watchdog)) {
                LOG_F(ERROR, "Warm-up stepper failed for %s", name);
                return;
            }
        } else {
            ppc_exec_until(target_pc);
        }
    }
    
    // Run benchmark
    for (uint32_t i = 0; i < runs; i++) {
        LOG_F(INFO, "[%s] run %u/%u, samples_per_run=%u", name, i + 1, runs, samples_per_run);
        std::vector<uint64_t> samples;
        samples.reserve(samples_per_run);
        uint64_t best_sample = UINT64_MAX;
        for (uint32_t j = 0; j < samples_per_run; j++) {
            reset_guest_state(iterations);
            BenchWatchdog watchdog(options.watchdog_ms);
            
            auto start_time = std::chrono::steady_clock::now();
            if (use_ppc_exec || options.force_ppc_exec) {
                if (!run_stepper(target_pc, iterations, watchdog)) {
                    LOG_F(ERROR, "Stepper failed for %s (run %u/%u, sample %u/%u)",
                          name, i + 1, runs, j + 1, samples_per_run);
                    return;
                }
            } else {
                ppc_exec_until(target_pc);
            }
            auto end_time = std::chrono::steady_clock::now();
            auto time_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end_time - start_time);
            
            if (watchdog.triggered.load(std::memory_order_relaxed)) {
                LOG_F(ERROR, "Watchdog triggered for %s (iteration %u/%u). pc=0x%08X ctr=0x%08X",
                      name, i + 1, runs,
                      ppc_state.pc, (uint32_t)ppc_state.spr[SPR::CTR]);
                return;
            }
            
            uint64_t sample = time_elapsed.count();
            samples.push_back(sample);
            if (sample < best_sample)
                best_sample = sample;
            LOG_F(1, "[%s] sample %u took %" PRIu64 " ns", name, j + 1, static_cast<uint64_t>(sample));
        }
        if (samples.empty()) {
            LOG_F(ERROR, "[%s] no samples collected (runs=%u, samples/run=%u)", name, runs, samples_per_run);
            return;
        }
        std::sort(samples.begin(), samples.end());
        uint64_t med_sample = samples[samples.size() / 2];
        uint64_t p95_sample = percentile(samples, 0.95);

        double total_insns = static_cast<double>(code_insns) * static_cast<double>(iterations);
        double ns_per_insn = total_insns > 0 ? (static_cast<double>(best_sample) / total_insns) : 0.0;
        double minsn_per_sec = ns_per_insn > 0 ? (1000.0 / ns_per_insn) : 0.0;
        LOG_F(INFO, "(%u) best %" PRIu64 " ns, median %" PRIu64 " ns, p95 %" PRIu64 " ns, %.4f ns/insn (best), %.2f Minsn/s",
              i + 1, static_cast<uint64_t>(best_sample), static_cast<uint64_t>(med_sample), static_cast<uint64_t>(p95_sample), ns_per_insn, minsn_per_sec);
    }
}

static void run_host_memcpy(const char* name, std::vector<uint8_t>& dst,
                            const std::vector<uint8_t>& src,
                            uint32_t runs, uint32_t samples_per_run) {
    LOG_F(INFO, "\n=== %s ===", name);
    LOG_F(INFO, "Bytes per copy: %zu", src.size());

    // To avoid timer quantization returning 0 ns, repeat the memcpy multiple times per sample.
    constexpr uint32_t kRepeats = 1024; // ~4 MB when src is 4 KB; adjust if src changes

    for (uint32_t i = 0; i < runs; i++) {
        uint64_t best_sample = UINT64_MAX;
        for (uint32_t j = 0; j < samples_per_run; j++) {
            auto start_time = std::chrono::steady_clock::now();
            for (uint32_t r = 0; r < kRepeats; ++r) {
                std::memcpy(dst.data(), src.data(), src.size());
            }
            auto end_time = std::chrono::steady_clock::now();
            auto time_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
            uint64_t sample_ns = std::max<uint64_t>(time_elapsed.count(), 1ULL); // avoid divide-by-zero
            if (sample_ns < best_sample)
                best_sample = sample_ns;
        }
        double bytes_copied = static_cast<double>(src.size()) * kRepeats;
        double mib_per_s = bytes_copied * 1e9 / (best_sample * 1024.0 * 1024.0);
        LOG_F(INFO, "(%u) %" PRIu64 " ns (total), %.2f MiB/s", i + 1, static_cast<uint64_t>(best_sample), mib_per_s);
    }
}

namespace bench_dispatch {

int run(const BenchOptions& options) {
    const uint32_t runs = options.runs ? options.runs : kDefaultRuns;
    const uint32_t samples = options.samples ? options.samples : kDefaultSamples;

    MPC106* grackle_obj = new MPC106;

    // Allocate a larger RAM window so strided tests stay mapped even if the MMU offsets
    // addresses; 512 MB avoids "unmapped physical" warnings during long sweeps.
    // Map a large RAM window to avoid "unmapped physical" warnings under MMU/BAT translations.
    // 1 GiB keeps us well clear for these microbenches.
    if (!grackle_obj->add_ram_region(0, 0x40000000)) {
        LOG_F(ERROR, "Could not create RAM region (0x0..0x40000000)");
        delete(grackle_obj);
        return -1;
    }

    constexpr uint64_t tbr_freq = 16705000;
    ppc_cpu_init(grackle_obj, PPC_VER::MPC750, false, tbr_freq);

    // Ensure a log directory exists for redirected logs / artifacts
    std::error_code ec;
    const std::string& opts_log_dir = options.log_dir; // options is in scope here
    std::filesystem::path log_dir = opts_log_dir.empty() ? std::filesystem::current_path() / "tmp" : std::filesystem::path(opts_log_dir);
    std::filesystem::create_directories(log_dir, ec);
    if (ec) {
        LOG_F(WARNING, "Could not create %s: %s", log_dir.string().c_str(), ec.message().c_str());
    } else {
        LOG_F(1, "Log dir ready: %s", log_dir.string().c_str());
    }

    BenchOptions opts = options;
    if (!opts.watchdog_ms) {
        opts.watchdog_ms = 5000; // default 5s per-test watchdog
    }

    // Pre-fill scratch regions used by the additional microbenches.
    constexpr uint32_t mixed_base = 0x4000;
    constexpr uint32_t fpu_base = 0x6000;
    constexpr uint32_t memcpy_src = 0x7000;
    constexpr uint32_t memcpy_dst = 0x8000;
    constexpr uint32_t branch_rand_tbl = 0x9000;
    constexpr uint32_t scratch_window = 0x1000; // 4 KB windows

    for (uint32_t off = 0; off < scratch_window; off += 4) {
        mmu_write_vmem<uint32_t>(0, mixed_base + off, off ^ 0x12345678);
        mmu_write_vmem<uint32_t>(0, memcpy_src + off, off * 2654435761u);
        mmu_write_vmem<uint32_t>(0, memcpy_dst + off, 0);

        float f = 1.0f + static_cast<float>(off) * 0.001f;
        uint32_t fbits;
        std::memcpy(&fbits, &f, sizeof(fbits));
        mmu_write_vmem<uint32_t>(0, fpu_base + off, fbits);

        // Precompute a pseudo-random branch pattern table (0/1 values) for branch_random
        uint32_t lfsr = 0xACE1u ^ off;
        lfsr ^= (lfsr << 13);
        lfsr ^= (lfsr >> 17);
        lfsr ^= (lfsr << 5);
        uint32_t bit = lfsr & 1u;
        mmu_write_vmem<uint32_t>(0, branch_rand_tbl + off, bit);
    }

    LOG_F(INFO, "PowerPC Dispatch Overhead Benchmark");
    LOG_F(INFO, "====================================");

    // Table-driven registry to keep things DRY
    struct DispatchTest {
        const char* label;
        const char* filter;
        std::function<std::vector<uint32_t>()> build_dynamic; // optional dynamic builder
        const uint32_t* static_code = nullptr;
        size_t static_code_bytes = 0;
        uint32_t iterations = 0;
        uint32_t loop_pc_hint = 0;
        bool use_ppc_exec = false;
    };

    // Loop PC hints to keep stepper iteration counting solid
    constexpr uint32_t LP_BRANCH_RAND = 0x10;
    constexpr uint32_t LP_STRIDE = 7 * 4;
    constexpr uint32_t LP_MIXED = 7 * 4;
    constexpr uint32_t LP_FPU = 7 * 4;
    constexpr uint32_t LP_MEMCPY = 7 * 4;
    constexpr uint32_t LP_MMIO = 5 * 4;

    const DispatchTest tests[] = {
        {"1M iterations", "Tight ALU", {}, tight_loop_code, sizeof(tight_loop_code), 1000000, 0, false},
        {"100K iterations", "Medium loop", {}, tight_loop_code, sizeof(tight_loop_code), 100000, 0, false},
        {"10K iterations", "Small loop", {}, tight_loop_code, sizeof(tight_loop_code), 10000, 0, false},
        {"Branch alt 1M", "Branch alt", {}, branch_test_code, sizeof(branch_test_code), 1000000, 0, false},
        {"Branch taken 1M", "Branch taken", {}, branch_taken_code, sizeof(branch_taken_code), 1000000, 0, false},
        {"Load/store 1M", "Load/store", {}, load_store_code, sizeof(load_store_code), 1000000, 0, false},
        {"Load/store stride 500 (64KB)", "stride 64KB", {}, stride_load_code, sizeof(stride_load_code), 500, 0, false},
        {"Load/store stride 1K (4KB)", "stride 4KB", {}, stride_load_small_code, sizeof(stride_load_small_code), 1000, 0, false},
        {"Stream load/store 5K (4KB)", "Stream load", {}, stream_load_code, sizeof(stream_load_code), 5000, LP_STRIDE, true},
        {"Stream store-only 5K (4KB)", "Stream store", {}, stream_store_code, sizeof(stream_store_code), 5000, LP_STRIDE, false},
        {"Branch never-taken 10K", "Branch never-taken", build_branch_not_taken_code, nullptr, 0, 10000, 0, true},
        {"Branch random 5K", "Branch random", [branch_rand_tbl]() { return build_branch_random_code(branch_rand_tbl); }, nullptr, 0, 5000, LP_BRANCH_RAND, true},
        {"Stride 1 line (64B) 2K iters", "Stride 1 line", []() { return build_stride_sweep_code(64, 0x0FFC); }, nullptr, 0, 2000, LP_STRIDE, true},
        {"Stride 2 lines (128B) 2K iters", "Stride 2 lines", []() { return build_stride_sweep_code(128, 0x0FFC); }, nullptr, 0, 2000, LP_STRIDE, true},
        {"Stride 4 lines (256B) 2K iters", "Stride 4 lines", []() { return build_stride_sweep_code(256, 0x0FFC); }, nullptr, 0, 2000, LP_STRIDE, true},
        {"Stride 8 lines (512B) 2K iters", "Stride 8 lines", []() { return build_stride_sweep_code(512, 0x0FFC); }, nullptr, 0, 2000, LP_STRIDE, true},
        {"Mixed instruction mix 2K", "Mixed instruction", []() { return build_mixed_mix_code(0x0FFC); }, nullptr, 0, 2000, LP_MIXED, false},
        {"FPU add/store 200K", "FPU", []() { return build_fpu_loop_code(0x0FFC); }, nullptr, 0, 200000, LP_FPU, true},
        {"Guest memcpy 1K words", "Guest memcpy", [memcpy_src, memcpy_dst]() { return build_memcpy_guest_code(memcpy_src, memcpy_dst); }, nullptr, 0, 1024, LP_MEMCPY, true},
        {"MMIO poll 100K (RAM)", "MMIO", []() { return build_mmio_poll_code(0x00001000); }, nullptr, 0, 100000, LP_MMIO, true},
    };

    bool any_ran = false;
    for (const auto& t : tests) {
        if (!matches_filter(t.filter, opts.test_filter)) {
            LOG_F(INFO, "Skipping %s (filter)", t.label);
            continue;
        }
        any_ran = true;
        LOG_F(INFO, "\nTest: %s", t.label);
        if (t.static_code) {
            const uint32_t target_pc = static_cast<uint32_t>(t.static_code_bytes - 4);
            run_benchmark(t.label, const_cast<uint32_t*>(t.static_code), t.static_code_bytes,
                          t.iterations, target_pc, runs, samples, opts, t.use_ppc_exec, t.loop_pc_hint);
        } else {
            auto code_vec = t.build_dynamic();
            const uint32_t target_pc = static_cast<uint32_t>(code_vec.size() * 4 - 4);
            run_benchmark(t.label, code_vec.data(), code_vec.size() * 4,
                          t.iterations, target_pc, runs, samples, opts, t.use_ppc_exec, t.loop_pc_hint);
        }
    }

    // Host memcpy baseline (4 KB)
    if (matches_filter("Host memcpy", opts.test_filter)) {
        any_ran = true;
        std::vector<uint8_t> host_src(scratch_window);
        std::vector<uint8_t> host_dst(scratch_window);
        for (size_t i = 0; i < host_src.size(); i += 4) {
            uint32_t val = static_cast<uint32_t>(i) ^ 0x89ABCDEF;
            std::memcpy(host_src.data() + i, &val, sizeof(val));
        }
        run_host_memcpy("Host memcpy 4KB", host_dst, host_src, runs, samples);
    } else {
        LOG_F(INFO, "Skipping Host memcpy (filter)");
    }

    if (!any_ran) {
        LOG_F(ERROR, "No dispatch tests matched filter '%s'", opts.test_filter.c_str());
        return -1;
    }

    delete(grackle_obj);
    return 0;
}

void register_benchmarks(std::vector<Bench>& benches) {
    benches.push_back({
        .name = "dispatch",
        .description = "Dispatch overhead, ALU/branch/load-store loops",
        .run = run,
    });
}

} // namespace bench_dispatch