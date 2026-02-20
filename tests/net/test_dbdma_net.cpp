/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Integration tests: DBDMA descriptor chain walking + BigMac Ethernet,
exercised without ROMs by hooking mmu_map_dma_mem.
*/

#include <devices/common/dbdma.h>
#include <devices/common/hwinterrupt.h>
#include <devices/ethernet/bigmac.h>
#include <devices/ethernet/mace.h>
#include <core/timermanager.h>
#include <endianswap.h>
#include <memaccess.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

// ---------------------------------------------------------------------------
// Flat 64 KB test memory that stands in for RAM (defined in test_mmu_stub.cpp)
// ---------------------------------------------------------------------------
extern std::array<uint8_t, 65536> g_test_mem;

// ---------------------------------------------------------------------------
// Minimal InterruptCtrl stub – records DMA interrupt assertions
// ---------------------------------------------------------------------------
class StubInterruptCtrl : public InterruptCtrl {
public:
    int  dma_ack_count = 0;

    uint64_t register_dev_int(IntSrc) override { return 0; }
    uint64_t register_dma_int(IntSrc) override { return 1; }
    void ack_int(uint64_t, uint8_t)     override {}
    void ack_dma_int(uint64_t, uint8_t) override { ++dma_ack_count; }
};

extern void init_timer_manager();

static void drain_timers() {
    TimerManager::get_instance()->process_timers();
}

// ---------------------------------------------------------------------------
// Descriptor helpers (all fields little-endian per DBDMA spec)
// ---------------------------------------------------------------------------
static void write_descriptor(uint32_t offset, uint8_t cmd, uint16_t req_count,
                             uint32_t address, uint8_t cmd_bits = 0,
                             uint32_t cmd_arg = 0)
{
    DMACmd d{};
    WRITE_WORD_LE_A(&d.req_count, req_count);
    d.cmd_bits = cmd_bits;
    d.cmd_key  = (cmd << 4);
    WRITE_DWORD_LE_A(&d.address, address);
    WRITE_DWORD_LE_A(&d.cmd_arg, cmd_arg);
    d.res_count = 0;
    d.xfer_stat = 0;
    std::memcpy(&g_test_mem[offset], &d, sizeof(d));
}

// ---------------------------------------------------------------------------
// Test 1: DBDMA OUTPUT_LAST feeds BigMac TX → loopback → RX fetch
//
// Sets up a two-descriptor TX program (OUTPUT_LAST + STOP), points the
// data buffer at a valid Ethernet frame in test memory, runs the channel,
// and verifies the frame arrived back through loopback.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_tx_loopback() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();


    // Create BigMac with loopback
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();
    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level) { if (level) irq_seen = true; });
    bmac.write(BigMacReg::EVENT_MASK, 0x0000); // unmask all

    // Build an Ethernet frame at phys 0x2000
    const uint8_t dst[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF}; // matches SROM MAC
    const size_t frame_len = 60;
    uint8_t* frame = &g_test_mem[0x2000];
    std::memcpy(frame, dst, 6);
    frame[6] = 0x00; frame[7] = 0x11; frame[8] = 0x22;
    frame[9] = 0x33; frame[10]= 0x44; frame[11]= 0x55;
    frame[12]= 0x08; frame[13]= 0x00;
    for (size_t i = 14; i < frame_len; ++i)
        frame[i] = static_cast<uint8_t>(i);

    // Build DBDMA program at phys 0x1000
    //   desc[0] @ 0x1000: OUTPUT_LAST, 60 bytes, addr=0x2000, interrupt=always
    //   desc[1] @ 0x1010: STOP
    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, frame_len, 0x2000,
                     0x30 /* interrupt=always */);
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    // Create TX DMA channel wired like MacIoTwo does it
    DMAChannel tx_dma("TestBmacTx");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                bmac.tx_from_host(p_data, avail_len);
            tx_dma.end_pull_data();
        },
        nullptr);

    // Program and start the channel
    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4); // set RUN

    // Loopback echoes TX→RX, so poll to dequeue
    bmac.poll_backend();

    // Verify the frame came back
    std::vector<uint8_t> rx_frame;
    if (!bmac.fetch_next_rx_frame(rx_frame)) {
        std::cerr << "FAIL: No frame received after DBDMA TX loopback" << std::endl;
        ++failures;
    } else if (rx_frame.size() != frame_len) {
        std::cerr << "FAIL: RX frame size " << rx_frame.size()
                  << " != expected " << frame_len << std::endl;
        ++failures;
    } else if (std::memcmp(rx_frame.data(), frame, frame_len) != 0) {
        std::cerr << "FAIL: RX frame data mismatch" << std::endl;
        ++failures;
    }

    // Channel should have processed STOP and become idle
    uint16_t res_count = READ_WORD_LE_A(&g_test_mem[0x1000 + 12]);
    if (res_count != 0) {
        std::cerr << "FAIL: Descriptor res_count = " << res_count
                  << ", expected 0 (all bytes transferred)" << std::endl;
        ++failures;
    }

    uint16_t xfer_stat = READ_WORD_LE_A(&g_test_mem[0x1000 + 14]);
    if (!(xfer_stat & CH_STAT_ACTIVE)) {
        std::cerr << "FAIL: Descriptor xfer_stat missing ACTIVE bit" << std::endl;
        ++failures;
    }

    // Channel should have processed STOP and become idle
    uint32_t ch_stat = BYTESWAP_32(tx_dma.reg_read(DMAReg::CH_STAT, 4));
    if (ch_stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Channel still ACTIVE after STOP" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 2: DBDMA INPUT_LAST receives a frame from BigMac RX queue
//
// Injects a frame into BigMac's RX queue, then runs a DBDMA INPUT_LAST
// program that pulls the frame into test memory.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_rx_to_memory() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();


    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    // Build a frame to inject
    const uint8_t dst[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    const size_t frame_len = 64;
    std::vector<uint8_t> src_frame(frame_len, 0);
    std::memcpy(src_frame.data(), dst, 6);
    src_frame[6] = 0xAA; src_frame[7] = 0xBB;
    for (size_t i = 14; i < frame_len; ++i)
        src_frame[i] = static_cast<uint8_t>(0x80 | (i & 0x7F));

    bmac.inject_rx_test_frame(src_frame.data(), src_frame.size());

    // Build DBDMA program at phys 0x3000
    //   desc[0] @ 0x3000: INPUT_LAST, frame_len bytes, addr=0x4000
    //   desc[1] @ 0x3010: STOP
    write_descriptor(0x3000, DBDMA_Cmd::INPUT_LAST, frame_len, 0x4000);
    write_descriptor(0x3010, DBDMA_Cmd::STOP, 0, 0);

    // Create RX DMA channel wired like MacIoTwo does it
    DMAChannel rx_dma("TestBmacRx");
    rx_dma.set_callbacks([](){}, [](){});
    rx_dma.set_data_callbacks(
        [&]() {
            // fetch a frame from BigMac and push into DBDMA buffer
            std::vector<uint8_t> frame;
            if (bmac.fetch_next_rx_frame(frame)) {
                rx_dma.push_data(reinterpret_cast<char*>(frame.data()),
                                 static_cast<int>(frame.size()));
                rx_dma.end_push_data();
            }
        },
        nullptr,
        nullptr);

    // Program and start the channel
    rx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x3000), 4);
    rx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Verify the frame landed in test memory at 0x4000
    if (std::memcmp(&g_test_mem[0x4000], src_frame.data(), frame_len) != 0) {
        std::cerr << "FAIL: Frame not correctly written to memory by DBDMA INPUT" << std::endl;
        ++failures;
    }

    // Check descriptor res_count = 0 (all bytes consumed)
    uint16_t res_count = READ_WORD_LE_A(&g_test_mem[0x3000 + 12]);
    if (res_count != 0) {
        std::cerr << "FAIL: RX descriptor res_count = " << res_count << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 3: Multi-descriptor TX chain (OUTPUT_MORE + OUTPUT_LAST)
//
// Splits a frame across two OUTPUT descriptors to exercise descriptor
// chaining / advancing.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_multi_desc_tx() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();


    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    // Full 60-byte frame in memory: header at 0x5000, payload at 0x5100
    const uint8_t dst[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    // First 14 bytes (header) at 0x5000
    std::memcpy(&g_test_mem[0x5000], dst, 6);
    g_test_mem[0x5006] = 0x00; g_test_mem[0x5007] = 0x11;
    g_test_mem[0x5008] = 0x22; g_test_mem[0x5009] = 0x33;
    g_test_mem[0x500A] = 0x44; g_test_mem[0x500B] = 0x55;
    g_test_mem[0x500C] = 0x08; g_test_mem[0x500D] = 0x00;
    // Remaining 46 bytes (payload) at 0x5100
    for (int i = 0; i < 46; ++i)
        g_test_mem[0x5100 + i] = static_cast<uint8_t>(0xA0 + i);

    // Accumulate both chunks so we can verify later
    std::vector<uint8_t> expected_frame(60);
    std::memcpy(expected_frame.data(), &g_test_mem[0x5000], 14);
    std::memcpy(expected_frame.data() + 14, &g_test_mem[0x5100], 46);

    // Collect TX data manually (since loopback will get the chunks separately)
    std::vector<uint8_t> tx_captured;
    auto capture_tx = [&](const uint8_t* buf, size_t len) {
        tx_captured.insert(tx_captured.end(), buf, buf + len);
    };

    // DBDMA program at 0x6000
    //   desc[0] @ 0x6000: OUTPUT_MORE, 14 bytes, addr=0x5000
    //   desc[1] @ 0x6010: OUTPUT_LAST, 46 bytes, addr=0x5100
    //   desc[2] @ 0x6020: STOP
    write_descriptor(0x6000, DBDMA_Cmd::OUTPUT_MORE, 14, 0x5000);
    write_descriptor(0x6010, DBDMA_Cmd::OUTPUT_LAST, 46, 0x5100);
    write_descriptor(0x6020, DBDMA_Cmd::STOP, 0, 0);

    DMAChannel tx_dma("TestMultiTx");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                capture_tx(p_data, avail_len);
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x6000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Verify both chunks arrived in order
    if (tx_captured.size() != 60) {
        std::cerr << "FAIL: Multi-desc TX captured " << tx_captured.size()
                  << " bytes, expected 60" << std::endl;
        ++failures;
    } else if (std::memcmp(tx_captured.data(), expected_frame.data(), 60) != 0) {
        std::cerr << "FAIL: Multi-desc TX data mismatch" << std::endl;
        ++failures;
    }

    // Both descriptors should have res_count = 0
    uint16_t rc0 = READ_WORD_LE_A(&g_test_mem[0x6000 + 12]);
    uint16_t rc1 = READ_WORD_LE_A(&g_test_mem[0x6010 + 12]);
    if (rc0 != 0 || rc1 != 0) {
        std::cerr << "FAIL: Multi-desc res_count not zero: " << rc0 << ", " << rc1 << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 4: DBDMA interrupt delivery via TimerManager
//
// Runs an OUTPUT_LAST with interrupt=always, then drains timers and checks
// that the InterruptCtrl stub saw the DMA interrupt.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_interrupt_delivery() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();


    StubInterruptCtrl int_ctrl;

    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    // Minimal 60-byte frame at 0x2000
    const uint8_t dst[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    std::memcpy(&g_test_mem[0x2000], dst, 6);
    g_test_mem[0x200C] = 0x08; g_test_mem[0x200D] = 0x00;

    // DBDMA: OUTPUT_LAST @ 0x1000 with interrupt=always (cmd_bits 0x30)
    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, 60, 0x2000, 0x30);
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    DMAChannel tx_dma("TestIrqTx");
    tx_dma.register_dma_int(&int_ctrl, 1);
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                bmac.tx_from_host(p_data, avail_len);
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // The interrupt is delivered via an immediate timer; drain it
    drain_timers();

    if (int_ctrl.dma_ack_count == 0) {
        std::cerr << "FAIL: No DMA interrupt delivered after OUTPUT_LAST" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 5: Full round-trip — TX DBDMA → loopback → RX DBDMA → memory
//
// Exercises both TX and RX DMA channels together: send a frame through
// the TX channel, let loopback echo it, then receive it via the RX channel.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_full_roundtrip() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();


    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();
    bmac.write(BigMacReg::EVENT_MASK, 0x0000);

    // Frame at 0x2000 (TX source)
    const uint8_t dst[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    const size_t frame_len = 60;
    std::memcpy(&g_test_mem[0x2000], dst, 6);
    g_test_mem[0x2006] = 0x00; g_test_mem[0x2007] = 0x11;
    g_test_mem[0x200C] = 0x08; g_test_mem[0x200D] = 0x00;
    for (size_t i = 14; i < frame_len; ++i)
        g_test_mem[0x2000 + i] = static_cast<uint8_t>(i ^ 0x55);

    // TX DBDMA at 0x1000
    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, frame_len, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    // RX DBDMA at 0x3000, receive buffer at 0x4000
    write_descriptor(0x3000, DBDMA_Cmd::INPUT_LAST, frame_len, 0x4000);
    write_descriptor(0x3010, DBDMA_Cmd::STOP, 0, 0);

    // Wire TX channel
    DMAChannel tx_dma("TestRtTx");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                bmac.tx_from_host(p_data, avail_len);
            tx_dma.end_pull_data();
        },
        nullptr);

    // Wire RX channel
    DMAChannel rx_dma("TestRtRx");
    rx_dma.set_callbacks([](){}, [](){});
    rx_dma.set_data_callbacks(
        [&]() {
            bmac.poll_backend();
            std::vector<uint8_t> frame;
            if (bmac.fetch_next_rx_frame(frame)) {
                rx_dma.push_data(reinterpret_cast<char*>(frame.data()),
                                 static_cast<int>(frame.size()));
                rx_dma.end_push_data();
            }
        },
        nullptr, nullptr);

    // Run TX
    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Run RX (loopback has the frame, RX in_cb will poll and push)
    rx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x3000), 4);
    rx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Verify: memory at 0x4000 should match the original frame at 0x2000
    if (std::memcmp(&g_test_mem[0x4000], &g_test_mem[0x2000], frame_len) != 0) {
        std::cerr << "FAIL: Full round-trip data mismatch" << std::endl;
        for (size_t i = 0; i < frame_len; ++i) {
            if (g_test_mem[0x4000 + i] != g_test_mem[0x2000 + i]) {
                std::cerr << "  byte " << i << ": got 0x"
                          << std::hex << (int)g_test_mem[0x4000 + i]
                          << " expected 0x" << (int)g_test_mem[0x2000 + i]
                          << std::dec << std::endl;
                break;
            }
        }
        ++failures;
    }

    // Both channels should be idle
    uint32_t tx_stat = BYTESWAP_32(tx_dma.reg_read(DMAReg::CH_STAT, 4));
    uint32_t rx_stat = BYTESWAP_32(rx_dma.reg_read(DMAReg::CH_STAT, 4));
    if (tx_stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: TX channel still active after round-trip" << std::endl;
        ++failures;
    }
    if (rx_stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: RX channel still active after round-trip" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 6: NOP command — skipped without data transfer
//
// Places NOP descriptors before an OUTPUT_LAST to verify that the DBDMA
// engine advances past them and still transfers data.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_nop_command() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // Data at 0x2000
    for (int i = 0; i < 32; ++i)
        g_test_mem[0x2000 + i] = static_cast<uint8_t>(0xC0 + i);

    // DBDMA program at 0x1000:
    //   desc[0] @ 0x1000: NOP
    //   desc[1] @ 0x1010: NOP
    //   desc[2] @ 0x1020: OUTPUT_LAST, 32 bytes, addr=0x2000
    //   desc[3] @ 0x1030: STOP
    write_descriptor(0x1000, DBDMA_Cmd::NOP, 0, 0);
    write_descriptor(0x1010, DBDMA_Cmd::NOP, 0, 0);
    write_descriptor(0x1020, DBDMA_Cmd::OUTPUT_LAST, 32, 0x2000);
    write_descriptor(0x1030, DBDMA_Cmd::STOP, 0, 0);

    std::vector<uint8_t> tx_captured;
    DMAChannel tx_dma("TestNop");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                tx_captured.insert(tx_captured.end(), p_data, p_data + avail_len);
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    if (tx_captured.size() != 32) {
        std::cerr << "FAIL: NOP test captured " << tx_captured.size()
                  << " bytes, expected 32" << std::endl;
        ++failures;
    } else if (std::memcmp(tx_captured.data(), &g_test_mem[0x2000], 32) != 0) {
        std::cerr << "FAIL: NOP test data mismatch" << std::endl;
        ++failures;
    }

    // NOP descriptors should have xfer_stat updated
    uint16_t nop0_stat = READ_WORD_LE_A(&g_test_mem[0x1000 + 14]);
    uint16_t nop1_stat = READ_WORD_LE_A(&g_test_mem[0x1010 + 14]);
    if (!(nop0_stat & CH_STAT_ACTIVE)) {
        std::cerr << "FAIL: NOP[0] xfer_stat missing ACTIVE" << std::endl;
        ++failures;
    }
    if (!(nop1_stat & CH_STAT_ACTIVE)) {
        std::cerr << "FAIL: NOP[1] xfer_stat missing ACTIVE" << std::endl;
        ++failures;
    }

    // Channel should be idle after STOP
    uint32_t ch_stat = BYTESWAP_32(tx_dma.reg_read(DMAReg::CH_STAT, 4));
    if (ch_stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Channel still ACTIVE after NOP->OUTPUT->STOP" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 7: Unconditional branch (cmd.b = always)
//
// Uses a branch to jump from one descriptor to a non-contiguous descriptor
// that performs the actual transfer. Verifies cmd_ptr follows the branch
// and the BT (branch taken) status bit is reflected in xfer_stat.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_branch() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // Data at 0x4000
    for (int i = 0; i < 16; ++i)
        g_test_mem[0x4000 + i] = static_cast<uint8_t>(0xD0 + i);

    // DBDMA program:
    //   desc[0] @ 0x1000: NOP with branch=always, cmd_arg (branch addr)=0x2000
    //   desc[1] @ 0x1010: STOP  (should be skipped by branch!)
    //   desc[2] @ 0x2000: OUTPUT_LAST, 16 bytes, addr=0x4000
    //   desc[3] @ 0x2010: STOP
    write_descriptor(0x1000, DBDMA_Cmd::NOP, 0, 0,
                     0x0C /* branch=always */, 0x2000 /* branch target */);
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);
    write_descriptor(0x2000, DBDMA_Cmd::OUTPUT_LAST, 16, 0x4000);
    write_descriptor(0x2010, DBDMA_Cmd::STOP, 0, 0);

    std::vector<uint8_t> tx_captured;
    DMAChannel tx_dma("TestBranch");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                tx_captured.insert(tx_captured.end(), p_data, p_data + avail_len);
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    if (tx_captured.size() != 16) {
        std::cerr << "FAIL: Branch test captured " << tx_captured.size()
                  << " bytes, expected 16" << std::endl;
        ++failures;
    } else if (std::memcmp(tx_captured.data(), &g_test_mem[0x4000], 16) != 0) {
        std::cerr << "FAIL: Branch test data mismatch" << std::endl;
        ++failures;
    }

    // The NOP descriptor should have BT set in xfer_stat
    uint16_t nop_stat = READ_WORD_LE_A(&g_test_mem[0x1000 + 14]);
    if (!(nop_stat & CH_STAT_BT)) {
        std::cerr << "FAIL: NOP xfer_stat missing BT (branch taken) bit" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 8: STORE_QUAD — writes a 32-bit value from cmd_arg to memory
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_store_quad() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // DBDMA program:
    //   desc[0] @ 0x1000: STORE_QUAD, req_count=4, addr=0x3000, key=6,
    //                     cmd_arg=0xDEADBEEF
    //   desc[1] @ 0x1010: STOP
    {
        DMACmd d{};
        WRITE_WORD_LE_A(&d.req_count, 4);
        d.cmd_bits = 0;
        d.cmd_key  = (DBDMA_Cmd::STORE_QUAD << 4) | 6; // key=6 required
        WRITE_DWORD_LE_A(&d.address, 0x3000);
        WRITE_DWORD_LE_A(&d.cmd_arg, 0xDEADBEEF);
        d.res_count = 0;
        d.xfer_stat = 0;
        std::memcpy(&g_test_mem[0x1000], &d, sizeof(d));
    }
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    DMAChannel dma("TestStoreQ");
    dma.set_callbacks([](){}, [](){});
    dma.set_data_callbacks(nullptr, nullptr, nullptr);

    dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Verify 0xDEADBEEF was written LE at address 0x3000
    uint32_t stored = READ_DWORD_LE_A(&g_test_mem[0x3000]);
    if (stored != 0xDEADBEEF) {
        std::cerr << "FAIL: STORE_QUAD wrote 0x" << std::hex << stored
                  << " expected 0xDEADBEEF" << std::dec << std::endl;
        ++failures;
    }

    // Channel should be idle
    uint32_t ch_stat = BYTESWAP_32(dma.reg_read(DMAReg::CH_STAT, 4));
    if (ch_stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Channel still ACTIVE after STORE_QUAD+STOP" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 9: LOAD_QUAD — reads a 32-bit value from memory into descriptor cmd_arg
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_load_quad() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // Put a known value at 0x3000
    WRITE_DWORD_LE_A(&g_test_mem[0x3000], 0xCAFEBABE);

    // DBDMA program:
    //   desc[0] @ 0x1000: LOAD_QUAD, req_count=4, addr=0x3000, key=6
    //   desc[1] @ 0x1010: STOP
    {
        DMACmd d{};
        WRITE_WORD_LE_A(&d.req_count, 4);
        d.cmd_bits = 0;
        d.cmd_key  = (DBDMA_Cmd::LOAD_QUAD << 4) | 6;
        WRITE_DWORD_LE_A(&d.address, 0x3000);
        WRITE_DWORD_LE_A(&d.cmd_arg, 0); // should be overwritten
        d.res_count = 0;
        d.xfer_stat = 0;
        std::memcpy(&g_test_mem[0x1000], &d, sizeof(d));
    }
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    DMAChannel dma("TestLoadQ");
    dma.set_callbacks([](){}, [](){});
    dma.set_data_callbacks(nullptr, nullptr, nullptr);

    dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Verify cmd_arg in the descriptor was updated with the loaded value
    uint32_t loaded = READ_DWORD_LE_A(&g_test_mem[0x1000 + 8]);
    if (loaded != 0xCAFEBABE) {
        std::cerr << "FAIL: LOAD_QUAD cmd_arg = 0x" << std::hex << loaded
                  << " expected 0xCAFEBABE" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 10: Channel abort — clearing RUN stops an in-progress channel
//
// Programs a two-OUTPUT chain but aborts after the first descriptor's
// callback. The channel should go inactive and the second descriptor
// should not execute.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_channel_abort() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    for (int i = 0; i < 32; ++i)
        g_test_mem[0x2000 + i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 32; ++i)
        g_test_mem[0x2100 + i] = static_cast<uint8_t>(0x80 + i);

    // Program: OUTPUT_LAST + OUTPUT_LAST + STOP
    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, 32, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::OUTPUT_LAST, 32, 0x2100);
    write_descriptor(0x1020, DBDMA_Cmd::STOP, 0, 0);

    int call_count = 0;
    DMAChannel tx_dma("TestAbort");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            ++call_count;
            // Abort after the first output by clearing RUN
            if (call_count == 1) {
                // Clear RUN: mask=0x8000, value=0 → write 0x80000000
                tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80000000), 4);
            }
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    if (call_count != 1) {
        std::cerr << "FAIL: Abort test: out_cb called " << call_count
                  << " times, expected 1" << std::endl;
        ++failures;
    }

    uint32_t ch_stat = BYTESWAP_32(tx_dma.reg_read(DMAReg::CH_STAT, 4));
    if (ch_stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Channel still ACTIVE after abort" << std::endl;
        ++failures;
    }
    if (ch_stat & CH_STAT_RUN) {
        std::cerr << "FAIL: Channel still has RUN after abort" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 11: Pause prevents start
//
// Sets PAUSE before RUN. Per DBDMA spec, the channel should not become
// ACTIVE when PAUSE is set.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_pause_prevents_start() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, 16, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    int call_count = 0;
    DMAChannel tx_dma("TestPause");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(nullptr,
        [&]() { ++call_count; tx_dma.end_pull_data(); },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);

    // Set PAUSE first: mask=0x4000, value=0x4000
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x40004000), 4);
    // Now try to set RUN
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    uint32_t ch_stat = BYTESWAP_32(tx_dma.reg_read(DMAReg::CH_STAT, 4));
    if (ch_stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Channel became ACTIVE despite PAUSE" << std::endl;
        ++failures;
    }

    if (call_count != 0) {
        std::cerr << "FAIL: out_cb was called despite PAUSE" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 12: Multi-descriptor RX chain (INPUT_MORE + INPUT_LAST)
//
// Receives a frame split across two INPUT descriptors to exercise
// RX-side descriptor chaining.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_multi_desc_rx() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();


    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    // Build and inject a 60-byte frame
    const uint8_t dst[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    const size_t frame_len = 60;
    std::vector<uint8_t> src_frame(frame_len);
    std::memcpy(src_frame.data(), dst, 6);
    src_frame[12] = 0x08; src_frame[13] = 0x00;
    for (size_t i = 14; i < frame_len; ++i)
        src_frame[i] = static_cast<uint8_t>(i ^ 0xAA);

    bmac.inject_rx_test_frame(src_frame.data(), src_frame.size());

    // DBDMA program at 0x3000:
    //   desc[0] @ 0x3000: INPUT_MORE, 20 bytes, addr=0x4000
    //   desc[1] @ 0x3010: INPUT_LAST, 40 bytes, addr=0x4100
    //   desc[2] @ 0x3020: STOP
    write_descriptor(0x3000, DBDMA_Cmd::INPUT_MORE, 20, 0x4000);
    write_descriptor(0x3010, DBDMA_Cmd::INPUT_LAST, 40, 0x4100);
    write_descriptor(0x3020, DBDMA_Cmd::STOP, 0, 0);

    // push_data copies min(queue_len, len) bytes. If that exhausts the
    // descriptor buffer, it calls interpret_cmd() which sets up the next
    // descriptor and re-invokes in_cb recursively. So we track our
    // position across calls.
    const char* push_ptr = reinterpret_cast<const char*>(src_frame.data());
    int push_remaining = static_cast<int>(src_frame.size());
    bool fetched = false;

    DMAChannel rx_dma("TestMultiRx");
    rx_dma.set_callbacks([](){}, [](){});
    rx_dma.set_data_callbacks(
        [&]() {
            if (!fetched) {
                std::vector<uint8_t> frame;
                if (!bmac.fetch_next_rx_frame(frame)) return;
                fetched = true;
            }
            if (push_remaining > 0) {
                int avail = rx_dma.get_push_data_remaining();
                int chunk = std::min(push_remaining, avail);
                if (chunk > 0) {
                    const char* p = push_ptr;
                    // Advance BEFORE push_data, because push_data may
                    // re-invoke in_cb recursively for the next descriptor
                    push_ptr += chunk;
                    push_remaining -= chunk;
                    rx_dma.push_data(p, chunk);
                }
            }
        },
        nullptr, nullptr);

    rx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x3000), 4);
    rx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Verify first 20 bytes at 0x4000, next 40 bytes at 0x4100
    if (std::memcmp(&g_test_mem[0x4000], src_frame.data(), 20) != 0) {
        std::cerr << "FAIL: Multi-desc RX first chunk mismatch" << std::endl;
        ++failures;
    }
    if (std::memcmp(&g_test_mem[0x4100], src_frame.data() + 20, 40) != 0) {
        std::cerr << "FAIL: Multi-desc RX second chunk mismatch" << std::endl;
        ++failures;
    }

    // Both descriptors should have res_count = 0
    uint16_t rc0 = READ_WORD_LE_A(&g_test_mem[0x3000 + 12]);
    uint16_t rc1 = READ_WORD_LE_A(&g_test_mem[0x3010 + 12]);
    if (rc0 != 0 || rc1 != 0) {
        std::cerr << "FAIL: Multi-desc RX res_count not zero: "
                  << rc0 << ", " << rc1 << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 13: Multiple interrupts across a descriptor chain
//
// Three OUTPUT_LAST descriptors each with interrupt=always, followed by
// STOP. Verifies that the interrupt controller is acked exactly 3 times.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_multi_interrupt() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    StubInterruptCtrl int_ctrl;

    // Three small data blocks
    for (int i = 0; i < 16; ++i) {
        g_test_mem[0x2000 + i] = static_cast<uint8_t>(i);
        g_test_mem[0x2100 + i] = static_cast<uint8_t>(0x40 + i);
        g_test_mem[0x2200 + i] = static_cast<uint8_t>(0x80 + i);
    }

    // Three OUTPUT_LAST with interrupt=always, then STOP
    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, 16, 0x2000, 0x30);
    write_descriptor(0x1010, DBDMA_Cmd::OUTPUT_LAST, 16, 0x2100, 0x30);
    write_descriptor(0x1020, DBDMA_Cmd::OUTPUT_LAST, 16, 0x2200, 0x30);
    write_descriptor(0x1030, DBDMA_Cmd::STOP, 0, 0);

    DMAChannel tx_dma("TestMultiIrq");
    tx_dma.register_dma_int(&int_ctrl, 1);
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    drain_timers();

    if (int_ctrl.dma_ack_count != 3) {
        std::cerr << "FAIL: Expected 3 DMA interrupts, got "
                  << int_ctrl.dma_ack_count << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 14: Conditional branch (branch if status bit set)
//
// Uses BRANCH_SELECT to branch only when a specific status bit is set.
// Sets up a NOP with branch=ifTrue, programs BRANCH_SELECT to match
// status bit S0, then tests with S0 set (branch taken) and S0 clear
// (branch not taken).
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_conditional_branch() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // Data at 0x4000 (branch target path)
    for (int i = 0; i < 16; ++i)
        g_test_mem[0x4000 + i] = static_cast<uint8_t>(0xE0 + i);

    // Data at 0x5000 (fall-through path)
    for (int i = 0; i < 16; ++i)
        g_test_mem[0x5000 + i] = static_cast<uint8_t>(0xF0 + i);

    // DBDMA program:
    //   desc[0] @ 0x1000: NOP with branch=ifTrue (cmd_bits 0x04), cmd_arg=0x2000
    //   desc[1] @ 0x1010: OUTPUT_LAST, 16 bytes, addr=0x5000 (fall-through)
    //   desc[2] @ 0x1020: STOP
    //   desc[3] @ 0x2000: OUTPUT_LAST, 16 bytes, addr=0x4000 (branch target)
    //   desc[4] @ 0x2010: STOP
    write_descriptor(0x1000, DBDMA_Cmd::NOP, 0, 0,
                     0x04 /* branch=ifTrue */, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::OUTPUT_LAST, 16, 0x5000);
    write_descriptor(0x1020, DBDMA_Cmd::STOP, 0, 0);
    write_descriptor(0x2000, DBDMA_Cmd::OUTPUT_LAST, 16, 0x4000);
    write_descriptor(0x2010, DBDMA_Cmd::STOP, 0, 0);

    // === Test A: S0 set → branch should be taken ===
    std::vector<uint8_t> captured_a;
    DMAChannel tx_a("TestCondBrA");
    tx_a.set_callbacks([](){}, [](){});
    tx_a.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_a.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                captured_a.insert(captured_a.end(), p_data, p_data + avail_len);
            tx_a.end_pull_data();
        },
        nullptr);

    // BRANCH_SELECT: mask=0x0001 (S0), value=0x0001 (match when S0 is set)
    // Format: upper 16 bits = mask, lower 16 bits = value to match
    tx_a.reg_write(DMAReg::BRANCH_SELECT, BYTESWAP_32(0x00010001), 4);
    tx_a.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);

    // Set S0 before RUN: write CH_CTRL with mask=0x0001, value=0x0001
    tx_a.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x00010001), 4);
    // Now set RUN
    tx_a.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Should have branched to 0x2000, pulling data from 0x4000
    if (captured_a.size() != 16) {
        std::cerr << "FAIL: Conditional branch (S0 set): captured "
                  << captured_a.size() << " bytes, expected 16" << std::endl;
        ++failures;
    } else if (std::memcmp(captured_a.data(), &g_test_mem[0x4000], 16) != 0) {
        std::cerr << "FAIL: Conditional branch (S0 set): data from wrong path" << std::endl;
        ++failures;
    }

    // Check BT bit in NOP descriptor's xfer_stat
    uint16_t nop_stat = READ_WORD_LE_A(&g_test_mem[0x1000 + 14]);
    if (!(nop_stat & CH_STAT_BT)) {
        std::cerr << "FAIL: Conditional branch (S0 set): BT bit not set" << std::endl;
        ++failures;
    }

    // === Test B: S0 clear → branch NOT taken, fall through ===
    std::memset(g_test_mem.data(), 0, 0x3000); // clear descriptor area
    write_descriptor(0x1000, DBDMA_Cmd::NOP, 0, 0,
                     0x04 /* branch=ifTrue */, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::OUTPUT_LAST, 16, 0x5000);
    write_descriptor(0x1020, DBDMA_Cmd::STOP, 0, 0);
    write_descriptor(0x2000, DBDMA_Cmd::OUTPUT_LAST, 16, 0x4000);
    write_descriptor(0x2010, DBDMA_Cmd::STOP, 0, 0);

    std::vector<uint8_t> captured_b;
    DMAChannel tx_b("TestCondBrB");
    tx_b.set_callbacks([](){}, [](){});
    tx_b.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_b.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                captured_b.insert(captured_b.end(), p_data, p_data + avail_len);
            tx_b.end_pull_data();
        },
        nullptr);

    tx_b.reg_write(DMAReg::BRANCH_SELECT, BYTESWAP_32(0x00010001), 4);
    tx_b.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    // Do NOT set S0 — just RUN
    tx_b.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Should have fallen through to 0x1010, pulling data from 0x5000
    if (captured_b.size() != 16) {
        std::cerr << "FAIL: Conditional branch (S0 clear): captured "
                  << captured_b.size() << " bytes, expected 16" << std::endl;
        ++failures;
    } else if (std::memcmp(captured_b.data(), &g_test_mem[0x5000], 16) != 0) {
        std::cerr << "FAIL: Conditional branch (S0 clear): data from wrong path" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test 15: Wake after STOP (self-modifying descriptor chain)
//
// Runs a channel through OUTPUT_LAST + STOP, then overwrites the STOP
// descriptor with a new OUTPUT_LAST + STOP and wakes the channel.
// This is a valid real-world pattern: the guest modifies the descriptor
// chain in memory while the channel is idle, then wakes to continue.
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_wake_from_pause() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // Two data blocks
    for (int i = 0; i < 16; ++i) {
        g_test_mem[0x2000 + i] = static_cast<uint8_t>(0xA0 + i);
        g_test_mem[0x2100 + i] = static_cast<uint8_t>(0xC0 + i);
    }

    // Initial program: OUTPUT_LAST + STOP
    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, 16, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    int call_count = 0;
    std::vector<uint8_t> captured;

    DMAChannel tx_dma("TestWake");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                captured.insert(captured.end(), p_data, p_data + avail_len);
            ++call_count;
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // After first run: OUTPUT_LAST executed, STOP reached
    if (call_count != 1) {
        std::cerr << "FAIL: Wake test: expected 1 call before STOP, got "
                  << call_count << std::endl;
        ++failures;
    }

    uint32_t stat_stopped = BYTESWAP_32(tx_dma.reg_read(DMAReg::CH_STAT, 4));
    if (stat_stopped & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Wake test: channel still ACTIVE after STOP" << std::endl;
        ++failures;
    }
    if (!(stat_stopped & CH_STAT_RUN)) {
        std::cerr << "FAIL: Wake test: channel lost RUN after STOP" << std::endl;
        ++failures;
    }

    // Overwrite the STOP at 0x1010 with OUTPUT_LAST + new STOP at 0x1020
    // cmd_ptr still points at 0x1010 (STOP doesn't advance)
    write_descriptor(0x1010, DBDMA_Cmd::OUTPUT_LAST, 16, 0x2100);
    write_descriptor(0x1020, DBDMA_Cmd::STOP, 0, 0);

    // WAKE the channel — it re-reads cmd_ptr (0x1010) which is now OUTPUT_LAST
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x10001000), 4);

    if (call_count != 2) {
        std::cerr << "FAIL: Wake test: expected 2 total calls after wake, got "
                  << call_count << std::endl;
        ++failures;
    }

    if (captured.size() != 32) {
        std::cerr << "FAIL: Wake test: captured " << captured.size()
                  << " bytes total, expected 32" << std::endl;
        ++failures;
    } else {
        if (std::memcmp(captured.data(), &g_test_mem[0x2000], 16) != 0) {
            std::cerr << "FAIL: Wake test: first chunk mismatch" << std::endl;
            ++failures;
        }
        if (std::memcmp(captured.data() + 16, &g_test_mem[0x2100], 16) != 0) {
            std::cerr << "FAIL: Wake test: second chunk mismatch" << std::endl;
            ++failures;
        }
    }

    // Channel should be idle after second STOP
    uint32_t stat_final = BYTESWAP_32(tx_dma.reg_read(DMAReg::CH_STAT, 4));
    if (stat_final & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Wake test: channel still ACTIVE after final STOP" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test: Unknown command code sets DEAD bit and clears ACTIVE
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_dead_on_unknown_cmd() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // Command code 0xF (only 0-7 are valid; codes > 7 hit the default case)
    // write_descriptor uses (cmd << 4) in cmd_key byte, so cmd=0x0F → cmd_key=0xF0
    DMACmd d{};
    d.cmd_key = 0xF0; // unknown command 0xF
    d.req_count = 0;
    std::memcpy(&g_test_mem[0x1000], &d, sizeof(d));
    // STOP after (shouldn't be reached)
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    DMAChannel ch("TestDead");
    ch.set_callbacks([](){}, [](){});
    ch.set_data_callbacks(nullptr, nullptr, nullptr);

    ch.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    ch.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4); // set RUN

    uint32_t stat = BYTESWAP_32(ch.reg_read(DMAReg::CH_STAT, 4));
    if (!(stat & CH_STAT_DEAD)) {
        std::cerr << "FAIL: Unknown cmd: DEAD not set (stat=0x"
                  << std::hex << stat << std::dec << ")" << std::endl;
        ++failures;
    }
    if (stat & CH_STAT_ACTIVE) {
        std::cerr << "FAIL: Unknown cmd: ACTIVE should be cleared" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test: FLUSH bit invokes flush_cb when channel is active + not dead
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_flush_callback() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    // Set up a data block and an INPUT_MORE that will keep channel active
    for (int i = 0; i < 16; ++i)
        g_test_mem[0x2000 + i] = static_cast<uint8_t>(0xBB);

    // Two INPUT_MORE descriptors — the channel will execute the first and
    // process through to the second (where it blocks waiting for data)
    write_descriptor(0x1000, DBDMA_Cmd::INPUT_MORE, 16, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::INPUT_MORE, 16, 0x2100);
    write_descriptor(0x1020, DBDMA_Cmd::STOP, 0, 0);

    int flush_count = 0;
    int in_calls = 0;
    DMAChannel ch("TestFlush");
    ch.set_callbacks([](){}, [](){});
    ch.set_data_callbacks(
        [&]() {
            ++in_calls;
            if (in_calls == 1) {
                // Complete the first INPUT_MORE; leave second one waiting
                char buf[16]{};
                ch.push_data(buf, 16);
            }
            // On second call, don't push data — channel stays active
        },
        nullptr,
        [&]() { ++flush_count; });

    ch.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    ch.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4); // RUN

    // Now issue FLUSH while channel is active (stuck on second INPUT_MORE)
    // FLUSH bit = 0x2000, set via CH_CTRL set/clr mechanism
    ch.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x20002000), 4);

    if (flush_count != 1) {
        std::cerr << "FAIL: flush_cb called " << flush_count
                  << " times, expected 1" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test: WAIT_SELECT register roundtrip
// ---------------------------------------------------------------------------
extern "C" int test_dbdma_wait_select_roundtrip() {
    int failures = 0;
    DMAChannel ch("TestWaitSel");
    ch.set_callbacks([](){}, [](){});

    // Write a value and read it back (masked to valid bits)
    uint32_t test_val = 0x00FF00AA;
    ch.reg_write(DMAReg::WAIT_SELECT, BYTESWAP_32(test_val), 4);
    uint32_t readback = BYTESWAP_32(ch.reg_read(DMAReg::WAIT_SELECT, 4));

    // WAIT_SELECT is masked to 0xFF00FF
    uint32_t expected = test_val & 0xFF00FFU;
    if (readback != expected) {
        std::cerr << "FAIL: WAIT_SELECT roundtrip: got 0x" << std::hex << readback
                  << " expected 0x" << expected << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test: DBDMA OUTPUT_LAST feeds MACE TX → loopback → RX fetch
//
// Same pattern as test_dbdma_tx_loopback but with MACE instead of BigMac.
// Exercises the MACE+DBDMA integration path used by GrandCentral.
// ---------------------------------------------------------------------------
extern "C" int test_mace_dbdma_tx_loopback() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();
    bool irq_seen = false;
    mace.set_irq_callback([&](bool level) { if (level) irq_seen = true; });

    // Program MACE MAC address via IAC
    using namespace MaceEnet;
    mace.write(Int_Addr_Config, IAC_ADDRCHG | IAC_PHYADDR);
    const uint8_t mac_addr[6] = {0x08, 0x00, 0x07, 0x01, 0x02, 0x03};
    for (int i = 0; i < 6; ++i)
        mace.write(Phys_Addr, mac_addr[i]);

    // Build an Ethernet frame at phys 0x2000 addressed to our MAC
    const size_t frame_len = 60;
    uint8_t* frame = &g_test_mem[0x2000];
    std::memcpy(frame, mac_addr, 6);          // dst = our MAC
    frame[6] = 0xAA; frame[7] = 0xBB; frame[8] = 0xCC;
    frame[9] = 0xDD; frame[10]= 0xEE; frame[11]= 0xFF;
    frame[12]= 0x08; frame[13]= 0x00;        // ethertype
    for (size_t i = 14; i < frame_len; ++i)
        frame[i] = static_cast<uint8_t>(i);

    // Build DBDMA program at phys 0x1000
    write_descriptor(0x1000, DBDMA_Cmd::OUTPUT_LAST, frame_len, 0x2000);
    write_descriptor(0x1010, DBDMA_Cmd::STOP, 0, 0);

    // Create TX DMA channel that pulls into MACE
    DMAChannel tx_dma("TestMaceTx");
    tx_dma.set_callbacks([](){}, [](){});
    tx_dma.set_data_callbacks(
        nullptr,
        [&]() {
            uint32_t avail_len = 0;
            uint8_t* p_data = nullptr;
            tx_dma.pull_data(1600, &avail_len, &p_data);
            if (avail_len && p_data)
                mace.dma_pull_tx(0x2000, avail_len);
            tx_dma.end_pull_data();
        },
        nullptr);

    tx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x1000), 4);
    tx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Loopback echoes TX→RX, so poll to dequeue
    mace.poll_backend();

    // Verify the frame came back via fetch_next_rx_frame
    std::vector<uint8_t> rx_frame;
    if (!mace.fetch_next_rx_frame(rx_frame)) {
        std::cerr << "FAIL: MACE DBDMA: No frame received after TX loopback" << std::endl;
        ++failures;
    } else if (rx_frame.size() != frame_len) {
        std::cerr << "FAIL: MACE DBDMA: RX frame size " << rx_frame.size()
                  << " != expected " << frame_len << std::endl;
        ++failures;
    } else if (std::memcmp(rx_frame.data(), frame, frame_len) != 0) {
        std::cerr << "FAIL: MACE DBDMA: RX frame data mismatch" << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// Test: DBDMA INPUT_LAST receives a frame from MACE RX queue
//
// Injects a frame into MACE's RX queue, then runs a DBDMA INPUT_LAST
// program that pulls the frame into test memory via fetch_next_rx_frame.
// ---------------------------------------------------------------------------
extern "C" int test_mace_dbdma_rx_to_memory() {
    int failures = 0;
    std::memset(g_test_mem.data(), 0, g_test_mem.size());
    init_timer_manager();

    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    // Program MACE MAC address
    using namespace MaceEnet;
    mace.write(Int_Addr_Config, IAC_ADDRCHG | IAC_PHYADDR);
    const uint8_t mac_addr[6] = {0x08, 0x00, 0x07, 0x01, 0x02, 0x03};
    for (int i = 0; i < 6; ++i)
        mace.write(Phys_Addr, mac_addr[i]);

    // Build a frame to inject addressed to our MAC
    const size_t frame_len = 64;
    std::vector<uint8_t> src_frame(frame_len, 0);
    std::memcpy(src_frame.data(), mac_addr, 6);  // dst = our MAC
    src_frame[6] = 0xAA; src_frame[7] = 0xBB;
    for (size_t i = 14; i < frame_len; ++i)
        src_frame[i] = static_cast<uint8_t>(0x80 | (i & 0x7F));

    mace.inject_rx_test_frame(src_frame.data(), src_frame.size());

    // DBDMA program at 0x3000, receive buffer at 0x4000
    write_descriptor(0x3000, DBDMA_Cmd::INPUT_LAST, frame_len, 0x4000);
    write_descriptor(0x3010, DBDMA_Cmd::STOP, 0, 0);

    // Create RX DMA channel
    DMAChannel rx_dma("TestMaceRx");
    rx_dma.set_callbacks([](){}, [](){});
    rx_dma.set_data_callbacks(
        [&]() {
            std::vector<uint8_t> frame;
            if (mace.fetch_next_rx_frame(frame)) {
                rx_dma.push_data(reinterpret_cast<char*>(frame.data()),
                                 static_cast<int>(frame.size()));
                rx_dma.end_push_data();
            }
        },
        nullptr, nullptr);

    rx_dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x3000), 4);
    rx_dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000), 4);

    // Verify the frame landed in test memory at 0x4000
    if (std::memcmp(&g_test_mem[0x4000], src_frame.data(), frame_len) != 0) {
        std::cerr << "FAIL: MACE DBDMA RX: frame not in memory" << std::endl;
        ++failures;
    }

    // Check descriptor res_count = 0
    uint16_t res_count = READ_WORD_LE_A(&g_test_mem[0x3000 + 12]);
    if (res_count != 0) {
        std::cerr << "FAIL: MACE DBDMA RX: res_count = " << res_count << std::endl;
        ++failures;
    }

    return failures;
}
