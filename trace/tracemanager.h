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

/** @file tracemanager.h
 *
 * TraceManager: ring-buffer sink + optional binary-file sink for PPC traces.
 *
 * Only compiled when DPPC_TRACE_PPC is defined.
 */

#pragma once

#ifdef DPPC_TRACE_PPC

#include <trace/ppc_trace.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

/** Default ring-buffer capacity (number of records). */
static constexpr size_t TRACE_RING_DEFAULT_SIZE = 65536;

/**
 * Manages zero, one, or both sinks:
 *  - in-memory ring buffer (always present when tracing is on)
 *  - optional binary file (enabled via open_file())
 *
 * Thread-safety: emit_bb / emit_insn are intentionally not locked —
 * they are called only from the single CPU execution thread.
 * open_file / dump_ring / close may be called from any thread before
 * or after emulation (not concurrently with emission).
 */
class TraceManager {
public:
    /** Return the process-wide singleton. */
    static TraceManager* get_instance();

    /* ---- configuration (call before emitting) ---- */

    void set_trace_bb(bool enable)   { trace_bb_   = enable; }
    void set_trace_insn(bool enable) { trace_insn_ = enable; }

    /** Only emit records where start_pc falls in [start, end]. */
    void set_pc_filter(uint32_t start_pc, uint32_t end_pc) {
        pc_start_ = start_pc;
        pc_end_   = end_pc;
    }

    /** Emit only every Nth instruction (for INSN records; 1 = every). */
    void set_sample_every(uint32_t n) { sample_n_ = (n < 1) ? 1 : n; }

    /** Open a binary file sink.  Writes the file header immediately.
     *  @return true on success. */
    bool open_file(const std::string& path);

    /* ---- emission (hot path, called from CPU thread) ---- */

    void emit_bb(uint32_t start_pc, uint32_t end_pc,
                 uint32_t next_pc, unsigned ef);

    void emit_insn(uint32_t pc, uint32_t opcode);

    /* ---- ring-buffer operations ---- */

    /** Dump the ring buffer contents (oldest first) to a binary file.
     *  The file gets the same header as a normal trace file.
     *  @return true on success. */
    bool dump_ring(const std::string& path);

    /* ---- lifecycle ---- */

    /** Flush and close the file sink (if open). */
    void close();

    ~TraceManager();

private:
    TraceManager();

    bool write_header(FILE* f);
    void push_ring(const TraceRecord& rec);
    void write_file(const TraceRecord& rec);

    bool     trace_bb_   = true;
    bool     trace_insn_ = false;
    uint32_t pc_start_   = 0;
    uint32_t pc_end_     = 0xFFFFFFFFu;
    uint32_t sample_n_   = 1;

    std::atomic<uint32_t> seq_{0};
    uint32_t              insn_ctr_ = 0;  /**< for sampling */

    /* ring buffer */
    size_t                   ring_size_;
    size_t                   ring_head_{0};
    std::vector<TraceRecord> ring_;

    /* file sink */
    FILE*  file_{nullptr};

    static constexpr size_t FILE_BUF_SIZE = 64 * 1024;
    char   file_buf_[FILE_BUF_SIZE];

    static TraceManager* instance_;
};

#endif /* DPPC_TRACE_PPC */
