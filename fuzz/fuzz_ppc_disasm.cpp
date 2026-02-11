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

// libFuzzer harness for the PPC disassembler.
//
// Takes 4 bytes of fuzz input, interprets them as a big-endian PPC opcode,
// and runs the disassembler on it. Exercises all opcode-dispatch,
// operand-extraction, and string-formatting paths.

#include "cpu/ppc/ppcdisasm.h"
#include "cpu/ppc/ppcemu.h"
#include <cstdint>
#include <cstring>

// Stub: absorb exceptions without crashing the fuzzer.
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    power_on = false;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4)
        return 0;

    uint32_t opcode = (uint32_t(data[0]) << 24) |
                      (uint32_t(data[1]) << 16) |
                      (uint32_t(data[2]) <<  8) |
                      (uint32_t(data[3]));

    PPCDisasmContext ctx;
    ctx.instr_addr = 0;          // aligned address
    ctx.instr_code = opcode;
    ctx.simplified = true;

    disassemble_single(&ctx);

    return 0;
}
