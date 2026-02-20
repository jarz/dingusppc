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

/** @file Media Access Controller for Ethernet (MACE) definitions. */

#ifndef MACE_H
#define MACE_H

#include <devices/common/dmacore.h>
#include <devices/common/hwcomponent.h>
#include <utils/net/ether_backend.h>

#include <cinttypes>
#include <memory>
#include <deque>
#include <vector>
#include <string>
#include <functional>
#include <mutex>

/** Known MACE chip IDs. */
constexpr auto MACE_ID_REV_B0 = 0x0940;    // Darwin-0.3 source
constexpr auto MACE_ID_REV_A2 = 0x0941;    // Darwin-0.3 source & Curio datasheet

/** MACE registers offsets. */
// Refer to the Am79C940 datasheet for details
namespace MaceEnet {
    enum MaceReg : uint8_t {
        Rcv_FIFO        =  0,
        Xmit_FIFO       =  1,
        Xmit_Frame_Ctrl =  2,
        Xmit_Frame_Stat =  3,
        Xmit_Retry_Cnt  =  4,
        Rcv_Frame_Ctrl  =  5,
        Rcv_Frame_Stat  =  6,
        FIFO_Frame_Cnt  =  7,
        Interrupt       =  8,
        Interrupt_Mask  =  9,
        Poll            = 10,
        BIU_Config_Ctrl = 11,
        FIFO_Config     = 12,
        MAC_Config_Ctrl = 13,
        PLS_Config_Ctrl = 14,
        PHY_Config_Ctrl = 15,
        Chip_ID_Lo      = 16,
        Chip_ID_Hi      = 17,
        Int_Addr_Config = 18,
        Log_Addr_Flt    = 20,
        Phys_Addr       = 21,
        Missed_Pkt_Cnt  = 24,
        Runt_Pkt_Cnt    = 26, // not used in Macintosh?
        Rcv_Collis_Cnt  = 27, // not used in Macintosh?
        User_Test       = 29,
        Rsrvd_Test_1    = 30, // not used in Macintosh?
        Rsrvd_Test_2    = 31, // not used in Macintosh?
    };

    /** Bit definitions for BIU_Config_Ctrl register. */
    enum {
        BIU_SWRST   = 1 << 0,
    };

    /** Bit definitions for the internal configuration register. */
    enum {
        IAC_LOGADDR = 1 << 1,
        IAC_PHYADDR = 1 << 2,
        IAC_ADDRCHG = 1 << 7
    };

} // namespace MaceEnet

class MaceController : public DmaDevice, public HWComponent {
public:
    MaceController(uint16_t id) {
        this->chip_id = id;
        this->set_name("MACE");
        this->supports_types(HWCompType::MMIO_DEV | HWCompType::ETHER_MAC);
    }
    ~MaceController() override = default;

    static std::unique_ptr<HWComponent> create() {
        return std::unique_ptr<MaceController>(new MaceController(MACE_ID_REV_A2));
    }

    int device_postinit() override;

    // MACE registers access
    uint8_t read(uint8_t reg_offset);
    void    write(uint8_t reg_offset, uint8_t value);

    // Called by AMIC DMA stubs when a TX buffer is ready; returns bytes consumed.
    size_t dma_pull_tx(uint32_t addr, size_t max_len);
    // Called by DBDMA out_cb when a TX buffer is ready (GrandCentral path).
    void tx_from_host(const uint8_t* buf, size_t len);
    // Called periodically (TimerManager) to poll backend and enqueue RX buffers.
    void poll_backend();


    // For tests/debugging
    void set_backend_name(const std::string& name) { this->backend_name = name; }
    void set_backend_for_test(std::unique_ptr<EtherBackend> be) {
        std::lock_guard<std::mutex> _{mu_};
        backend = std::move(be);
    }

    // Inject a frame directly into RX queue (test helper)
    void inject_rx_test_frame(const uint8_t* buf, size_t len);

    // Dequeue next complete RX frame (for DBDMA integration)
    bool fetch_next_rx_frame(std::vector<uint8_t>& out_frame);

    // AMIC wires this so we can assert/clear the IRQ line.
    void set_irq_callback(std::function<void(bool)> cb) { irq_cb = std::move(cb); }

private:
    struct TxFrame {
        std::vector<uint8_t> data;
    };

    bool ensure_backend();
    void enqueue_rx_frame(const uint8_t* buf, size_t len);
    bool mac_accepts(const uint8_t* buf, size_t len) const;

    uint16_t    chip_id;          // per-instance MACE Chip ID
    uint8_t     addr_cfg      = 0;
    uint8_t     addr_ptr      = 0;
    uint8_t     xmt_fs        = 0;
    uint8_t     xmt_retry     = 0;
    uint8_t     rcv_fc        = 1;
    uint8_t     rcv_fs        = 0;
    uint8_t     biu_ctrl      = 0;
    uint8_t     fifo_ctrl     = 0;

    uint8_t     poll_reg      = 0;

    uint8_t     mac_cfg       = 0;
    uint8_t     mac_cc        = 0;
    uint8_t     pls_cc        = 0;
    uint8_t     phy_cc        = 0;
    uint8_t     fifo_fc       = 0;
    uint8_t     missed_pkts   = 0;
    uint64_t    phys_addr     = 0;
    uint64_t    log_addr      = 0;

    // interrupt stuff
    uint8_t     int_stat  = 0;
    uint8_t     int_mask  = 0;

    // backend + queues
    std::string backend_name = "null";
    std::unique_ptr<EtherBackend> backend;
    MacAddr mac{};

    std::function<void(bool)> irq_cb;

    std::deque<TxFrame> txq;

    // RX buffer staging; small circular buffer of fixed-size slots. We don't model the full
    // hardware FIFO yet, just enough to hand frames to the guest via DMA.
    struct RxSlot { std::vector<uint8_t> data; };
    std::deque<RxSlot> rxq;
    mutable std::mutex mu_;

    // TODO: consider using last_poll_ns for adaptive poll pacing
    uint64_t last_poll_ns = 0;
};

#endif // MACE_H
