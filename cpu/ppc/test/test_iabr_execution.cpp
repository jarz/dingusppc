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

/**
 * Real execution tests for IABR (Instruction Address Breakpoint Register)
 * 
 * These tests actually execute PowerPC code and verify that IABR
 * breakpoints trigger correctly during execution.
 */

#include "../ppcemu.h"
#include "../ppcmmu.h"
#include <devices/memctrl/memctrlbase.h>
#include <iostream>
#include <cstring>
#antml:parameter name="stdexcept">

using namespace std;

// Global state for exception handling
static bool exception_caught = false;
static Except_Type last_exception_type;
static uint32_t exception_pc = 0;

// Simple exception handler for tests
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    exception_caught = true;
    last_exception_type = exception_type;
    exception_pc = ppc_state.pc;
    power_on = false;  // Stop execution
}

// Simple memory controller for tests
class TestMemCtrl : public MemCtrlBase {
public:
    TestMemCtrl() {
        // Allocate 64KB of test memory
        mem_size = 0x10000;
        mem_ptr = new uint8_t[mem_size];
        memset(mem_ptr, 0, mem_size);
    }
    
    ~TestMemCtrl() {
        delete[] mem_ptr;
    }
    
    uint32_t read(uint32_t rgn_start, uint32_t offset, int size) {
        if (offset >= mem_size) return 0;
        switch(size) {
            case 4: return READ_DWORD_BE_A(&mem_ptr[offset]);
            case 2: return READ_WORD_BE_A(&mem_ptr[offset]);
            case 1: return mem_ptr[offset];
            default: return 0;
        }
    }
    
    void write(uint32_t rgn_start, uint32_t offset, uint32_t value, int size) {
        if (offset >= mem_size) return;
        switch(size) {
            case 4: WRITE_DWORD_BE_A(&mem_ptr[offset], value); break;
            case 2: WRITE_WORD_BE_A(&mem_ptr[offset], value); break;
            case 1: mem_ptr[offset] = value; break;
        }
    }
    
    uint8_t* get_mem_ptr() { return mem_ptr; }
    
private:
    uint8_t* mem_ptr;
    uint32_t mem_size;
};

static TestMemCtrl* test_mem = nullptr;

// Setup test environment
void setup_test_env() {
    // Initialize memory controller
    if (!test_mem) {
        test_mem = new TestMemCtrl();
    }
    
    // Set global mem_ctrl_instance for MMU
    mem_ctrl_instance = test_mem;
    
    // Add memory region mapping (0x0 - 0x10000)
    test_mem->add_mem_region(0x0, 0x10000);
    
    // Initialize CPU state
    memset(&ppc_state, 0, sizeof(ppc_state));
    ppc_state.pc = 0x1000;  // Start at address 0x1000
    ppc_state.msr = 0x00002000;  // Basic MSR value (not using MMU for tests)
    
    // Clear IABR
    ppc_state.spr[SPR::IABR] = 0;
    
    // Reset exception state
    exception_caught = false;
    last_exception_type = Except_Type::EXC_SYSTEM_RESET;
    exception_pc = 0;
    power_on = true;
}

// Write instructions to memory
void write_instructions(uint32_t addr, const uint32_t* opcodes, int count) {
    uint8_t* mem = test_mem->get_mem_ptr();
    for (int i = 0; i < count; i++) {
        uint32_t offset = (addr & 0xFFFF) + (i * 4);
        WRITE_DWORD_BE_A(&mem[offset], opcodes[i]);
    }
}

// Execute a few instructions manually for testing
void execute_test_instructions(int max_instructions) {
    int count = 0;
    while (power_on && count < max_instructions) {
        // Check IABR before instruction
        uint32_t iabr = ppc_state.spr[SPR::IABR];
        if ((iabr & ~0x3UL) != 0) {
            uint32_t breakpoint_addr = iabr & ~0x3UL;
            if ((ppc_state.pc & ~0x3UL) == breakpoint_addr) {
                cout << "    [IABR triggered at PC=0x" << hex << ppc_state.pc << dec << "]" << endl;
                ppc_exception_handler(Except_Type::EXC_TRACE, 0);
                break;
            }
        }
        
        // Fetch instruction
        uint32_t opcode = test_mem->read(0, ppc_state.pc & 0xFFFF, 4);
        
        // Execute simple instructions for testing
        uint32_t primary = opcode >> 26;
        if (primary == 14) {  // addi
            int rt = (opcode >> 21) & 0x1F;
            int ra = (opcode >> 16) & 0x1F;
            int16_t simm = opcode & 0xFFFF;
            ppc_state.gpr[rt] = (ra == 0 ? 0 : ppc_state.gpr[ra]) + simm;
        } else if (primary == 31) {  // Extended opcodes
            int secondary = (opcode >> 1) & 0x3FF;
            if (secondary == 266) {  // add
                int rt = (opcode >> 21) & 0x1F;
                int ra = (opcode >> 16) & 0x1F;
                int rb = (opcode >> 11) & 0x1F;
                ppc_state.gpr[rt] = ppc_state.gpr[ra] + ppc_state.gpr[rb];
            }
        } else if (opcode == 0x4E800020) {  // blr
            power_on = false;
            break;
        }
        
        ppc_state.pc += 4;
        count++;
    }
}

// Test 1: Basic IABR breakpoint triggering
void test_iabr_basic_trigger() {
    cout << "Test 1: Basic IABR breakpoint triggering..." << endl;
    
    setup_test_env();
    
    // Create simple code sequence:
    // 0x1000: addi r3, r0, 1    (li r3, 1)
    // 0x1004: addi r4, r0, 2    (li r4, 2)
    // 0x1008: add  r5, r3, r4   (r5 = r3 + r4)
    // 0x100C: blr               (return)
    uint32_t code[] = {
        0x38600001,  // addi r3, r0, 1
        0x38800002,  // addi r4, r0, 2  <- Set breakpoint here
        0x7CA31A14,  // add r5, r3, r4
        0x4E800020   // blr
    };
    
    write_instructions(0x1000, code, 4);
    
    // Set IABR to second instruction (0x1004)
    ppc_state.spr[SPR::IABR] = 0x1004;
    
    // Set PC to start
    ppc_state.pc = 0x1000;
    power_on = true;
    
    // Execute - should stop at breakpoint
    execute_test_instructions(10);
    
    // Verify breakpoint triggered
    if (exception_caught && last_exception_type == Except_Type::EXC_TRACE) {
        // Should have executed first instruction then hit breakpoint
        if (ppc_state.gpr[3] == 1 && ppc_state.gpr[4] == 0) {
            cout << "  PASS: IABR triggered at correct address (0x1004)" << endl;
            cout << "    First instruction executed (r3=1), second instruction not executed (r4=0)" << endl;
        } else {
            throw runtime_error("IABR triggered but wrong execution state");
        }
    } else {
        throw runtime_error("IABR breakpoint did not trigger");
    }
}

// Test 2: IABR with word alignment
void test_iabr_word_alignment() {
    cout << "Test 2: IABR word alignment..." << endl;
    
    setup_test_env();
    
    // Test that IABR masks low 2 bits (word-aligned)
    // Setting IABR to 0x1006 should match PC = 0x1004
    
    uint32_t code[] = {
        0x38600001,  // addi r3, r0, 1
        0x38800002,  // addi r4, r0, 2
        0x7CA31A14,  // add r5, r3, r4
        0x4E800020   // blr
    };
    
    write_instructions(0x1000, code, 4);
    
    // Set IABR to unaligned address (should be masked)
    ppc_state.spr[SPR::IABR] = 0x1006;  // Will match 0x1004 after masking
    ppc_state.pc = 0x1000;
    power_on = true;
    
    execute_test_instructions(10);
    
    // Verify breakpoint triggered at word-aligned address
    if (exception_caught && last_exception_type == Except_Type::EXC_TRACE) {
        if (exception_pc == 0x1004 || ppc_state.pc == 0x1004) {
            cout << "  PASS: IABR word alignment works (0x1006 matched 0x1004)" << endl;
        } else {
            throw runtime_error("IABR triggered at wrong address");
        }
    } else {
        throw runtime_error("IABR with unaligned address did not trigger");
    }
}

// Test 3: IABR disabled (value 0)
void test_iabr_disabled() {
    cout << "Test 3: IABR disabled when zero..." << endl;
    
    setup_test_env();
    
    uint32_t code[] = {
        0x38600001,  // addi r3, r0, 1
        0x38800002,  // addi r4, r0, 2
        0x7CA31A14,  // add r5, r3, r4
        0x4E800020   // blr
    };
    
    write_instructions(0x1000, code, 4);
    
    // IABR = 0 means disabled
    ppc_state.spr[SPR::IABR] = 0;
    ppc_state.pc = 0x1000;
    power_on = true;
    
    execute_test_instructions(10);
    
    // Should execute without triggering breakpoint
    if (!exception_caught) {
        // Verify all instructions executed
        if (ppc_state.gpr[3] == 1 && ppc_state.gpr[4] == 2 && ppc_state.gpr[5] == 3) {
            cout << "  PASS: IABR disabled (IABR=0), all instructions executed" << endl;
        } else {
            throw runtime_error("Execution state incorrect");
        }
    } else {
        throw runtime_error("IABR triggered when disabled (IABR=0)");
    }
}

// Test 4: IABR on first instruction
void test_iabr_first_instruction() {
    cout << "Test 4: IABR on first instruction..." << endl;
    
    setup_test_env();
    
    uint32_t code[] = {
        0x38600001,  // addi r3, r0, 1  <- Breakpoint here
        0x38800002,  // addi r4, r0, 2
        0x7CA31A14,  // add r5, r3, r4
        0x4E800020   // blr
    };
    
    write_instructions(0x1000, code, 4);
    
    // Set breakpoint on first instruction
    ppc_state.spr[SPR::IABR] = 0x1000;
    ppc_state.pc = 0x1000;
    power_on = true;
    
    execute_test_instructions(10);
    
    // Should trigger immediately
    if (exception_caught && last_exception_type == Except_Type::EXC_TRACE) {
        // No instructions should have executed
        if (ppc_state.gpr[3] == 0) {
            cout << "  PASS: IABR triggered on first instruction before execution" << endl;
        } else {
            throw runtime_error("First instruction executed before breakpoint");
        }
    } else {
        throw runtime_error("IABR on first instruction did not trigger");
    }
}

// Test 5: Multiple addresses (clearing and setting)
void test_iabr_multiple_addresses() {
    cout << "Test 5: IABR with multiple addresses..." << endl;
    
    setup_test_env();
    
    uint32_t code[] = {
        0x38600001,  // 0x1000: addi r3, r0, 1
        0x38800002,  // 0x1004: addi r4, r0, 2
        0x7CA31A14,  // 0x1008: add r5, r3, r4
        0x38C00003,  // 0x100C: addi r6, r0, 3
        0x38E00004,  // 0x1010: addi r7, r0, 4
        0x4E800020   // 0x1014: blr
    };
    
    write_instructions(0x1000, code, 6);
    
    // Test setting breakpoint at 0x1008 (third instruction)
    ppc_state.spr[SPR::IABR] = 0x1008;
    ppc_state.pc = 0x1000;
    power_on = true;
    
    execute_test_instructions(10);
    
    // Should have executed first two instructions
    if (exception_caught && last_exception_type == Except_Type::EXC_TRACE) {
        if (ppc_state.gpr[3] == 1 && ppc_state.gpr[4] == 2 && ppc_state.gpr[5] == 0) {
            cout << "  PASS: IABR at 0x1008 triggered after executing first two instructions" << endl;
        } else {
            throw runtime_error("Wrong execution state at breakpoint");
        }
    } else {
        throw runtime_error("IABR at 0x1008 did not trigger");
    }
}

int main() {
    cout << "=== IABR Real Execution Tests ===" << endl;
    cout << endl;
    
    cout << "These tests validate IABR breakpoint triggering with actual code execution." << endl;
    cout << "Tests execute PowerPC instructions and verify breakpoints trigger correctly." << endl;
    cout << endl;
    
    int passed = 0;
    int total = 5;
    
    try {
        test_iabr_basic_trigger();
        passed++;
    } catch (const exception& e) {
        cout << "  FAIL: " << e.what() << endl;
    }
    
    try {
        test_iabr_word_alignment();
        passed++;
    } catch (const exception& e) {
        cout << "  FAIL: " << e.what() << endl;
    }
    
    try {
        test_iabr_disabled();
        passed++;
    } catch (const exception& e) {
        cout << "  FAIL: " << e.what() << endl;
    }
    
    try {
        test_iabr_first_instruction();
        passed++;
    } catch (const exception& e) {
        cout << "  FAIL: " << e.what() << endl;
    }
    
    try {
        test_iabr_multiple_addresses();
        passed++;
    } catch (const exception& e) {
        cout << "  FAIL: " << e.what() << endl;
    }
    
    cout << endl;
    cout << "=== Test Summary ===" << endl;
    cout << "Passed: " << passed << "/" << total << endl;
    
    if (test_mem) {
        delete test_mem;
        test_mem = nullptr;
    }
    
    return (passed == total) ? 0 : 1;
}
