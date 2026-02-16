#pragma once

#include <thirdparty/loguru/loguru.hpp>

// Shared log configuration for benchmarks.
void bench_init_logging(int argc, char** argv);

// Benchmark builds install a lightweight exception handler to stop execution
// cleanly when the guest raises an exception.
void bench_install_exception_handler();
