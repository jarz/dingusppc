// libFuzzer harness for PPC disassembler
#include "cpu/ppc/ppcdisasm.h"
#include <cstdint>
#include <cstring>
#include <loguru.hpp>

static inline uint32_t be32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static bool initialized = false;
    if (!initialized) {
        loguru::g_stderr_verbosity = -9;
        g_quiet_disasm = true;
        initialized = true;
    }
    if (size < 4) return 0;
    uint32_t opcode = be32(data);
    PPCDisasmContext ctx{};
    ctx.instr_code = opcode;
    ctx.instr_addr = 0;
    ctx.simplified = false;
    disassemble_single(&ctx);
    return 0;
}
