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

/** @file tracemanager.cpp
 *
 * TraceManager implementation: ring-buffer + file sink for PPC traces.
 */

#include <trace/tracemanager.h>

#include <chrono>
#include <cstring>
#include <cstdlib>

/* Detect host endianness at compile time. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static constexpr uint8_t HOST_ENDIAN = 0;
#else
static constexpr uint8_t HOST_ENDIAN = 1;
#endif

TraceManager* TraceManager::instance_ = nullptr;

TraceManager* TraceManager::get_instance()
{
    if (!instance_) {
        instance_ = new TraceManager();
    }
    return instance_;
}

TraceManager::TraceManager()
    : ring_size_(TRACE_RING_DEFAULT_SIZE)
{
    ring_.resize(ring_size_);
}

TraceManager::~TraceManager()
{
    close();
}

/* ------------------------------------------------------------------ */
/*  File sink helpers                                                   */
/* ------------------------------------------------------------------ */

bool TraceManager::write_header(FILE* f)
{
    TraceFileHeader hdr{};
    hdr.magic[0] = 'D';
    hdr.magic[1] = 'P';
    hdr.magic[2] = 'P';
    hdr.magic[3] = 'C';
    hdr.version  = 1;
    hdr.endian   = HOST_ENDIAN;
    hdr.rec_size = static_cast<uint8_t>(sizeof(TraceRecord));
    return fwrite(&hdr, sizeof(hdr), 1, f) == 1;
}

bool TraceManager::open_file(const std::string& path)
{
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
    file_ = fopen(path.c_str(), "wb");
    if (!file_) return false;
    setvbuf(file_, file_buf_, _IOFBF, FILE_BUF_SIZE);
    return write_header(file_);
}

void TraceManager::close()
{
    if (file_) {
        fflush(file_);
        fclose(file_);
        file_ = nullptr;
    }
}

/* ------------------------------------------------------------------ */
/*  Ring buffer                                                         */
/* ------------------------------------------------------------------ */

void TraceManager::push_ring(const TraceRecord& rec)
{
    ring_[ring_head_] = rec;
    ring_head_ = (ring_head_ + 1) % ring_size_;
}

/* ------------------------------------------------------------------ */
/*  File write                                                          */
/* ------------------------------------------------------------------ */

void TraceManager::write_file(const TraceRecord& rec)
{
    if (file_) {
        fwrite(&rec, sizeof(rec), 1, file_);
    }
}

/* ------------------------------------------------------------------ */
/*  Emission (hot path)                                                 */
/* ------------------------------------------------------------------ */

void TraceManager::emit_bb(uint32_t start_pc, uint32_t end_pc,
                           uint32_t next_pc, unsigned ef)
{
    if (!trace_bb_) return;

    /* PC range filter */
    if (start_pc < pc_start_ || start_pc > pc_end_) return;

    /* Build reason flags from exec_flags bits. */
    uint8_t flags = 0;
    if (ef & 0x1) flags |= TF_BRANCH;
    if (ef & 0x2) flags |= TF_EXCEPTION;
    if (ef & 0x4) flags |= TF_RFI;

    /* Timestamp: use steady_clock nanoseconds.
     * We intentionally avoid calling get_virt_time_ns() here to keep
     * the hot path free of additional indirections when tracing is on. */
    uint64_t ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    TraceRecord rec{};
    rec.type      = static_cast<uint8_t>(TraceRecType::BB);
    rec.flags     = flags;
    rec.seq       = seq_.fetch_add(1, std::memory_order_relaxed);
    rec.timestamp = ts;
    rec.start_pc  = start_pc;
    rec.end_pc    = end_pc;
    rec.next_pc   = next_pc;
    rec.reason    = ef;

    push_ring(rec);
    write_file(rec);
}

void TraceManager::emit_insn(uint32_t pc, uint32_t opcode)
{
    if (!trace_insn_) return;

    /* PC range filter */
    if (pc < pc_start_ || pc > pc_end_) return;

    /* Sampling */
    if (++insn_ctr_ < sample_n_) return;
    insn_ctr_ = 0;

    uint64_t ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    TraceRecord rec{};
    rec.type      = static_cast<uint8_t>(TraceRecType::INSN);
    rec.seq       = seq_.fetch_add(1, std::memory_order_relaxed);
    rec.timestamp = ts;
    rec.start_pc  = pc;
    rec.end_pc    = opcode;  /* re-use end_pc slot for opcode word */
    rec.next_pc   = 0;
    rec.reason    = 0;

    push_ring(rec);
    write_file(rec);
}

/* ------------------------------------------------------------------ */
/*  Ring buffer dump                                                    */
/* ------------------------------------------------------------------ */

bool TraceManager::dump_ring(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    static constexpr size_t DUMP_BUF = 64 * 1024;
    char buf[DUMP_BUF];
    setvbuf(f, buf, _IOFBF, DUMP_BUF);

    if (!write_header(f)) {
        fclose(f);
        return false;
    }

    /* The ring is a circular buffer; emit oldest entries first.
     * ring_head_ points to the NEXT write position (i.e., the oldest
     * record if the buffer is full, or index 0 for a partially-filled
     * buffer that has wrapped). We always iterate the full ring_size_
     * slots starting at ring_head_; slots that were never written contain
     * zero-valued records whose seq==0 will naturally sort first. */
    bool ok = true;
    for (size_t i = 0; i < ring_size_ && ok; ++i) {
        size_t idx = (ring_head_ + i) % ring_size_;
        const TraceRecord& rec = ring_[idx];
        if (rec.type == 0) continue;  /* skip empty/unwritten slots */
        ok = (fwrite(&rec, sizeof(rec), 1, f) == 1);
    }

    fflush(f);
    fclose(f);
    return ok;
}
