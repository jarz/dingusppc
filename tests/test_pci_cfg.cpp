/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for PCI configuration-space helpers.

Tests cover:
  - pci_conv_rd_data: byte extraction, aligned words, cross-dword word
    (offset 3), and the non-trivial dword-at-offset-1 shift path.
    All expected values are hardcoded (derived independently of the code).
  - pci_conv_wr_data: byte insertion, dword aligned write, write/read
    roundtrip that validates the two functions are mutual inverses.
  - PCIDevice command register: the command_cfg masking logic that
    prevents writes to reserved bits (special-cycles bit 3, stepping bit 7).
  - PCIDevice DWORD_3: lat_timer and cache_ln_sz persist through the
    write/read hook pair, and are stored independently (not aliased).
  - PCIDevice DWORD_15: irq_line is stored in bits [31:24].
  - PCIBase BAR sizing: the mask-on-write logic clips addresses to the
    configured alignment, and the standard all-ones sizing probe returns
    the mask.

Trivially-true cases (store vendor_id, read it back; BYTESWAP is its own
inverse; etc.) are omitted.

No ROMs, SDL, or real hardware required.
*/

#include <devices/common/pci/pcidevice.h>
#include <devices/common/pci/pcihost.h>
#include <endianswap.h>

#include <cstdint>
#include <iostream>
#include <vector>

using namespace std;

static int tests_run    = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << endl; \
        tests_failed++; \
    } \
} while(0)

// Build an AccessDetails without relying on aggregate initialisation order.
static AccessDetails AD(uint8_t size, uint8_t offset, uint8_t flags = PCI_CONFIG_READ) {
    AccessDetails d;
    d.size   = size;
    d.offset = offset;
    d.flags  = flags;
    return d;
}

// ============================================================
// pci_conv_rd_data
//
// Source dword (LE in config space): 0x12345678
//   byte 0 = 0x78, byte 1 = 0x56, byte 2 = 0x34, byte 3 = 0x12
// "Next" dword (for cross-boundary accesses): 0xAABBCCDD
//   byte 0 = 0xDD, byte 1 = 0xCC, byte 2 = 0xBB, byte 3 = 0xAA
// ============================================================

static void test_rd_bytes_decode_each_byte_independently() {
    // Verifies that each byte lane is extracted into the right position.
    // Expected values are independent of the implementation formula.
    uint32_t v = 0x12345678UL, v2 = 0xAABBCCDDUL;

    AccessDetails a0 = AD(1,0), a1 = AD(1,1), a2 = AD(1,2), a3 = AD(1,3);
    TEST_ASSERT(pci_conv_rd_data(v, v2, a0) == 0x78U, "rd byte 0 = 0x78");
    TEST_ASSERT(pci_conv_rd_data(v, v2, a1) == 0x56U, "rd byte 1 = 0x56");
    TEST_ASSERT(pci_conv_rd_data(v, v2, a2) == 0x34U, "rd byte 2 = 0x34");
    TEST_ASSERT(pci_conv_rd_data(v, v2, a3) == 0x12U, "rd byte 3 = 0x12");
}

static void test_rd_words_byteswapped_to_big_endian() {
    // Aligned word at offset 0: LE bytes 0-1 are 0x78,0x56 → host reads 0x7856 (BE).
    // Aligned word at offset 2: LE bytes 2-3 are 0x34,0x12 → host reads 0x3412 (BE).
    uint32_t v = 0x12345678UL, v2 = 0;

    AccessDetails a0 = AD(2,0), a2 = AD(2,2);
    TEST_ASSERT(pci_conv_rd_data(v, v2, a0) == 0x7856U, "rd word offset 0 = 0x7856");
    TEST_ASSERT(pci_conv_rd_data(v, v2, a2) == 0x3412U, "rd word offset 2 = 0x3412");
}

static void test_rd_word_offset3_spans_two_dwords() {
    // The word at offset 3 is the only case that spans a dword boundary:
    // byte 3 of v (=0x12) and byte 0 of v2 (=0xDD).
    // Formula: ((v>>16)&0xFF00) | (v2&0xFF) = 0x1200|0xDD = 0x12DD.
    uint32_t v = 0x12345678UL, v2 = 0xAABBCCDDUL;
    AccessDetails a3 = AD(2,3);
    TEST_ASSERT(pci_conv_rd_data(v, v2, a3) == 0x12DDU,
        "rd word offset 3 spans boundary: byte3(v)=0x12, byte0(v2)=0xDD → 0x12DD");
}

static void test_rd_dword_offset0_is_byteswap() {
    // Stored as LE 0x12345678; read as BE 0x78563412.
    uint32_t v = 0x12345678UL;
    AccessDetails a0 = AD(4,0);
    TEST_ASSERT(pci_conv_rd_data(v, 0, a0) == 0x78563412UL,
        "rd dword offset 0: LE 0x12345678 reads as BE 0x78563412");
}

static void test_rd_dword_offset1_shift_and_byteswap() {
    // Bytes 1..4: v[1]=0x56, v[2]=0x34, v[3]=0x12, v2[0]=0xDD.
    // As a LE dword: 0xDD123456; byteswapped (BE): 0x563412DD.
    // This specifically exercises the shift-before-byteswap path.
    uint32_t v = 0x12345678UL, v2 = 0xAABBCCDDUL;
    AccessDetails a1 = AD(4,1);
    TEST_ASSERT(pci_conv_rd_data(v, v2, a1) == 0x563412DDUL,
        "rd dword offset 1: bytes[1..4] LE 0xDD123456 → BE 0x563412DD");
}

static void test_rd_invalid_size_returns_all_ones() {
    uint32_t v = 0x12345678UL, v2 = 0;
    AccessDetails a = AD(3, 0); // size=3 is not a valid PCI access size
    TEST_ASSERT(pci_conv_rd_data(v, v2, a) == 0xFFFFFFFFUL,
        "rd invalid access size returns 0xFFFFFFFF (PCI convention)");
}

// ============================================================
// pci_conv_wr_data
// ============================================================

static void test_wr_bytes_insert_into_correct_lane() {
    // Each byte-size write should replace exactly one byte lane of the
    // existing dword and leave the other three unchanged.
    const uint32_t base = 0xFFFFFFFFUL, val = 0xABU;

    AccessDetails a0 = AD(1,0,PCI_CONFIG_WRITE);
    AccessDetails a1 = AD(1,1,PCI_CONFIG_WRITE);
    AccessDetails a2 = AD(1,2,PCI_CONFIG_WRITE);
    AccessDetails a3 = AD(1,3,PCI_CONFIG_WRITE);

    TEST_ASSERT(pci_conv_wr_data(base, val, a0) == 0xFFFFFFABUL, "wr byte lane 0");
    TEST_ASSERT(pci_conv_wr_data(base, val, a1) == 0xFFFFABFFUL, "wr byte lane 1");
    TEST_ASSERT(pci_conv_wr_data(base, val, a2) == 0xFFABFFFFUL, "wr byte lane 2");
    TEST_ASSERT(pci_conv_wr_data(base, val, a3) == 0xABFFFFFFUL, "wr byte lane 3");
}

static void test_wr_dword_write_read_roundtrip() {
    // A host dword value written via pci_conv_wr_data and then read back via
    // pci_conv_rd_data must equal the original value.  This validates that the
    // two functions are proper inverses and that neither silently corrupts data.
    const uint32_t original = 0xDEADBEEFUL;
    AccessDetails wr = AD(4, 0, PCI_CONFIG_WRITE);
    AccessDetails rd = AD(4, 0, PCI_CONFIG_READ);

    uint32_t stored  = pci_conv_wr_data(0, original, wr);
    uint32_t readval = pci_conv_rd_data(stored, 0, rd);
    TEST_ASSERT(readval == original, "wr/rd dword roundtrip: value survives write then read");
}

static void test_wr_invalid_size_returns_all_ones() {
    AccessDetails a = AD(3, 0, PCI_CONFIG_WRITE);
    TEST_ASSERT(pci_conv_wr_data(0xFFFFFFFFUL, 0xABUL, a) == 0xFFFFFFFFUL,
        "wr invalid size returns 0xFFFFFFFF (PCI convention)");
}

// ============================================================
// PCIDevice config-space register logic.
//
// Only tests that exercise non-trivial *logic* are included.
// Store-and-load paths (vendor_id, device_id, class_rev, subsys_id)
// are omitted — they are plain struct-field assignments with no processing.
// ============================================================

class MinimalPCIDevice : public PCIDevice {
public:
    MinimalPCIDevice() : PCIDevice("TestPCIDev") {
        this->vendor_id = 0x106BU; // Apple
        this->device_id = 0x0001U;
        this->class_rev = 0x06000000UL;
    }
    void pub_setup_bars(std::vector<BarConfig> cfg) { this->setup_bars(cfg); }
};

static void test_cfg_command_masks_reserved_bits() {
    // command_cfg has bits 3 (special cycles) and 7 (stepping) cleared by
    // default.  Writing 0xFF to the command register must not set those bits.
    MinimalPCIDevice dev;
    AccessDetails wr = AD(4, 0, PCI_CONFIG_WRITE);
    AccessDetails rd = AD(4, 0, PCI_CONFIG_READ);

    dev.pci_cfg_write(PCI_CFG_STAT_CMD, 0x000000FFUL, wr);
    uint16_t cmd = dev.pci_cfg_read(PCI_CFG_STAT_CMD, rd) & 0xFFFFU;

    TEST_ASSERT(!(cmd & (1u << 3)), "command: reserved bit 3 (special cycles) must be masked off");
    TEST_ASSERT(!(cmd & (1u << 7)), "command: reserved bit 7 (stepping) must be masked off");
    // Writable bits 0-2 (I/O, memory, bus-master) must survive.
    TEST_ASSERT((cmd & 0x07U) == 0x07U, "command: writable bits 0-2 survive masking");
}

static void test_cfg_dword3_lat_timer_and_cache_lnsz_are_independent() {
    // DWORD_3 routes bits[15:8] to lat_timer and bits[7:0] to cache_ln_sz
    // through separate write/read hooks.  Two writes with different values
    // confirm the fields are stored independently (not aliased).
    MinimalPCIDevice dev;
    AccessDetails wr = AD(4, 0, PCI_CONFIG_WRITE);
    AccessDetails rd = AD(4, 0, PCI_CONFIG_READ);

    dev.pci_cfg_write(PCI_CFG_DWORD_3, 0x00004008UL, wr); // lat=0x40, cache=0x08
    uint32_t raw = dev.pci_cfg_read(PCI_CFG_DWORD_3, rd);
    TEST_ASSERT(((raw >> 8) & 0xFFU) == 0x40U, "lat_timer = 0x40 after first write");
    TEST_ASSERT((raw & 0xFFU) == 0x08U,        "cache_ln_sz = 0x08 after first write");

    dev.pci_cfg_write(PCI_CFG_DWORD_3, 0x00001000UL, wr); // lat=0x10, cache=0x00
    raw = dev.pci_cfg_read(PCI_CFG_DWORD_3, rd);
    TEST_ASSERT(((raw >> 8) & 0xFFU) == 0x10U, "lat_timer updated to 0x10 without corrupting cache_ln_sz");
    TEST_ASSERT((raw & 0xFFU) == 0x00U,        "cache_ln_sz updated to 0x00 without corrupting lat_timer");
}

static void test_cfg_irq_line_write_bits_31_24_read_bits_7_0() {
    // On write: irq_line = value >> 24  (the OS writes the line in the top byte).
    // On read: returned as the low byte:  result[7:0] = irq_line.
    // This asymmetry is what makes the test non-trivial.
    MinimalPCIDevice dev;
    AccessDetails wr = AD(4, 0, PCI_CONFIG_WRITE);
    AccessDetails rd = AD(4, 0, PCI_CONFIG_READ);

    dev.pci_cfg_write(PCI_CFG_DWORD_15, 0x07000000UL, wr); // write irq_line=7 in bits[31:24]
    TEST_ASSERT((dev.pci_cfg_read(PCI_CFG_DWORD_15, rd) & 0xFFU) == 7,
        "irq_line written from bits[31:24] is read back in bits[7:0]");

    dev.pci_cfg_write(PCI_CFG_DWORD_15, 0x0A000000UL, wr); // update to 10
    TEST_ASSERT((dev.pci_cfg_read(PCI_CFG_DWORD_15, rd) & 0xFFU) == 10,
        "irq_line updated to 10");
}

static void test_cfg_bar_sizing_and_address_masking() {
    // BAR configured as 32-bit memory, 1 MiB aligned (mask = 0xFFF00000).
    // 1) Standard PCI sizing probe (all-ones write) must return the mask.
    // 2) A correctly aligned address is stored verbatim.
    // 3) An address with sub-alignment bits set is rounded down.
    MinimalPCIDevice dev;
    dev.pub_setup_bars({{ 0, 0xFFF00000UL }});

    AccessDetails wr = AD(4, 0, PCI_CONFIG_WRITE);
    AccessDetails rd = AD(4, 0, PCI_CONFIG_READ);

    // Sizing probe.
    dev.pci_cfg_write(PCI_CFG_BAR0, 0xFFFFFFFFUL, wr);
    TEST_ASSERT(dev.pci_cfg_read(PCI_CFG_BAR0, rd) == 0xFFF00000UL,
        "BAR0 sizing probe: 0xFFFFFFFF write returns alignment mask 0xFFF00000");

    // Aligned address.
    dev.pci_cfg_write(PCI_CFG_BAR0, 0x10000000UL, wr);
    TEST_ASSERT(dev.pci_cfg_read(PCI_CFG_BAR0, rd) == 0x10000000UL,
        "BAR0: 1 MiB-aligned address stored correctly");

    // Misaligned address — sub-alignment bits must be stripped.
    dev.pci_cfg_write(PCI_CFG_BAR0, 0x10080000UL, wr); // 0x10080000 & 0xFFF00000 = 0x10000000
    TEST_ASSERT(dev.pci_cfg_read(PCI_CFG_BAR0, rd) == 0x10000000UL,
        "BAR0: misaligned address 0x10080000 is masked to 0x10000000");
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "PCI configuration space tests" << endl;
    cout << "=============================" << endl;

    cout << endl << "pci_conv_rd_data:" << endl;
    test_rd_bytes_decode_each_byte_independently();
    test_rd_words_byteswapped_to_big_endian();
    test_rd_word_offset3_spans_two_dwords();
    test_rd_dword_offset0_is_byteswap();
    test_rd_dword_offset1_shift_and_byteswap();
    test_rd_invalid_size_returns_all_ones();

    cout << endl << "pci_conv_wr_data:" << endl;
    test_wr_bytes_insert_into_correct_lane();
    test_wr_dword_write_read_roundtrip();
    test_wr_invalid_size_returns_all_ones();

    cout << endl << "PCIDevice config-space logic:" << endl;
    test_cfg_command_masks_reserved_bits();
    test_cfg_dword3_lat_timer_and_cache_lnsz_are_independent();
    test_cfg_irq_line_write_bits_31_24_read_bits_7_0();
    test_cfg_bar_sizing_and_address_masking();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;
    return tests_failed ? 1 : 0;
}
