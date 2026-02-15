/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-24 divingkatae and maximum
                      (theweirdo)     spatium

(Contact divingkatae#1017 or powermax#2286 on Discord for more info)

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
 * DABR (Data Address Breakpoint Register) Execution Tests
 * 
 * These tests validate that DABR data breakpoints actually trigger during
 * real memory access operations, not just register read/write.
 */

#include <devices/memctrl.h>
#include <cpu/ppc/ppcemu.h>
#include <cpu/ppc/ppcmmu.h>
#include <memaccess.h>
#include <iostream>
#include <cstring>

// Simple memory controller for testing
class TestMemCtrl : public MemCtrlBase {
public:
    TestMemCtrl() {
        mem_size = 0x10000;  // 64KB
        mem_data = new uint8_t[mem_size];
        std::memset(mem_data, 0, mem_size);
    }
    
    ~TestMemCtrl() {
        delete[] mem_data;
    }
    
    bool add_mem_region(uint32_t start, uint32_t size, uint32_t dest) {
        return true;
    }
    
    void set_data(uint32_t addr, uint32_t value) {
        if (addr + 4 <= mem_size) {
            WRITE_DWORD_BE_A(&mem_data[addr], value);
        }
    }
    
    uint32_t get_data(uint32_t addr) {
        if (addr + 4 <= mem_size) {
            return READ_DWORD_BE_A(&mem_data[addr]);
        }
        return 0;
    }

private:
    uint8_t* mem_data;
    uint32_t mem_size;
};

// Exception tracking
static bool exception_triggered = false;
static Except_Type exception_type = Except_Type::EXC_PROGRAM;
static uint32_t exception_pc = 0;

// Exception handler callback
void test_exception_handler(Except_Type exception, uint32_t srr1_bits) {
    exception_triggered = true;
    exception_type = exception;
    exception_pc = ppc_state.pc;
    ppc_state.pc = 0xFFFFFFFF;  // Stop execution
    power_on = false;
}

// Memory access test helper
void execute_memory_test(uint32_t test_addr, bool is_write) {
    // Reset exception state
    exception_triggered = false;
    exception_type = Except_Type::EXC_PROGRAM;
    exception_pc = 0;
    power_on = true;
    
    // Set up CPU state
    ppc_state.pc = 0x1000;
    ppc_state.msr = 0x9032;  // PR=0 (supervisor mode)
    
    try {
        if (is_write) {
            // Test write access
            mmu_write_vmem<uint32_t>(test_addr, 0x12345678);
        } else {
            // Test read access
            uint32_t value = mmu_read_vmem<uint32_t>(test_addr);
            (void)value;  // Suppress unused warning
        }
    } catch (...) {
        // Exception caught
    }
}

// Test 1: Data read breakpoint (DR bit set)
int test_dabr_read_breakpoint() {
    std::cout << "Test 1: DABR read breakpoint... ";
    
    // Set DABR with DR bit (read enable)
    uint32_t dabr_value = 0x00002001;  // Address 0x2000, DR=1
    ppc_state.spr[SPR::DABR] = dabr_value;
    
    // Try to read from address 0x2000
    execute_memory_test(0x2000, false);
    
    // Verify breakpoint triggered
    if (!exception_triggered || exception_type != Except_Type::EXC_TRACE) {
        std::cout << "FAILED - Breakpoint didn't trigger on read\n";
        return 1;
    }
    
    std::cout << "PASSED\n";
    return 0;
}

// Test 2: Data write breakpoint (DW bit set)
int test_dabr_write_breakpoint() {
    std::cout << "Test 2: DABR write breakpoint... ";
    
    // Set DABR with DW bit (write enable)
    uint32_t dabr_value = 0x00003002;  // Address 0x3000, DW=1
    ppc_state.spr[SPR::DABR] = dabr_value;
    
    // Try to write to address 0x3000
    execute_memory_test(0x3000, true);
    
    // Verify breakpoint triggered
    if (!exception_triggered || exception_type != Except_Type::EXC_TRACE) {
        std::cout << "FAILED - Breakpoint didn't trigger on write\n";
        return 1;
    }
    
    std::cout << "PASSED\n";
    return 0;
}

// Test 3: Read+Write breakpoint (DR+DW bits set)
int test_dabr_readwrite_breakpoint() {
    std::cout << "Test 3: DABR read+write breakpoint... ";
    
    // Set DABR with both DR and DW bits
    uint32_t dabr_value = 0x00004003;  // Address 0x4000, DR=1, DW=1
    ppc_state.spr[SPR::DABR] = dabr_value;
    
    // Test read
    execute_memory_test(0x4000, false);
    if (!exception_triggered || exception_type != Except_Type::EXC_TRACE) {
        std::cout << "FAILED - Read didn't trigger\n";
        return 1;
    }
    
    // Test write
    execute_memory_test(0x4000, true);
    if (!exception_triggered || exception_type != Except_Type::EXC_TRACE) {
        std::cout << "FAILED - Write didn't trigger\n";
        return 1;
    }
    
    std::cout << "PASSED\n";
    return 0;
}

// Test 4: Address masking (low 3 bits ignored)
int test_dabr_address_masking() {
    std::cout << "Test 4: DABR address masking (8-byte granularity)... ";
    
    // Set DABR to 0x5000 with DR bit
    uint32_t dabr_value = 0x00005001;  // Address 0x5000, DR=1
    ppc_state.spr[SPR::DABR] = dabr_value;
    
    // Access 0x5004 should also trigger (within 8-byte range)
    execute_memory_test(0x5004, false);
    
    if (!exception_triggered || exception_type != Except_Type::EXC_TRACE) {
        std::cout << "FAILED - Masking didn't work\n";
        return 1;
    }
    
    std::cout << "PASSED\n";
    return 0;
}

// Test 5: DABR disabled (address = 0)
int test_dabr_disabled() {
    std::cout << "Test 5: DABR disabled (DABR=0)... ";
    
    // Set DABR to 0 (disabled)
    ppc_state.spr[SPR::DABR] = 0;
    
    // Access should not trigger breakpoint
    execute_memory_test(0x2000, false);
    
    if (exception_triggered) {
        std::cout << "FAILED - Breakpoint triggered when disabled\n";
        return 1;
    }
    
    std::cout << "PASSED\n";
    return 0;
}

// Test 6: Write-only doesn't trigger on read
int test_dabr_writeonly() {
    std::cout << "Test 6: DABR write-only (no trigger on read)... ";
    
    // Set DABR with only DW bit (write enable)
    uint32_t dabr_value = 0x00006002;  // Address 0x6000, DW=1, DR=0
    ppc_state.spr[SPR::DABR] = dabr_value;
    
    // Read should NOT trigger
    execute_memory_test(0x6000, false);
    
    if (exception_triggered) {
        std::cout << "FAILED - Read triggered write-only breakpoint\n";
        return 1;
    }
    
    // Write SHOULD trigger
    execute_memory_test(0x6000, true);
    
    if (!exception_triggered || exception_type != Except_Type::EXC_TRACE) {
        std::cout << "FAILED - Write didn't trigger\n";
        return 1;
    }
    
    std::cout << "PASSED\n";
    return 0;
}

int main() {
    std::cout << "\n=== DABR Execution Tests ===\n\n";
    
    // Set up test environment
    TestMemCtrl test_mem;
    mem_ctrl_instance = &test_mem;
    
    // Add memory region
    test_mem.add_mem_region(0, 0x10000, 0);
    
    // Set up exception handler
    ppc_exception_handler = test_exception_handler;
    
    // Run tests
    int failures = 0;
    failures += test_dabr_read_breakpoint();
    failures += test_dabr_write_breakpoint();
    failures += test_dabr_readwrite_breakpoint();
    failures += test_dabr_address_masking();
    failures += test_dabr_disabled();
    failures += test_dabr_writeonly();
    
    // Summary
    std::cout << "\n=== Summary ===\n";
    std::cout << "Tests run: 6\n";
    std::cout << "Failures: " << failures << "\n";
    
    if (failures == 0) {
        std::cout << "\n✅ All DABR execution tests PASSED!\n";
        std::cout << "DABR data breakpoints are execution-validated.\n\n";
    } else {
        std::cout << "\n❌ Some tests FAILED\n\n";
    }
    
    return failures > 0 ? 1 : 0;
}
