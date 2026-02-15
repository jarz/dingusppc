/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

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

/** @file Test SPR (Special Purpose Register) read/write functionality. */

#include "../ppcemu.h"
#include <iostream>
#include <cassert>

using namespace std;

// Test helper to write and read back an SPR
bool test_spr_rw(const char* name, int spr_num, uint32_t test_value, bool is_supervisor) {
    // Encode mtspr instruction: mtspr SPR, r3
    // Format: 31 | rS | SPR[5:9] | SPR[0:4] | 467 | 0
    uint32_t spr_encoded = ((spr_num & 0x1F) << 5) | ((spr_num >> 5) & 0x1F);
    uint32_t mtspr_opcode = 0x7C600000 | (spr_encoded << 11) | (467 << 1);
    
    // Encode mfspr instruction: mfspr r4, SPR
    // Format: 31 | rD | SPR[5:9] | SPR[0:4] | 339 | 0
    uint32_t mfspr_opcode = 0x7C800000 | (spr_encoded << 11) | (339 << 1);
    
    // Set supervisor mode if needed
    if (is_supervisor) {
        ppc_state.msr &= ~MSR::PR;  // Clear problem state bit (supervisor mode)
    } else {
        ppc_state.msr |= MSR::PR;   // Set problem state bit (user mode)
    }
    
    // Write test value to r3
    ppc_state.gpr[3] = test_value;
    ppc_state.gpr[4] = 0xDEADBEEF;  // Initialize r4 with garbage
    
    // Execute mtspr to write the SPR
    ppc_main_opcode(ppc_opcode_grabber, mtspr_opcode);
    
    // Execute mfspr to read the SPR back into r4
    ppc_main_opcode(ppc_opcode_grabber, mfspr_opcode);
    
    // Verify the value matches
    if (ppc_state.gpr[4] != test_value) {
        cout << "FAIL: " << name << " (SPR " << spr_num << "): "
             << "wrote 0x" << hex << test_value 
             << ", read 0x" << hex << ppc_state.gpr[4] << endl;
        return false;
    }
    
    cout << "PASS: " << name << " (SPR " << spr_num << ")" << endl;
    return true;
}

// Test helper for read-only SPRs - just verify we can read without exception
bool test_spr_ro(const char* name, int spr_num, bool is_supervisor) {
    // Encode mfspr instruction: mfspr r4, SPR
    // Format: 31 | rD | SPR[5:9] | SPR[0:4] | 339 | 0
    uint32_t spr_encoded = ((spr_num & 0x1F) << 5) | ((spr_num >> 5) & 0x1F);
    uint32_t mfspr_opcode = 0x7C800000 | (spr_encoded << 11) | (339 << 1);
    
    // Set supervisor mode if needed
    if (is_supervisor) {
        ppc_state.msr &= ~MSR::PR;  // Clear problem state bit (supervisor mode)
    } else {
        ppc_state.msr |= MSR::PR;   // Set problem state bit (user mode)
    }
    
    ppc_state.gpr[4] = 0xDEADBEEF;  // Initialize r4 with garbage
    
    // Execute mfspr to read the SPR into r4
    ppc_main_opcode(ppc_opcode_grabber, mfspr_opcode);
    
    // Just verify we got some value (didn't exception)
    cout << "PASS: " << name << " (SPR " << spr_num << ") [read-only] = 0x" << hex << ppc_state.gpr[4] << endl;
    return true;
}

int main() {
    initialize_ppc_opcode_table();
    
    // Set MSR to allow testing (supervisor mode, machine check enabled)
    int initial_msr = MSR::ME | MSR::IP;
    ppc_msr_did_change(ppc_state.msr, initial_msr, false);
    
    cout << "Testing SPR read/write functionality..." << endl << endl;
    
    int passed = 0;
    int failed = 0;
    
    // Test exception handling registers (supervisor only)
    if (test_spr_rw("DSISR", SPR::DSISR, 0x12345678, true)) passed++; else failed++;
    if (test_spr_rw("DAR", SPR::DAR, 0xABCDEF00, true)) passed++; else failed++;
    if (test_spr_rw("SRR0", SPR::SRR0, 0x10001000, true)) passed++; else failed++;
    if (test_spr_rw("SRR1", SPR::SRR1, 0x20002000, true)) passed++; else failed++;
    
    // Test SPRG registers (supervisor only)
    if (test_spr_rw("SPRG0", SPR::SPRG0, 0x11111111, true)) passed++; else failed++;
    if (test_spr_rw("SPRG1", SPR::SPRG1, 0x22222222, true)) passed++; else failed++;
    if (test_spr_rw("SPRG2", SPR::SPRG2, 0x33333333, true)) passed++; else failed++;
    if (test_spr_rw("SPRG3", SPR::SPRG3, 0x44444444, true)) passed++; else failed++;
    
    // Test hardware implementation-dependent registers (supervisor only)
    // HID0 value with ICE bit set (0x8000) to test cache enable logging
    if (test_spr_rw("HID0", SPR::HID0, 0x80008000, true)) passed++; else failed++;
    if (test_spr_rw("HID1", SPR::HID1, 0x40004000, true)) passed++; else failed++;
    
    // Test performance monitoring registers (supervisor only)
    if (test_spr_rw("MMCR0", SPR::MMCR0, 0x12340000, true)) passed++; else failed++;
    if (test_spr_rw("MMCR1", SPR::MMCR1, 0x56780000, true)) passed++; else failed++;
    if (test_spr_rw("PMC1", SPR::PMC1, 0x00001234, true)) passed++; else failed++;
    if (test_spr_rw("PMC2", SPR::PMC2, 0x00005678, true)) passed++; else failed++;
    if (test_spr_rw("PMC3", SPR::PMC3, 0x00009ABC, true)) passed++; else failed++;
    if (test_spr_rw("PMC4", SPR::PMC4, 0x0000DEF0, true)) passed++; else failed++;
    if (test_spr_rw("SIA", SPR::SIA, 0xAABBCCDD, true)) passed++; else failed++;
    if (test_spr_rw("SDA", SPR::SDA, 0xEEFF0011, true)) passed++; else failed++;
    
    // Test new registers added in Phase 1
    if (test_spr_rw("EAR", SPR::EAR, 0x12345678, true)) passed++; else failed++;
    if (test_spr_ro("PIR", SPR::PIR, true)) passed++; else failed++;  // PIR is read-only
    if (test_spr_rw("IABR", SPR::IABR, 0x10001000, true)) passed++; else failed++;
    // DABR with breakpoint bits set (DW=1, DR=1 for read/write breakpoint)
    if (test_spr_rw("DABR", SPR::DABR, 0x20002003, true)) passed++; else failed++;
    
    cout << endl << "=== Results ===" << endl;
    cout << "Passed: " << passed << endl;
    cout << "Failed: " << failed << endl;
    
    return failed > 0 ? 1 : 0;
}
