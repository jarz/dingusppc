#include "benchmark/bench_api.h"
#include "benchmark/bench_common.h"

#include <CLI11.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <thirdparty/loguru/loguru.hpp>

namespace bench_checksum {
void register_benchmarks(std::vector<Bench>& benches);
}

namespace bench_dispatch {
void register_benchmarks(std::vector<Bench>& benches);
}

static void list_benchmarks(const std::vector<Bench>& benches) {
    std::cout << "Available benchmarks:\n";
    for (const auto& b : benches) {
        std::cout << "  " << b.name << " - " << b.description << "\n";
    }
}

int main(int argc, char** argv) {
    bench_init_logging(argc, argv);
    bench_install_exception_handler();

    std::vector<Bench> benches;
    bench_checksum::register_benchmarks(benches);
    bench_dispatch::register_benchmarks(benches);

    CLI::App app{"DingusPPC benchmark suite"};
    std::string bench_name;
    bool list_only = false;
    BenchOptions options;
    std::string log_path = "./tmp/benchmarks.log";

    app.add_option("-b,--bench", bench_name, "Benchmark to run (use --list to see options)");
    app.add_flag("--list", list_only, "List available benchmarks and exit");
    app.add_option("--runs", options.runs, "Outer run count (bench default if 0)");
    app.add_option("--samples", options.samples, "Samples per run (bench default if 0)");
    app.add_option("--test-filter", options.test_filter, "Substring filter for test names (case-insensitive)");
    app.add_option("--watchdog-ms", options.watchdog_ms, "Per-test watchdog in ms (0 disables)");
    app.add_flag("--force-ppc-exec", options.force_ppc_exec, "Use ppc_exec instead of ppc_exec_until for all tests");
    app.add_flag("--verbose-dump", options.verbose_dump, "Dump encoded snippets for debugging");
    app.add_option("--logfile", log_path, "Log output path (default ./tmp/benchmarks.log)");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    // Ensure parent dir for logs exists (if provided)
    std::error_code ec;
    std::filesystem::path log_path_fs(log_path);
    std::filesystem::path parent_dir = log_path_fs.parent_path();
    if (parent_dir.empty()) {
        parent_dir = std::filesystem::current_path();
    }
    std::filesystem::create_directories(parent_dir, ec);
    if (ec) {
        LOG_F(WARNING, "Could not create %s for logs: %s", parent_dir.string().c_str(), ec.message().c_str());
    } else {
        loguru::add_file(log_path.c_str(), loguru::Truncate, loguru::Verbosity_INFO);
    }
    options.log_dir = parent_dir.string();

    if (list_only) {
        list_benchmarks(benches);
        return 0;
    }

    if (bench_name.empty()) {
        list_benchmarks(benches);
        LOG_F(ERROR, "No benchmark selected (use -b/--bench)");
        return 1;
    }

    auto it = std::find_if(benches.begin(), benches.end(), [&](const Bench& b) {
        return b.name == bench_name;
    });

    if (it == benches.end()) {
        list_benchmarks(benches);
        LOG_F(ERROR, "Unknown benchmark: %s", bench_name.c_str());
        return 1;
    }

    return it->run(options);
}
