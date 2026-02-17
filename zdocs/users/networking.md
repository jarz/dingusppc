# Networking (experimental)

DingusPPC now ships with pluggable Ethernet backends. Choose one via `--net_backend=<backend>` or environment variable `DINGUS_NET_BACKEND`.

## Backends
- `loopback` (default for tests): echoes frames back to the guest. Great for verifying drivers.
- `null`: drops everything.
- `slirp` (optional): user-mode NAT via libslirp (no root required). Build with `-D DINGUS_NET_ENABLE_SLIRP=ON`.
- `pcap` (optional): bridged capture/injection via libpcap. Needs permissions/promisc.
- `vmnet` (macOS): uses `vmnet.framework` for NAT/bridged modes; build with `-D DINGUS_NET_ENABLE_VMNET=ON`.

## Build
```bash
cmake -B build -S . \
  -D DINGUS_NET_ENABLE_SLIRP=ON \
  -D DINGUS_NET_ENABLE_PCAP=ON \
  -D DINGUS_NET_ENABLE_VMNET=ON  # macOS only
cmake --build build -j4
```
> The top-level CMake will auto-enable slirp/pcap if pkg-config can find them.

## Run
```bash
# Loopback (driver self-test)
./build/bin/dingusppc --net_backend=loopback --rambank1_size 128 -b Power_Mac_G3_Beige.ROM

# Slirp NAT (if enabled)
DINGUS_NET_BACKEND=slirp ./build/bin/dingusppc -b Mac_OS_ROM --rambank1_size 128

# pcap bridge
sudo ./build/bin/dingusppc --net_backend=pcap --net_ifname=en0 -b Power_Mac_G3_Beige.ROM
```
`net_ifname` is derived from `EtherConfig.ifname` (use `--net_ifname=` once we expose it via CLI; default is first non-loopback interface discovered via `pcap_findalldevs`)

## Notes / Limitations
- **BigMac** (Heathrow/Paddington): DBDMA TX/RX hooked via `MacIoTwo` DBDMA channels. RX FIFO is simplified but supports `MEM_DATA_HI/LO` window and `GLOB_STAT`/`EVENT_MASK` bits for basic driver bring-up.
- **MACE** (AMIC): TX/RX paths implemented with optional backend; interrupt mask respected. DMA stubs in AMIC call into `dma_pull_tx` and `poll_backend`.
- CRC insertion/verification is stubbed (`ether_crc32` helper available); drivers that expect hardware CRC may still work since libpcap/libslirp usually accept raw L2 frames without FCS.
- Slirp backend is experimental; NAT connectivity depends on libslirp version.

## Testing
- Build tests: `cmake -B build -S . -D DPPC_BUILD_PPC_TESTS=ON && cmake --build build --target testppc`
- Run: `./build/bin/testppc` (includes MACE & BigMac loopback smoke tests)

## Troubleshooting
- **No network in guest**: ensure `--net_backend` set; check logs (`--log-level 5`).
- **pcap fails to open device**: run as root or grant permissions; specify interface via `DINGUS_NET_IFNAME` (to be exposed explicitly soon).
- **Compilation errors**: disable backends `-D DINGUS_NET_ENABLE_SLIRP=OFF -D DINGUS_NET_ENABLE_PCAP=OFF -D DINGUS_NET_ENABLE_VMNET=OFF`.
