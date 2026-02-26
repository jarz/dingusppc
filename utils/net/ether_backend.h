#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Minimal MAC address helper
struct MacAddr {
    uint8_t bytes[6]{};
    bool operator==(const MacAddr& rhs) const {
        for (size_t i = 0; i < 6; ++i) if (bytes[i] != rhs.bytes[i]) return false;
        return true;
    }
};

struct EtherConfig {
    std::string ifname; // optional name to bind (e.g., "en0", "tap0")
    bool promisc = false;
    bool no_filter = false; // if true, bypass MAC filtering (for testing)
};

// Simple backend interface; implemented by NullBackend, SlirpBackend, PcapBackend, VmnetBackend
class EtherBackend {
public:
    virtual ~EtherBackend() = default;

    virtual bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) = 0;
    virtual void close() = 0;

    // Non-blocking send; return true if queued/sent.
    virtual bool send(const uint8_t* buf, size_t len, std::string& err) = 0;

    // Non-blocking recv; returns bytes copied into buf. 0 = no data; <0 = error.
    virtual int recv(uint8_t* buf, size_t max_len, std::string& err) = 0;
};

// Factory; implemented in ether_backend_factory.cpp
std::unique_ptr<EtherBackend> make_ether_backend(const std::string& backend_name);

// Utility CRC32 for Ethernet frames (BigMac wants CRC insertion/verify)
uint32_t ether_crc32(const uint8_t* data, size_t len);

// helpers for specific backends (defined in corresponding .cpp)
std::unique_ptr<EtherBackend> make_loopback_backend();
std::unique_ptr<EtherBackend> make_slirp_backend();
std::unique_ptr<EtherBackend> make_pcap_backend();
std::unique_ptr<EtherBackend> make_tap_backend();   // Linux TUN/TAP (ether_backend_tap.cpp)
std::unique_ptr<EtherBackend> make_vmnet_backend();
