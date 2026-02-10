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

/** Athens clock generator regression tests. */

#include "test_devices.h"
#include <devices/common/clockgen/athens.h>

#include <iostream>
#include <memory>

using namespace std;

static void check_eq(const char* name, int actual, int expected) {
    ntested++;
    if (actual != expected) {
        cout << "  FAIL [" << name << "]: expected " << expected
             << " got " << actual << endl;
        nfailed++;
    }
}

static void check_true(const char* name, bool actual) {
    ntested++;
    if (!actual) {
        cout << "  FAIL [" << name << "]: expected true got false" << endl;
        nfailed++;
    }
}

static void check_false(const char* name, bool actual) {
    ntested++;
    if (actual) {
        cout << "  FAIL [" << name << "]: expected false got true" << endl;
        nfailed++;
    }
}

static void check_byte_eq(const char* name, uint8_t actual, uint8_t expected) {
    ntested++;
    if (actual != expected) {
        cout << "  FAIL [" << name << "]: expected 0x" << hex << (int)expected
             << " got 0x" << (int)actual << dec << endl;
        nfailed++;
    }
}

void run_athens_tests() {
    cout << "Running Athens tests..." << endl;

    // Default constructor (addr 0x28)
    auto dev = make_unique<AthensClocks>(0x28);

    // receive_byte always returns 0x41 (ID)
    {
        uint8_t data = 0;
        bool ret = dev->receive_byte(&data);
        check_true("athens_receive_ack", ret);
        check_byte_eq("athens_receive_id", data, 0x41);
    }

    // send_subaddress always returns true
    check_true("athens_subaddr", dev->send_subaddress(0x00));
    check_true("athens_subaddr_ff", dev->send_subaddress(0xFF));

    // get_sys_freq returns 0 (unimplemented)
    check_eq("athens_sys_freq", dev->get_sys_freq(), 0);

    // Default dot freq: mux=2 (crystal), post_div=2 -> xtal/2 = 15667200
    check_eq("athens_default_dot_freq", dev->get_dot_freq(), 15667200);

    // I2C send_byte protocol: pos=0 sets reg_num, pos=1 writes register
    // Set D2=7 via I2C
    dev->start_transaction();
    check_true("athens_send_reg_d2", dev->send_byte(AthensRegs::D2));  // pos=0: reg_num=1
    check_true("athens_send_val_7", dev->send_byte(7));                // pos=1: regs[1]=7

    // Set N2=22
    dev->start_transaction();
    check_true("athens_send_reg_n2", dev->send_byte(AthensRegs::N2));  // reg_num=2
    check_true("athens_send_val_22", dev->send_byte(22));              // regs[2]=22

    // Set P2_MUX2=0x02 (mux=0, VCO mode, post_div_bits=2 -> post_div=2)
    dev->start_transaction();
    check_true("athens_send_reg_p2", dev->send_byte(AthensRegs::P2_MUX2));
    check_true("athens_send_val_02", dev->send_byte(0x02));

    // VCO freq: xtal * (22 / (7 * 2)) = 49239772 (single-precision float)
    check_eq("athens_vco_dot_freq", dev->get_dot_freq(), 49239772);

    // Crystal mode with post_div=1: P2_MUX2=0x23
    dev->start_transaction();
    dev->send_byte(AthensRegs::P2_MUX2);
    dev->send_byte(0x23);
    check_eq("athens_crystal_div1", dev->get_dot_freq(), 31334400);

    // Crystal mode with post_div=8: P2_MUX2=0x20
    dev->start_transaction();
    dev->send_byte(AthensRegs::P2_MUX2);
    dev->send_byte(0x20);
    check_eq("athens_crystal_div8", dev->get_dot_freq(), 3916800);

    // Disabled (bit 7 set): P2_MUX2=0xE2
    dev->start_transaction();
    dev->send_byte(AthensRegs::P2_MUX2);
    dev->send_byte(0xE2);
    check_eq("athens_disabled", dev->get_dot_freq(), 0);

    // System clock VCO (mux=1): P2_MUX2=0x12 -> unsupported, returns 50000000
    dev->start_transaction();
    dev->send_byte(AthensRegs::P2_MUX2);
    dev->send_byte(0x12);
    check_eq("athens_sys_vco", dev->get_dot_freq(), 50000000);

    // Reserved mux (mux=3): P2_MUX2=0x32 -> returns 50000000
    dev->start_transaction();
    dev->send_byte(AthensRegs::P2_MUX2);
    dev->send_byte(0x32);
    check_eq("athens_reserved_mux", dev->get_dot_freq(), 50000000);

    // Invalid register number (>=8) -> send_byte returns false (NACK)
    dev->start_transaction();
    dev->send_byte(8);  // reg_num = 8 (invalid)
    check_false("athens_invalid_reg", dev->send_byte(0x42));

    // Third send_byte (pos=2) triggers warning but returns true
    dev->start_transaction();
    dev->send_byte(0);
    dev->send_byte(0);
    check_true("athens_extra_byte", dev->send_byte(0x99));

    // Custom crystal frequency constructor
    auto dev2 = make_unique<AthensClocks>(0x29, 40000000.0f);
    // Default P2_MUX2=0x62 -> mux=2, crystal mode, post_div=2
    // freq = 40000000 / 2 = 20000000
    check_eq("athens_custom_xtal", dev2->get_dot_freq(), 20000000);
}
