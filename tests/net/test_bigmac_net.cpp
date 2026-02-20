#include <devices/ethernet/bigmac.h>
#include <utils/net/ether_backend.h>
#include "mock_error_backend.h"
#include <cpu/ppc/ppcmmu.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <array>
#include <vector>

extern std::array<uint8_t, 65536> g_test_mem;

static const uint8_t kTestMac[6] = {0x02,0x00,0xDE,0xAD,0xBE,0xEF};

// Build a test frame with given dst MAC
static std::vector<uint8_t> make_frame(const uint8_t* dst, size_t payload_len = 46) {
    std::vector<uint8_t> frame(14 + payload_len, 0);
    std::memcpy(frame.data(), dst, 6);
    // src
    frame[6] = 0x00; frame[7] = 0x11; frame[8] = 0x22;
    frame[9] = 0x33; frame[10] = 0x44; frame[11] = 0x55;
    frame[12] = 0x08; frame[13] = 0x00; // IPv4
    for (size_t i = 14; i < frame.size(); ++i)
        frame[i] = static_cast<uint8_t>(i & 0xFF);
    return frame;
}

extern "C" int test_bigmac_loopback_basic();
extern "C" int test_bigmac_mac_filter();
extern "C" int test_bigmac_broadcast();
extern "C" int test_bigmac_multiframe_order();
extern "C" int test_bigmac_event_mask_gating();
extern "C" int test_bigmac_glob_stat_clear_on_read();
extern "C" int test_bigmac_chip_id();
extern "C" int test_bigmac_crc32();
extern "C" int test_bigmac_fifo_csr_roundtrip();
extern "C" int test_bigmac_tx_max_min_roundtrip();
extern "C" int test_bigmac_mac_addr_roundtrip();
extern "C" int test_bigmac_hash_table_roundtrip();
extern "C" int test_bigmac_tx_sw_reset_self_clear();
extern "C" int test_bigmac_srom_mac_read();
extern "C" int test_bigmac_mii_phy_read_bmsr();
extern "C" int test_bigmac_mii_phy_write_read_bmcr();
extern "C" int test_bigmac_mii_phy_read_id();
extern "C" int test_bigmac_dma_pull_tx_unmapped();
extern "C" int test_bigmac_chip_reset_clears_state();
extern "C" int test_bigmac_addr_filter_roundtrip();
extern "C" int test_bigmac_misc_reg_roundtrip();
extern "C" int test_bigmac_mii_anar_write_read();
extern "C" int test_bigmac_peak_att_clear_on_read();
extern "C" int test_bigmac_poll_backend_error();
extern "C" int test_bigmac_tx_from_host_error();
extern "C" int test_bigmac_srom_sequential_read();
extern "C" int test_bigmac_mem_data_lo_mirrors_hi();
extern "C" int test_bigmac_rx_frm_cnt();
extern "C" int test_bigmac_mii_wrong_phy_addr();

// ---- basic loopback (original test) ----
int test_bigmac_loopback_basic() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level){ if (level) irq_seen = true; });

    bmac.write(BigMacReg::EVENT_MASK, 0x0000);

    auto frame = make_frame(kTestMac);

    bmac.inject_rx_test_frame(frame.data(), frame.size());
    uint16_t glob_stat = bmac.read(BigMacReg::GLOB_STAT);
    if ((glob_stat & 0x0001) == 0) {
        std::cerr << "FAIL: BigMac GLOB_STAT missing RX bit" << std::endl;
        ++failures;
    }
    // Drain via MEM_DATA_HI register (two bytes per read)
    for (size_t i = 0; i < frame.size() / 2; ++i) {
        uint16_t word = bmac.read(BigMacReg::MEM_DATA_HI);
        uint8_t hi = (word >> 8) & 0xFF;
        uint8_t lo = word & 0xFF;
        size_t idx_hi = i * 2;
        size_t idx_lo = idx_hi + 1;
        if (idx_hi < frame.size() && hi != frame[idx_hi]) {
            std::cerr << "FAIL: BigMac MEM_DATA_HI byte mismatch hi idx=" << idx_hi << " got " << (int)hi << std::endl;
            ++failures;
            break;
        }
        if (idx_lo < frame.size() && lo != frame[idx_lo]) {
            std::cerr << "FAIL: BigMac MEM_DATA_HI byte mismatch lo idx=" << idx_lo << " got " << (int)lo << std::endl;
            ++failures;
            break;
        }
    }
    // After draining, GLOB_STAT should clear
    glob_stat = bmac.read(BigMacReg::GLOB_STAT);
    if (glob_stat != 0) {
        std::cerr << "FAIL: BigMac GLOB_STAT not cleared after read" << std::endl;
        ++failures;
    }

    // TX path via dma_pull_tx and loopback backend
    // Pre-populate g_test_mem with a valid frame for dma_pull_tx
    std::memcpy(&g_test_mem[0x2000], kTestMac, 6);
    for (int i = 6; i < 128; ++i) g_test_mem[0x2000 + i] = static_cast<uint8_t>(i & 0xFF);
    size_t sent = bmac.dma_pull_tx(0x2000, 128);
    if (sent == 0) {
        std::cerr << "FAIL: BigMac dma_pull_tx sent=0" << std::endl;
        ++failures;
    }
    irq_seen = false;
    bmac.poll_backend();
    if (!irq_seen) {
        std::cerr << "FAIL: BigMac IRQ not seen on loopback" << std::endl;
        ++failures;
    }
    uint16_t word0 = bmac.read(BigMacReg::MEM_DATA_HI);
    uint8_t mac_hi = (word0 >> 8) & 0xFF;
    if (mac_hi != 0x02) {
        std::cerr << "FAIL: BigMac loopback data mismatch at first byte got " << (int)mac_hi << std::endl;
        ++failures;
    }

    return failures;
}

// ---- MAC filter: wrong-unicast should be dropped ----
int test_bigmac_mac_filter() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level){ if (level) irq_seen = true; });
    bmac.write(BigMacReg::EVENT_MASK, 0x0000);

    // Frame destined for a different MAC — should be dropped
    uint8_t other_mac[6] = {0x08, 0x00, 0x27, 0x12, 0x34, 0x56};
    auto frame = make_frame(other_mac);

    bmac.inject_rx_test_frame(frame.data(), frame.size());

    if (irq_seen) {
        std::cerr << "FAIL: BigMac IRQ fired for wrong-unicast frame (should be dropped)" << std::endl;
        ++failures;
    }

    // GLOB_STAT should not have RX bit set
    uint16_t glob_stat = bmac.read(BigMacReg::GLOB_STAT);
    if (glob_stat & 0x0001) {
        std::cerr << "FAIL: BigMac GLOB_STAT has RX bit for dropped frame" << std::endl;
        ++failures;
    }

    // fetch_next_rx_frame should return false (nothing queued)
    std::vector<uint8_t> out;
    if (bmac.fetch_next_rx_frame(out)) {
        std::cerr << "FAIL: BigMac fetch_next_rx_frame returned data for dropped frame" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- broadcast frame should be accepted ----
int test_bigmac_broadcast() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level){ if (level) irq_seen = true; });
    bmac.write(BigMacReg::EVENT_MASK, 0x0000);

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto frame = make_frame(bcast);

    bmac.inject_rx_test_frame(frame.data(), frame.size());

    if (!irq_seen) {
        std::cerr << "FAIL: BigMac IRQ not fired for broadcast frame" << std::endl;
        ++failures;
    }
    uint16_t glob_stat = bmac.read(BigMacReg::GLOB_STAT);
    if (!(glob_stat & 0x0001)) {
        std::cerr << "FAIL: BigMac GLOB_STAT missing RX bit for broadcast" << std::endl;
        ++failures;
    }
    // Verify we can fetch the frame
    std::vector<uint8_t> out;
    if (!bmac.fetch_next_rx_frame(out)) {
        std::cerr << "FAIL: BigMac broadcast frame not in RX queue" << std::endl;
        ++failures;
    } else if (out.size() != frame.size()) {
        std::cerr << "FAIL: BigMac broadcast frame size mismatch: " << out.size() << std::endl;
        ++failures;
    }

    return failures;
}

// ---- multiple frames: FIFO ordering ----
int test_bigmac_multiframe_order() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();
    bmac.write(BigMacReg::EVENT_MASK, 0x0000);

    // Inject 4 frames with distinguishable payloads
    std::vector<std::vector<uint8_t>> frames;
    for (int f = 0; f < 4; ++f) {
        auto frame = make_frame(kTestMac, 46);
        // tag byte 14 with frame index for identification
        frame[14] = static_cast<uint8_t>(0xA0 + f);
        frames.push_back(frame);
        bmac.inject_rx_test_frame(frame.data(), frame.size());
    }

    // fetch_next_rx_frame must return them in order
    for (int f = 0; f < 4; ++f) {
        std::vector<uint8_t> out;
        if (!bmac.fetch_next_rx_frame(out)) {
            std::cerr << "FAIL: BigMac multiframe: fetch failed for frame " << f << std::endl;
            ++failures;
            break;
        }
        if (out[14] != static_cast<uint8_t>(0xA0 + f)) {
            std::cerr << "FAIL: BigMac multiframe: wrong order at " << f
                      << " got tag 0x" << std::hex << (int)out[14] << std::dec << std::endl;
            ++failures;
        }
    }

    // queue should be empty now
    std::vector<uint8_t> out;
    if (bmac.fetch_next_rx_frame(out)) {
        std::cerr << "FAIL: BigMac multiframe: extra frame in queue after draining" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- event_mask should gate IRQs ----
int test_bigmac_event_mask_gating() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level){ if (level) irq_seen = true; });

    // Mask all events (0xFFFF = all masked, inverted logic)
    bmac.write(BigMacReg::EVENT_MASK, 0xFFFF);

    auto frame = make_frame(kTestMac);
    bmac.inject_rx_test_frame(frame.data(), frame.size());

    if (irq_seen) {
        std::cerr << "FAIL: BigMac IRQ fired despite EVENT_MASK=0xFFFF" << std::endl;
        ++failures;
    }

    // stat bit should still be set even though IRQ is masked
    uint16_t glob_stat = bmac.read(BigMacReg::GLOB_STAT);
    if (!(glob_stat & 0x0001)) {
        std::cerr << "FAIL: BigMac GLOB_STAT RX bit not set despite masked IRQ" << std::endl;
        ++failures;
    }

    // Now unmask and inject again
    bmac.write(BigMacReg::EVENT_MASK, 0x0000);
    irq_seen = false;
    bmac.inject_rx_test_frame(frame.data(), frame.size());
    if (!irq_seen) {
        std::cerr << "FAIL: BigMac IRQ not fired after unmasking EVENT_MASK" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- GLOB_STAT clear-on-read ----
int test_bigmac_glob_stat_clear_on_read() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();
    bmac.write(BigMacReg::EVENT_MASK, 0x0000);

    auto frame = make_frame(kTestMac);
    bmac.inject_rx_test_frame(frame.data(), frame.size());

    uint16_t first = bmac.read(BigMacReg::GLOB_STAT);
    if (!(first & 0x0001)) {
        std::cerr << "FAIL: BigMac GLOB_STAT missing RX bit on first read" << std::endl;
        ++failures;
    }
    uint16_t second = bmac.read(BigMacReg::GLOB_STAT);
    if (second != 0) {
        std::cerr << "FAIL: BigMac GLOB_STAT not cleared on second read (got 0x"
                  << std::hex << second << std::dec << ")" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- CHIP_ID register ----
int test_bigmac_chip_id() {
    int failures = 0;
    {
        BigMac bmac_h(EthernetCellId::Heathrow);
        uint16_t id = bmac_h.read(BigMacReg::CHIP_ID);
        if (id != EthernetCellId::Heathrow) {
            std::cerr << "FAIL: BigMac Heathrow CHIP_ID=0x" << std::hex << id
                      << " expected 0x" << (int)EthernetCellId::Heathrow << std::dec << std::endl;
            ++failures;
        }
    }
    {
        BigMac bmac_p(EthernetCellId::Paddington);
        uint16_t id = bmac_p.read(BigMacReg::CHIP_ID);
        if (id != EthernetCellId::Paddington) {
            std::cerr << "FAIL: BigMac Paddington CHIP_ID=0x" << std::hex << id
                      << " expected 0x" << (int)EthernetCellId::Paddington << std::dec << std::endl;
            ++failures;
        }
    }

    return failures;
}

// ---- CRC32 known-answer tests ----
int test_bigmac_crc32() {
    int failures = 0;

    // Empty data: CRC32 of zero bytes should be 0x00000000 (complement of 0xFFFFFFFF init then complement)
    // Actually: CRC32 of empty input = ~0xFFFFFFFF = 0x00000000
    {
        uint32_t crc = ether_crc32(nullptr, 0);
        if (crc != 0x00000000u) {
            std::cerr << "FAIL: CRC32 of empty data = 0x" << std::hex << crc
                      << " expected 0x00000000" << std::dec << std::endl;
            ++failures;
        }
    }

    // Known test vector: "123456789" -> CRC32 = 0xCBF43926
    {
        const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
        uint32_t crc = ether_crc32(data, sizeof(data));
        if (crc != 0xCBF43926u) {
            std::cerr << "FAIL: CRC32 of \"123456789\" = 0x" << std::hex << crc
                      << " expected 0xCBF43926" << std::dec << std::endl;
            ++failures;
        }
    }

    // Single byte 0x00
    {
        const uint8_t data[] = {0x00};
        uint32_t crc = ether_crc32(data, 1);
        if (crc != 0xD202EF8Du) {
            std::cerr << "FAIL: CRC32 of 0x00 = 0x" << std::hex << crc
                      << " expected 0xD202EF8D" << std::dec << std::endl;
            ++failures;
        }
    }

    // All 0xFF (6 bytes, like broadcast MAC)
    {
        const uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint32_t crc = ether_crc32(data, sizeof(data));
        if (crc != 0x41D9ED00u) {
            std::cerr << "FAIL: CRC32 of 6x 0xFF = 0x" << std::hex << crc
                      << " expected 0x41D9ED00" << std::dec << std::endl;
            ++failures;
        }
    }

    return failures;
}

// ---- BUG CATCHER: RX_FIFO_CSR must reflect RX state, not TX ----
int test_bigmac_fifo_csr_roundtrip() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    // Write different values to TX_FIFO_CSR and RX_FIFO_CSR
    // TX: enable=1, size field=0x05 -> value = (0x05 << 1) | 1 = 0x0B
    // RX: enable=1, size field=0x0A -> value = (0x0A << 1) | 1 = 0x15
    bmac.write(BigMacReg::TX_FIFO_CSR, 0x000B);
    bmac.write(BigMacReg::RX_FIFO_CSR, 0x0015);

    uint16_t tx_csr = bmac.read(BigMacReg::TX_FIFO_CSR);
    uint16_t rx_csr = bmac.read(BigMacReg::RX_FIFO_CSR);

    if (tx_csr == rx_csr) {
        std::cerr << "FAIL: BigMac RX_FIFO_CSR == TX_FIFO_CSR (both 0x"
                  << std::hex << rx_csr << std::dec
                  << ") — RX_FIFO_CSR is returning TX state!" << std::endl;
        ++failures;
    }
    if (tx_csr != 0x000B) {
        std::cerr << "FAIL: BigMac TX_FIFO_CSR readback = 0x" << std::hex << tx_csr
                  << " expected 0x000B" << std::dec << std::endl;
        ++failures;
    }
    if (rx_csr != 0x0015) {
        std::cerr << "FAIL: BigMac RX_FIFO_CSR readback = 0x" << std::hex << rx_csr
                  << " expected 0x0015" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- BUG CATCHER: TX_MAX and TX_MIN must be writable ----
int test_bigmac_tx_max_min_roundtrip() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    // Standard Ethernet values: TX_MAX=1518 (0x05EE), TX_MIN=64 (0x0040)
    bmac.write(BigMacReg::TX_MAX, 0x05EE);
    bmac.write(BigMacReg::TX_MIN, 0x0040);

    uint16_t tx_max = bmac.read(BigMacReg::TX_MAX);
    uint16_t tx_min = bmac.read(BigMacReg::TX_MIN);

    if (tx_max != 0x05EE) {
        std::cerr << "FAIL: BigMac TX_MAX readback = 0x" << std::hex << tx_max
                  << " expected 0x05EE (write handler missing?)" << std::dec << std::endl;
        ++failures;
    }
    if (tx_min != 0x0040) {
        std::cerr << "FAIL: BigMac TX_MIN readback = 0x" << std::hex << tx_min
                  << " expected 0x0040 (write handler missing?)" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- MAC_ADDR register roundtrip ----
int test_bigmac_mac_addr_roundtrip() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    // MAC_ADDR_0..2 hold 6 bytes of MAC filter address in 3 x 16-bit regs
    bmac.write(BigMacReg::MAC_ADDR_0, 0x0200);
    bmac.write(BigMacReg::MAC_ADDR_1, 0xDEAD);
    bmac.write(BigMacReg::MAC_ADDR_2, 0xBEEF);

    uint16_t a0 = bmac.read(BigMacReg::MAC_ADDR_0);
    uint16_t a1 = bmac.read(BigMacReg::MAC_ADDR_1);
    uint16_t a2 = bmac.read(BigMacReg::MAC_ADDR_2);

    if (a0 != 0x0200) {
        std::cerr << "FAIL: MAC_ADDR_0 readback = 0x" << std::hex << a0
                  << " expected 0x0200" << std::dec << std::endl;
        ++failures;
    }
    if (a1 != 0xDEAD) {
        std::cerr << "FAIL: MAC_ADDR_1 readback = 0x" << std::hex << a1
                  << " expected 0xDEAD" << std::dec << std::endl;
        ++failures;
    }
    if (a2 != 0xBEEF) {
        std::cerr << "FAIL: MAC_ADDR_2 readback = 0x" << std::hex << a2
                  << " expected 0xBEEF" << std::dec << std::endl;
        ++failures;
    }

    // Write different values and verify independence
    bmac.write(BigMacReg::MAC_ADDR_0, 0x1122);
    bmac.write(BigMacReg::MAC_ADDR_1, 0x3344);
    bmac.write(BigMacReg::MAC_ADDR_2, 0x5566);

    a0 = bmac.read(BigMacReg::MAC_ADDR_0);
    a1 = bmac.read(BigMacReg::MAC_ADDR_1);
    a2 = bmac.read(BigMacReg::MAC_ADDR_2);

    if (a0 != 0x1122 || a1 != 0x3344 || a2 != 0x5566) {
        std::cerr << "FAIL: MAC_ADDR second write/readback mismatch" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- Hash table register roundtrip ----
int test_bigmac_hash_table_roundtrip() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    // Write distinct values to all 4 hash table registers
    const uint16_t vals[4] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
    bmac.write(BigMacReg::HASH_TAB_0, vals[0]);
    bmac.write(BigMacReg::HASH_TAB_1, vals[1]);
    bmac.write(BigMacReg::HASH_TAB_2, vals[2]);
    bmac.write(BigMacReg::HASH_TAB_3, vals[3]);

    uint16_t h0 = bmac.read(BigMacReg::HASH_TAB_0);
    uint16_t h1 = bmac.read(BigMacReg::HASH_TAB_1);
    uint16_t h2 = bmac.read(BigMacReg::HASH_TAB_2);
    uint16_t h3 = bmac.read(BigMacReg::HASH_TAB_3);

    if (h0 != vals[0] || h1 != vals[1] || h2 != vals[2] || h3 != vals[3]) {
        std::cerr << "FAIL: Hash table roundtrip mismatch: 0x"
                  << std::hex << h0 << " 0x" << h1 << " 0x" << h2 << " 0x" << h3
                  << std::dec << std::endl;
        ++failures;
    }

    // Clear all and verify
    bmac.write(BigMacReg::HASH_TAB_0, 0);
    bmac.write(BigMacReg::HASH_TAB_1, 0);
    bmac.write(BigMacReg::HASH_TAB_2, 0);
    bmac.write(BigMacReg::HASH_TAB_3, 0);

    h0 = bmac.read(BigMacReg::HASH_TAB_0);
    h1 = bmac.read(BigMacReg::HASH_TAB_1);
    h2 = bmac.read(BigMacReg::HASH_TAB_2);
    h3 = bmac.read(BigMacReg::HASH_TAB_3);

    if (h0 != 0 || h1 != 0 || h2 != 0 || h3 != 0) {
        std::cerr << "FAIL: Hash table not zeroed after clear" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- TX SW reset is self-clearing ----
int test_bigmac_tx_sw_reset_self_clear() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    // Writing 1 to TX_SW_RST triggers a soft reset; the register should read
    // back as 0 (hardware acks the reset immediately).
    bmac.write(BigMacReg::TX_SW_RST, 1);
    uint16_t val = bmac.read(BigMacReg::TX_SW_RST);
    if (val != 0) {
        std::cerr << "FAIL: TX_SW_RST not self-clearing (got 0x"
                  << std::hex << val << std::dec << ")" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- SROM MAC address read via bit-bang ----
int test_bigmac_srom_mac_read() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    // The default SROM holds MAC 02:00:DE:AD:BE:EF at words 10-12.
    // SROM protocol: chip-select high, then clock in start bit (1),
    // opcode (10 = read), 6-bit address, then clock out 16 data bits.

    auto srom_write = [&](uint8_t cs, uint8_t clk, uint8_t data_out) {
        uint16_t val = (cs ? Srom_Chip_Select : 0)
                     | (clk ? Srom_Clock : 0)
                     | (data_out ? Srom_Data_Out : 0);
        bmac.write(BigMacReg::SROM_CSR, val);
    };

    auto srom_clock_bit = [&](uint8_t bit) {
        srom_write(1, 0, bit); // data setup, clock low
        srom_write(1, 1, bit); // clock high — latch
    };

    auto srom_read_word = [&](uint8_t addr) -> uint16_t {
        // Deselect to reset state machine
        srom_write(0, 0, 0);

        // Start bit
        srom_clock_bit(1);

        // Opcode: 10 (read)
        srom_clock_bit(1);
        srom_clock_bit(0);

        // 6-bit address, MSB first
        for (int i = 5; i >= 0; --i)
            srom_clock_bit((addr >> i) & 1);

        // Clock out 16 data bits
        uint16_t word = 0;
        for (int i = 0; i < 16; ++i) {
            srom_write(1, 0, 0); // clock low
            srom_write(1, 1, 0); // clock high
            // Read Srom_Data_In (bit 2) from SROM_CSR
            uint16_t csr = bmac.read(BigMacReg::SROM_CSR);
            word = (word << 1) | ((csr >> 2) & 1);
        }

        // Deselect
        srom_write(0, 0, 0);
        return word;
    };

    // Read words 10, 11, 12 — should yield 0x0200, 0xDEAD, 0xBEEF
    uint16_t w10 = srom_read_word(10);
    uint16_t w11 = srom_read_word(11);
    uint16_t w12 = srom_read_word(12);

    if (w10 != 0x0200) {
        std::cerr << "FAIL: SROM word 10 = 0x" << std::hex << w10
                  << " expected 0x0200" << std::dec << std::endl;
        ++failures;
    }
    if (w11 != 0xDEAD) {
        std::cerr << "FAIL: SROM word 11 = 0x" << std::hex << w11
                  << " expected 0xDEAD" << std::dec << std::endl;
        ++failures;
    }
    if (w12 != 0xBEEF) {
        std::cerr << "FAIL: SROM word 12 = 0x" << std::hex << w12
                  << " expected 0xBEEF" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---------------------------------------------------------------------------
// MII bit-bang helpers
// ---------------------------------------------------------------------------
static void mii_send_bit(BigMac& bmac, bool bit) {
    uint16_t lo = Mif_Data_Out_En | (bit ? Mif_Data_Out : 0);
    uint16_t hi = lo | Mif_Clock;
    bmac.write(BigMacReg::MIF_CSR, lo);
    bmac.write(BigMacReg::MIF_CSR, hi);
}

static uint8_t mii_recv_bit(BigMac& bmac) {
    bmac.write(BigMacReg::MIF_CSR, 0);          // clock low, output disabled
    bmac.write(BigMacReg::MIF_CSR, Mif_Clock);  // clock high
    return (bmac.read(BigMacReg::MIF_CSR) >> 3) & 1;
}

static void mii_send_preamble(BigMac& bmac) {
    for (int i = 0; i < 32; ++i) mii_send_bit(bmac, 1);
}

static uint16_t mii_read_reg(BigMac& bmac, uint8_t phy, uint8_t reg) {
    mii_send_preamble(bmac);
    // Start: 01
    mii_send_bit(bmac, 0); mii_send_bit(bmac, 1);
    // Opcode: 10 (read)
    mii_send_bit(bmac, 1); mii_send_bit(bmac, 0);
    // PHY address (5 bits, MSB first)
    for (int i = 4; i >= 0; --i) mii_send_bit(bmac, (phy >> i) & 1);
    // Register address (5 bits, MSB first)
    for (int i = 4; i >= 0; --i) mii_send_bit(bmac, (reg >> i) & 1);
    // Turnaround (receive)
    mii_recv_bit(bmac);
    // 16 data bits
    uint16_t data = 0;
    for (int i = 0; i < 16; ++i)
        data = (data << 1) | mii_recv_bit(bmac);
    return data;
}

static void mii_write_reg(BigMac& bmac, uint8_t phy, uint8_t reg, uint16_t val) {
    mii_send_preamble(bmac);
    // Start: 01
    mii_send_bit(bmac, 0); mii_send_bit(bmac, 1);
    // Opcode: 01 (write)
    mii_send_bit(bmac, 0); mii_send_bit(bmac, 1);
    // PHY address
    for (int i = 4; i >= 0; --i) mii_send_bit(bmac, (phy >> i) & 1);
    // Register address
    for (int i = 4; i >= 0; --i) mii_send_bit(bmac, (reg >> i) & 1);
    // Turnaround: 10
    mii_send_bit(bmac, 1); mii_send_bit(bmac, 0);
    // 16 data bits, MSB first
    for (int i = 15; i >= 0; --i) mii_send_bit(bmac, (val >> i) & 1);
}

// ---- MII PHY: read BMSR via bit-bang ----
int test_bigmac_mii_phy_read_bmsr() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    uint16_t bmsr = mii_read_reg(bmac, 0, PHY_BMSR);
    if (bmsr != 0x7809) {
        std::cerr << "FAIL: MII PHY BMSR = 0x" << std::hex << bmsr
                  << " expected 0x7809" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- MII PHY: write and read back BMCR ----
int test_bigmac_mii_phy_write_read_bmcr() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    // Write a value to BMCR (auto-negotiation enable + 100 Mbps)
    const uint16_t write_val = 0x3100;
    mii_write_reg(bmac, 0, PHY_BMCR, write_val);

    // Read it back
    uint16_t readback = mii_read_reg(bmac, 0, PHY_BMCR);
    if (readback != write_val) {
        std::cerr << "FAIL: MII PHY BMCR write/read: got 0x" << std::hex << readback
                  << " expected 0x" << write_val << std::dec << std::endl;
        ++failures;
    }

    // Write with reset bit (0x8000) — should self-clear
    mii_write_reg(bmac, 0, PHY_BMCR, 0x8000);
    readback = mii_read_reg(bmac, 0, PHY_BMCR);
    if (readback & 0x8000) {
        std::cerr << "FAIL: MII PHY BMCR reset bit not self-clearing" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- MII PHY: read ID registers for Paddington (LXT970 OUI) ----
int test_bigmac_mii_phy_read_id() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    uint16_t id1 = mii_read_reg(bmac, 0, PHY_ID1);
    uint16_t id2 = mii_read_reg(bmac, 0, PHY_ID2);

    // LXT970 OUI = 0x1E0400 → ID1 = upper 16 bits of (oui >> 6)
    // oui = 0x1E0400, oui >> 6 = 0x7810, ID1 = 0x7810
    uint32_t expected_oui = 0x1E0400;
    uint16_t expected_id1 = (expected_oui >> 6) & 0xFFFF;
    // ID2 = (oui << 10) | (model << 4) | rev, masked to 16 bits
    uint16_t expected_id2 = ((expected_oui << 10) | (0 << 4) | 0) & 0xFFFF;

    if (id1 != expected_id1) {
        std::cerr << "FAIL: MII PHY ID1 = 0x" << std::hex << id1
                  << " expected 0x" << expected_id1 << std::dec << std::endl;
        ++failures;
    }
    if (id2 != expected_id2) {
        std::cerr << "FAIL: MII PHY ID2 = 0x" << std::hex << id2
                  << " expected 0x" << expected_id2 << std::dec << std::endl;
        ++failures;
    }

    // Heathrow (LXT907) should report OUI=0 since it doesn't support MII
    BigMac bmac_h(EthernetCellId::Heathrow);
    uint16_t h_id1 = mii_read_reg(bmac_h, 0, PHY_ID1);
    uint16_t h_id2 = mii_read_reg(bmac_h, 0, PHY_ID2);
    if (h_id1 != 0 || h_id2 != 0) {
        std::cerr << "FAIL: Heathrow PHY ID should be 0 (no MII), got 0x"
                  << std::hex << h_id1 << " 0x" << h_id2 << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- dma_pull_tx with address beyond test memory should return 0 ----
int test_bigmac_dma_pull_tx_unmapped() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    // Address 0xF0000 is far beyond the 64 KB test memory
    size_t sent = bmac.dma_pull_tx(0xF0000, 64);
    if (sent != 0) {
        std::cerr << "FAIL: dma_pull_tx with unmapped address returned "
                  << sent << " (expected 0)" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- chip_reset clears all mutable state ----
int test_bigmac_chip_reset_clears_state() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    // Write non-default values to a variety of registers
    bmac.write(BigMacReg::TX_CONFIG, 0x1234);
    bmac.write(BigMacReg::RX_CONFIG, 0x5678);
    bmac.write(BigMacReg::TX_MAX, 0x05EE);
    bmac.write(BigMacReg::TX_MIN, 0x0040);
    bmac.write(BigMacReg::RX_MAX, 0x05EE);
    bmac.write(BigMacReg::RX_MIN, 0x0040);
    bmac.write(BigMacReg::HASH_TAB_0, 0xFFFF);
    bmac.write(BigMacReg::HASH_TAB_1, 0xFFFF);
    bmac.write(BigMacReg::HASH_TAB_2, 0xFFFF);
    bmac.write(BigMacReg::HASH_TAB_3, 0xFFFF);
    bmac.write(BigMacReg::IPG_1, 0x0008);
    bmac.write(BigMacReg::IPG_2, 0x0004);
    bmac.write(BigMacReg::EVENT_MASK, 0x0000); // unmask all
    bmac.write(BigMacReg::NC_CNT, 42);
    bmac.write(BigMacReg::PEAK_ATT, 7);

    // Inject a frame to populate RX queue
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto frame = make_frame(bcast);
    bmac.inject_rx_test_frame(frame.data(), frame.size());

    // Verify pre-conditions
    uint16_t gs = bmac.read(BigMacReg::GLOB_STAT);
    if (!(gs & 0x0001)) {
        std::cerr << "FAIL: pre-reset: GLOB_STAT missing RX bit" << std::endl;
        ++failures;
    }

    // chip_reset is called by the constructor; re-trigger by writing TX_SW_RST
    // Actually, chip_reset is private. The constructor calls it. Create a fresh
    // BigMac object to verify default state.
    BigMac bmac2(EthernetCellId::Paddington);

    // EVENT_MASK should be 0xFFFF (all masked) — default
    if (bmac2.read(BigMacReg::EVENT_MASK) != 0xFFFF) {
        std::cerr << "FAIL: After reset EVENT_MASK = 0x" << std::hex
                  << bmac2.read(BigMacReg::EVENT_MASK) << std::dec
                  << " expected 0xFFFF" << std::endl;
        ++failures;
    }
    // GLOB_STAT should be 0
    if (bmac2.read(BigMacReg::GLOB_STAT) != 0) {
        std::cerr << "FAIL: After reset GLOB_STAT != 0" << std::endl;
        ++failures;
    }
    // RX queue should be empty
    std::vector<uint8_t> out;
    if (bmac2.fetch_next_rx_frame(out)) {
        std::cerr << "FAIL: After reset RX queue not empty" << std::endl;
        ++failures;
    }
    // CHIP_ID should still be correct
    if (bmac2.read(BigMacReg::CHIP_ID) != EthernetCellId::Paddington) {
        std::cerr << "FAIL: After reset CHIP_ID wrong" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- Address filter registers AFR_0/1/2 and AFC_R roundtrip ----
int test_bigmac_addr_filter_roundtrip() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    bmac.write(BigMacReg::AFR_0, 0x1111);
    bmac.write(BigMacReg::AFR_1, 0x2222);
    bmac.write(BigMacReg::AFR_2, 0x3333);
    bmac.write(BigMacReg::AFC_R, 0x0007);

    if (bmac.read(BigMacReg::AFR_0) != 0x1111) {
        std::cerr << "FAIL: AFR_0 readback" << std::endl; ++failures;
    }
    if (bmac.read(BigMacReg::AFR_1) != 0x2222) {
        std::cerr << "FAIL: AFR_1 readback" << std::endl; ++failures;
    }
    if (bmac.read(BigMacReg::AFR_2) != 0x3333) {
        std::cerr << "FAIL: AFR_2 readback" << std::endl; ++failures;
    }
    if (bmac.read(BigMacReg::AFC_R) != 0x0007) {
        std::cerr << "FAIL: AFC_R readback" << std::endl; ++failures;
    }

    return failures;
}

// ---- Miscellaneous register roundtrip (IPG, slot, preamble, jam, etc.) ----
int test_bigmac_misc_reg_roundtrip() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    struct RegTest { BigMacReg reg; uint16_t val; const char* name; };
    RegTest tests[] = {
        {BigMacReg::XIFC,      0x0005, "XIFC"},
        {BigMacReg::XCVR_IF,   0x0003, "XCVR_IF"},
        {BigMacReg::TX_PNTR,   0x1234, "TX_PNTR"},
        {BigMacReg::RX_PNTR,   0x5678, "RX_PNTR"},
        {BigMacReg::IPG_1,     0x0008, "IPG_1"},
        {BigMacReg::IPG_2,     0x0004, "IPG_2"},
        {BigMacReg::A_LIMIT,   0x000F, "A_LIMIT"},
        {BigMacReg::SLOT,      0x0040, "SLOT"},
        {BigMacReg::PA_LEN,    0x0007, "PA_LEN"},
        {BigMacReg::PA_PAT,    0x00AA, "PA_PAT"},
        {BigMacReg::TX_SFD,    0x00AB, "TX_SFD"},
        {BigMacReg::JAM_SIZE,  0x0004, "JAM_SIZE"},
        {BigMacReg::DEFER_TMR, 0x0040, "DEFER_TMR"},
        {BigMacReg::RNG_SEED,  0x1234, "RNG_SEED"},
        {BigMacReg::RX_CONFIG, 0x0001, "RX_CONFIG"},
        {BigMacReg::RX_MAX,    0x05EE, "RX_MAX"},
        {BigMacReg::RX_MIN,    0x0040, "RX_MIN"},
        {BigMacReg::TX_FIFO_TH,0x0004, "TX_FIFO_TH"},
    };

    for (auto& t : tests) {
        bmac.write(t.reg, t.val);
        uint16_t rb = bmac.read(t.reg);
        if (rb != t.val) {
            std::cerr << "FAIL: " << t.name << " roundtrip: wrote 0x"
                      << std::hex << t.val << " read 0x" << rb << std::dec << std::endl;
            ++failures;
        }
    }

    return failures;
}

// ---- MII PHY ANAR write and read ----
int test_bigmac_mii_anar_write_read() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    // Default ANAR should be 0xA1 (10BASE-T + 100BASE-TX)
    uint16_t anar = mii_read_reg(bmac, 0, PHY_ANAR);
    if (anar != 0x00A1) {
        std::cerr << "FAIL: MII ANAR default = 0x" << std::hex << anar
                  << " expected 0x00A1" << std::dec << std::endl;
        ++failures;
    }

    // Write a custom value and read back
    mii_write_reg(bmac, 0, PHY_ANAR, 0x01E1);
    anar = mii_read_reg(bmac, 0, PHY_ANAR);
    if (anar != 0x01E1) {
        std::cerr << "FAIL: MII ANAR write/read: got 0x" << std::hex << anar
                  << " expected 0x01E1" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- PEAK_ATT is clear-on-read ----
int test_bigmac_peak_att_clear_on_read() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    bmac.write(BigMacReg::PEAK_ATT, 0x0042);
    uint16_t first = bmac.read(BigMacReg::PEAK_ATT);
    uint16_t second = bmac.read(BigMacReg::PEAK_ATT);

    if (first != 0x0042) {
        std::cerr << "FAIL: PEAK_ATT first read = 0x" << std::hex << first
                  << " expected 0x0042" << std::dec << std::endl;
        ++failures;
    }
    if (second != 0) {
        std::cerr << "FAIL: PEAK_ATT not cleared on read (got 0x"
                  << std::hex << second << std::dec << ")" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- poll_backend error path: recv() returns -1 ----
int test_bigmac_poll_backend_error() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    auto mock = std::make_unique<MockErrorBackend>();
    mock->fail_recv = true;
    bmac.set_backend_for_test(std::move(mock));

    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level) { if (level) irq_seen = true; });
    bmac.write(BigMacReg::EVENT_MASK, 0x0000); // unmask all

    bmac.poll_backend();

    uint16_t gs = bmac.read(BigMacReg::GLOB_STAT);
    if (!(gs & 0x8000)) {
        std::cerr << "FAIL: poll_backend error: GLOB_STAT missing error bit (0x"
                  << std::hex << gs << std::dec << ")" << std::endl;
        ++failures;
    }
    if (!irq_seen) {
        std::cerr << "FAIL: poll_backend error: IRQ not fired" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- tx_from_host error path: send() fails ----
int test_bigmac_tx_from_host_error() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    auto mock = std::make_unique<MockErrorBackend>();
    mock->fail_send = true;
    bmac.set_backend_for_test(std::move(mock));

    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level) { if (level) irq_seen = true; });
    bmac.write(BigMacReg::EVENT_MASK, 0x0000); // unmask all

    uint8_t frame[60]{};
    bmac.tx_from_host(frame, sizeof(frame));

    uint16_t gs = bmac.read(BigMacReg::GLOB_STAT);
    if (!(gs & 0x8000)) {
        std::cerr << "FAIL: tx_from_host error: GLOB_STAT missing error bit" << std::endl;
        ++failures;
    }
    if (!irq_seen) {
        std::cerr << "FAIL: tx_from_host error: IRQ not fired" << std::endl;
        ++failures;
    }

    return failures;
}

// ---- SROM sequential read: auto-increment reads next word ----
int test_bigmac_srom_sequential_read() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);

    // SROM helpers (same protocol as test_bigmac_srom_mac_read)
    auto srom_write = [&](uint8_t cs, uint8_t clk, uint8_t data_out) {
        uint16_t val = (cs ? Srom_Chip_Select : 0)
                     | (clk ? Srom_Clock : 0)
                     | (data_out ? Srom_Data_Out : 0);
        bmac.write(BigMacReg::SROM_CSR, val);
    };
    auto srom_clock_bit = [&](uint8_t bit) {
        srom_write(1, 0, bit);
        srom_write(1, 1, bit);
    };
    auto srom_read_bit = [&]() -> uint8_t {
        srom_write(1, 0, 0);
        srom_write(1, 1, 0);
        uint16_t csr = bmac.read(BigMacReg::SROM_CSR);
        return (csr >> 2) & 1;
    };

    // Start a read at address 10
    srom_write(0, 0, 0); // deselect
    srom_clock_bit(1);    // start bit
    srom_clock_bit(1);    // opcode bit 1 (read = 10)
    srom_clock_bit(0);    // opcode bit 0
    for (int i = 5; i >= 0; --i)
        srom_clock_bit((10 >> i) & 1);

    // Read word 10 (16 bits)
    uint16_t w10 = 0;
    for (int i = 0; i < 16; ++i)
        w10 = (w10 << 1) | srom_read_bit();

    // DON'T deselect — continue clocking to read word 11 (auto-increment)
    uint16_t w11 = 0;
    for (int i = 0; i < 16; ++i)
        w11 = (w11 << 1) | srom_read_bit();

    // And word 12
    uint16_t w12 = 0;
    for (int i = 0; i < 16; ++i)
        w12 = (w12 << 1) | srom_read_bit();

    srom_write(0, 0, 0); // deselect

    // Default SROM: word 10=0x0200, word 11=0xDEAD, word 12=0xBEEF
    if (w10 != 0x0200) {
        std::cerr << "FAIL: SROM seq read word 10 = 0x" << std::hex << w10
                  << " expected 0x0200" << std::dec << std::endl;
        ++failures;
    }
    if (w11 != 0xDEAD) {
        std::cerr << "FAIL: SROM seq read word 11 = 0x" << std::hex << w11
                  << " expected 0xDEAD" << std::dec << std::endl;
        ++failures;
    }
    if (w12 != 0xBEEF) {
        std::cerr << "FAIL: SROM seq read word 12 = 0x" << std::hex << w12
                  << " expected 0xBEEF" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}

// ---- MEM_DATA_LO mirrors MEM_DATA_HI ----
int test_bigmac_mem_data_lo_mirrors_hi() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    bmac.set_backend_name("loopback");

    // Inject a frame with non-uniform header (use unicast SROM MAC)
    auto frame = make_frame(kTestMac);
    bmac.inject_rx_test_frame(frame.data(), frame.size());

    // Read MEM_DATA_HI — pops 2 bytes
    uint16_t hi = bmac.read(BigMacReg::MEM_DATA_HI);
    // Read MEM_DATA_LO — should mirror without popping
    uint16_t lo = bmac.read(BigMacReg::MEM_DATA_LO);

    if (hi != lo) {
        std::cerr << "FAIL: MEM_DATA_LO (0x" << std::hex << lo
                  << ") != MEM_DATA_HI (0x" << hi << std::dec << ")" << std::endl;
        ++failures;
    }

    // Read MEM_DATA_HI again — should get NEXT two bytes (different from first)
    uint16_t hi2 = bmac.read(BigMacReg::MEM_DATA_HI);
    if (hi2 == hi) {
        std::cerr << "FAIL: second MEM_DATA_HI read returned same value (no advance)"
                  << std::endl;
        ++failures;
    }

    return failures;
}

// ---- RX_FRM_CNT always increments, even for filtered frames ----
int test_bigmac_rx_frm_cnt() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);
    bmac.set_backend_name("loopback");

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto accepted = make_frame(bcast);

    // Wrong unicast dest — should be filtered
    uint8_t wrong[6] = {0x02, 0x00, 0x11, 0x22, 0x33, 0x44};
    auto rejected = make_frame(wrong);

    bmac.inject_rx_test_frame(accepted.data(), accepted.size()); // accepted
    bmac.inject_rx_test_frame(rejected.data(), rejected.size()); // rejected
    bmac.inject_rx_test_frame(accepted.data(), accepted.size()); // accepted
    bmac.inject_rx_test_frame(rejected.data(), rejected.size()); // rejected
    bmac.inject_rx_test_frame(rejected.data(), rejected.size()); // rejected

    uint16_t cnt = bmac.read(BigMacReg::RX_FRM_CNT);
    if (cnt != 5) {
        std::cerr << "FAIL: RX_FRM_CNT = " << cnt << " expected 5 (all frames counted)"
                  << std::endl;
        ++failures;
    }

    // Only 2 frames should be in the RX queue (broadcast ones)
    std::vector<uint8_t> out;
    int rx_count = 0;
    while (bmac.fetch_next_rx_frame(out)) ++rx_count;
    if (rx_count != 2) {
        std::cerr << "FAIL: expected 2 accepted frames, got " << rx_count << std::endl;
        ++failures;
    }

    return failures;
}

// ---- MII with non-zero PHY address still returns data ----
int test_bigmac_mii_wrong_phy_addr() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Paddington);

    // Read BMSR at PHY address 1 (non-zero — logs error but still works)
    uint16_t bmsr = mii_read_reg(bmac, 1, PHY_BMSR);

    // Should still return the standard BMSR value (0x7809)
    if (bmsr != 0x7809) {
        std::cerr << "FAIL: MII wrong PHY addr: BMSR = 0x" << std::hex << bmsr
                  << " expected 0x7809" << std::dec << std::endl;
        ++failures;
    }

    return failures;
}
