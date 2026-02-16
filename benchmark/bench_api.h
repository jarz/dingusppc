#pragma once

#include <functional>
#include <string>
#include <vector>

struct BenchOptions {
    uint32_t runs = 0;          // number of outer iterations (per-case default if zero)
    uint32_t samples = 0;       // number of samples per run (per-case default if zero)
    std::string test_filter {}; // optional substring match on test name (case-insensitive)
    uint64_t watchdog_ms = 0;   // optional per-test watchdog; 0 disables
    bool force_ppc_exec = false;// run via ppc_exec instead of ppc_exec_until
    bool verbose_dump = false;  // optionally dump encoded snippet for debugging
    std::string log_dir;        // directory for log files / artifacts (may be empty)
};

struct Bench {
    std::string name;
    std::string description;
    std::function<int(const BenchOptions&)> run;
};
