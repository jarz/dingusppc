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

/** @file Media Access Controller for Ethernet (MACE) emulation. */

#include <devices/deviceregistry.h>
#include <devices/ethernet/mace.h>
#include <cpu/ppc/ppcmmu.h>
#include <core/timermanager.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>

#include <cinttypes>
#include <algorithm>

using namespace MaceEnet;

namespace {
constexpr size_t kMaxFrameLen = 1600; // standard Ethernet MTU + overhead
constexpr uint64_t kPollIntervalNs = 10'000'000ull; // 10 ms

// Interrupt bits (documented in Am79C940 datasheet; subset used here)
enum : uint8_t {
    INT_TX = 1 << 0,
    INT_RX = 1 << 1,
    INT_ERR = 1 << 7,
};
}

int MaceController::device_postinit() {
    // Backend selection via machine property
    try {
        backend_name = GET_STR_PROP("net_backend");
    } catch (...) {
        // property may be missing; fallback to env var handled in make_ether_backend
    }
    // Initial backend will be constructed lazily when needed; nothing to do here yet.
    return 0;
}

bool MaceController::ensure_backend() {
    std::lock_guard<std::mutex> _{mu_};
    if (backend)
        return true;
    std::string err;
    backend = make_ether_backend(backend_name);
    // Build mac from phys_addr (little helper)
    for (int i = 0; i < 6; ++i) mac.bytes[i] = (phys_addr >> (i * 8)) & 0xFF;
    bool mac_is_zero = true;
    for (int i = 0; i < 6; ++i) mac_is_zero &= (mac.bytes[i] == 0);
    if (mac_is_zero) {
        uint8_t defmac[6] = {0x02,0x00,0xDE,0xAD,0xBE,0xEF};
        for (int i = 0; i < 6; ++i) mac.bytes[i] = defmac[i];
    }
    EtherConfig cfg{};
    cfg.promisc = (mac_cfg & 0x01) != 0; // bit0 typically promisc/enable
    if (!backend->open(mac, cfg, err)) {
        LOG_F(ERROR, "%s: backend open failed: %s", name.c_str(), err.c_str());
        backend.reset();
        return false;
    }
    TimerManager::get_instance()->add_cyclic_timer(kPollIntervalNs, [this]() { this->poll_backend(); });
    LOG_F(INFO, "%s: backend '%s' initialized, mac=%02X:%02X:%02X:%02X:%02X:%02X", name.c_str(), backend_name.c_str(),
          mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3], mac.bytes[4], mac.bytes[5]);
    return true;
}

uint8_t MaceController::read(uint8_t reg_offset)
{
    switch (reg_offset) {
    case MaceReg::Rcv_FIFO: {
        // Deliver from front RX slot
        std::lock_guard<std::mutex> _{mu_};
        if (rxq.empty()) return 0;
        auto &slot = rxq.front();
        if (slot.data.empty()) {
            rxq.pop_front();
            return 0;
        }
        uint8_t byte = slot.data.front();
        slot.data.erase(slot.data.begin());
        if (slot.data.empty()) {
            rxq.pop_front();
            if (fifo_fc) --fifo_fc; // one frame consumed
        }
        return byte;
    }
    case MaceReg::Rcv_Frame_Ctrl:
        return this->rcv_fc;
    case MaceReg::Xmit_Frame_Stat:
        return this->xmt_fs;
    case MaceReg::Xmit_Retry_Cnt:
        return this->xmt_retry;
    case MaceReg::Rcv_Frame_Stat:
        return this->rcv_fs;
    case MaceReg::FIFO_Frame_Cnt:
        return this->fifo_fc;
    case MaceReg::Interrupt: {
        uint8_t ret_val = this->int_stat;
        this->int_stat = 0;
        LOG_F(9, "%s: all interrupt flags cleared", this->name.c_str());
        return ret_val;
    }
    case MaceReg::Interrupt_Mask:
        return this->int_mask;
    case MaceReg::Poll:
        return this->poll_reg;
    case MaceReg::BIU_Config_Ctrl:
        return this->biu_ctrl;
    case MaceReg::FIFO_Config:
        return this->fifo_ctrl;
    case MaceReg::MAC_Config_Ctrl:
        return this->mac_cfg;
    case MaceReg::PLS_Config_Ctrl:
        return this->pls_cc;
    case MaceReg::PHY_Config_Ctrl:
        return this->phy_cc;
    case MaceReg::Chip_ID_Lo:
        return this->chip_id & 0xFFU;
    case MaceReg::Chip_ID_Hi:
        return (this->chip_id >> 8) & 0xFFU;
    case MaceReg::Int_Addr_Config:
        return this->addr_cfg;
    case MaceReg::Phys_Addr:
        if (this->addr_cfg & IAC_PHYADDR) {
            if (this->addr_ptr < 6) {
                uint8_t b = mac.bytes[this->addr_ptr];
                if (++this->addr_ptr >= 6) {
                    this->addr_cfg &= ~IAC_PHYADDR;
                    this->addr_ptr = 0;
                }
                return b;
            }
        }
        return 0;
    case MaceReg::Missed_Pkt_Cnt:
        return this->missed_pkts;
    default:
        LOG_F(INFO, "%s: reading from register %d", this->name.c_str(), reg_offset);
    }

    return 0;
}

void MaceController::write(uint8_t reg_offset, uint8_t value)
{
    switch(reg_offset) {
    case MaceReg::Rcv_Frame_Ctrl:
        this->rcv_fc = value;
        break;
    case MaceReg::Interrupt_Mask:
        this->int_mask = value;
        break;
    case MaceReg::BIU_Config_Ctrl:
        if (value & BIU_SWRST) {
            LOG_F(INFO, "%s: soft reset asserted", this->name.c_str());
            value &= ~BIU_SWRST; // acknowledge soft reset
            // clear queues under lock
            {
                std::lock_guard<std::mutex> _{mu_};
                txq.clear(); rxq.clear(); fifo_fc = 0;
            }
            int_stat = 0; xmt_retry = 0; xmt_fs = 0; rcv_fs = 0;
        }
        this->biu_ctrl = value;
        break;
    case MaceReg::FIFO_Config:
        this->fifo_ctrl = value;
        break;
    case MaceReg::MAC_Config_Ctrl:
        this->mac_cfg = value;
        // MAC address may have been programmed earlier; rebuild mac struct
        for (int i = 0; i < 6; ++i) mac.bytes[i] = (phys_addr >> (i * 8)) & 0xFF;
        break;
    case MaceReg::PLS_Config_Ctrl:
        if (value != 7)
            LOG_F(WARNING, "%s: unsupported transceiver interface 0x%X in PLSCC",
                  this->name.c_str(), value);
        this->pls_cc = value;
        break;
    case MaceReg::Int_Addr_Config:
        if ((value & IAC_LOGADDR) && (value & IAC_PHYADDR))
            value &= ~IAC_PHYADDR;
        if (value & (IAC_LOGADDR | IAC_PHYADDR))
            this->addr_ptr = 0;
        this->addr_cfg = value;
        break;
    case MaceReg::Log_Addr_Flt:
        if (this->addr_cfg & IAC_LOGADDR) {
            uint64_t mask = ~(0xFFULL << (this->addr_ptr * 8));
            this->log_addr = (this->log_addr & mask) | ((uint64_t)value << (this->addr_ptr * 8));
            if (++this->addr_ptr >= 8) {
                this->addr_cfg &= ~IAC_LOGADDR;
                this->addr_ptr = 0;
            }
        }
        break;
    case MaceReg::Phys_Addr:
        if (this->addr_cfg & IAC_PHYADDR) {
            uint64_t mask = ~(0xFFULL << (this->addr_ptr * 8));
            this->phys_addr = (this->phys_addr & mask) | ((uint64_t)value << (this->addr_ptr * 8));
            // Update mac bytes immediately so filters work on subsequent RX
            mac.bytes[this->addr_ptr] = value;
            if (++this->addr_ptr >= 6) {
                this->addr_cfg &= ~IAC_PHYADDR;
                this->addr_ptr = 0;
            }
        }
        break;
    default:
        LOG_F(INFO, "%s: writing 0x%X to register %d", this->name.c_str(),
              value, reg_offset);
    }
}

size_t MaceController::dma_pull_tx(uint32_t addr, size_t max_len) {
    if (!ensure_backend()) return 0;
    if (!max_len || max_len > kMaxFrameLen) max_len = kMaxFrameLen;

    MapDmaResult res = mmu_map_dma_mem(addr, max_len, false);
    if (!res.host_va) {
        LOG_F(ERROR, "%s: dma_pull_tx failed to map addr=0x%X len=%zu", name.c_str(), addr, max_len);
        return 0;
    }
    TxFrame frame;
    frame.data.assign(res.host_va, res.host_va + max_len);
    std::string err;
    {
        std::lock_guard<std::mutex> _{mu_};
        if (!backend || !backend->send(frame.data.data(), frame.data.size(), err)) {
            LOG_F(ERROR, "%s: backend send failed: %s", name.c_str(), err.c_str());
            xmt_fs |= 0x80; // error
            int_stat |= INT_ERR;
        } else {
            xmt_fs = 0; // success
            int_stat |= INT_TX;
        }
    }
    // IRQ gating with mask
    if (int_stat & int_mask) {
        if (irq_cb) irq_cb(true);
    }
    return max_len;
}

void MaceController::tx_from_host(const uint8_t* buf, size_t len) {
    if (!ensure_backend()) return;
    std::string err;
    {
        std::lock_guard<std::mutex> _{mu_};
        if (!backend || !backend->send(buf, len, err)) {
            LOG_F(ERROR, "%s: backend send failed: %s", name.c_str(), err.c_str());
            xmt_fs |= 0x80;
            int_stat |= INT_ERR;
        } else {
            xmt_fs = 0;
            int_stat |= INT_TX;
        }
    }
    if (int_stat & int_mask) {
        if (irq_cb) irq_cb(true);
    }
}

bool MaceController::mac_accepts(const uint8_t* buf, size_t len) const {
    if (len < 6) return false;
    const uint8_t* dst = buf;
    // broadcast
    bool broadcast = true;
    for (int i = 0; i < 6; ++i) broadcast &= (dst[i] == 0xFF);
    if (broadcast) return true;
    // promisc bit assumed at mac_cfg bit0 (tbd verify)
    if (mac_cfg & 0x01) return true;
    // match programmed MAC
    for (int i = 0; i < 6; ++i) {
        if (dst[i] != mac.bytes[i]) return false;
    }
    return true;
}

void MaceController::enqueue_rx_frame(const uint8_t* buf, size_t len) {
    if (!mac_accepts(buf, len)) {
        ++missed_pkts;
        return;
    }
    RxSlot slot;
    slot.data.assign(buf, buf + len);
    {
        std::lock_guard<std::mutex> _{mu_};
        rxq.push_back(std::move(slot));
        if (fifo_fc < 0xFF) ++fifo_fc;
    }
    rcv_fs = 0; // reset status; could set bitfields per MACE spec
    int_stat |= INT_RX;
    if (int_stat & int_mask) {
        if (irq_cb) irq_cb(true);
    }
}

void MaceController::inject_rx_test_frame(const uint8_t* buf, size_t len) {
    enqueue_rx_frame(buf, len);
}

bool MaceController::fetch_next_rx_frame(std::vector<uint8_t>& out_frame) {
    std::lock_guard<std::mutex> _{mu_};
    if (rxq.empty()) return false;
    out_frame = rxq.front().data;
    rxq.pop_front();
    if (fifo_fc) --fifo_fc;
    return true;
}

void MaceController::poll_backend() {
    if (!ensure_backend()) return;
    uint8_t buf[kMaxFrameLen];
    std::string err;
    int got = 0;
    {
        std::lock_guard<std::mutex> _{mu_};
        got = backend ? backend->recv(buf, sizeof(buf), err) : 0;
    }
    if (got < 0) {
        LOG_F(ERROR, "%s: backend recv error: %s", name.c_str(), err.c_str());
        int_stat |= INT_ERR;
        if (int_stat & int_mask && irq_cb) irq_cb(true);
        return;
    }
    if (got == 0) return;
    enqueue_rx_frame(buf, static_cast<size_t>(got));
}

static const std::vector<std::string> NetBackends = {
    "null", "loopback", "slirp", "pcap", "vmnet"
};

static const PropMap Mace_Properties = {
    {"net_backend", new StrProperty("null", NetBackends)},
};

static const DeviceDescription Mace_Descriptor = {
    MaceController::create, {}, Mace_Properties, HWCompType::MMIO_DEV | HWCompType::ETHER_MAC
};

REGISTER_DEVICE(Mace, Mace_Descriptor);
