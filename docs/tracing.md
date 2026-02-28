# PPC Execution Tracing

DingusPPC includes a compile-time optional, structured tracing system for
PowerPC execution.  When tracing is **disabled** (the default) the feature
compiles out entirely — there are no runtime branches or overhead.

---

## Building with tracing enabled

```bash
cmake -B build -DDPPC_TRACE_PPC=ON
cmake --build build
```

`DPPC_TRACE_PPC=ON` implicitly sets `DPPC_TRACE=ON`.  You can also enable
`DPPC_TRACE` alone if you only want the infrastructure compiled in without
the PPC hook points.

---

## CLI flags

All tracing flags are only available in binaries built with `-DDPPC_TRACE_PPC=ON`.

| Flag | Default | Description |
|------|---------|-------------|
| `--trace-file <path>` | *(none)* | Write binary trace records to `<path>` |
| `--trace-bb` | on | Enable basic-block boundary tracing |
| `--no-trace-bb` | off | Disable basic-block tracing |
| `--trace-insn` | off | Enable per-instruction tracing |
| `--trace-pc-start <hex>` | `0x0` | Only trace PCs ≥ this value |
| `--trace-pc-end <hex>` | `0xFFFFFFFF` | Only trace PCs ≤ this value |
| `--trace-every <N>` | `1` | Emit one INSN record per N instructions (sampling) |
| `--trace-dump <path>` | *(none)* | Dump the in-memory ring buffer to `<path>` on exit |

### Example

```bash
# Trace all basic-blocks to a file and also dump the ring buffer on exit:
./build/bin/dingusppc --trace-file trace.bin --trace-dump ring_dump.bin \
    -b bootrom.bin

# Trace only a specific PC range with per-instruction sampling:
./build/bin/dingusppc \
    --trace-bb \
    --trace-insn --trace-every 100 \
    --trace-pc-start 0x00003000 --trace-pc-end 0x0000FFFF \
    --trace-file narrow_trace.bin \
    -b bootrom.bin
```

---

## Binary file format

Each trace file begins with a 16-byte header followed by a stream of
fixed-size 32-byte records.

### File header (16 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | `magic` | ASCII `"DPPC"` |
| 4 | 2 | `version` | Format version, currently `1` |
| 6 | 1 | `endian` | `0` = big-endian host, `1` = little-endian host |
| 7 | 1 | `rec_size` | Size of each record in bytes, currently `32` |
| 8 | 8 | `reserved` | Zero |

### Record layout (32 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | `type` | `0x01` = BB (basic-block), `0x02` = INSN |
| 1 | 1 | `flags` | `0x01` branch, `0x02` exception, `0x04` rfi |
| 2 | 2 | `reserved` | Zero |
| 4 | 4 | `seq` | Monotonic sequence number (wraps) |
| 8 | 8 | `timestamp` | Nanoseconds (host steady_clock) |
| 16 | 4 | `start_pc` | First PC of the basic block (or PC for INSN) |
| 20 | 4 | `end_pc` | Last PC of the basic block (or opcode word for INSN) |
| 24 | 4 | `next_pc` | First PC of the next basic block |
| 28 | 4 | `reason` | Raw `exec_flags` value at the time of emission |

---

## In-memory ring buffer

The ring buffer holds the last **65 536** records in memory at all times
(when tracing is compiled in and at least one trace type is enabled).
This means the most recent ~64 K basic-block boundaries are always
available for post-mortem inspection even without `--trace-file`.

Dump the ring buffer:
- At runtime: pass `--trace-dump <path>` and the file is written on clean exit.
- On crash: a best-effort dump is written to `dingusppc_crash_trace.bin` in the
  current working directory when a fatal error is encountered.

---

## Crash / debugger integration

When a fatal error occurs and tracing is compiled in, the fatal handler
automatically writes `dingusppc_crash_trace.bin` containing the ring buffer
contents (last ≤65 536 BB events).  This is best-effort; if the process
state is severely corrupted the dump may be incomplete or absent.

---

## Disabling tracing at runtime (when compiled in)

Pass `--no-trace-bb` to suppress BB records without recompiling.
The ring buffer still exists in memory, it just stays empty.
