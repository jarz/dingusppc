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

#include "../ppcdisasm.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

using namespace std;

/** testing vehicle */
static bool open_csv(std::ifstream &stream, const std::string &filename) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    candidates.emplace_back(filename);
    candidates.emplace_back(fs::path("cpu/ppc/test") / filename);
#ifdef DPPC_SOURCE_DIR
    candidates.emplace_back(fs::path(DPPC_SOURCE_DIR) / "cpu/ppc/test" / filename);
#endif
#ifdef __APPLE__
    {
        char buf[PATH_MAX]; uint32_t sz = sizeof(buf);
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            fs::path exe = fs::weakly_canonical(buf);
            fs::path dir = exe.parent_path();
            candidates.emplace_back(dir / filename);
            candidates.emplace_back(dir / "cpu/ppc/test" / filename);
            candidates.emplace_back(dir.parent_path() / "cpu/ppc/test" / filename);
            candidates.emplace_back(dir.parent_path().parent_path() / "cpu/ppc/test" / filename);
        }
    }
#elif defined(__linux__)
    {
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf)-1);
        if (len > 0) { buf[len] = 0; fs::path exe = fs::weakly_canonical(buf); fs::path dir = exe.parent_path();
            candidates.emplace_back(dir / filename);
            candidates.emplace_back(dir / "cpu/ppc/test" / filename);
            candidates.emplace_back(dir.parent_path() / "cpu/ppc/test" / filename);
        }
    }
#endif
    for (auto &c : candidates) {
        stream.open(c);
        if (stream.is_open()) return true;
    }
    return false;
}

static vector<PPCDisasmContext> read_test_data() {
    string line, token;
    int i, lineno;
    PPCDisasmContext ctx;
    vector<PPCDisasmContext> tstvec;

    ifstream tfstream;
    if (!open_csv(tfstream, "ppcdisasmtest.csv")) {
        cout << "Could not open tests CSV file. Exiting..." << endl;
        return tstvec;
    }

    lineno = 0;

    while (getline(tfstream, line)) {
        lineno++;

        if (line.empty() || !line.rfind("#", 0))
            continue;    // skip empty/comment lines

        istringstream lnstream(line);

        vector<string> tokens;

        while (getline(lnstream, token, ',')) {
            // cout << "Token: " << token << endl;
            tokens.push_back(token);
        }

        if (tokens.size() < 3) {
            cout << "Too few values in line " << lineno << ". Skipping..." << endl;
            continue;
        }

        ctx            = {0};
        ctx.instr_addr = (uint32_t)stoul(tokens[0], NULL, 16);
        ctx.instr_code = (uint32_t)stoul(tokens[1], NULL, 16);

        /* build disassembly string out of comma-separated parts */
        ostringstream idisasm;

        /* put instruction mnemonic padded with trailing spaces */
        idisasm << tokens[2];
        idisasm << setw(streamsize(8) - (int)tokens[2].length()) << " ";

        /* now add comma-separated operands */
        for (i = 3; i < tokens.size(); i++) {
            if (i > 3)
                idisasm << ", ";
            idisasm << tokens[i];
        }

        ctx.instr_str = idisasm.str();

        // cout << idisasm.str() << endl;

        tstvec.push_back(ctx);
    }

    return tstvec;
}

int test_ppc_disasm() {
    int i, nfailed;
    PPCDisasmContext ctx;

    vector<PPCDisasmContext> testdata = read_test_data();

    cout << "Imported " << testdata.size() << " test instructions." << endl;

    nfailed = 0;

    for (i = 0; i < testdata.size(); i++) {
        ctx            = {0};
        ctx.instr_addr = testdata[i].instr_addr;
        ctx.instr_code = testdata[i].instr_code;
        ctx.simplified = true;

        std::string disas = disassemble_single(&ctx);

        if (disas != testdata[i].instr_str) {
            cout << "Mismatch found, expected={" << testdata[i].instr_str << "}, got={" << disas
                 << "}" << endl;
            nfailed++;
        }
    }

    cout << "Tested " << testdata.size() << " instructions. Failed: " << nfailed << "." << endl;

    return 0;
}
