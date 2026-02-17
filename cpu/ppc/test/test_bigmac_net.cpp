#include <devices/ethernet/bigmac.h>
#include <cpu/ppc/ppcmmu.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <array>

// test stub for mmu_map_dma_mem
static auto bigmac_mmu_stub = [](uint32_t addr, uint32_t size, bool allow_mmio) -> MapDmaResult {
    static std::array<uint8_t, 2048> buf{};
    (void)addr; (void)allow_mmio;
    MapDmaResult res{};
    res.type = 0; res.is_writable = true; res.host_va = buf.data();
    // Default MAC prefix
    uint8_t mac_bytes[6] = {0x02,0x00,0xDE,0xAD,0xBE,0xEF};
    for (int i = 0; i < 6 && i < (int)size; ++i) buf[i] = mac_bytes[i];
    for (uint32_t i = 6; i < size && i < buf.size(); ++i) buf[i] = static_cast<uint8_t>(i & 0xFF);
    return res;
};

extern "C" int test_bigmac_loopback_basic();

int test_bigmac_loopback_basic() {
    int failures = 0;
    BigMac bmac(EthernetCellId::Heathrow);
    BigMac::disable_timer_for_tests(true);
    BigMac::set_mmu_map_dma_hook(+bigmac_mmu_stub);
    bmac.set_backend_name("loopback");
    bmac.device_postinit();

    bool irq_seen = false;
    bmac.set_irq_callback([&](bool level){ if (level) irq_seen = true; });

    // Enable events (mask bit 0 low enables RX)
    bmac.write(BigMacReg::EVENT_MASK, 0x0000);

    // Inject a frame destined for our default MAC
    uint8_t frame[64]{};
    uint8_t mac_bytes[6] = {0x02,0x00,0xDE,0xAD,0xBE,0xEF};
    std::memcpy(frame, mac_bytes, 6);
    frame[6] = 0x00; frame[7] = 0x11; frame[8] = 0x22; frame[9] = 0x33; frame[10] = 0x44; frame[11] = 0x55;
    frame[12] = 0x08; frame[13] = 0x00;
    for (int i = 14; i < 60; ++i) frame[i] = static_cast<uint8_t>(i);

    bmac.inject_rx_test_frame(frame, 60);
    uint16_t glob_stat = bmac.read(BigMacReg::GLOB_STAT);
    if ((glob_stat & 0x0001) == 0) {
        std::cerr << "FAIL: BigMac GLOB_STAT missing RX bit" << std::endl;
        ++failures;
    }
    // FIFO count via rxq size (exposed via MEM_DATA_HI consumption). We can also peek via event.
    // Drain via MEM_DATA_HI register (two bytes per read)
    for (int i = 0; i < 30; ++i) {
        uint16_t word = bmac.read(BigMacReg::MEM_DATA_HI);
        uint8_t hi = (word >> 8) & 0xFF;
        uint8_t lo = word & 0xFF;
        int idx_hi = i * 2;
        int idx_lo = idx_hi + 1;
        if (idx_hi < 60 && hi != frame[idx_hi]) {
            std::cerr << "FAIL: BigMac MEM_DATA_HI byte mismatch hi idx=" << idx_hi << " got " << (int)hi << std::endl;
            ++failures;
            break;
        }
        if (idx_lo < 60 && lo != frame[idx_lo]) {
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
    size_t sent = bmac.dma_pull_tx(0x2000, 128);
    if (sent == 0) {
        std::cerr << "FAIL: BigMac dma_pull_tx sent=0" << std::endl;
        ++failures;
    }
    // Force poll to collect loopback frame
    irq_seen = false;
    bmac.poll_backend();
    if (!irq_seen) {
        std::cerr << "FAIL: BigMac IRQ not seen on loopback" << std::endl;
        ++failures;
    }
    // Drain a few bytes to confirm pattern
    uint16_t word0 = bmac.read(BigMacReg::MEM_DATA_HI);
    uint8_t mac_hi = (word0 >> 8) & 0xFF;
    if (mac_hi != 0x02) {
        std::cerr << "FAIL: BigMac loopback data mismatch at first byte got " << (int)mac_hi << std::endl;
        ++failures;
    }

    return failures;
}
