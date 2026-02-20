/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team

Networking subsystem test driver.
*/

#include <iostream>

// EtherBackend tests
extern "C" int test_ether_backends();

// MACE tests
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

// BigMac tests
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

// DBDMA + BigMac integration tests
extern "C" int test_dbdma_tx_loopback();
extern "C" int test_dbdma_rx_to_memory();
extern "C" int test_dbdma_multi_desc_tx();
extern "C" int test_dbdma_interrupt_delivery();
extern "C" int test_dbdma_full_roundtrip();

// DBDMA engine tests
extern "C" int test_dbdma_nop_command();
extern "C" int test_dbdma_branch();
extern "C" int test_dbdma_store_quad();
extern "C" int test_dbdma_load_quad();
extern "C" int test_dbdma_channel_abort();
extern "C" int test_dbdma_pause_prevents_start();
extern "C" int test_dbdma_multi_desc_rx();
extern "C" int test_dbdma_multi_interrupt();
extern "C" int test_dbdma_conditional_branch();
extern "C" int test_dbdma_wake_from_pause();
extern "C" int test_dbdma_dead_on_unknown_cmd();
extern "C" int test_dbdma_flush_callback();
extern "C" int test_dbdma_wait_select_roundtrip();

// DBDMA + MACE integration tests
extern "C" int test_mace_dbdma_tx_loopback();
extern "C" int test_mace_dbdma_rx_to_memory();

extern void init_timer_manager();

static int run_test(const char* name, int(*fn)()) {
    int f = fn();
    if (f)
        std::cout << "  " << name << ": " << f << " FAILURES" << std::endl;
    else
        std::cout << "  " << name << ": PASS" << std::endl;
    return f;
}

int main() {
    std::cout << "Running DingusPPC networking tests..." << std::endl;

    init_timer_manager();

    int failures = 0;

    failures += run_test("EtherBackend unit tests",        test_ether_backends);
    failures += run_test("MACE loopback",                  test_mace_loopback_basic);
    failures += run_test("MACE Phys_Addr readback",        test_mace_phys_addr_readback);
    failures += run_test("MACE broadcast accept",          test_mace_broadcast);
    failures += run_test("MACE Chip_ID readback",          test_mace_chip_id);
    failures += run_test("MACE BIU software reset",        test_mace_biu_reset);
    failures += run_test("MACE multi-frame order",         test_mace_multiframe_order);
    failures += run_test("MACE Log_Addr_Flt",              test_mace_log_addr_flt);
    failures += run_test("MACE dma_pull_tx unmapped",      test_mace_dma_pull_tx_unmapped);
    failures += run_test("MACE IAC mutual exclusion",      test_mace_iac_mutual_exclusion);
    failures += run_test("MACE TX interrupt status",       test_mace_tx_interrupt_status);
    failures += run_test("MACE missed packet count",       test_mace_missed_pkt_count);
    failures += run_test("MACE promisc accepts all",       test_mace_promisc_accepts_all);
    failures += run_test("MACE poll backend error",        test_mace_poll_backend_error);
    failures += run_test("MACE TX error",                  test_mace_tx_error);
    failures += run_test("MACE FIFO_Config roundtrip",     test_mace_fifo_config_roundtrip);
    failures += run_test("MACE MAC_Config_Ctrl roundtrip", test_mace_mac_config_ctrl_roundtrip);
    failures += run_test("MACE PLS_Config_Ctrl roundtrip", test_mace_pls_config_ctrl_roundtrip);
    failures += run_test("BigMac loopback",                test_bigmac_loopback_basic);
    failures += run_test("BigMac MAC filter reject",       test_bigmac_mac_filter);
    failures += run_test("BigMac broadcast accept",        test_bigmac_broadcast);
    failures += run_test("BigMac multi-frame order",       test_bigmac_multiframe_order);
    failures += run_test("BigMac EVENT_MASK gating",       test_bigmac_event_mask_gating);
    failures += run_test("BigMac GLOB_STAT clear-on-read", test_bigmac_glob_stat_clear_on_read);
    failures += run_test("BigMac CHIP_ID",                 test_bigmac_chip_id);
    failures += run_test("Ethernet CRC32",                 test_bigmac_crc32);
    failures += run_test("BigMac FIFO_CSR roundtrip",      test_bigmac_fifo_csr_roundtrip);
    failures += run_test("BigMac TX_MAX/MIN roundtrip",    test_bigmac_tx_max_min_roundtrip);
    failures += run_test("BigMac MAC_ADDR roundtrip",      test_bigmac_mac_addr_roundtrip);
    failures += run_test("BigMac hash table roundtrip",    test_bigmac_hash_table_roundtrip);
    failures += run_test("BigMac TX_SW_RST self-clear",    test_bigmac_tx_sw_reset_self_clear);
    failures += run_test("BigMac SROM MAC read",           test_bigmac_srom_mac_read);
    failures += run_test("BigMac MII PHY read BMSR",       test_bigmac_mii_phy_read_bmsr);
    failures += run_test("BigMac MII PHY write/read BMCR", test_bigmac_mii_phy_write_read_bmcr);
    failures += run_test("BigMac MII PHY read ID",         test_bigmac_mii_phy_read_id);
    failures += run_test("BigMac dma_pull_tx unmapped",    test_bigmac_dma_pull_tx_unmapped);
    failures += run_test("BigMac chip_reset clears state", test_bigmac_chip_reset_clears_state);
    failures += run_test("BigMac addr filter roundtrip",   test_bigmac_addr_filter_roundtrip);
    failures += run_test("BigMac misc reg roundtrip",      test_bigmac_misc_reg_roundtrip);
    failures += run_test("BigMac MII PHY ANAR write/read", test_bigmac_mii_anar_write_read);
    failures += run_test("BigMac PEAK_ATT clear-on-read",  test_bigmac_peak_att_clear_on_read);
    failures += run_test("BigMac poll backend error",      test_bigmac_poll_backend_error);
    failures += run_test("BigMac TX from host error",      test_bigmac_tx_from_host_error);
    failures += run_test("BigMac SROM sequential read",    test_bigmac_srom_sequential_read);
    failures += run_test("BigMac MEM_DATA_LO mirrors HI",  test_bigmac_mem_data_lo_mirrors_hi);
    failures += run_test("BigMac RX_FRM_CNT",              test_bigmac_rx_frm_cnt);
    failures += run_test("BigMac MII wrong PHY addr",      test_bigmac_mii_wrong_phy_addr);

    // DBDMA + BigMac integration
    failures += run_test("DBDMA TX loopback",              test_dbdma_tx_loopback);
    failures += run_test("DBDMA RX to memory",             test_dbdma_rx_to_memory);
    failures += run_test("DBDMA multi-desc TX",            test_dbdma_multi_desc_tx);
    failures += run_test("DBDMA interrupt delivery",       test_dbdma_interrupt_delivery);
    failures += run_test("DBDMA full round-trip",          test_dbdma_full_roundtrip);

    // DBDMA engine
    failures += run_test("DBDMA NOP command",              test_dbdma_nop_command);
    failures += run_test("DBDMA branch",                   test_dbdma_branch);
    failures += run_test("DBDMA STORE_QUAD",               test_dbdma_store_quad);
    failures += run_test("DBDMA LOAD_QUAD",                test_dbdma_load_quad);
    failures += run_test("DBDMA channel abort",            test_dbdma_channel_abort);
    failures += run_test("DBDMA pause prevents start",     test_dbdma_pause_prevents_start);
    failures += run_test("DBDMA multi-desc RX",            test_dbdma_multi_desc_rx);
    failures += run_test("DBDMA multi-interrupt",          test_dbdma_multi_interrupt);
    failures += run_test("DBDMA conditional branch",       test_dbdma_conditional_branch);
    failures += run_test("DBDMA wake from pause",          test_dbdma_wake_from_pause);
    failures += run_test("DBDMA dead on unknown cmd",      test_dbdma_dead_on_unknown_cmd);
    failures += run_test("DBDMA flush callback",           test_dbdma_flush_callback);
    failures += run_test("DBDMA wait_select roundtrip",    test_dbdma_wait_select_roundtrip);

    // DBDMA + MACE integration
    failures += run_test("MACE DBDMA TX loopback",         test_mace_dbdma_tx_loopback);
    failures += run_test("MACE DBDMA RX to memory",        test_mace_dbdma_rx_to_memory);

    std::cout << std::endl << "=== Summary ===" << std::endl;
    std::cout << "Networking test failures: " << failures << std::endl;

    return failures > 0 ? 1 : 0;
}
