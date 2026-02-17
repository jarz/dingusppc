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

/** @file BigMac Ethernet controller emulation. */

#include <devices/deviceregistry.h>
#include <devices/ethernet/bigmac.h>
#include <core/timermanager.h>
#include <machines/machineproperties.h>
#include <cpu/ppc/ppcmmu.h>
#include <loguru.hpp>

namespace {
constexpr uint64_t kBigMacPollIntervalNs = 10'000'000ull; // 10 ms
constexpr size_t kBigMacMaxFrameLen = 1600;
}

BigMac::BigMac(uint8_t id) {
    set_name("BigMac");
    supports_types(HWCompType::MMIO_DEV | HWCompType::ETHER_MAC);

    this->chip_id = id;
    this->chip_reset();
}

int BigMac::device_postinit() {
    try {
        backend_name = GET_STR_PROP("net_backend");
    } catch (...) {}
    return 0;
}

// Static hooks for tests
BigMac::MmuMapFn BigMac::mmu_map_dma_hook = nullptr;
bool BigMac::disable_timer_for_tests_flag = false;

bool bigmac_mac_accepts(const MacAddr& mac, const uint8_t* dst) {
    // broadcast?
    bool broadcast = true;
    for (int i = 0; i < 6; ++i) broadcast &= (dst[i] == 0xFF);
    if (broadcast) return true;
    // exact match
    for (int i = 0; i < 6; ++i) if (dst[i] != mac.bytes[i]) return false;
    return true;
}

bool BigMac::ensure_backend() {
    std::lock_guard<std::mutex> _{mu_};
    if (backend) return true;
    std::string err;
    backend = make_ether_backend(backend_name);
    if (!backend) return false;
    MacAddr mac = get_mac_from_srom();
    EtherConfig cfg{}; cfg.promisc = true; // BigMac supports promisc; enable for now
    if (!backend->open(mac, cfg, err)) {
        LOG_F(ERROR, "%s: backend open failed: %s", name.c_str(), err.c_str());
        backend.reset();
        return false;
    }
    if (!disable_timer_for_tests_flag) {
        TimerManager::get_instance()->add_cyclic_timer(kBigMacPollIntervalNs, [this]() { this->poll_backend(); });
    }
    return true;
}

void BigMac::enqueue_rx_frame(const uint8_t* buf, size_t len) {
    MacAddr mac = get_mac_from_srom();
    if (!bigmac_mac_accepts(mac, buf)) {
        ++rcv_frame_cnt; // count but drop
        return;
    }
    RxSlot slot;
    slot.data.assign(buf, buf + len);
    slot.cursor = 0;
    {
        std::lock_guard<std::mutex> _{mu_};
        rxq.push_back(std::move(slot));
        if (fifo_fc < 0xFF) ++fifo_fc;
    }
    ++rcv_frame_cnt;
    // set status bit (GLOB_STAT) and trigger interrupt if unmasked (mask is inverted: 0 enables)
    stat |= 0x0001; // bit0 = RX event
    if (!(event_mask & 0x0001) && irq_cb) {
        irq_cb(true);
    }
}

bool BigMac::fetch_next_rx_frame(std::vector<uint8_t>& out_frame) {
    std::lock_guard<std::mutex> _{mu_};
    if (rxq.empty()) return false;
    out_frame = rxq.front().data;
    rxq.pop_front();
    if (fifo_fc) --fifo_fc;
    return true;
}

MacAddr BigMac::get_mac_from_srom() const {
    MacAddr mac{};
    mac.bytes[0] = (srom_data[10] >> 8) & 0xFF; mac.bytes[1] = srom_data[10] & 0xFF;
    mac.bytes[2] = (srom_data[11] >> 8) & 0xFF; mac.bytes[3] = srom_data[11] & 0xFF;
    mac.bytes[4] = (srom_data[12] >> 8) & 0xFF; mac.bytes[5] = srom_data[12] & 0xFF;
    return mac;
}

void BigMac::inject_rx_test_frame(const uint8_t* buf, size_t len) {
    enqueue_rx_frame(buf, len);
}

size_t BigMac::dma_pull_tx(uint32_t addr, size_t len) {
    if (!ensure_backend()) return 0;
    if (!len || len > kBigMacMaxFrameLen) len = kBigMacMaxFrameLen;
    MapDmaResult res{};
    if (mmu_map_dma_hook) {
        res = mmu_map_dma_hook(addr, len, false);
    } else {
        res = mmu_map_dma_mem(addr, len, false);
    }
    if (!res.host_va) return 0;
    tx_from_host(res.host_va, len);
    return len;
}

void BigMac::tx_from_host(const uint8_t* buf, size_t len) {
    if (!ensure_backend()) return;
    std::string err;
    {
        std::lock_guard<std::mutex> _{mu_};
        if (!backend || !backend->send(buf, len, err)) {
            LOG_F(ERROR, "%s: backend send failed: %s", name.c_str(), err.c_str());
            stat |= 0x8000; // error bit
            if (!(event_mask & 0x8000) && irq_cb) irq_cb(true);
        }
    }
}

void BigMac::chip_reset() {
    // According to docs, EVENT_MASK bits are inverted: 0 = enabled, 1 = masked.
    // Hardware typically comes up with interrupts masked. Keep that behavior;
    // tests may explicitly unmask via EVENT_MASK writes.
    this->event_mask = 0xFFFFU;
    this->rng_seed   = (uint16_t)rand();
    this->stat = 0;
    this->fifo_fc = 0;
    std::lock_guard<std::mutex> _{mu_};
    while (!rxq.empty()) rxq.pop_front();
    this->phy_reset();
    this->mii_reset();
    this->srom_reset();
}

uint16_t BigMac::read(uint16_t reg_offset) {
    switch (reg_offset) {
    case BigMacReg::XIFC:
        return this->tx_if_ctrl;
    case BigMacReg::XCVR_IF:
        return this->xcvr_if_ctrl;
    case BigMacReg::CHIP_ID:
        return this->chip_id;
    case BigMacReg::TX_FIFO_TH:
        return this->tx_fifo_tresh;
    case BigMacReg::TX_FIFO_CSR:
        return (this->tx_fifo_enable & 1) | ((((this->tx_fifo_size >> 7) - 1) & 0xFF) << 1);
    case BigMacReg::RX_FIFO_CSR:
        return (this->tx_fifo_enable & 1) | ((((this->tx_fifo_size >> 7) - 1) & 0xFF) << 1);
    case BigMacReg::TX_PNTR:
        return this->tx_ptr;
    case BigMacReg::RX_PNTR:
        return this->rx_ptr;
    case BigMacReg::MIF_CSR:
        return (this->mif_csr_old & ~Mif_Data_In) | (this->mii_in_bit << 3);
    case BigMacReg::GLOB_STAT: {
        uint16_t old_stat = this->stat;
        this->stat        = 0;    // clear-on-read
        return old_stat;
    }
    case BigMacReg::EVENT_MASK:
        return this->event_mask;
    case BigMacReg::SROM_CSR:
        return (this->srom_csr_old & ~Srom_Data_In) | (this->srom_in_bit << 2);
    case BigMacReg::TX_SW_RST:
        return this->tx_reset;
    case BigMacReg::TX_CONFIG:
        return this->tx_config;
    case BigMacReg::TX_MAX:
        return this->tx_max;
    case BigMacReg::TX_MIN:
        return this->tx_min;
    case BigMacReg::PEAK_ATT: {
        uint8_t old_val     = this->peak_attempts;
        this->peak_attempts = 0;    // clear-on-read
        return old_val;
    }
    case BigMacReg::NC_CNT:
        return this->norm_coll_cnt;
    case BigMacReg::NT_CNT:
        return this->net_coll_cnt;
    case BigMacReg::EX_CNT:
        return this->excs_coll_cnt;
    case BigMacReg::LT_CNT:
        return this->late_coll_cnt;
    case BigMacReg::RNG_SEED:
        return this->rng_seed;
    case BigMacReg::RX_FRM_CNT:
        return this->rcv_frame_cnt;
    case BigMacReg::RX_LE_CNT:
        return this->len_err_cnt;
    case BigMacReg::RX_AE_CNT:
        return this->align_err_cnt;
    case BigMacReg::RX_FE_CNT:
        return this->fcs_err_cnt;
    case BigMacReg::RX_CVE_CNT:
        return this->cv_err_cnt;
    case BigMacReg::RX_CONFIG:
        return this->rx_config;
    case BigMacReg::RX_MAX:
        return this->rx_max;
    case BigMacReg::RX_MIN:
        return this->rx_min;
    case BigMacReg::MEM_ADD:
        return this->mem_add;
    case BigMacReg::MEM_DATA_HI: {
        // Simulate FIFO: return two bytes at a time (big endian)
        uint8_t hi = pop_rx_byte();
        uint8_t lo = pop_rx_byte();
        this->mem_data_hi = (hi << 8) | lo;
        this->mem_add += 2; // advance pointer
        return this->mem_data_hi;
    }
    case BigMacReg::MEM_DATA_LO:
        // Lower 16 bits mirror MEM_DATA_HI for now
        return this->mem_data_hi;
    case BigMacReg::IPG_1:
        return this->ipg1;
    case BigMacReg::IPG_2:
        return this->ipg2;
    case BigMacReg::A_LIMIT:
        return this->attempt_limit;
    case BigMacReg::SLOT:
        return this->slot_time;
    case BigMacReg::PA_LEN:
        return this->preamble_len;
    case BigMacReg::PA_PAT:
        return this->preamble_pat;
    case BigMacReg::TX_SFD:
        return this->tx_sfd;
    case BigMacReg::JAM_SIZE:
        return this->jam_size;
    case BigMacReg::DEFER_TMR:
        return this->defer_timer;
    case BigMacReg::MAC_ADDR_0:
    case BigMacReg::MAC_ADDR_1:
    case BigMacReg::MAC_ADDR_2:
        return this->mac_addr_flt[8 - ((reg_offset >> 4) & 0xF)];
    case BigMacReg::HASH_TAB_0:
    case BigMacReg::HASH_TAB_1:
    case BigMacReg::HASH_TAB_2:
    case BigMacReg::HASH_TAB_3:
        return this->hash_table[(reg_offset >> 4) & 3];
    case BigMacReg::AFR_0:
    case BigMacReg::AFR_1:
    case BigMacReg::AFR_2:
        return this->addr_filters[((reg_offset >> 4) & 0xF) - 4];
    case BigMacReg::AFC_R:
        return this->addr_filt_mask;

    default:
        LOG_F(WARNING, "%s: unimplemented register at 0x%X", this->name.c_str(), reg_offset);
    }

    return 0;
}

void BigMac::write(uint16_t reg_offset, uint16_t value) {
    switch (reg_offset) {
    case BigMacReg::XIFC:
        this->tx_if_ctrl = value;
        break;
    case BigMacReg::XCVR_IF:
        this->xcvr_if_ctrl = value;
        break;
    case BigMacReg::TX_FIFO_CSR:
        this->tx_fifo_enable = !!(value & 1);
        this->tx_fifo_size   = (((value >> 1) & 0xFF) + 1) << 7;
        break;
    case BigMacReg::TX_FIFO_TH:
        this->tx_fifo_tresh = value;
        break;
    case BigMacReg::RX_FIFO_CSR:
        this->rx_fifo_enable = !!(value & 1);
        this->rx_fifo_size   = (((value >> 1) & 0xFF) + 1) << 7;
        break;
    case BigMacReg::TX_PNTR:
        this->tx_ptr = value;
        break;
    case BigMacReg::RX_PNTR:
        this->rx_ptr = value;
        break;
    case BigMacReg::MIF_CSR:
        if (value & Mif_Data_Out_En) {
            // send bits one by one on each low-to-high transition of Mif_Clock
            if (((this->mif_csr_old ^ value) & Mif_Clock) && (value & Mif_Clock))
                this->mii_xmit_bit(!!(value & Mif_Data_Out));
        } else {
            if (((this->mif_csr_old ^ value) & Mif_Clock) && (value & Mif_Clock))
                this->mii_rcv_bit();
        }
        this->mif_csr_old = value;
        break;
    case BigMacReg::MEM_ADD:
        this->mem_add = value;
        break;
    case BigMacReg::MEM_DATA_HI:
    case BigMacReg::MEM_DATA_LO:
        // For now, ignore writes to data window
        break;
    case BigMacReg::EVENT_MASK:
        this->event_mask = value;
        break;
    case BigMacReg::SROM_CSR:
        if (value & Srom_Chip_Select) {
            // exchange data on each low-to-high transition of Srom_Clock
            if (((this->srom_csr_old ^ value) & Srom_Clock) && (value & Srom_Clock))
                this->srom_xmit_bit(!!(value & Srom_Data_Out));
        } else {
            this->srom_reset();
        }
        this->srom_csr_old = value;
        break;
    case BigMacReg::TX_SW_RST:
        if (value == 1) {
            LOG_F(INFO, "%s: transceiver soft reset asserted", this->name.c_str());
            this->tx_reset = 0;    // acknowledge SW reset
        }
        break;
    case BigMacReg::TX_CONFIG:
        this->tx_config = value;
        break;
    case BigMacReg::NC_CNT:
        this->norm_coll_cnt = value;
        break;
    case BigMacReg::NT_CNT:
        this->net_coll_cnt = value;
        break;
    case BigMacReg::EX_CNT:
        this->excs_coll_cnt = value;
        break;
    case BigMacReg::LT_CNT:
        this->late_coll_cnt = value;
        break;
    case BigMacReg::RNG_SEED:
        this->rng_seed = value;
        break;
    case BigMacReg::RX_SW_RST:
        if (!value) {
            LOG_F(INFO, "%s: receiver soft reset asserted", this->name.c_str());
        }
        break;
    case BigMacReg::RX_CONFIG:
        this->rx_config = value;
        break;
    case BigMacReg::RX_MAX:
        this->rx_max = value;
        break;
    case BigMacReg::RX_MIN:
        this->rx_min = value;
        break;
    case BigMacReg::SLOT:
        this->slot_time = value;
        break;
    case BigMacReg::PA_LEN:
        this->preamble_len = value;
        break;
    case BigMacReg::PA_PAT:
        this->preamble_pat = value;
        break;
    case BigMacReg::TX_SFD:
        this->tx_sfd = value;
        break;
    case BigMacReg::JAM_SIZE:
        this->jam_size = value;
        break;
    case BigMacReg::PEAK_ATT:
        this->peak_attempts = value;
        break;
    case BigMacReg::DEFER_TMR:
        this->defer_timer = value;
        break;
    case BigMacReg::RX_FRM_CNT:
        this->rcv_frame_cnt = value;
        break;
    case BigMacReg::RX_LE_CNT:
        this->len_err_cnt = value;
        break;
    case BigMacReg::RX_AE_CNT:
        this->align_err_cnt = value;
        break;
    case BigMacReg::RX_FE_CNT:
        this->fcs_err_cnt = value;
        break;
    case BigMacReg::RX_CVE_CNT:
        this->cv_err_cnt = value;
        break;
    case BigMacReg::MAC_ADDR_0:
    case BigMacReg::MAC_ADDR_1:
    case BigMacReg::MAC_ADDR_2:
        this->mac_addr_flt[8 - ((reg_offset >> 4) & 0xF)] = value;
        break;
    case BigMacReg::HASH_TAB_0:
    case BigMacReg::HASH_TAB_1:
    case BigMacReg::HASH_TAB_2:
    case BigMacReg::HASH_TAB_3:
        this->hash_table[(reg_offset >> 4) & 3] = value;
        break;
    case BigMacReg::IPG_1:
        this->ipg1 = value;
        break;
    case BigMacReg::IPG_2:
        this->ipg2 = value;
        break;
    case BigMacReg::A_LIMIT:
        this->attempt_limit = value;
        break;
    case BigMacReg::AFR_0:
    case BigMacReg::AFR_1:
    case BigMacReg::AFR_2:
        this->addr_filters[((reg_offset >> 4) & 0xF) - 4] = value;
        break;
    case BigMacReg::AFC_R:
        this->addr_filt_mask = value;
        break;
    case BigMacReg::CHIP_ID:
    case BigMacReg::TX_SM:
    case BigMacReg::RX_ST_MCHN:
        LOG_F(WARNING, "%s: Attempted write to read-only register at 0x%X with 0x%X",
            this->name.c_str(), reg_offset, value);
        break;
    default:
        LOG_F(WARNING, "%s: unimplemented register at 0x%X is written with 0x%X",
              this->name.c_str(), reg_offset, value);

    }
}

// ================ Media Independent Interface (MII) emulation ================
bool BigMac::mii_rcv_value(uint16_t& var, uint8_t num_bits, uint8_t next_bit) {
    var = (var << 1) | (next_bit & 1);
    this->mii_bit_counter++;
    if (this->mii_bit_counter >= num_bits) {
        this->mii_bit_counter = 0;
        return true; // all bits have been received -> return true
    }
    return false; // more bits expected
}

void BigMac::mii_rcv_bit() {
    switch(this->mii_state) {
    case MII_FRAME_SM::Preamble:
        this->mii_in_bit = 1; // required for OSX
        this->mii_reset();
        break;
    case MII_FRAME_SM::Turnaround:
        this->mii_in_bit = 0;
        this->mii_bit_counter = 16;
        this->mii_state = MII_FRAME_SM::Read_Data;
        break;
    case MII_FRAME_SM::Read_Data:
        if (this->mii_bit_counter) {
            --this->mii_bit_counter;
            this->mii_in_bit = (this->mii_data >> this->mii_bit_counter) & 1;
            if (!this->mii_bit_counter) {
                this->mii_state = MII_FRAME_SM::Preamble;
            }
        } else { // out of sync (shouldn't happen)
            this->mii_reset();
        }
        break;
    case MII_FRAME_SM::Stop:
        this->mii_reset();
        break;
    default:
        LOG_F(ERROR, "%s: unhandled state %d in mii_rcv_bit", this->name.c_str(),
              this->mii_state);
        this->mii_reset();
    }
}

void BigMac::mii_xmit_bit(const uint8_t bit_val) {
    switch(this->mii_state) {
    case MII_FRAME_SM::Preamble:
        if (bit_val) {
            this->mii_bit_counter++;
            if (this->mii_bit_counter >= 32) {
                this->mii_state = MII_FRAME_SM::Start;
                this->mii_in_bit = 1; // checked in OSX
                this->mii_bit_counter = 0;
            }
        } else { // zero bit -> out of sync
            this->mii_reset();
        }
        break;
    case MII_FRAME_SM::Start:
        if (this->mii_rcv_value(this->mii_start, 2, bit_val)) {
            LOG_F(9, "MII_Start=0x%X", this->mii_start);
            this->mii_state = MII_FRAME_SM::Opcode;
        }
        break;
    case MII_FRAME_SM::Opcode:
        if (this->mii_rcv_value(this->mii_opcode, 2, bit_val)) {
            LOG_F(9, "MII_Opcode=0x%X", this->mii_opcode);
            this->mii_state = MII_FRAME_SM::Phy_Address;
        }
        break;
    case MII_FRAME_SM::Phy_Address:
        if (this->mii_rcv_value(this->mii_phy_address, 5, bit_val)) {
            LOG_F(9, "MII_PHY_Address=0x%X", this->mii_phy_address);
            this->mii_state = MII_FRAME_SM::Reg_Address;
        }
        break;
    case MII_FRAME_SM::Reg_Address:
        if (this->mii_rcv_value(this->mii_reg_address, 5, bit_val)) {
            LOG_F(9, "MII_REG_Address=0x%X", this->mii_reg_address);

            if (this->mii_start != 1)
                LOG_F(ERROR, "%s: unsupported frame type %d", this->name.c_str(),
                      this->mii_start);
            if (this->mii_phy_address)
                LOG_F(ERROR, "%s: unsupported PHY address %d", this->name.c_str(),
                      this->mii_phy_address);
            switch (this->mii_opcode) {
            case 1: // write
                this->mii_state = MII_FRAME_SM::Turnaround;
                break;
            case 2: // read
                this->mii_data = this->phy_reg_read(this->mii_reg_address);
                this->mii_state = MII_FRAME_SM::Turnaround;
                break;
            default:
                LOG_F(ERROR, "%s: invalid MII opcode %d", this->name.c_str(),
                      this->mii_opcode);
            }
        }
        break;
    case MII_FRAME_SM::Turnaround:
        if (this->mii_rcv_value(this->mii_turnaround, 2, bit_val)) {
            if (this->mii_turnaround != 2)
                LOG_F(ERROR, "%s: unexpected turnaround 0x%X", this->name.c_str(),
                      this->mii_turnaround);
            this->mii_state = MII_FRAME_SM::Write_Data;
        }
        break;
    case MII_FRAME_SM::Write_Data:
        if (this->mii_rcv_value(this->mii_data, 16, bit_val)) {
            LOG_F(9, "%s: MII data received = 0x%X", this->name.c_str(),
                  this->mii_data);
            this->phy_reg_write(this->mii_reg_address, this->mii_data);
            this->mii_state = MII_FRAME_SM::Stop;
        }
        break;
    case MII_FRAME_SM::Stop:
        if (this->mii_rcv_value(this->mii_stop, 2, bit_val)) {
            LOG_F(9, "MII_Stop=0x%X", this->mii_stop);
            this->mii_reset();
        }
        break;
    default:
        LOG_F(ERROR, "%s: unhandled state %d in mii_xmit_bit", this->name.c_str(),
              this->mii_state);
        this->mii_reset();
    }
}

void BigMac::mii_reset() {
    mii_start = 0;
    mii_opcode = 0;
    mii_phy_address = 0;
    mii_reg_address = 0;
    mii_turnaround = 0;
    mii_data = 0;
    mii_stop = 0;
    this->mii_bit_counter = 0;
    this->mii_state = MII_FRAME_SM::Preamble;
}

// ===================== Ethernet PHY interface emulation =====================
void BigMac::phy_reset() {
    // TODO: add PHY type property to be able to select another PHY (DP83843)
    if (this->chip_id == EthernetCellId::Paddington) {
        this->phy_oui   = 0x1E0400; // LXT970 aka ST10040 PHY
        this->phy_model = 0;
        this->phy_rev   = 0;
    } else { // assume Heathrow with LXT907 PHY
        this->phy_oui   = 0; // LXT907 doesn't support MII, MDIO is pulled low
        this->phy_model = 0;
        this->phy_rev   = 0;
    }
    this->phy_anar = 0xA1; // tell the world we support 10BASE-T and 100BASE-TX
}

uint16_t BigMac::phy_reg_read(uint8_t reg_num) {
    switch(reg_num) {
    case PHY_BMCR:
        return this->phy_bmcr;
    case PHY_BMSR:
        return 0x7809; // value from LXT970 datasheet
    case PHY_ID1:
        return (this->phy_oui >> 6) & 0xFFFFU;
    case PHY_ID2:
        return ((this->phy_oui << 10) | (phy_model << 4) | phy_rev) & 0xFFFFU;
    case PHY_ANAR:
        return this->phy_anar;
    default:
        LOG_F(ERROR, "Reading unimplemented PHY register %d", reg_num);
    }

    return 0;
}

void BigMac::phy_reg_write(uint8_t reg_num, uint16_t value) {
    switch(reg_num) {
    case PHY_BMCR:
        if (value & 0x8000) {
            LOG_F(INFO, "PHY reset requested");
            value &= ~0x8000; // Reset bit is self-clearing
        }
        this->phy_bmcr = value;
        break;
    case PHY_ANAR:
        this->phy_anar = value;
        break;
    default:
        LOG_F(ERROR, "Writing unimplemented PHY register %d", reg_num);
    }
}

void BigMac::poll_backend() {
    if (!ensure_backend()) return;
    uint8_t buf[kBigMacMaxFrameLen];
    std::string err;
    int got = 0;
    {
        std::lock_guard<std::mutex> _{mu_};
        got = backend ? backend->recv(buf, sizeof(buf), err) : 0;
    }
    if (got < 0) {
        LOG_F(ERROR, "%s: backend recv error: %s", name.c_str(), err.c_str());
        stat |= 0x8000;
        if (!(event_mask & 0x8000) && irq_cb) irq_cb(true);
        return;
    }
    if (got == 0) return;
    enqueue_rx_frame(buf, static_cast<size_t>(got));
}

uint8_t BigMac::pop_rx_byte() {
    std::lock_guard<std::mutex> _{mu_};
    while (!rxq.empty()) {
        auto &front = rxq.front();
        if (front.cursor >= front.data.size()) {
            rxq.pop_front();
            if (fifo_fc) --fifo_fc;
            continue;
        }
        uint8_t b = front.data[front.cursor++];
        if (front.cursor >= front.data.size()) {
            rxq.pop_front();
            if (fifo_fc) --fifo_fc;
        }
        return b;
    }
    return 0;
}

// ======================== MAC Serial EEPROM emulation ========================
void BigMac::srom_reset() {
    this->srom_csr_old = 0;
    this->srom_bit_counter = 0;
    this->srom_opcode = 0;
    this->srom_address = 0;
    this->srom_state = Srom_Start;
}

bool BigMac::srom_rcv_value(uint16_t& var, uint8_t num_bits, uint8_t next_bit) {
    var = (var << 1) | (next_bit & 1);
    this->srom_bit_counter++;
    if (this->srom_bit_counter >= num_bits) {
        this->srom_bit_counter = 0;
        return true; // all bits have been received -> return true
    }
    return false; // more bits expected
}

void BigMac::srom_xmit_bit(const uint8_t bit_val) {
    switch(this->srom_state) {
    case Srom_Start:
        if (bit_val)
            this->srom_state = Srom_Opcode;
        else
            this->srom_reset();
        break;
    case Srom_Opcode:
        if (this->srom_rcv_value(this->srom_opcode, 2, bit_val)) {
            switch(this->srom_opcode) {
            case 2: // read
                this->srom_state = Srom_Address;
                break;
            default:
                LOG_F(ERROR, "%s: unsupported SROM opcode %d", this->name.c_str(),
                      this->srom_opcode);
                this->srom_reset();
            }
        }
        break;
    case Srom_Address:
        if (this->srom_rcv_value(this->srom_address, 6, bit_val)) {
            LOG_F(9, "SROM address received = 0x%X", this->srom_address);
            this->srom_bit_counter = 16;
            this->srom_state = Srom_Read_Data;
        }
        break;
    case Srom_Read_Data:
        if (this->srom_bit_counter) {
            this->srom_bit_counter--;
            this->srom_in_bit = (this->srom_data[this->srom_address] >> this->srom_bit_counter) & 1;
            if (!this->srom_bit_counter) {
                this->srom_address++;
                this->srom_bit_counter = 16;
            }
        }
        break;
    default:
        LOG_F(ERROR, "%s: unhandled state %d in srom_xmit_bit", this->name.c_str(),
              this->srom_state);
        this->srom_reset();
    }
}

static const std::vector<std::string> NetBackends = {"null", "loopback", "slirp", "pcap", "vmnet"};
static const PropMap BigMac_Properties = {
    {"net_backend", new StrProperty("null", NetBackends)},
};

static const DeviceDescription BigMac_Heathrow_Descriptor = {
    BigMac::create_for_heathrow, {}, BigMac_Properties, HWCompType::MMIO_DEV | HWCompType::ETHER_MAC
};

static const DeviceDescription BigMac_Paddington_Descriptor = {
    BigMac::create_for_paddington, {}, BigMac_Properties, HWCompType::MMIO_DEV | HWCompType::ETHER_MAC
};

REGISTER_DEVICE(BigMacHeathrow, BigMac_Heathrow_Descriptor);
REGISTER_DEVICE(BigMacPaddington, BigMac_Paddington_Descriptor);
