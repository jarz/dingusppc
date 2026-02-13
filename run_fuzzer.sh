#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-fuzz"}
TARGET=${TARGET:-fuzz_ppc_insn}

has_libfuzzer() {
  local cxx="$1"
  local resdir
  resdir="$($cxx --print-resource-dir 2>/dev/null || true)" || resdir=""
  [ -z "$resdir" ] && return 1
  find "$resdir/lib" -name 'libclang_rt.fuzzer*.a' -print -quit 2>/dev/null | grep -q .
}

pick_clang() {
  local candidates=()
  [ -n "${CXX:-}" ] && candidates+=("$CXX")
  candidates+=(clang++ clang++-19 clang++-18 clang++-17 clang++-16 clang++-15 clang++-14)
  if command -v brew >/dev/null 2>&1; then
    local brewclang
    brewclang="$(brew --prefix llvm 2>/dev/null)/bin/clang++"
    [ -x "$brewclang" ] && candidates+=("$brewclang")
  fi
  for c in "${candidates[@]}"; do
    command -v "$c" >/dev/null 2>&1 || continue
    if has_libfuzzer "$c"; then
      echo "$c"; return 0
    fi
  done
  return 1
}

if [[ "${1:-}" == "--print-cxx" ]]; then
  CXX_CAND=$(pick_clang) || exit 1
  echo "$CXX_CAND"
  exit 0
fi

CXX_CAND=$(pick_clang) || { echo "No clang++ with libFuzzer found. Install clang with libFuzzer (macOS: brew install llvm; Ubuntu: apt-get install clang-15 libclang-rt-15-dev) or export LIB_FUZZING_ENGINE."; exit 1; }
CXX="$CXX_CAND"
CC="${CC:-${CXX/clang++/clang}}"

LLVM_PREFIX="$(cd "$(dirname "$CXX")/.." && pwd)"
CLANG_RESOURCE_DIR="$($CXX --print-resource-dir)"
LLVM_LIBCXX_DIR="$LLVM_PREFIX/lib/c++"
CXXFLAGS_EXTRA=""
LDFLAGS_EXTRA=""
if [ -d "$LLVM_LIBCXX_DIR" ]; then
  CXXFLAGS_EXTRA+=" -stdlib=libc++ -I$LLVM_PREFIX/include/c++/v1"
  LDFLAGS_EXTRA+=" -stdlib=libc++ -L$LLVM_LIBCXX_DIR -Wl,-rpath,$LLVM_LIBCXX_DIR"
fi
if [ -d "$CLANG_RESOURCE_DIR/lib/darwin" ]; then
  LDFLAGS_EXTRA+=" -L$CLANG_RESOURCE_DIR/lib/darwin -Wl,-rpath,$CLANG_RESOURCE_DIR/lib/darwin"
fi

mkdir -p "$BUILD_DIR"
cmake -B"$BUILD_DIR" -S"$ROOT" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDPPC_BUILD_FUZZERS=ON \
  -DDPPC_BUILD_PPC_TESTS=ON \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_CXX_FLAGS_INIT="$CXXFLAGS_EXTRA" -DCMAKE_EXE_LINKER_FLAGS_INIT="$LDFLAGS_EXTRA"
cmake --build "$BUILD_DIR" --target "$TARGET" -j"$(sysctl -n hw.ncpu)"

CORPUS=${CORPUS:-"$ROOT/corpus/$TARGET"}
SEEDS=${SEEDS:-"$ROOT/seeds/$TARGET"}
mkdir -p "$CORPUS" "$SEEDS" "$ROOT/artifacts/$TARGET"

# Optional: convert CSV tests to seeds (see README snippet below)

DICT_ARG=""
if [[ -f "$ROOT/fuzz/${TARGET}.dict" ]]; then
  DICT_ARG="-dict=$ROOT/fuzz/${TARGET}.dict"
fi
DEFAULT_ARGS="-use_value_profile=1 -print_final_stats=1"
EXTRA_ARGS=${EXTRA_ARGS:-}
ASAN_OPTIONS=allocator_may_return_null=1:detect_odr_violation=0:handle_abort=1:handle_segv=1:handle_sigbus=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1 \
"$BUILD_DIR/$TARGET" $DICT_ARG $DEFAULT_ARGS $EXTRA_ARGS "$CORPUS" "$SEEDS" -artifact_prefix="${ROOT}/artifacts/$TARGET/"

# To generate seeds from CSV opcodes:
# python - <<'PY'
# import csv, binascii, pathlib
# root = pathlib.Path(__file__).resolve().parent
# seeds = root / 'seeds' / 'fuzz_ppc_insn'
# seeds.mkdir(parents=True, exist_ok=True)
# for fname in ['ppcinttests.csv','ppcfloattests.csv','ppcdisasmtest.csv']:
#     path = root / 'cpu' / 'ppc' / 'test' / fname
#     with path.open() as f:
#         rdr = csv.reader(f)
#         for i,row in enumerate(rdr):
#             if not row: continue
#             try:
#                 opcode = int(row[0], 16)
#             except Exception:
#                 continue
#             seed = opcode.to_bytes(4, 'big')
#             (seeds / f"{fname}-{i:05d}").write_bytes(seed)
# PY
