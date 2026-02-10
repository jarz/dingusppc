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

/** Shared declarations for device regression tests. */

#ifndef TEST_DEVICES_H
#define TEST_DEVICES_H

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

extern int ntested;
extern int nfailed;

inline std::vector<std::string> tokenize_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;

    while (std::getline(stream, token, ',')) {
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
        else
            tokens.push_back("");
    }

    return tokens;
}

inline uint32_t parse_hex32(const std::string& s) {
    return (uint32_t)std::stoul(s, nullptr, 16);
}

inline uint64_t parse_hex64(const std::string& s) {
    return (uint64_t)std::stoull(s, nullptr, 16);
}

void run_hammerhead_tests();
void run_hmc_tests();
void run_aspen_tests();
void run_psx_tests();
void run_machineid_tests();

#endif // TEST_DEVICES_H
