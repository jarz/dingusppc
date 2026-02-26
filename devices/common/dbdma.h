/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

(You may also contact divingkxt or powermax2286 on Discord)

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

/** @file Descriptor-based direct memory access emulation.

    Official documentation can be found in the fifth chapter of the book
    "Macintosh Technology in the Common Hardware Reference Platform"
    by Apple Computer, Inc.

    Threading model:
    ----------------
    DMAChannel is accessed from two threads:
      - Main thread: reg_read, reg_write, set_stat, set_callbacks,
        set_data_callbacks, push_data, end_push_data, end_pull_data.
      - Audio thread (CoreAudio via cubeb): is_out_active, pull_data,
        get_pull_data_remaining.

    is_ready and xfer_retry are called transitively from both threads
    via interpret_cmd while the DMA mutex is already held.

    Public methods are protected by `mtx` (a std::recursive_mutex).
    It is recursive because device callbacks invoked during DMA execution
    (start_cb → Sc53C94::dma_start → real_dma_xfer_in → push_data) can
    re-enter locked public methods on the same channel from the main thread.

    Audio-thread methods (is_out_active, is_in_active) use try_lock to avoid
    priority inversion on the RT thread — if the lock is contended they return
    false rather than blocking.  try_lock on a recursive_mutex held by a
    *different* thread still fails, so RT try-lock fallback is unaffected.

    pull_data uses a blocking lock so the cubeb callback always gets data or
    a legitimate NoMoreData result, and never stops the stream due to spurious
    lock contention.  IRQ notifications are deferred until after mtx is released
    (via defer_irq_mode) to avoid calling TimerManager while holding this lock.

    Lock ordering: mtx → TimerManager::recursive_mutex.  Timer callbacks
    MUST NOT acquire DMAChannel::mtx to avoid deadlock.

    The data pointer returned by pull_data points into guest physical RAM
    (via mmu_map_dma_mem).  Guest RAM mappings are stable during emulation,
    so the pointer remains valid after the lock is released.  mmu_map_dma_mem
    is also called from the audio thread under the lock; this is safe because
    DMA buffer mappings are established at init time and never modified.
 */

#ifndef DB_DMA_H
#define DB_DMA_H

#include <devices/common/dmacore.h>

#include <cinttypes>
#include <functional>
#include <mutex>

class InterruptCtrl;

/** DBDMA Channel registers offsets */
enum DMAReg : uint32_t {
    CH_CTRL         = 0,
    CH_STAT         = 4,
    CMD_PTR_HI      = 8, // not implemented
    CMD_PTR_LO      = 12,
    INT_SELECT      = 16,
    BRANCH_SELECT   = 20,
    WAIT_SELECT     = 24,
//  TANSFER_MODES   = 28,
//  DATA_2_PTR_HI   = 32, // not implemented
//  DATA_2_PTR_LO   = 36,
//  RESERVED_1      = 40,
//  ADDRESS_HI      = 44,
//  RESERVED_2_0    = 48,
//  RESERVED_2_1    = 52,
//  RESERVED_2_2    = 56,
//  RESERVED_2_3    = 60,
//  UNIMPLEMENTED   = 64,
//  UNDEFINED       = 128,
};

/** Channel Status bits (DBDMA spec, 5.5.3) */
enum : uint16_t {
    CH_STAT_S0     = 0x0001, // general purpose status and control
    CH_STAT_S1     = 0x0002, // general purpose status and control
    CH_STAT_S2     = 0x0004, // general purpose status and control
    CH_STAT_S3     = 0x0008, // general purpose status and control
    CH_STAT_S4     = 0x0010, // general purpose status and control
    CH_STAT_S5     = 0x0020, // general purpose status and control
    CH_STAT_S6     = 0x0040, // general purpose status and control
    CH_STAT_S7     = 0x0080, // general purpose status and control
    CH_STAT_BT     = 0x0100, // hardware status bit
    CH_STAT_ACTIVE = 0x0400, // hardware status bit
    CH_STAT_DEAD   = 0x0800, // hardware status bit
    CH_STAT_WAKE   = 0x1000, // command bit set by software and cleared by hardware once the action has been performed
    CH_STAT_FLUSH  = 0x2000, // command bit set by software and cleared by hardware once the action has been performed
    CH_STAT_PAUSE  = 0x4000, // control bit set and cleared by software
    CH_STAT_RUN    = 0x8000  // control bit set and cleared by software
};

/** DBDMA command (DBDMA spec, 5.6.1) - all fields are little-endian! */
typedef struct DMACmd {
    uint16_t    req_count;
    uint8_t     cmd_bits; // wait: & 3, branch: & 0xC, interrupt: & 0x30, reserved: & 0xc0
    uint8_t     cmd_key; // key: & 7, reserved: & 8, cmd: >> 4
    uint32_t    address;
    uint32_t    cmd_arg;
    uint16_t    res_count;
    uint16_t    xfer_stat;
} DMACmd;

namespace DBDMA_Cmd {
    enum : uint8_t {
        OUTPUT_MORE = 0,
        OUTPUT_LAST = 1,
        INPUT_MORE  = 2,
        INPUT_LAST  = 3,
        STORE_QUAD  = 4,
        LOAD_QUAD   = 5,
        NOP         = 6,
        STOP        = 7
    };
}

typedef std::function<void(void)> DbdmaCallback;

class DMAChannel : public DmaBidirChannel, public DmaChannel {
public:
    DMAChannel(std::string name) : DmaBidirChannel(name) {}
    ~DMAChannel() = default;

    void set_callbacks(DbdmaCallback start_cb, DbdmaCallback stop_cb);
    void set_data_callbacks(DbdmaCallback in_cb, DbdmaCallback out_cb, DbdmaCallback flush_cb);
    uint32_t reg_read(uint32_t offset, int size);
    void reg_write(uint32_t offset, uint32_t value, int size);
    void set_stat(uint8_t new_stat) { std::lock_guard<std::recursive_mutex> lk(mtx); this->ch_stat = (this->ch_stat & 0xff00) | new_stat; }

    bool            is_out_active() override;
    bool            is_in_active() override;
    DmaPullResult   pull_data(uint32_t req_len, uint32_t *avail_len, uint8_t **p_data) override;
    int             push_data(const char* src_ptr, int len) override;
    int             get_pull_data_remaining() override { std::lock_guard<std::recursive_mutex> lk(mtx); return this->queue_len; }
    int             get_push_data_remaining() override { std::lock_guard<std::recursive_mutex> lk(mtx); return this->queue_len; }
    void            end_pull_data() override;
    void            end_push_data() override;

    bool            is_ready() override;
    void            xfer_retry() override;

    // Returns a reference to the channel mutex. Callers that access DMA state
    // from a non-main thread (e.g. cubeb audio callback) MUST hold this lock.
    std::recursive_mutex& get_mutex() { return this->mtx; }

    void register_dma_int(InterruptCtrl* int_ctrl_obj, uint64_t irq_id) {
        this->int_ctrl = int_ctrl_obj;
        this->irq_id   = irq_id;
    }

protected:
    DMACmd* fetch_cmd(uint32_t cmd_addr, DMACmd* p_cmd, bool *is_writable);
    uint8_t interpret_cmd(void);
    void finish_cmd();
    void xfer_quad(const DMACmd *cmd_desc, DMACmd *cmd_host);
    void update_irq();
    void update_irq(uint8_t cmd_bits);
    void xfer_from_device();
    void xfer_to_device();

    void start(void);
    void resume(void);
    void abort(void);
    void pause(void);

private:
    std::function<void(void)> start_cb = nullptr; // DMA channel start callback
    std::function<void(void)> stop_cb  = nullptr; // DMA channel stop callback
    std::function<void(void)> in_cb    = nullptr; // DMA channel in callback
    std::function<void(void)> out_cb   = nullptr; // DMA channel out callback
    std::function<void(void)> flush_cb = nullptr; // DMA channel flush callback

    uint16_t ch_stat        = 0;
    uint32_t cmd_ptr        = 0;
    uint32_t queue_len      = 0;
    uint8_t* queue_data     = 0;
    uint32_t res_count      = 0;
    uint32_t int_select     = 0;
    uint32_t branch_select  = 0;
    uint32_t wait_select    = 0;

    bool     cmd_in_progress = false;
    uint8_t  cur_cmd;

    // When true, update_irq() records pending IRQs instead of calling
    // TimerManager.  Used by pull_data() to defer IRQ posting until
    // after the DMA mutex is released (avoids RT priority inversion).
    bool     defer_irq_mode = false;
    bool     deferred_irq_pending = false;

    // Interrupt related stuff
    InterruptCtrl* int_ctrl = nullptr;
    uint64_t       irq_id   = 0;

    // Protects DMA channel state accessed from the cubeb audio thread.
    // Must be recursive: device callbacks called during DMA execution can
    // re-enter public methods (push_data etc.) on the same channel from the main thread.
    std::recursive_mutex mtx;
};

#endif // DB_DMA_H
