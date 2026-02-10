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

/** Device regression tests for memory controllers.

    Tests register read/write behavior of HammerheadCtrl, HMC, and AspenCtrl
    using CSV-driven test vectors.
*/

#include "test_devices.h"

#include <iostream>

using namespace std;

int ntested = 0;
int nfailed = 0;

int main() {
    cout << "Running DingusPPC device regression tests..." << endl << endl;

    ntested = 0;
    nfailed = 0;

    run_hammerhead_tests();
    run_hmc_tests();
    run_aspen_tests();
    run_psx_tests();
    run_machineid_tests();

    cout << endl << "... completed." << endl;
    cout << "--> Tested: " << dec << ntested << endl;
    cout << "--> Failed: " << dec << nfailed << endl << endl;

    return (nfailed > 0) ? 1 : 0;
}
