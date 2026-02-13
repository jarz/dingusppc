# DingusPPC Fuzzing

## Targets
- `fuzz_ppc_insn` — single-instruction executor (highest value)
- `fuzz_ppc_sequence` — short instruction sequences (stateful)
- `fuzz_ppc_disasm` — pure disassembler fuzzer

## Build
```bash
# Script picks a clang++ with libFuzzer automatically if available
./run_fuzzer.sh
# Or manually:
CXX=$(./run_fuzzer.sh --print-cxx 2>/dev/null || echo clang++)
CC=${CXX/clang++/clang}
cmake -Bbuild-fuzz -DDPPC_BUILD_FUZZERS=ON -DDPPC_BUILD_PPC_TESTS=ON -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" .
cmake --build build-fuzz --target fuzz_ppc_insn -j$(sysctl -n hw.ncpu)
```

> If libFuzzer is missing, CMake will warn and skip fuzz targets.

**Install tips**
- macOS: `brew install llvm` (then set `CC=$(brew --prefix llvm)/bin/clang` and `CXX=.../clang++`)
- Ubuntu: `apt-get install clang-15 libclang-rt-15-dev`
- OSS-Fuzz: set `LIB_FUZZING_ENGINE` env var

## Run
```bash
ASAN_OPTIONS=allocator_may_return_null=1:detect_odr_violation=0:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1 \
build-fuzz/fuzz_ppc_insn corpus/fuzz_ppc_insn seeds/fuzz_ppc_insn -artifact_prefix=artifacts/fuzz_ppc_insn/ \
  -use_value_profile=1 -print_final_stats=1
```

`run_fuzzer.sh` auto-loads `fuzz/<target>.dict` when present; pass extra flags via `EXTRA_ARGS` env.

## Seeds
- Use existing CSV test files: `cpu/ppc/test/ppcinttests.csv`, `ppcfloattests.csv`, `ppcdisasmtest.csv`
- Convert CSV to seeds (4-byte opcode files):
  ```bash
  python scripts/make_seeds.py
  ```
- `run_fuzzer.sh` compares `fuzz/<target>.dict` automatically when present

## Dictionary (optional)
Create `fuzz_ppc_insn.dict` and pass `-dict=fuzz/fuzz_ppc_insn.dict` with useful bitfields:
```
0x4C000020 # bclr
0x4C000420 # bctr
0x7C0003A6 # mtlr
0x7C0903A6 # mtctr
0x7C0004AC # sync
0x7C0002A6 # mfspr
0x7C0003A6 # mtspr
```

## Notes
- `PPC_TESTS` disables alignment exceptions to surface crashes instead of longjmps.
- RAM is masked to a small region for safety; fuzzers reset CPU state per input.
- Prefer `-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all` (already set in CMake).
