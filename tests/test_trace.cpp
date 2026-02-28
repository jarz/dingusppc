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

/**
 * @file test_trace.cpp
 *
 * Standalone unit tests for the PPC tracing infrastructure.
 *
 * Tests:
 *  1. TraceRecord and TraceFileHeader have correct sizes.
 *  2. TraceManager ring-buffer stores and wraps records correctly.
 *  3. TraceManager::dump_ring writes a valid file (header + records).
 *  4. TraceManager::open_file + emit_bb writes to a file with a valid header.
 *  5. PC-range filter suppresses out-of-range records.
 *  6. Instruction-tracing with sampling emits the correct number of records.
 *  7. TRACE_EMIT_BB / TRACE_EMIT_INSN macros are defined (compile test).
 */

/* Force the tracing subsystem on regardless of build flags so the test
 * always exercises the real code paths. */
#ifndef DPPC_TRACE_PPC
#  define DPPC_TRACE_PPC 1
#endif

#include <trace/ppc_trace.h>
#include <trace/tracemanager.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using std::cerr;
using std::cout;
using std::endl;

static int tests_run    = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do {                         \
    tests_run++;                                            \
    if (!(cond)) {                                          \
        cerr << "FAIL: " << (msg)                           \
             << " (" << __FILE__ << ":" << __LINE__ << ")"  \
             << endl;                                       \
        tests_failed++;                                     \
    }                                                       \
} while (0)

/* Helper: read back the file header from a file opened for reading. */
static bool read_file_header(const std::string& path, TraceFileHeader& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = (fread(&out, sizeof(out), 1, f) == 1);
    fclose(f);
    return ok;
}

/* Helper: count records after the header in a trace file. */
static size_t count_file_records(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    if (size < (long)sizeof(TraceFileHeader)) return 0;
    size_t data_bytes = static_cast<size_t>(size) - sizeof(TraceFileHeader);
    return data_bytes / sizeof(TraceRecord);
}

/* Helper: reset the singleton between tests by calling close(). */
static void reset_manager()
{
    TraceManager::get_instance()->close();
    /* Re-enable defaults */
    TraceManager::get_instance()->set_trace_bb(true);
    TraceManager::get_instance()->set_trace_insn(false);
    TraceManager::get_instance()->set_pc_filter(0, 0xFFFFFFFFu);
    TraceManager::get_instance()->set_sample_every(1);
}

/* ------------------------------------------------------------------ */
/*  Test 1: record sizes                                               */
/* ------------------------------------------------------------------ */
static void test_record_sizes()
{
    TEST_ASSERT(sizeof(TraceRecord)    == 32, "TraceRecord must be 32 bytes");
    TEST_ASSERT(sizeof(TraceFileHeader) == 16, "TraceFileHeader must be 16 bytes");
}

/* ------------------------------------------------------------------ */
/*  Test 2: ring-buffer stores records and wraps                       */
/* ------------------------------------------------------------------ */
static void test_ring_buffer()
{
    reset_manager();
    TraceManager* tm = TraceManager::get_instance();

    /* Emit more records than the default ring size to force wrapping. */
    const size_t N = TRACE_RING_DEFAULT_SIZE + 128;
    for (size_t i = 0; i < N; ++i) {
        tm->emit_bb(static_cast<uint32_t>(i * 4),
                    static_cast<uint32_t>(i * 4),
                    static_cast<uint32_t>((i + 1) * 4),
                    0x1 /* TF_BRANCH */);
    }

    /* After wrapping we should be able to dump without crash and the
     * resulting file should have exactly TRACE_RING_DEFAULT_SIZE records
     * (the oldest overwrote, so ring is full). */
    std::string path = std::tmpnam(nullptr);
    path += "_ring_test.bin";
    bool ok = tm->dump_ring(path);
    TEST_ASSERT(ok, "dump_ring must succeed");

    size_t cnt = count_file_records(path);
    TEST_ASSERT(cnt == TRACE_RING_DEFAULT_SIZE,
                "ring dump should contain exactly TRACE_RING_DEFAULT_SIZE records");

    std::remove(path.c_str());
}

/* ------------------------------------------------------------------ */
/*  Test 3: dump_ring writes a valid file header                       */
/* ------------------------------------------------------------------ */
static void test_dump_ring_header()
{
    reset_manager();
    TraceManager* tm = TraceManager::get_instance();

    tm->emit_bb(0x1000, 0x1010, 0x2000, 0x1);

    std::string path = std::tmpnam(nullptr);
    path += "_dump_hdr.bin";
    bool ok = tm->dump_ring(path);
    TEST_ASSERT(ok, "dump_ring must succeed for header test");

    TraceFileHeader hdr{};
    TEST_ASSERT(read_file_header(path, hdr), "must be able to read header back");
    TEST_ASSERT(hdr.magic[0] == 'D' && hdr.magic[1] == 'P' &&
                hdr.magic[2] == 'P' && hdr.magic[3] == 'C',
                "header magic must be DPPC");
    TEST_ASSERT(hdr.version  == 1,  "header version must be 1");
    TEST_ASSERT(hdr.rec_size == 32, "header rec_size must be 32");

    std::remove(path.c_str());
}

/* ------------------------------------------------------------------ */
/*  Test 4: open_file + emit_bb writes records to file                 */
/* ------------------------------------------------------------------ */
static void test_file_sink()
{
    reset_manager();
    TraceManager* tm = TraceManager::get_instance();

    std::string path = std::tmpnam(nullptr);
    path += "_file_sink.bin";

    bool opened = tm->open_file(path);
    TEST_ASSERT(opened, "open_file must succeed");

    const int COUNT = 10;
    for (int i = 0; i < COUNT; ++i) {
        tm->emit_bb(static_cast<uint32_t>(i * 4), static_cast<uint32_t>(i * 4),
                    static_cast<uint32_t>((i + 1) * 4), 0x1);
    }
    tm->close();

    TraceFileHeader hdr{};
    TEST_ASSERT(read_file_header(path, hdr), "must read header from file sink");
    TEST_ASSERT(hdr.magic[0] == 'D', "file sink header magic[0] must be D");
    TEST_ASSERT(hdr.version  == 1,   "file sink header version must be 1");

    size_t cnt = count_file_records(path);
    TEST_ASSERT(cnt == static_cast<size_t>(COUNT),
                "file sink must contain exactly COUNT records");

    std::remove(path.c_str());
}

/* ------------------------------------------------------------------ */
/*  Test 5: PC-range filter suppresses out-of-range records            */
/* ------------------------------------------------------------------ */
static void test_pc_filter()
{
    reset_manager();
    TraceManager* tm = TraceManager::get_instance();
    tm->set_pc_filter(0x1000, 0x2000);

    std::string path = std::tmpnam(nullptr);
    path += "_pc_filter.bin";
    bool opened = tm->open_file(path);
    TEST_ASSERT(opened, "open_file for pc_filter test must succeed");

    /* In range */
    tm->emit_bb(0x1000, 0x1004, 0x1008, 0x1);
    tm->emit_bb(0x1500, 0x1504, 0x1508, 0x1);
    /* Out of range */
    tm->emit_bb(0x0FFF, 0x0FFF, 0x1000, 0x1);
    tm->emit_bb(0x2001, 0x2001, 0x2004, 0x1);
    tm->close();

    size_t cnt = count_file_records(path);
    TEST_ASSERT(cnt == 2, "PC filter must pass only in-range records (expect 2)");

    std::remove(path.c_str());
}

/* ------------------------------------------------------------------ */
/*  Test 6: instruction tracing with sampling                          */
/* ------------------------------------------------------------------ */
static void test_insn_sampling()
{
    reset_manager();
    TraceManager* tm = TraceManager::get_instance();
    tm->set_trace_bb(false);
    tm->set_trace_insn(true);
    tm->set_sample_every(3);  /* emit every 3rd instruction */

    std::string path = std::tmpnam(nullptr);
    path += "_insn_sample.bin";
    bool opened = tm->open_file(path);
    TEST_ASSERT(opened, "open_file for insn_sampling test must succeed");

    /* Emit 9 instructions; expect 3 records (every 3rd). */
    for (int i = 0; i < 9; ++i) {
        tm->emit_insn(static_cast<uint32_t>(0x1000 + i * 4), 0x60000000u);
    }
    tm->close();

    size_t cnt = count_file_records(path);
    TEST_ASSERT(cnt == 3, "sampling every 3 of 9 insns must emit 3 records");

    std::remove(path.c_str());
}

/* ------------------------------------------------------------------ */
/*  Test 7: macro compile test — macros must be callable               */
/* ------------------------------------------------------------------ */
static void test_macros_compile()
{
    /* If DPPC_TRACE_PPC is defined (as it is in this TU), these macros
     * forward to TraceManager.  Just ensure they compile and don't crash. */
    TRACE_EMIT_BB(0x1000u, 0x1004u, 0x2000u, 0x1u);
    TRACE_EMIT_INSN(0x1000u, 0x60000000u);
    TEST_ASSERT(true, "TRACE_EMIT_BB / TRACE_EMIT_INSN compiled and ran");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main()
{
    cout << "===================" << endl;
    cout << "  PPC Trace Tests  " << endl;
    cout << "===================" << endl;

    test_record_sizes();
    test_ring_buffer();
    test_dump_ring_header();
    test_file_sink();
    test_pc_filter();
    test_insn_sampling();
    test_macros_compile();

    cout << endl;
    cout << "Results: " << tests_run << " tests, "
         << tests_failed << " failed" << endl;

    return tests_failed ? 1 : 0;
}
