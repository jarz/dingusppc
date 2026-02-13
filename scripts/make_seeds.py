import csv, pathlib, re

root = pathlib.Path(__file__).resolve().parents[1]
seeds_insn = root / 'seeds' / 'fuzz_ppc_insn'
seeds_dis = root / 'seeds' / 'fuzz_ppc_disasm'
seeds_insn.mkdir(parents=True, exist_ok=True)
seeds_dis.mkdir(parents=True, exist_ok=True)
files = [root/'cpu/ppc/test/ppcinttests.csv', root/'cpu/ppc/test/ppcfloattests.csv', root/'cpu/ppc/test/ppcdisasmtest.csv']
hex_re = re.compile(r'^0x[0-9A-Fa-f]{1,8}$')
count = 0
for path in files:
    with path.open() as f:
        rdr = csv.reader(f)
        for i,row in enumerate(rdr):
            opcode_hex = None
            for tok in row:
                tok = tok.strip()
                if hex_re.match(tok):
                    opcode_hex = tok
                    break
            if not opcode_hex:
                continue
            opcode = int(opcode_hex, 16)
            data = opcode.to_bytes(4, 'big')
            (seeds_insn / f"{path.stem}-{i:05d}").write_bytes(data)
            (seeds_dis / f"{path.stem}-{i:05d}").write_bytes(data)
            count += 1
print(f"wrote {count} seeds")
