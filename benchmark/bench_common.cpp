#include "bench_common.h"

#include "cpu/ppc/ppcemu.h"

void bench_init_logging(int argc, char** argv) {
    loguru::g_preamble_date    = false;
    loguru::g_preamble_time    = false;
    loguru::g_preamble_thread  = false;
    loguru::g_stderr_verbosity = 0;
    loguru::init(argc, argv);
}

#if defined(PPC_BENCHMARKS)
void ppc_exception_handler(Except_Type exception_type, uint32_t srr1_bits) {
    power_on = false;
    power_off_reason = po_benchmark_exception;
}
#endif

void bench_install_exception_handler() {
    // Nothing to do: the presence of ppc_exception_handler in this TU is enough.
}
