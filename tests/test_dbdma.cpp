/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

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
 * @file DBDMA channel functional tests.
 *
 * Tests exercise real command processing (NOP, STOP, STORE_QUAD, LOAD_QUAD,
 * OUTPUT_MORE, INPUT_MORE) and control logic (branching, interrupt
 * generation, wait stalling, descriptor write-back).  No tautological
 * register round-trips; every assertion verifies observable side-effects
 * produced by the DBDMA state machine.
 *
 * RAM layout used by the tests (all within a 4 KiB backing buffer):
 *   0x0000 – 0x00FF  DMA command descriptors (up to 16 × 16 B)
 *   0x0100 – 0x01FF  Source data for OUTPUT tests
 *   0x0200 – 0x02FF  Destination buffer for INPUT tests
 *   0x0300 – 0x03FF  Scratch area (STORE_QUAD target, LOAD_QUAD source)
 */

#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/common/dbdma.h>
#include <devices/common/dmacore.h>
#include <devices/common/hwinterrupt.h>
#include <devices/memctrl/memctrlbase.h>
#include <endianswap.h>
#include <memaccess.h>

#include <cstdint>
#include <cstring>
#include <iostream>

using std::cerr;
using std::cout;
using std::endl;

// ---------------------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------------------
static int tests_run    = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do {         \
    tests_run++;                             \
    if (!(cond)) {                           \
        cerr << "FAIL: " << msg             \
             << " (" << __FILE__            \
             << ":" << __LINE__ << ")"      \
             << endl;                        \
        tests_failed++;                      \
    }                                        \
} while (0)

// ---------------------------------------------------------------------------
// Global test fixtures
// ---------------------------------------------------------------------------
static constexpr uint32_t RAM_BASE = 0x00000000;
static constexpr uint32_t RAM_SIZE = 4096;

static uint8_t       g_ram[RAM_SIZE];
static MemCtrlBase  *g_mem_ctrl = nullptr;
static uint64_t      g_fake_time_ns = 0;

// Offsets within g_ram
static constexpr uint32_t CMD_BASE  = 0x0000; // DMA descriptors
static constexpr uint32_t SRC_BASE  = 0x0100; // OUTPUT source data
static constexpr uint32_t DST_BASE  = 0x0200; // INPUT  destination
static constexpr uint32_t SCR_BASE  = 0x0300; // scratch / QUAD target

// ---------------------------------------------------------------------------
// Helpers: write/read DMA channel registers in PPC bus (big-endian) format.
//
// DMAChannel::reg_write/reg_read swap bytes to/from PPC bus order.
// These helpers transparently cancel that swap so callers work with
// native host values (as visible in the channel's internal state).
// ---------------------------------------------------------------------------
static inline void dma_write(DMAChannel &ch, uint32_t reg, uint32_t val) {
    ch.reg_write(reg, BYTESWAP_32(val), 4);
}

static inline uint32_t dma_read(DMAChannel &ch, uint32_t reg) {
    return BYTESWAP_32(ch.reg_read(reg, 4));
}

// Write CH_CTRL: mask selects which status bits are updated, data is the new value.
static inline void dma_ctrl(DMAChannel &ch, uint16_t mask, uint16_t data) {
    dma_write(ch, DMAReg::CH_CTRL, ((uint32_t)mask << 16) | data);
}

// Start the DMA channel (set RUN=1, which also asserts ACTIVE).
static inline void dma_start(DMAChannel &ch) {
    dma_ctrl(ch, CH_STAT_RUN, CH_STAT_RUN);
}

// Stop the DMA channel (clear RUN=1).
static inline void dma_stop(DMAChannel &ch) {
    dma_ctrl(ch, CH_STAT_RUN, 0);
}

// Set cmd_ptr to a physical address within g_ram.
static inline void dma_set_cmd_ptr(DMAChannel &ch, uint32_t addr) {
    dma_write(ch, DMAReg::CMD_PTR_LO, addr);
}

// ---------------------------------------------------------------------------
// DMA command descriptor builder.
//
// The DMACmd struct fields use little-endian byte order (per DBDMA spec).
// On a little-endian (x86-64) host the struct layout matches memory
// directly, so we can write to g_ram via pointer casts.
// ---------------------------------------------------------------------------
static void write_cmd(uint32_t offset, uint8_t cmd, uint8_t cmd_bits,
                      uint16_t req_count, uint32_t address, uint32_t cmd_arg)
{
    DMACmd *d = reinterpret_cast<DMACmd *>(&g_ram[offset]);
    d->req_count = req_count;
    d->cmd_bits  = cmd_bits;
    d->cmd_key   = static_cast<uint8_t>(cmd << 4);   // key = 0, cmd in high nibble
    d->address   = address;
    d->cmd_arg   = cmd_arg;
    d->res_count = 0xFFFF; // sentinel – should be overwritten on completion
    d->xfer_stat = 0xFFFF; // sentinel
}

// Variant for STORE_QUAD / LOAD_QUAD which require key = 6.
static void write_quad_cmd(uint32_t offset, uint8_t cmd, uint8_t cmd_bits,
                           uint16_t req_count, uint32_t address, uint32_t cmd_arg)
{
    DMACmd *d = reinterpret_cast<DMACmd *>(&g_ram[offset]);
    d->req_count = req_count;
    d->cmd_bits  = cmd_bits;
    d->cmd_key   = static_cast<uint8_t>((cmd << 4) | 6); // key = 6 required for QUAD
    d->address   = address;
    d->cmd_arg   = cmd_arg;
    d->res_count = 0xFFFF;
    d->xfer_stat = 0xFFFF;
}

// ---------------------------------------------------------------------------
// Mock interrupt controller
// ---------------------------------------------------------------------------
class MockIntCtrl : public InterruptCtrl {
public:
    uint64_t register_dev_int(IntSrc) override { return 0; }
    uint64_t register_dma_int(IntSrc) override { return 0; }
    void ack_int(uint64_t, uint8_t) override {}
    void ack_dma_int(uint64_t, uint8_t irq_line_state) override {
        if (irq_line_state) dma_irq_count++;
    }

    int dma_irq_count = 0;
};

// ---------------------------------------------------------------------------
// Per-test channel factory: fresh DMAChannel + clean RAM each time.
// ---------------------------------------------------------------------------
static DMAChannel make_channel(const char *name = "test") {
    memset(g_ram, 0, RAM_SIZE);
    return DMAChannel(name);
}

// ---------------------------------------------------------------------------
// 1. CH_CTRL always reads as zero (DBDMA spec section 5.5.1, table 74)
// ---------------------------------------------------------------------------
static void test_ch_ctrl_reads_zero() {
    cout << "  test_ch_ctrl_reads_zero..." << endl;

    DMAChannel ch = make_channel();

    // CH_CTRL must return 0 regardless of what was written
    ch.reg_write(DMAReg::CH_CTRL, 0xFFFFFFFFU, 4);
    uint32_t val = dma_read(ch, DMAReg::CH_CTRL);
    TEST_ASSERT(val == 0, "CH_CTRL should always read as 0");
}

// ---------------------------------------------------------------------------
// 2. STOP command clears ACTIVE and leaves the channel idle
// ---------------------------------------------------------------------------
static void test_stop_halts_channel() {
    cout << "  test_stop_halts_channel..." << endl;

    DMAChannel ch = make_channel();

    write_cmd(CMD_BASE, DBDMA_Cmd::STOP, 0, 0, 0, 0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    uint32_t stat = dma_read(ch, DMAReg::CH_STAT);
    TEST_ASSERT(!(stat & CH_STAT_ACTIVE), "ACTIVE should be cleared after STOP");
    TEST_ASSERT(!(stat & CH_STAT_DEAD),   "DEAD should not be set after STOP");
}

// ---------------------------------------------------------------------------
// 3. NOP advances cmd_ptr by one descriptor (16 bytes), then STOP halts
// ---------------------------------------------------------------------------
static void test_nop_stop() {
    cout << "  test_nop_stop..." << endl;

    DMAChannel ch = make_channel();

    write_cmd(CMD_BASE,       DBDMA_Cmd::NOP,  0, 0, 0, 0);
    write_cmd(CMD_BASE + 16,  DBDMA_Cmd::STOP, 0, 0, 0, 0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // Channel should have processed both commands and now be idle
    uint32_t stat = dma_read(ch, DMAReg::CH_STAT);
    TEST_ASSERT(!(stat & CH_STAT_ACTIVE), "ACTIVE cleared after NOP+STOP sequence");
    TEST_ASSERT(!(stat & CH_STAT_DEAD),   "DEAD not set after clean NOP+STOP");
}

// ---------------------------------------------------------------------------
// 4. STORE_QUAD writes the cmd_arg value to the specified RAM address
// ---------------------------------------------------------------------------
static void test_store_quad_writes_ram() {
    cout << "  test_store_quad_writes_ram..." << endl;

    DMAChannel ch = make_channel();

    static constexpr uint32_t TARGET  = SCR_BASE;
    static constexpr uint32_t PAYLOAD = 0xDEADBEEFU;

    // Write 0 to the target first so we can confirm it changed
    WRITE_DWORD_LE_A(&g_ram[TARGET], 0);

    // STORE_QUAD: req_count=4 → xfer_size=4, key must be 6
    write_quad_cmd(CMD_BASE,      DBDMA_Cmd::STORE_QUAD, 0, 4, TARGET, PAYLOAD);
    write_cmd     (CMD_BASE + 16, DBDMA_Cmd::STOP,       0, 0, 0,      0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    uint32_t stored = READ_DWORD_LE_A(&g_ram[TARGET]);
    TEST_ASSERT(stored == PAYLOAD, "STORE_QUAD must write cmd_arg to target address");

    uint32_t stat = dma_read(ch, DMAReg::CH_STAT);
    TEST_ASSERT(!(stat & CH_STAT_ACTIVE), "Channel idle after STORE_QUAD+STOP");
}

// ---------------------------------------------------------------------------
// 5. LOAD_QUAD reads a RAM word and stores it back into cmd_arg in the
//    descriptor (the mmu_map_dma_mem region is writable because g_ram is RAM)
// ---------------------------------------------------------------------------
static void test_load_quad_reads_ram() {
    cout << "  test_load_quad_reads_ram..." << endl;

    DMAChannel ch = make_channel();

    static constexpr uint32_t SRC    = SCR_BASE;
    static constexpr uint32_t EXPECT = 0xCAFEBABEU;

    WRITE_DWORD_LE_A(&g_ram[SRC], EXPECT);

    // LOAD_QUAD: req_count=4, key=6, address=source
    write_quad_cmd(CMD_BASE,      DBDMA_Cmd::LOAD_QUAD, 0, 4, SRC, 0xFFFFFFFFU);
    write_cmd     (CMD_BASE + 16, DBDMA_Cmd::STOP,      0, 0, 0,   0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // cmd_arg field of the LOAD_QUAD descriptor must now hold the value read
    DMACmd *d = reinterpret_cast<DMACmd *>(&g_ram[CMD_BASE]);
    uint32_t loaded = READ_DWORD_LE_A(&d->cmd_arg);
    TEST_ASSERT(loaded == EXPECT, "LOAD_QUAD must update descriptor cmd_arg with the read value");
}

// ---------------------------------------------------------------------------
// 6. Branch-always (b=11) must skip the intervening command
// ---------------------------------------------------------------------------
static void test_branch_always() {
    cout << "  test_branch_always..." << endl;

    DMAChannel ch = make_channel();

    // NOP at CMD_BASE with branch=always (cmd_bits[3:2] = 0b11 = 0x0C)
    // cmd_arg holds the branch target address
    static constexpr uint32_t SKIP_CMD  = CMD_BASE + 16;
    static constexpr uint32_t STOP_CMD  = CMD_BASE + 32;

    // Place a unique sentinel at the SKIP_CMD descriptor address field so we
    // can tell whether it was executed (STORE_QUAD would write the field).
    uint32_t sentinel = 0xBADC0FFEU;
    WRITE_DWORD_LE_A(&g_ram[SCR_BASE], sentinel);

    // If branching works, this STORE_QUAD should never run.
    write_quad_cmd(SKIP_CMD, DBDMA_Cmd::STORE_QUAD, 0, 4, SCR_BASE, 0x12345678U);
    write_cmd     (STOP_CMD, DBDMA_Cmd::STOP,        0, 0, 0,        0);

    // NOP with b=11 (branch always), cmd_arg = branch target
    DMACmd *nop = reinterpret_cast<DMACmd *>(&g_ram[CMD_BASE]);
    nop->req_count = 0;
    nop->cmd_bits  = 0x0C; // branch = always
    nop->cmd_key   = (DBDMA_Cmd::NOP << 4);
    nop->address   = 0;
    nop->cmd_arg   = STOP_CMD; // branch target
    nop->res_count = 0;
    nop->xfer_stat = 0;

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // The STORE_QUAD was skipped; sentinel must be unchanged
    uint32_t after = READ_DWORD_LE_A(&g_ram[SCR_BASE]);
    TEST_ASSERT(after == sentinel, "Branch-always must skip the STORE_QUAD command");

    uint32_t stat = dma_read(ch, DMAReg::CH_STAT);
    TEST_ASSERT(!(stat & CH_STAT_ACTIVE), "Channel idle after branch-always + STOP");
}

// ---------------------------------------------------------------------------
// 7. OUTPUT_MORE: pull_data returns the bytes described by the command
// ---------------------------------------------------------------------------
static void test_output_pull_data() {
    cout << "  test_output_pull_data..." << endl;

    DMAChannel ch = make_channel();

    // Seed source data
    static constexpr uint32_t DATA_LEN = 8;
    for (uint32_t i = 0; i < DATA_LEN; i++)
        g_ram[SRC_BASE + i] = static_cast<uint8_t>(0xA0 + i);

    // OUTPUT_MORE pointing at g_ram[SRC_BASE]
    write_cmd(CMD_BASE,      DBDMA_Cmd::OUTPUT_MORE, 0, DATA_LEN, SRC_BASE, 0);
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP,        0, 0,        0,        0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // Channel should now be waiting (cmd_in_progress=true, queue_len=DATA_LEN)
    TEST_ASSERT(ch.is_out_active(), "Channel must be active after OUTPUT_MORE start");

    uint8_t  *p_data   = nullptr;
    uint32_t  avail    = 0;
    DmaPullResult r = ch.pull_data(DATA_LEN, &avail, &p_data);

    TEST_ASSERT(r == DmaPullResult::MoreData,   "pull_data must report MoreData");
    TEST_ASSERT(avail == DATA_LEN,               "pull_data must return all requested bytes");
    TEST_ASSERT(p_data != nullptr,               "pull_data must return a non-null pointer");
    TEST_ASSERT(memcmp(p_data, &g_ram[SRC_BASE], DATA_LEN) == 0,
                "pull_data must return the bytes from the OUTPUT_MORE source address");
}

// ---------------------------------------------------------------------------
// 8. INPUT_MORE: push_data copies bytes into the DMA buffer
// ---------------------------------------------------------------------------
static void test_input_push_data() {
    cout << "  test_input_push_data..." << endl;

    DMAChannel ch = make_channel();

    static constexpr uint32_t DATA_LEN = 8;
    static const uint8_t SRC[DATA_LEN] = {0x11, 0x22, 0x33, 0x44,
                                           0x55, 0x66, 0x77, 0x88};

    // Fill destination with a known pattern so we can detect it being overwritten
    memset(&g_ram[DST_BASE], 0xFF, DATA_LEN);

    // INPUT_MORE pointing at g_ram[DST_BASE]
    write_cmd(CMD_BASE,      DBDMA_Cmd::INPUT_MORE, 0, DATA_LEN, DST_BASE, 0);
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP,       0, 0,        0,        0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    TEST_ASSERT(ch.is_in_active(), "Channel must be active after INPUT_MORE start");

    int rc = ch.push_data(reinterpret_cast<const char *>(SRC), DATA_LEN);
    TEST_ASSERT(rc == 0, "push_data must succeed");
    TEST_ASSERT(memcmp(&g_ram[DST_BASE], SRC, DATA_LEN) == 0,
                "push_data must copy bytes into the INPUT_MORE destination buffer");
}

// ---------------------------------------------------------------------------
// 9. Interrupt always (i=11): a completed NOP must fire a DMA interrupt.
//    This test validates the fix for the finish_cmd/update_irq bug where
//    cmd_ptr was advanced before interrupt bits were sampled, causing the
//    interrupt bits from the NEXT command to be used instead of the
//    completing command's bits.
// ---------------------------------------------------------------------------
static void test_interrupt_always() {
    cout << "  test_interrupt_always..." << endl;

    DMAChannel ch = make_channel();
    MockIntCtrl mock_int;
    ch.register_dma_int(&mock_int, 0);

    // NOP with i=11 (interrupt always, cmd_bits[5:4] = 0b11 = 0x30)
    write_cmd(CMD_BASE,      DBDMA_Cmd::NOP,  0x30, 0, 0, 0);
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP, 0x00, 0, 0, 0); // no interrupt bits

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // Interrupt is posted via TimerManager; fire it now
    TimerManager::get_instance()->process_timers();

    TEST_ASSERT(mock_int.dma_irq_count == 1,
                "NOP with i=always must generate exactly one DMA interrupt");
}

// ---------------------------------------------------------------------------
// 10. Interrupt conditional (i=01, int_select matches): interrupt fires only
//     when the selected status bits match.
// ---------------------------------------------------------------------------
static void test_interrupt_conditional() {
    cout << "  test_interrupt_conditional..." << endl;

    DMAChannel ch = make_channel();
    MockIntCtrl mock_int;
    ch.register_dma_int(&mock_int, 0);

    // i=01 means "interrupt if condition is true"
    // INT_SELECT: mask=0x0001 (bit 0 = S0), compare value=0x0001
    // Set S0 via CH_CTRL so the condition is met
    dma_write(ch, DMAReg::INT_SELECT, (0x0001U << 16) | 0x0001U);
    dma_ctrl(ch, 0x0001, 0x0001); // set status bit S0

    write_cmd(CMD_BASE,      DBDMA_Cmd::NOP,  0x10, 0, 0, 0); // i=01
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP, 0x00, 0, 0, 0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    TimerManager::get_instance()->process_timers();

    TEST_ASSERT(mock_int.dma_irq_count == 1,
                "NOP with i=01 must generate interrupt when condition is true");

    // --- now with the condition NOT met (S0 cleared) ---
    DMAChannel ch2 = make_channel();
    MockIntCtrl mock_int2;
    ch2.register_dma_int(&mock_int2, 0);

    dma_write(ch2, DMAReg::INT_SELECT, (0x0001U << 16) | 0x0001U);
    // S0 not set → condition is false

    write_cmd(CMD_BASE,      DBDMA_Cmd::NOP,  0x10, 0, 0, 0);
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP, 0x00, 0, 0, 0);

    dma_set_cmd_ptr(ch2, CMD_BASE);
    dma_start(ch2);

    TimerManager::get_instance()->process_timers();

    TEST_ASSERT(mock_int2.dma_irq_count == 0,
                "NOP with i=01 must not generate interrupt when condition is false");
}

// ---------------------------------------------------------------------------
// 11. Wait condition: w=01 does NOT stall when the condition evaluates false.
//     (Testing the affirmative stall path requires multi-threading because
//     all public command-driving methods loop until active or cmd_in_progress,
//     so we verify only that a false condition lets the channel proceed.)
// ---------------------------------------------------------------------------
static void test_wait_not_triggered() {
    cout << "  test_wait_not_triggered..." << endl;

    DMAChannel ch = make_channel();

    // WAIT_SELECT: mask = S0 (bit 0), compare value = 1.
    // Condition is "(ch_stat & mask) == compare_value" = "(0 & 1) == 1" = false.
    // w=01 means wait only when condition is TRUE, so no stall here.
    dma_write(ch, DMAReg::WAIT_SELECT, (0x0001U << 16) | 0x0001U);

    write_cmd(CMD_BASE,      DBDMA_Cmd::NOP,  0x01, 0, 0, 0); // w=01
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP, 0x00, 0, 0, 0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    uint32_t stat = dma_read(ch, DMAReg::CH_STAT);
    TEST_ASSERT(!(stat & CH_STAT_ACTIVE),
                "w=01 must not stall when the wait condition evaluates false");
}

// ---------------------------------------------------------------------------
// 12. CMD_PTR_LO must be ignored while the channel is ACTIVE
// ---------------------------------------------------------------------------
static void test_cmd_ptr_locked_while_running() {
    cout << "  test_cmd_ptr_locked_while_running..." << endl;

    DMAChannel ch = make_channel();

    // OUTPUT_MORE keeps the channel active with cmd_in_progress=true so
    // start() exits its loop; the channel then awaits pull_data.
    static constexpr uint32_t DATA_LEN = 4;
    for (uint32_t i = 0; i < DATA_LEN; i++)
        g_ram[SRC_BASE + i] = static_cast<uint8_t>(i);

    write_cmd(CMD_BASE,      DBDMA_Cmd::OUTPUT_MORE, 0, DATA_LEN, SRC_BASE, 0);
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP,        0, 0,        0,        0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // Channel is now ACTIVE (awaiting pull_data); attempt to change cmd_ptr
    TEST_ASSERT(ch.is_out_active(), "Channel must be active before cmd_ptr write test");
    dma_write(ch, DMAReg::CMD_PTR_LO, 0xDEAD0000U);

    uint32_t ptr = dma_read(ch, DMAReg::CMD_PTR_LO);
    TEST_ASSERT(ptr == CMD_BASE,
                "CMD_PTR_LO must be ignored when the channel is active/running");

    // Drain the channel so it shuts down cleanly
    uint8_t *p = nullptr; uint32_t avail = 0;
    ch.pull_data(DATA_LEN, &avail, &p);
    ch.end_pull_data();
}

// ---------------------------------------------------------------------------
// 13. xfer_stat written back into the descriptor after command completion
// ---------------------------------------------------------------------------
static void test_xfer_stat_updated() {
    cout << "  test_xfer_stat_updated..." << endl;

    DMAChannel ch = make_channel();

    write_cmd(CMD_BASE,      DBDMA_Cmd::NOP,  0, 0, 0, 0);
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP, 0, 0, 0, 0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // After NOP completes, descriptor[14..15] must hold (ch_stat | ACTIVE)
    // at the time of completion.  RUN + ACTIVE were set at start.
    DMACmd *d = reinterpret_cast<DMACmd *>(&g_ram[CMD_BASE]);
    uint16_t xfer_stat = READ_WORD_LE_A(&d->xfer_stat);

    TEST_ASSERT(xfer_stat != 0xFFFF,
                "xfer_stat sentinel must have been overwritten");
    TEST_ASSERT(xfer_stat & CH_STAT_ACTIVE,
                "xfer_stat must have ACTIVE set at the time the NOP completed");
}

// ---------------------------------------------------------------------------
// 14. res_count written back after a complete transfer (should become 0)
// ---------------------------------------------------------------------------
static void test_res_count_updated() {
    cout << "  test_res_count_updated..." << endl;

    DMAChannel ch = make_channel();

    static constexpr uint32_t DATA_LEN = 4;
    for (uint32_t i = 0; i < DATA_LEN; i++)
        g_ram[SRC_BASE + i] = static_cast<uint8_t>(i);

    write_cmd(CMD_BASE,      DBDMA_Cmd::OUTPUT_MORE, 0, DATA_LEN, SRC_BASE, 0);
    write_cmd(CMD_BASE + 16, DBDMA_Cmd::STOP,        0, 0,        0,        0);

    dma_set_cmd_ptr(ch, CMD_BASE);
    dma_start(ch);

    // Consume all data so finish_cmd runs and writes back res_count
    uint8_t *p = nullptr; uint32_t avail = 0;
    ch.pull_data(DATA_LEN, &avail, &p);
    // After exhausting the buffer, advance the DBDMA program
    ch.end_pull_data();

    DMACmd *d = reinterpret_cast<DMACmd *>(&g_ram[CMD_BASE]);
    uint16_t res = READ_WORD_LE_A(&d->res_count);
    TEST_ASSERT(res == 0,
                "res_count must be 0 after all bytes have been transferred");
}

// ---------------------------------------------------------------------------
// Setup: one-time initialisation of memory controller and TimerManager
// ---------------------------------------------------------------------------
static void setup() {
    // Memory controller backed by g_ram
    g_mem_ctrl = new MemCtrlBase();
    g_mem_ctrl->add_ram_region(RAM_BASE, RAM_SIZE, g_ram);
    mem_ctrl_instance = g_mem_ctrl;

    // TimerManager needs time/notify callbacks
    auto *tm = TimerManager::get_instance();
    tm->set_time_now_cb([]() -> uint64_t { return g_fake_time_ns; });
    tm->set_notify_changes_cb([]() {});
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    setup();

    cout << "Running DBDMA tests..." << endl;

    test_ch_ctrl_reads_zero();
    test_stop_halts_channel();
    test_nop_stop();
    test_store_quad_writes_ram();
    test_load_quad_reads_ram();
    test_branch_always();
    test_output_pull_data();
    test_input_push_data();
    test_interrupt_always();
    test_interrupt_conditional();
    test_wait_not_triggered();
    test_cmd_ptr_locked_while_running();
    test_xfer_stat_updated();
    test_res_count_updated();

    cout << tests_run    << " tests run, "
         << tests_failed << " failed." << endl;

    return tests_failed ? 1 : 0;
}
