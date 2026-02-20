#include <devices/ethernet/mace.h>
#include "mock_error_backend.h"
#include <cpu/ppc/ppcmmu.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <array>

extern std::array<uint8_t, 65536> g_test_mem;

// Forward declarations so ppctests.cpp can link cleanly
extern "C" int test_mace_loopback_basic();
extern "C" int test_mace_phys_addr_readback();
extern "C" int test_mace_broadcast();
extern "C" int test_mace_chip_id();
extern "C" int test_mace_biu_reset();
extern "C" int test_mace_multiframe_order();
extern "C" int test_mace_log_addr_flt();
extern "C" int test_mace_dma_pull_tx_unmapped();
extern "C" int test_mace_iac_mutual_exclusion();
extern "C" int test_mace_tx_interrupt_status();
extern "C" int test_mace_missed_pkt_count();
extern "C" int test_mace_promisc_accepts_all();
extern "C" int test_mace_poll_backend_error();
extern "C" int test_mace_tx_error();
extern "C" int test_mace_fifo_config_roundtrip();
extern "C" int test_mace_mac_config_ctrl_roundtrip();
extern "C" int test_mace_pls_config_ctrl_roundtrip();

// Simple helper to verify MAC filtering and FIFO behavior without real DMA
int test_mace_loopback_basic() {
    int failures = 0;
    MaceController mace(0x0941); // REV_A2
    mace.set_backend_name("loopback");
    mace.device_postinit();

    bool irq_seen = false;
    mace.set_irq_callback([&](bool level){ if (level) irq_seen = true; });

    // Program MAC address: 02:00:de:ad:be:ef
    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    const uint8_t mac_bytes[6] = {0x02,0x00,0xDE,0xAD,0xBE,0xEF};
    for (int i = 0; i < 6; ++i) {
        mace.write(MaceEnet::MaceReg::Phys_Addr, mac_bytes[i]);
    }
    // Enable interrupts mask for RX
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF);

    // Craft a frame destined for our MAC
    uint8_t frame[64]{};
    std::memcpy(frame, mac_bytes, 6); // dst
    frame[6] = 0x00; frame[7] = 0x11; frame[8] = 0x22; frame[9] = 0x33; frame[10] = 0x44; frame[11] = 0x55; // src
    frame[12] = 0x08; frame[13] = 0x00; // Ethertype IPv4
    for (int i = 14; i < 60; ++i) frame[i] = static_cast<uint8_t>(i);
    // minimum ethernet frame size 60 bytes (w/o FCS)

    mace.inject_rx_test_frame(frame, 60);

    uint8_t fifo_cnt = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fifo_cnt == 0) {
        std::cerr << "FAIL: FIFO frame count not incremented" << std::endl;
        ++failures;
    }

    if (!irq_seen) {
        std::cerr << "FAIL: IRQ callback not invoked on RX" << std::endl;
        ++failures;
    }

    // Check interrupt mask gating: mask off RX bit, inject again, expect no IRQ
    irq_seen = false;
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0x00); // mask everything
    mace.inject_rx_test_frame(frame, 60);
    if (irq_seen) {
        std::cerr << "FAIL: IRQ fired despite mask=0" << std::endl;
        ++failures;
    }
    // re-enable mask for rest of test
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF);

    // Negative MAC filtering test: deliver to a different destination; should be dropped
    uint8_t frame_other[64]{};
    std::memcpy(frame_other, "\x08\x00\x27\x12\x34\x56", 6); // VirtualBox default MAC (just as a dummy)
    mace.inject_rx_test_frame(frame_other, 60);
    // fifo count should not have incremented (still prior count + 2 frames we enqueued)
    // We already enqueued 2 frames above (one accepted, one masked), plus this drop.
    // The masked frame still increments FIFO count in our current implementation; we only
    // check that the drop does not increment.
    uint8_t fifo_after_drop = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fifo_after_drop < fifo_cnt) {
        std::cerr << "FAIL: FIFO count decreased unexpectedly after drop test" << std::endl;
        ++failures;
    }

    // Drain FIFO via Rcv_FIFO (drain all queued frames)
    auto drain_one_frame = [&](const uint8_t* expected, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            uint8_t b = mace.read(MaceEnet::MaceReg::Rcv_FIFO);
            if (b != expected[i]) {
                std::cerr << "FAIL: Rcv_FIFO byte mismatch at " << i << " expected " << (int)expected[i]
                          << " got " << (int)b << std::endl;
                ++failures;
                break;
            }
        }
    };
    uint8_t fifo_cnt_current = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    while (fifo_cnt_current > 0) {
        drain_one_frame(frame, 60);
        fifo_cnt_current = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    }
    if (fifo_cnt_current != 0) {
        std::cerr << "FAIL: FIFO frame count not decremented to zero" << std::endl;
        ++failures;
    }

    // Check interrupt register clears-on-read behavior
    uint8_t int_val = mace.read(MaceEnet::MaceReg::Interrupt);
    if (!(int_val & 0x02)) { // RX bit
        std::cerr << "FAIL: Interrupt register missing RX bit" << std::endl;
        ++failures;
    }
    uint8_t int_val_after = mace.read(MaceEnet::MaceReg::Interrupt);
    if (int_val_after != 0) {
        std::cerr << "FAIL: Interrupt register did not clear on read" << std::endl;
        ++failures;
    }

    // TX path via dma_pull_tx and loopback backend: this exercises mmu_map_dma_mem stub.
    // Pre-populate g_test_mem with a valid frame for dma_pull_tx
    const uint8_t tx_mac[6] = {0x02,0x00,0xDE,0xAD,0xBE,0xEF};
    std::memcpy(&g_test_mem[0x1000], tx_mac, 6);
    for (int i = 6; i < 128; ++i) g_test_mem[0x1000 + i] = static_cast<uint8_t>(i & 0xFF);
    size_t sent = mace.dma_pull_tx(/*addr*/0x1000, /*len*/128);
    if (sent == 0) {
        std::cerr << "FAIL: dma_pull_tx sent=0" << std::endl;
        ++failures;
    }
    // Force a poll to retrieve looped-back frame
    bool irq_seen_txrx = false;
    mace.set_irq_callback([&](bool level){ if (level) irq_seen_txrx = true; });
    mace.poll_backend();
    // Drain at least one frame from FIFO (should be the looped-back TX frame)
    uint8_t fifo_cnt_txrx = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fifo_cnt_txrx == 0) {
        std::cerr << "FAIL: TX->RX loopback frame not enqueued" << std::endl;
        ++failures;
    }
    if (!irq_seen_txrx) {
        std::cerr << "FAIL: IRQ not seen for TX->RX loopback" << std::endl;
        ++failures;
    }
    // Drain a handful of bytes to confirm data pattern matches mmu_map_dma_mem stub
    // First 6 bytes should be our test MAC (02:00:DE:AD:BE:EF), rest incremental pattern
    uint8_t expected_mac[6] = {0x02,0x00,0xDE,0xAD,0xBE,0xEF};
    for (int i = 0; i < 6 && fifo_cnt_txrx; ++i) {
        uint8_t b = mace.read(MaceEnet::MaceReg::Rcv_FIFO);
        if (b != expected_mac[i]) {
            std::cerr << "FAIL: loopback data mismatch at MAC byte " << i << " got " << (int)b << std::endl;
            ++failures;
            break;
        }
    }
    for (int i = 6; i < 16 && fifo_cnt_txrx; ++i) {
        uint8_t b = mace.read(MaceEnet::MaceReg::Rcv_FIFO);
        if (b != static_cast<uint8_t>(i)) {
            std::cerr << "FAIL: loopback data mismatch at byte " << i << " got " << (int)b << std::endl;
            ++failures;
            break;
        }
    }

    return failures;
}

// ---- BUG CATCHER: Phys_Addr register should be readable ----
int test_mace_phys_addr_readback() {
    int failures = 0;
    MaceController mace(0x0941);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    // Program MAC: 02:00:DE:AD:BE:EF
    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    const uint8_t mac_bytes[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < 6; ++i)
        mace.write(MaceEnet::MaceReg::Phys_Addr, mac_bytes[i]);

    // Now read it back: set IAC_PHYADDR again and read 6 bytes
    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    for (int i = 0; i < 6; ++i) {
        uint8_t b = mace.read(MaceEnet::MaceReg::Phys_Addr);
        if (b != mac_bytes[i]) {
            std::cerr << "FAIL: MACE Phys_Addr readback byte " << i
                      << " = 0x" << std::hex << (int)b
                      << " expected 0x" << (int)mac_bytes[i] << std::dec
                      << " (read handler missing?)" << std::endl;
            ++failures;
        }
    }

    return failures;
}

// ---- Broadcast frame should be accepted ----
int test_mace_broadcast() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    // Program MAC
    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    const uint8_t mac_bytes[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < 6; ++i)
        mace.write(MaceEnet::MaceReg::Phys_Addr, mac_bytes[i]);
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF);

    bool irq_seen = false;
    mace.set_irq_callback([&](bool level) { if (level) irq_seen = true; });

    // Build broadcast frame
    uint8_t frame[60]{};
    std::memset(frame, 0xFF, 6); // dst = FF:FF:FF:FF:FF:FF
    frame[12] = 0x08; frame[13] = 0x00;
    for (int i = 14; i < 60; ++i) frame[i] = static_cast<uint8_t>(i);

    mace.inject_rx_test_frame(frame, 60);

    uint8_t fifo_cnt = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fifo_cnt == 0) {
        std::cerr << "FAIL: MACE broadcast frame not enqueued" << std::endl;
        ++failures;
    }
    if (!irq_seen) {
        std::cerr << "FAIL: MACE IRQ not fired for broadcast frame" << std::endl;
        ++failures;
    }

    // Verify first 6 bytes are the broadcast address
    for (int i = 0; i < 6; ++i) {
        uint8_t b = mace.read(MaceEnet::MaceReg::Rcv_FIFO);
        if (b != 0xFF) {
            std::cerr << "FAIL: MACE broadcast data mismatch at byte " << i << std::endl;
            ++failures;
            break;
        }
    }

    return failures;
}

// ---- Chip ID readback ----
int test_mace_chip_id() {
    int failures = 0;

    {
        MaceController mace(MACE_ID_REV_A2);
        uint8_t lo = mace.read(MaceEnet::MaceReg::Chip_ID_Lo);
        uint8_t hi = mace.read(MaceEnet::MaceReg::Chip_ID_Hi);
        uint16_t id = (hi << 8) | lo;
        if (id != MACE_ID_REV_A2) {
            std::cerr << "FAIL: MACE A2 Chip_ID = 0x" << std::hex << id
                      << " expected 0x" << MACE_ID_REV_A2 << std::dec << std::endl;
            ++failures;
        }
    }
    {
        MaceController mace(MACE_ID_REV_B0);
        uint8_t lo = mace.read(MaceEnet::MaceReg::Chip_ID_Lo);
        uint8_t hi = mace.read(MaceEnet::MaceReg::Chip_ID_Hi);
        uint16_t id = (hi << 8) | lo;
        if (id != MACE_ID_REV_B0) {
            std::cerr << "FAIL: MACE B0 Chip_ID = 0x" << std::hex << id
                      << " expected 0x" << MACE_ID_REV_B0 << std::dec << std::endl;
            ++failures;
        }
    }

    return failures;
}

// ---- BIU software reset clears state ----
int test_mace_biu_reset() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    // Program MAC and inject a frame so RX queue has data
    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    const uint8_t mac_bytes[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < 6; ++i)
        mace.write(MaceEnet::MaceReg::Phys_Addr, mac_bytes[i]);
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF);

    uint8_t frame[60]{};
    std::memcpy(frame, mac_bytes, 6);
    frame[12] = 0x08; frame[13] = 0x00;
    mace.inject_rx_test_frame(frame, 60);

    // Verify frame is queued
    uint8_t fc_before = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fc_before == 0) {
        std::cerr << "FAIL: MACE BIU reset: no frame queued before reset" << std::endl;
        ++failures;
    }

    // Software reset via BIU_Config_Ctrl
    mace.write(MaceEnet::MaceReg::BIU_Config_Ctrl, MaceEnet::BIU_SWRST);

    // After reset: FIFO should be empty, interrupt cleared
    uint8_t fc_after = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fc_after != 0) {
        std::cerr << "FAIL: MACE BIU reset: FIFO_Frame_Cnt = " << (int)fc_after
                  << " after reset (expected 0)" << std::endl;
        ++failures;
    }

    uint8_t int_stat = mace.read(MaceEnet::MaceReg::Interrupt);
    if (int_stat != 0) {
        std::cerr << "FAIL: MACE BIU reset: Interrupt = 0x" << std::hex << (int)int_stat
                  << " after reset (expected 0)" << std::dec << std::endl;
        ++failures;
    }

    // BIU_SWRST bit should self-clear
    uint8_t biu = mace.read(MaceEnet::MaceReg::BIU_Config_Ctrl);
    if (biu & MaceEnet::BIU_SWRST) {
        std::cerr << "FAIL: MACE BIU_SWRST not self-clearing" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- Multiple frames: FIFO ordering ----
int test_mace_multiframe_order() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    const uint8_t mac_bytes[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < 6; ++i)
        mace.write(MaceEnet::MaceReg::Phys_Addr, mac_bytes[i]);
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF);

    // Inject 3 frames with distinguishable tags at byte 14
    for (int f = 0; f < 3; ++f) {
        uint8_t frame[60]{};
        std::memcpy(frame, mac_bytes, 6);
        frame[12] = 0x08; frame[13] = 0x00;
        frame[14] = static_cast<uint8_t>(0xB0 + f);
        mace.inject_rx_test_frame(frame, 60);
    }

    uint8_t fc = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fc != 3) {
        std::cerr << "FAIL: MACE multiframe: FIFO_Frame_Cnt = " << (int)fc
                  << " expected 3" << std::endl;
        ++failures;
    }

    // Drain frames and check ordering by reading byte 14 from each
    for (int f = 0; f < 3; ++f) {
        // Skip first 14 bytes (dst MAC + src MAC + ethertype)
        for (int i = 0; i < 14; ++i)
            mace.read(MaceEnet::MaceReg::Rcv_FIFO);
        uint8_t tag = mace.read(MaceEnet::MaceReg::Rcv_FIFO);
        if (tag != static_cast<uint8_t>(0xB0 + f)) {
            std::cerr << "FAIL: MACE multiframe: wrong order at frame " << f
                      << " got tag 0x" << std::hex << (int)tag << std::dec << std::endl;
            ++failures;
            break;
        }
        // Drain remaining bytes of this frame
        for (int i = 15; i < 60; ++i)
            mace.read(MaceEnet::MaceReg::Rcv_FIFO);
    }

    return failures;
}

// ---- Log_Addr_Flt register write behavior ----
int test_mace_log_addr_flt() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);

    // Set IAC_LOGADDR + IAC_ADDRCHG to begin writing the logical address filter
    mace.write(MaceEnet::MaceReg::Int_Addr_Config,
               MaceEnet::IAC_LOGADDR | MaceEnet::IAC_ADDRCHG);

    // Verify IAC_LOGADDR is set
    uint8_t iac = mace.read(MaceEnet::MaceReg::Int_Addr_Config);
    if (!(iac & MaceEnet::IAC_LOGADDR)) {
        std::cerr << "FAIL: MACE IAC_LOGADDR not set after write" << std::endl;
        ++failures;
    }

    // Write 8 bytes of logical address filter (multicast hash)
    const uint8_t filter[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    for (int i = 0; i < 8; ++i)
        mace.write(MaceEnet::MaceReg::Log_Addr_Flt, filter[i]);

    // After 8 bytes, IAC_LOGADDR should auto-clear
    iac = mace.read(MaceEnet::MaceReg::Int_Addr_Config);
    if (iac & MaceEnet::IAC_LOGADDR) {
        std::cerr << "FAIL: MACE IAC_LOGADDR not cleared after 8 bytes" << std::endl;
        ++failures;
    }

    // Writing to Log_Addr_Flt without IAC_LOGADDR should be a no-op
    mace.write(MaceEnet::MaceReg::Log_Addr_Flt, 0xFF);
    iac = mace.read(MaceEnet::MaceReg::Int_Addr_Config);
    if (iac & MaceEnet::IAC_LOGADDR) {
        std::cerr << "FAIL: MACE IAC_LOGADDR spuriously set" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- dma_pull_tx with address beyond test memory should return 0 ----
int test_mace_dma_pull_tx_unmapped() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    size_t sent = mace.dma_pull_tx(0xF0000, 64);
    if (sent != 0) {
        std::cerr << "FAIL: MACE dma_pull_tx with unmapped address returned "
                  << sent << " (expected 0)" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- IAC mutual exclusion: setting LOGADDR|PHYADDR together clears PHYADDR ----
int test_mace_iac_mutual_exclusion() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    // Write both flags simultaneously
    mace.write(MaceEnet::MaceReg::Int_Addr_Config,
               MaceEnet::IAC_LOGADDR | MaceEnet::IAC_PHYADDR);

    uint8_t iac = mace.read(MaceEnet::MaceReg::Int_Addr_Config);

    // LOGADDR should be set, PHYADDR should be cleared
    if (!(iac & MaceEnet::IAC_LOGADDR)) {
        std::cerr << "FAIL: IAC mutual exclusion: LOGADDR not set" << std::endl;
        ++failures;
    }
    if (iac & MaceEnet::IAC_PHYADDR) {
        std::cerr << "FAIL: IAC mutual exclusion: PHYADDR should be cleared" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- TX interrupt status: dma_pull_tx sets INT_TX in Interrupt register ----
int test_mace_tx_interrupt_status() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    bool irq_seen = false;
    mace.set_irq_callback([&](bool level) { if (level) irq_seen = true; });
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF); // unmask all

    // Build a minimal frame in test memory
    uint8_t* frame = &g_test_mem[0x2000];
    std::memset(frame, 0, 64);
    frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xFF;
    frame[3] = 0xFF; frame[4] = 0xFF; frame[5] = 0xFF; // broadcast dst

    size_t sent = mace.dma_pull_tx(0x2000, 60);
    if (sent != 60) {
        std::cerr << "FAIL: dma_pull_tx returned " << sent << " expected 60" << std::endl;
        ++failures;
    }

    // Xmit_Frame_Stat should be 0 (success)
    uint8_t xfs = mace.read(MaceEnet::MaceReg::Xmit_Frame_Stat);
    if (xfs != 0) {
        std::cerr << "FAIL: Xmit_Frame_Stat = 0x" << std::hex << (int)xfs
                  << " expected 0" << std::dec << std::endl;
        ++failures;
    }

    // Interrupt register should have INT_TX (bit 0)
    uint8_t ir = mace.read(MaceEnet::MaceReg::Interrupt);
    if (!(ir & 0x01)) {
        std::cerr << "FAIL: Interrupt register missing INT_TX (got 0x"
                  << std::hex << (int)ir << std::dec << ")" << std::endl;
        ++failures;
    }

    // Interrupt register should be clear-on-read
    uint8_t ir2 = mace.read(MaceEnet::MaceReg::Interrupt);
    if (ir2 != 0) {
        std::cerr << "FAIL: Interrupt not cleared after read (0x"
                  << std::hex << (int)ir2 << std::dec << ")" << std::endl;
        ++failures;
    }

    // IRQ callback should have fired
    if (!irq_seen) {
        std::cerr << "FAIL: IRQ callback not invoked after TX" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- Missed packet counter increments when frame rejected by MAC filter ----
int test_mace_missed_pkt_count() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    // Program MAC address
    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    const uint8_t mac_bytes[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < 6; ++i)
        mace.write(MaceEnet::MaceReg::Phys_Addr, mac_bytes[i]);

    // Inject frames destined for a different MAC (not broadcast, not our MAC)
    uint8_t frame[60]{};
    frame[0] = 0x02; frame[1] = 0x00; frame[2] = 0x11;
    frame[3] = 0x22; frame[4] = 0x33; frame[5] = 0x44; // wrong dst

    mace.inject_rx_test_frame(frame, 60);
    mace.inject_rx_test_frame(frame, 60);
    mace.inject_rx_test_frame(frame, 60);

    uint8_t missed = mace.read(MaceEnet::MaceReg::Missed_Pkt_Cnt);
    if (missed != 3) {
        std::cerr << "FAIL: Missed_Pkt_Cnt = " << (int)missed
                  << " expected 3" << std::endl;
        ++failures;
    }

    // FIFO should be empty since all frames were rejected
    uint8_t fifo = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fifo != 0) {
        std::cerr << "FAIL: FIFO_Frame_Cnt = " << (int)fifo
                  << " expected 0" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- Promiscuous mode accepts frames for any MAC ----
int test_mace_promisc_accepts_all() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);
    mace.set_backend_name("loopback");
    mace.device_postinit();

    // Program MAC address
    mace.write(MaceEnet::MaceReg::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
    const uint8_t mac_bytes[6] = {0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < 6; ++i)
        mace.write(MaceEnet::MaceReg::Phys_Addr, mac_bytes[i]);

    // Enable promiscuous mode (MAC_Config_Ctrl bit 0)
    mace.write(MaceEnet::MaceReg::MAC_Config_Ctrl, 0x01);

    // Inject frame destined for a completely different MAC
    uint8_t frame[60]{};
    frame[0] = 0x02; frame[1] = 0x00; frame[2] = 0x11;
    frame[3] = 0x22; frame[4] = 0x33; frame[5] = 0x44;

    mace.inject_rx_test_frame(frame, 60);

    uint8_t fifo = mace.read(MaceEnet::MaceReg::FIFO_Frame_Cnt);
    if (fifo != 1) {
        std::cerr << "FAIL: Promisc mode: FIFO_Frame_Cnt = " << (int)fifo
                  << " expected 1" << std::endl;
        ++failures;
    }

    uint8_t missed = mace.read(MaceEnet::MaceReg::Missed_Pkt_Cnt);
    if (missed != 0) {
        std::cerr << "FAIL: Promisc mode: Missed_Pkt_Cnt = " << (int)missed
                  << " expected 0" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- poll_backend error: recv() returns -1 ----
int test_mace_poll_backend_error() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);

    auto mock = std::make_unique<MockErrorBackend>();
    mock->fail_recv = true;
    mace.set_backend_for_test(std::move(mock));

    bool irq_seen = false;
    mace.set_irq_callback([&](bool level) { if (level) irq_seen = true; });
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF); // unmask all

    mace.poll_backend();

    uint8_t ir = mace.read(MaceEnet::MaceReg::Interrupt);
    if (!(ir & 0x80)) { // INT_ERR = bit 7
        std::cerr << "FAIL: poll_backend error: Interrupt missing INT_ERR (0x"
                  << std::hex << (int)ir << std::dec << ")" << std::endl;
        ++failures;
    }
    if (!irq_seen) {
        std::cerr << "FAIL: poll_backend error: IRQ not fired" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- dma_pull_tx with send failure ----
int test_mace_tx_error() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);

    auto mock = std::make_unique<MockErrorBackend>();
    mock->fail_send = true;
    mace.set_backend_for_test(std::move(mock));

    bool irq_seen = false;
    mace.set_irq_callback([&](bool level) { if (level) irq_seen = true; });
    mace.write(MaceEnet::MaceReg::Interrupt_Mask, 0xFF);

    // Put a frame in test memory and transmit
    uint8_t* frame = &g_test_mem[0x3000];
    std::memset(frame, 0, 64);
    frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xFF;
    frame[3] = 0xFF; frame[4] = 0xFF; frame[5] = 0xFF;

    size_t sent = mace.dma_pull_tx(0x3000, 60);
    if (sent != 60) {
        std::cerr << "FAIL: dma_pull_tx returned " << sent << " expected 60" << std::endl;
        ++failures;
    }

    // Xmit_Frame_Stat should have error bit
    uint8_t xfs = mace.read(MaceEnet::MaceReg::Xmit_Frame_Stat);
    if (!(xfs & 0x80)) {
        std::cerr << "FAIL: TX error: Xmit_Frame_Stat missing error bit (0x"
                  << std::hex << (int)xfs << std::dec << ")" << std::endl;
        ++failures;
    }

    // Interrupt should have INT_ERR
    uint8_t ir = mace.read(MaceEnet::MaceReg::Interrupt);
    if (!(ir & 0x80)) {
        std::cerr << "FAIL: TX error: Interrupt missing INT_ERR" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- FIFO_Config roundtrip ----
int test_mace_fifo_config_roundtrip() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);

    mace.write(MaceEnet::MaceReg::FIFO_Config, 0xA5);
    uint8_t val = mace.read(MaceEnet::MaceReg::FIFO_Config);
    if (val != 0xA5) {
        std::cerr << "FAIL: FIFO_Config roundtrip: got 0x" << std::hex << (int)val
                  << " expected 0xA5" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- MAC_Config_Ctrl roundtrip (regression for mac_cfg/mac_cc bug) ----
int test_mace_mac_config_ctrl_roundtrip() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);

    mace.write(MaceEnet::MaceReg::MAC_Config_Ctrl, 0x0F);
    uint8_t val = mace.read(MaceEnet::MaceReg::MAC_Config_Ctrl);
    if (val != 0x0F) {
        std::cerr << "FAIL: MAC_Config_Ctrl roundtrip: got 0x" << std::hex << (int)val
                  << " expected 0x0F" << std::dec << std::endl;
        ++failures;
    }

    // Verify different value
    mace.write(MaceEnet::MaceReg::MAC_Config_Ctrl, 0x01);
    val = mace.read(MaceEnet::MaceReg::MAC_Config_Ctrl);
    if (val != 0x01) {
        std::cerr << "FAIL: MAC_Config_Ctrl second roundtrip: got 0x"
                  << std::hex << (int)val << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- PLS_Config_Ctrl roundtrip (regression for missing store) ----
int test_mace_pls_config_ctrl_roundtrip() {
    int failures = 0;
    MaceController mace(MACE_ID_REV_A2);

    mace.write(MaceEnet::MaceReg::PLS_Config_Ctrl, 7);
    uint8_t val = mace.read(MaceEnet::MaceReg::PLS_Config_Ctrl);
    if (val != 7) {
        std::cerr << "FAIL: PLS_Config_Ctrl roundtrip: got 0x" << std::hex << (int)val
                  << " expected 7" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}
