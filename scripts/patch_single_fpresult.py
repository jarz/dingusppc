#!/usr/bin/env python3
import re
from pathlib import Path

SINGLES = {
    'fadds','fsubs','fmuls','fdivs','fmadds','fmsubs','fnmadds','fnmsubs',
    'frsp','fres','fsqrts','fsqrts','fre','frsqrte','fsel',
    # include integer→float? none here; adjust as needed
}

path = Path('cpu/ppc/ppcfpopcodes.cpp')
data = path.read_text().splitlines()
out = []

in_single = False
brace_depth = 0
reps = 0

for line in data:
    stripped = line.strip()
    # detect function start
    m = re.match(r"void\s+dppc_interpreter::ppc_(\w+)\s*\(", stripped)
    if m:
        name = m.group(1)
        in_single = name in SINGLES or name.endswith('s')
        brace_depth = 0
    # track braces
    brace_depth += line.count('{')
    brace_depth -= line.count('}')
    if brace_depth <= 0 and '{' not in line:
        in_single = False

    if in_single:
        before = line
        line = line.replace('fpresult_update(ppc_dblresult64_d);', 'fpresult_update(ppc_dblresult64_d, true);')
        line = line.replace('fpresult_update(0.0);', 'fpresult_update(0.0, true);')
        line = line.replace('fpresult_update(select_nan(fpscr_invalid_raised()));', 'fpresult_update(select_nan(fpscr_invalid_raised()), true);')
        if line != before:
            reps += 1
    out.append(line)

path.write_text('\n'.join(out) + '\n')
print(f"replacements: {reps}")
