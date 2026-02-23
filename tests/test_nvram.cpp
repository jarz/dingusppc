/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Unit tests for the NVram device.

Tests cover:
  - Fresh NVram initialises all storage to zero when no backing file exists
  - write_byte / read_byte at arbitrary offsets
  - get_of_nvram_offset() returns OF_NVRAM_OFFSET for the canonical 8 KiB
    size and 0 for all other sizes (tests the conditional in the constructor)
  - Save / restore persistence: write data, let the destructor flush to disk,
    reopen on the same file, verify bytes are intact
  - Size-mismatch on reload: if the backing file was written by a different-
    sized NVram, init() must discard it and start with zeroed storage

Trivially-true cases (write 0x42; assert 0x42 equals 0x42) are replaced
by cases that exercise the NVram's boundary conditions and file I/O logic.

The backing file is written to /tmp so no machine data is touched.

No ROMs, SDL, or hardware required.
*/

#include <devices/common/nvram.h>
#include <devices/common/ofnvram.h>   // OF_NVRAM_OFFSET

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

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

static const std::string NVTMP = "/tmp/dppc_test_nvram.bin";

static void cleanup() {
    std::error_code ec;
    std::filesystem::remove(NVTMP, ec);
}

// ============================================================
// Tests
// ============================================================

static void test_fresh_storage_is_zeroed() {
    // When no backing file exists the constructor must zero all bytes.
    cleanup();
    NVram nv(NVTMP, 256);
    bool all_zero = true;
    for (uint32_t i = 0; i < 256; i++)
        if (nv.read_byte(i)) { all_zero = false; break; }
    TEST_ASSERT(all_zero, "fresh NVram: all 256 bytes must be zero");
}

static void test_write_read_at_various_offsets() {
    // Confirms that independent offsets store independent values —
    // i.e. writing to offset N does not corrupt offset M.
    const uint8_t pattern[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    NVram nv(NVTMP, 256);
    for (uint32_t i = 0; i < 8; i++)
        nv.write_byte(i * 32, pattern[i]); // spread across the 256-byte space
    for (uint32_t i = 0; i < 8; i++)
        TEST_ASSERT(nv.read_byte(i * 32) == pattern[i],
            "offset-independent write/read: byte at offset " + to_string(i * 32));
}

static void test_of_nvram_offset_only_set_for_8k() {
    // The constructor sets of_nvram_offset = OF_NVRAM_OFFSET only for the
    // canonical 8 KiB size; other sizes must get 0.
    cleanup(); { NVram nv8k(NVTMP, 8192);
        TEST_ASSERT(nv8k.get_of_nvram_offset() == OF_NVRAM_OFFSET,
            "8 KiB NVram: get_of_nvram_offset() == OF_NVRAM_OFFSET");
    }
    for (uint32_t sz : { 256u, 4096u, 16384u }) {
        cleanup();
        NVram nv(NVTMP, sz);
        TEST_ASSERT(nv.get_of_nvram_offset() == 0,
            "NVram size " + to_string(sz) + ": get_of_nvram_offset() == 0");
    }
}

static void test_persistence_data_survives_reopen() {
    // Write known data, let the destructor flush it, reopen, verify intact.
    cleanup();
    const uint8_t expected[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };
    {
        NVram nv(NVTMP, 256);
        for (uint32_t i = 0; i < 8; i++)
            nv.write_byte(i, expected[i]);
    } // destructor saves here
    {
        NVram nv(NVTMP, 256);
        for (uint32_t i = 0; i < 8; i++)
            TEST_ASSERT(nv.read_byte(i) == expected[i],
                "persistence: byte " + to_string(i) + " survives save/restore");
    }
}

static void test_size_mismatch_discards_stale_file() {
    // A file written by a 512-byte NVram must not be loaded by a 256-byte
    // NVram — init() should detect the size mismatch and zero the storage.
    cleanup();
    { NVram nv(NVTMP, 512); nv.write_byte(0, 0xFF); }
    { NVram nv(NVTMP, 256);
      TEST_ASSERT(nv.read_byte(0) == 0,
          "size-mismatch: 256-byte NVram must discard a 512-byte backing file");
    }
}

// ============================================================
// Entry point
// ============================================================

int main() {
    cout << "NVram device tests" << endl;
    cout << "==================" << endl;

    test_fresh_storage_is_zeroed();
    test_write_read_at_various_offsets();
    test_of_nvram_offset_only_set_for_8k();
    test_persistence_data_survives_reopen();
    test_size_mismatch_discards_stale_file();

    cleanup();

    cout << endl << "Results: " << (tests_run - tests_failed)
         << "/" << tests_run << " passed." << endl;
    return tests_failed ? 1 : 0;
}
