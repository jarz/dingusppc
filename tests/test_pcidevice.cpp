/**
 * Unit tests for PCI config space read/write logic and BAR sizing.
 *
 * Tests the real bit-packing in PCIBase::pci_cfg_read/write and PCIDevice,
 * BAR masking in set_bar_value, and BAR type detection in finish_config_bars.
 */

#include <devices/common/pci/pcidevice.h>
#include <endianswap.h>
#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>

static int total  = 0;
static int failed = 0;

#define CHECK_EQ(expr, expected)                                            \
    do {                                                                    \
        ++total;                                                            \
        auto _v = (expr);                                                   \
        auto _e = (expected);                                               \
        if (_v != _e) {                                                     \
            ++failed;                                                       \
            std::printf("FAIL %s:%d: %s == 0x%llx (expected 0x%llx)\n",    \
                        __FILE__, __LINE__, #expr,                          \
                        (unsigned long long)_v, (unsigned long long)_e);    \
        }                                                                   \
    } while (0)

#define CHECK_TRUE(expr)                                                    \
    do {                                                                    \
        ++total;                                                            \
        if (!(expr)) {                                                      \
            ++failed;                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
        }                                                                   \
    } while (0)

/**
 * Test subclass that exposes protected members for testing.
 * This lets us set up vendor/device IDs, BARs, etc. directly.
 */
class TestPCIDevice : public PCIDevice {
public:
    TestPCIDevice() : PCIDevice("TestDevice") {}

    void set_ids(uint16_t vendor, uint16_t device) {
        this->vendor_id = vendor;
        this->device_id = device;
    }

    void set_class_rev(uint32_t cr) {
        this->class_rev = cr;
    }

    void set_subsys(uint16_t vndr, uint16_t id) {
        this->subsys_vndr = vndr;
        this->subsys_id   = id;
    }

    void set_irq(uint8_t pin, uint8_t line) {
        this->irq_pin  = pin;
        this->irq_line = line;
    }

    void set_latency(uint8_t ml, uint8_t mg) {
        this->max_lat = ml;
        this->min_gnt = mg;
    }

    void configure_bars(std::vector<BarConfig> cfg) {
        this->setup_bars(cfg);
    }

    uint32_t get_bar(int n) const { return this->bars[n]; }

    PCIBarType get_bar_type(int n) const { return this->bars_typ[n]; }

    bool io_space_supported() const { return this->has_io_space; }

    void set_status_bits(uint16_t s) { this->status = s; }

    uint16_t get_status() const { return this->status; }

    uint16_t get_command() const { return this->command; }

    void set_command_cfg(uint16_t c) { this->command_cfg = c; }
};

// -------------------------------------------------------------------
// Test: config space register read bit-packing
// -------------------------------------------------------------------
static int test_cfg_read_packing() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // DEV_ID register: (device_id << 16) | vendor_id
    dev.set_ids(0x106B, 0x0003);
    uint32_t val = dev.pci_cfg_read(PCI_CFG_DEV_ID, d);
    CHECK_EQ(val, (uint32_t)((0x0003 << 16) | 0x106B));

    // CLASS_REV register
    dev.set_class_rev(0x06000034);
    val = dev.pci_cfg_read(PCI_CFG_CLASS_REV, d);
    CHECK_EQ(val, (uint32_t)0x06000034);

    // SUBSYS_ID register: (subsys_id << 16) | subsys_vndr
    dev.set_subsys(0x1234, 0x5678);
    val = dev.pci_cfg_read(PCI_CFG_SUBSYS_ID, d);
    CHECK_EQ(val, (uint32_t)((0x5678 << 16) | 0x1234));

    // DWORD_15: (max_lat << 24) | (min_gnt << 16) | (irq_pin << 8) | irq_line
    dev.set_latency(0xAA, 0xBB);
    dev.set_irq(0x01, 0x0A);
    val = dev.pci_cfg_read(PCI_CFG_DWORD_15, d);
    CHECK_EQ(val, (uint32_t)((0xAA << 24) | (0xBB << 16) | (0x01 << 8) | 0x0A));

    return 0;
}

// -------------------------------------------------------------------
// Test: status register clear-on-write-1 semantics
// -------------------------------------------------------------------
static int test_status_clear_on_write_1() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // Status bits 15:9 and 8 (RW1C as per PCI spec mask 0b1111100100000000 = 0xF900)
    dev.set_status_bits(0xF900);

    // Read back status|command
    uint32_t val = dev.pci_cfg_read(PCI_CFG_STAT_CMD, d);
    CHECK_EQ((val >> 16) & 0xFFFF, (uint32_t)0xF900);

    // Write 1s to clear bits 15 and 14 of status (write 0xC000 in upper 16 bits)
    dev.pci_cfg_write(PCI_CFG_STAT_CMD, 0xC0000000, d);
    CHECK_EQ(dev.get_status(), (uint16_t)0x3900); // bits 15,14 cleared

    // Write 1s to clear remaining bits
    dev.pci_cfg_write(PCI_CFG_STAT_CMD, 0x39000000, d);
    CHECK_EQ(dev.get_status(), (uint16_t)0x0000);

    return 0;
}

// -------------------------------------------------------------------
// Test: command register write masking
// -------------------------------------------------------------------
static int test_command_write_masking() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // Default command_cfg disables bits 3 (special cycles) and 7 (stepping)
    // command_cfg = 0xffff - (1<<3) - (1<<7) = 0xFF77

    // Writing all-ones to command should be masked by command_cfg
    dev.pci_cfg_write(PCI_CFG_STAT_CMD, 0x0000FFFF, d);
    uint16_t cmd = dev.get_command();
    CHECK_EQ(cmd & (1 << 3), (uint16_t)0); // bit 3 masked off
    CHECK_EQ(cmd & (1 << 7), (uint16_t)0); // bit 7 masked off
    CHECK_TRUE((cmd & 0xFF77) != 0);        // other bits set

    // With custom command_cfg allowing only bit 1 (Memory Space)
    dev.set_command_cfg(0x0002);
    dev.pci_cfg_write(PCI_CFG_STAT_CMD, 0x0000FFFF, d);
    CHECK_EQ(dev.get_command(), (uint16_t)0x0002);

    return 0;
}

// -------------------------------------------------------------------
// Test: latency timer and cache line size write/readback
// -------------------------------------------------------------------
static int test_lat_timer_cache_lnsz() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // DWORD_3: (bist << 24) | (hdr_type << 16) | (lat_timer << 8) | cache_ln_sz
    // Initial read - should have hdr_type = PCI_HEADER_TYPE_0 (0) in bits [23:16]
    uint32_t val = dev.pci_cfg_read(PCI_CFG_DWORD_3, d);
    CHECK_EQ(val & 0xFF, (uint32_t)0); // cache_ln_sz initially 0
    CHECK_EQ((val >> 8) & 0xFF, (uint32_t)0); // lat_timer initially 0

    // Write lat_timer = 0x40, cache_ln_sz = 0x08
    dev.pci_cfg_write(PCI_CFG_DWORD_3, 0x00004008, d);
    val = dev.pci_cfg_read(PCI_CFG_DWORD_3, d);
    CHECK_EQ(val & 0xFF, (uint32_t)0x08);
    CHECK_EQ((val >> 8) & 0xFF, (uint32_t)0x40);

    return 0;
}

// -------------------------------------------------------------------
// Test: BAR sizing for 32-bit memory BAR
// -------------------------------------------------------------------
static int test_bar_sizing_mem32() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // Configure BAR0 as 1MB 32-bit memory BAR
    // Config: size mask = ~(1MB - 1) = 0xFFF00000, type bits [3:1] = 0b000 (32-bit), bit 0 = 0 (mem)
    dev.configure_bars({{0, 0xFFF00000}});

    // Verify BAR type detected correctly
    CHECK_EQ((int)dev.get_bar_type(0), (int)PCIBarType::Mem_32_Bit);

    // BAR sizing: write all 1s, read back reveals size
    dev.pci_cfg_write(PCI_CFG_BAR0, 0xFFFFFFFF, d);
    uint32_t bar_val = dev.pci_cfg_read(PCI_CFG_BAR0, d);
    // For mem BAR: (0xFFFFFFFF & 0xFFF00000 & ~0xF) | (0xFFF00000 & 0xF) = 0xFFF00000
    CHECK_EQ(bar_val, (uint32_t)0xFFF00000);

    // Write an actual address (e.g. 0x80000000)
    dev.pci_cfg_write(PCI_CFG_BAR0, 0x80000000, d);
    bar_val = dev.pci_cfg_read(PCI_CFG_BAR0, d);
    CHECK_EQ(bar_val, (uint32_t)0x80000000);

    // Write misaligned address - should be truncated to BAR alignment
    dev.pci_cfg_write(PCI_CFG_BAR0, 0x80012345, d);
    bar_val = dev.pci_cfg_read(PCI_CFG_BAR0, d);
    CHECK_EQ(bar_val, (uint32_t)0x80000000); // aligned to 1MB

    return 0;
}

// -------------------------------------------------------------------
// Test: BAR sizing for IO BAR (16-bit)
// -------------------------------------------------------------------
static int test_bar_sizing_io16() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // Configure BAR0 as 256-byte IO BAR (16-bit)
    // Size mask for 256 bytes: ~(256-1) = 0xFFFFFF00 in lower 16 bits = 0x0000FF00
    // bit 0 = 1 (IO), remaining low bits are the mask
    uint32_t io_cfg = 0x0000FF01; // 256-byte IO, 16-bit (no upper 16 bits set)
    dev.configure_bars({{0, io_cfg}});

    CHECK_EQ((int)dev.get_bar_type(0), (int)PCIBarType::Io_16_Bit);
    CHECK_TRUE(dev.io_space_supported());

    // BAR sizing
    dev.pci_cfg_write(PCI_CFG_BAR0, 0xFFFFFFFF, d);
    uint32_t bar_val = dev.pci_cfg_read(PCI_CFG_BAR0, d);
    // For IO BAR: (0xFFFFFFFF & 0x0000FF01 & ~3) | (0x0000FF01 & 3) = 0x0000FF00 | 0x01 = 0x0000FF01
    CHECK_EQ(bar_val, (uint32_t)0x0000FF01);

    return 0;
}

// -------------------------------------------------------------------
// Test: BAR sizing for IO BAR (32-bit)
// -------------------------------------------------------------------
static int test_bar_sizing_io32() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // IO BAR with upper 16 bits set → 32-bit IO
    uint32_t io_cfg = 0xFFFF0001; // 64KB IO, 32-bit (upper bits set)
    dev.configure_bars({{0, io_cfg}});

    CHECK_EQ((int)dev.get_bar_type(0), (int)PCIBarType::Io_32_Bit);
    CHECK_TRUE(dev.io_space_supported());

    return 0;
}

// -------------------------------------------------------------------
// Test: BAR sizing for 64-bit memory BAR
// -------------------------------------------------------------------
static int test_bar_sizing_mem64() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // 64-bit memory BAR: bits[2:1] = 0b10 (type 2 = 64-bit)
    // BAR0 (low): size mask with type bits = 0xFFF00004
    // BAR1 (high): upper 32-bit mask = 0xFFFFFFFF
    dev.configure_bars({
        {0, 0xFFF00004}, // 64-bit mem lo, 1MB
        {1, 0xFFFFFFFF}, // 64-bit mem hi
    });

    CHECK_EQ((int)dev.get_bar_type(0), (int)PCIBarType::Mem_64_Bit_Lo);
    CHECK_EQ((int)dev.get_bar_type(1), (int)PCIBarType::Mem_64_Bit_Hi);

    return 0;
}

// -------------------------------------------------------------------
// Test: 20-bit memory BAR type detection
// -------------------------------------------------------------------
static int test_bar_type_mem20() {
    TestPCIDevice dev;

    // 20-bit memory BAR: bits[2:1] = 0b01 (type 1 = <1MB)
    dev.configure_bars({{0, 0xFFF00002}});

    CHECK_EQ((int)dev.get_bar_type(0), (int)PCIBarType::Mem_20_Bit);

    return 0;
}

// -------------------------------------------------------------------
// Test: multiple BARs configured simultaneously
// -------------------------------------------------------------------
static int test_multiple_bars() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    // BAR0: 4KB mem32, BAR1: unused (0), BAR2: 256B IO
    dev.configure_bars({
        {0, 0xFFFFF000}, // 4KB memory
        {2, 0x0000FF01}, // 256B IO
    });

    CHECK_EQ((int)dev.get_bar_type(0), (int)PCIBarType::Mem_32_Bit);
    CHECK_EQ((int)dev.get_bar_type(1), (int)PCIBarType::Unused);
    CHECK_EQ((int)dev.get_bar_type(2), (int)PCIBarType::Io_16_Bit);

    // Size BAR0
    dev.pci_cfg_write(PCI_CFG_BAR0, 0xFFFFFFFF, d);
    CHECK_EQ(dev.pci_cfg_read(PCI_CFG_BAR0, d), (uint32_t)0xFFFFF000);

    // Size BAR2
    dev.pci_cfg_write(PCI_CFG_BAR2, 0xFFFFFFFF, d);
    CHECK_EQ(dev.pci_cfg_read(PCI_CFG_BAR2, d), (uint32_t)0x0000FF01);

    // BAR1 is unused — should read 0
    CHECK_EQ(dev.pci_cfg_read(PCI_CFG_BAR1, d), (uint32_t)0);

    return 0;
}

// -------------------------------------------------------------------
// Test: IRQ line write via DWORD_15
// -------------------------------------------------------------------
static int test_irq_line_write() {
    TestPCIDevice dev;
    AccessDetails d{};
    d.size = 4;
    d.offset = 0;
    d.flags = 0;

    dev.set_irq(0x01, 0x00);
    dev.set_latency(0x00, 0x00);

    // Write DWORD_15 - irq_line is extracted from bits [31:24] of written value
    dev.pci_cfg_write(PCI_CFG_DWORD_15, 0x0B000000, d);

    uint32_t val = dev.pci_cfg_read(PCI_CFG_DWORD_15, d);
    // irq_line should now be 0x0B, irq_pin stays 0x01
    CHECK_EQ(val & 0xFF, (uint32_t)0x0B);         // irq_line
    CHECK_EQ((val >> 8) & 0xFF, (uint32_t)0x01);  // irq_pin unchanged

    return 0;
}

int main() {
    test_cfg_read_packing();
    test_status_clear_on_write_1();
    test_command_write_masking();
    test_lat_timer_cache_lnsz();
    test_bar_sizing_mem32();
    test_bar_sizing_io16();
    test_bar_sizing_io32();
    test_bar_sizing_mem64();
    test_bar_type_mem20();
    test_multiple_bars();
    test_irq_line_write();

    std::printf("%d / %d checks passed\n", total - failed, total);

    return failed ? 1 : 0;
}
