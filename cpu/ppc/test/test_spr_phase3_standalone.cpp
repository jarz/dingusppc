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

/** @file Test Phase 3 G3/G4 SPRs (BAT4-7, L2CR, ICTC, MSSCR0, THRM1-3) */

#include "../ppcemu.h"
#include <iostream>

using namespace std;

// Test helper to write and read back an SPR
bool test_spr_rw(const char* name, int spr_num, uint32_t test_value) {
    // Encode mtspr/mfspr instructions
    uint32_t spr_encoded = ((spr_num & 0x1F) << 5) | ((spr_num >> 5) & 0x1F);
    uint32_t mtspr_opcode = 0x7C600000 | (spr_encoded << 11) | (467 << 1);
    uint32_t mfspr_opcode = 0x7C800000 | (spr_encoded << 11) | (339 << 1);
    
    // Set supervisor mode
    ppc_state.msr &= ~MSR::PR;
    
    ppc_state.gpr[3] = test_value;
    ppc_state.gpr[4] = 0xDEADBEEF;
    
    ppc_main_opcode(ppc_opcode_grabber, mtspr_opcode);
    ppc_main_opcode(ppc_opcode_grabber, mfspr_opcode);
    
    if (ppc_state.gpr[4] != test_value) {
        cout << "FAIL: " << name << " (SPR " << spr_num << "): wrote 0x" << hex << test_value 
             << ", read 0x" << hex << ppc_state.gpr[4] << endl;
        return false;
    }
    
    cout << "PASS: " << name << " (SPR " << spr_num << ")" << endl;
    return true;
}

int main() {
    initialize_ppc_opcode_table();
    ppc_mmu_init();  // Initialize MMU including ibat_update/dbat_update functions
    
    uint32_t initial_msr = MSR::ME | MSR::IP;
    ppc_msr_did_change(ppc_state.msr, initial_msr, false);
    
    cout << "Testing Phase 3 G3/G4 SPRs..." << endl << endl;
    
    int passed = 0;
    int failed = 0;
    
    // Test G4 additional BAT registers
    cout << "Testing IBAT4-7..." << endl;
    if (test_spr_rw("IBAT4U", SPR::IBAT4U, 0x80001234)) passed++; else failed++;
    if (test_spr_rw("IBAT4L", SPR::IBAT4L, 0x00005678)) passed++; else failed++;
    if (test_spr_rw("IBAT5U", SPR::IBAT5U, 0x90001234)) passed++; else failed++;
    if (test_spr_rw("IBAT5L", SPR::IBAT5L, 0x00009ABC)) passed++; else failed++;
    if (test_spr_rw("IBAT6U", SPR::IBAT6U, 0xA0001234)) passed++; else failed++;
    if (test_spr_rw("IBAT6L", SPR::IBAT6L, 0x0000DEF0)) passed++; else failed++;
    if (test_spr_rw("IBAT7U", SPR::IBAT7U, 0xB0001234)) passed++; else failed++;
    if (test_spr_rw("IBAT7L", SPR::IBAT7L, 0x00001111)) passed++; else failed++;
    
    cout << "Testing DBAT4-7..." << endl;
    if (test_spr_rw("DBAT4U", SPR::DBAT4U, 0xC0001234)) passed++; else failed++;
    if (test_spr_rw("DBAT4L", SPR::DBAT4L, 0x00002222)) passed++; else failed++;
    if (test_spr_rw("DBAT5U", SPR::DBAT5U, 0xD0001234)) passed++; else failed++;
    if (test_spr_rw("DBAT5L", SPR::DBAT5L, 0x00003333)) passed++; else failed++;
    if (test_spr_rw("DBAT6U", SPR::DBAT6U, 0xE0001234)) passed++; else failed++;
    if (test_spr_rw("DBAT6L", SPR::DBAT6L, 0x00004444)) passed++; else failed++;
    if (test_spr_rw("DBAT7U", SPR::DBAT7U, 0xF0001234)) passed++; else failed++;
    if (test_spr_rw("DBAT7L", SPR::DBAT7L, 0x00005555)) passed++; else failed++;
    
    // Test L2 cache control
    cout << "Testing L2CR..." << endl;
    if (test_spr_rw("L2CR", SPR::L2CR, 0x80000000)) passed++; else failed++;  // L2E bit set
    
    // Test system control registers
    cout << "Testing system control..." << endl;
    if (test_spr_rw("ICTC", SPR::ICTC, 0x000001FF)) passed++; else failed++;
    if (test_spr_rw("MSSCR0", SPR::MSSCR0, 0x12345678)) passed++; else failed++;
    
    // Test thermal management
    cout << "Testing thermal management..." << endl;
    if (test_spr_rw("THRM1", SPR::THRM1, 0x80000001)) passed++; else failed++;
    if (test_spr_rw("THRM2", SPR::THRM2, 0x40000002)) passed++; else failed++;
    if (test_spr_rw("THRM3", SPR::THRM3, 0x00000001)) passed++; else failed++;
    
    cout << endl << "=== Phase 3 Test Results ===" << endl;
    cout << "Passed: " << passed << endl;
    cout << "Failed: " << failed << endl;
    
    return failed > 0 ? 1 : 0;
}
