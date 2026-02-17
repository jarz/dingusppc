#include <utils/net/ether_backend.h>
#include <loguru.hpp>
#include <memory>
#include <cstdlib>

namespace {
class NullBackend final : public EtherBackend {
public:
    bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) override {
        (void)mac; (void)cfg; (void)err;
        LOG_F(INFO, "NullBackend: open (promisc=%d)", cfg.promisc);
        return true;
    }
    void close() override {}
    bool send(const uint8_t* buf, size_t len, std::string& err) override {
        (void)err;
        // Optionally loop back for tests later
        LOG_F(9, "NullBackend: send len=%zu", len);
        return true;
    }
    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        (void)buf; (void)max_len; (void)err;
        return 0; // no data
    }
};
} // namespace

std::unique_ptr<EtherBackend> make_ether_backend(const std::string& backend_name) {
    std::string name = backend_name;
    if (name.empty()) {
        const char* env = std::getenv("DINGUS_NET_BACKEND");
        if (env) name = env;
    }
    if (name == "loopback") return make_loopback_backend();
    if (name == "slirp") return make_slirp_backend();
    if (name == "pcap") return make_pcap_backend();
    if (name == "vmnet") return make_vmnet_backend();
    if (name == "null" || name.empty()) return std::unique_ptr<EtherBackend>(new NullBackend());
    LOG_F(WARNING, "Unknown net backend '%s', falling back to null", name.c_str());
    return std::unique_ptr<EtherBackend>(new NullBackend());
}

// Polynomial 0xEDB88320, standard Ethernet CRC32
uint32_t ether_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = data[i];
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}
