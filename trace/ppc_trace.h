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

/** @file ppc_trace.h
 *
 * Compile-time optional PPC execution tracing.
 *
 * When DPPC_TRACE_PPC is not defined every macro expands to a no-op
 * and nothing is linked into the final binary.
 *
 * Binary record layout (32 bytes, little-endian on little-endian hosts):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *      0     1    type      (TraceRecType)
 *      1     1    flags     (TraceFlags)
 *      2     2    reserved  (zero)
 *      4     4    seq       (monotonic counter, wraps)
 *      8     8    timestamp (nanoseconds, virtual CPU time)
 *     16     4    start_pc  (BB: first PC of block; INSN: PC)
 *     20     4    end_pc    (BB: PC of branch/trap insn; INSN: opcode word)
 *     24     4    next_pc   (first PC of next block)
 *     28     4    reason    (raw exec_flags value)
 *
 * File header (16 bytes):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *      0     4    magic     "DPPC"
 *      4     2    version   1
 *      6     1    endian    0 = big, 1 = little
 *      7     1    rec_size  sizeof(TraceRecord) == 32
 *      8     8    reserved  (zero)
 */

#pragma once

#include <cstdint>

/** Record type discriminator. */
enum class TraceRecType : uint8_t {
    BB   = 0x01,  /**< basic-block boundary */
    INSN = 0x02,  /**< single instruction (optional, off by default) */
};

/** Reason flags stored in TraceRecord::flags (mirrors exec_flags bits). */
enum TraceFlags : uint8_t {
    TF_BRANCH    = 0x01,  /**< branch taken */
    TF_EXCEPTION = 0x02,  /**< exception handler invoked */
    TF_RFI       = 0x04,  /**< rfi instruction */
};

#pragma pack(push, 1)
/** Fixed-size 32-byte trace record written to ring buffer and/or file. */
struct TraceRecord {
    uint8_t  type;       /**< TraceRecType */
    uint8_t  flags;      /**< TraceFlags */
    uint16_t reserved;
    uint32_t seq;        /**< monotonic sequence number (wraps) */
    uint64_t timestamp;  /**< virtual CPU nanoseconds */
    uint32_t start_pc;   /**< start of basic block (or PC for INSN) */
    uint32_t end_pc;     /**< last PC of basic block (or opcode for INSN) */
    uint32_t next_pc;    /**< first PC of the next basic block */
    uint32_t reason;     /**< raw exec_flags value */
};

/** 16-byte file header that precedes the record stream. */
struct TraceFileHeader {
    uint8_t  magic[4];    /**< "DPPC" */
    uint16_t version;     /**< 1 */
    uint8_t  endian;      /**< 0 = big-endian host, 1 = little-endian host */
    uint8_t  rec_size;    /**< sizeof(TraceRecord), currently 32 */
    uint8_t  reserved[8]; /**< zero */
};
#pragma pack(pop)

static_assert(sizeof(TraceRecord)    == 32, "TraceRecord must be 32 bytes");
static_assert(sizeof(TraceFileHeader) == 16, "TraceFileHeader must be 16 bytes");

/* ------------------------------------------------------------------ */
/*  Compile-time gate                                                   */
/* ------------------------------------------------------------------ */

#ifdef DPPC_TRACE_PPC
#  include <trace/tracemanager.h>

/** Emit a basic-block boundary event. */
#  define TRACE_EMIT_BB(start_pc, end_pc, next_pc, ef) \
     TraceManager::get_instance()->emit_bb((start_pc), (end_pc), (next_pc), (ef))

/** Emit a per-instruction event (only active when insn tracing is on). */
#  define TRACE_EMIT_INSN(pc, opcode) \
     TraceManager::get_instance()->emit_insn((pc), (opcode))

#else  /* !DPPC_TRACE_PPC — zero overhead */

#  define TRACE_EMIT_BB(start_pc, end_pc, next_pc, ef)  do {} while (0)
#  define TRACE_EMIT_INSN(pc, opcode)                    do {} while (0)

#endif /* DPPC_TRACE_PPC */
