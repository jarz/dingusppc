#include <utils/net/ether_backend.h>
#include <loguru.hpp>
#include <memory>
#include <cstdlib>

#if defined(DINGUS_NET_ENABLE_PCAP)
#include <pcap/pcap.h>

namespace {
class PcapBackend final : public EtherBackend {
public:
    ~PcapBackend() override { close(); }
    bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) override {
        mac_ = mac;
        char errbuf[PCAP_ERRBUF_SIZE];
        std::string dev;
        // Allow env override until CLI exposes ifname
        if (const char* env = std::getenv("DINGUS_NET_IFNAME")) {
            dev = env;
        }
        if (!cfg.ifname.empty()) dev = cfg.ifname;

        if (dev.empty()) {
            pcap_if_t* alldevs = nullptr;
            if (pcap_findalldevs(&alldevs, errbuf) == -1) {
                err = errbuf;
                return false;
            }
            for (pcap_if_t* d = alldevs; d; d = d->next) {
                if (d->flags & PCAP_IF_LOOPBACK) continue; // skip loopback unless nothing else
                dev = d->name;
                break;
            }
            if (dev.empty() && alldevs) dev = alldevs->name; // fallback to first
            if (alldevs) pcap_freealldevs(alldevs);
            if (dev.empty()) {
                err = "no pcap devices found";
                return false;
            }
        }

        pcap_ = pcap_open_live(dev.c_str(), 65535, cfg.promisc ? 1 : 0, 10, errbuf);
        if (!pcap_) { err = errbuf; return false; }
        return true;
    }
    void close() override {
        if (pcap_) { pcap_close(pcap_); pcap_ = nullptr; }
    }
    bool send(const uint8_t* buf, size_t len, std::string& err) override {
        if (!pcap_) { err = "pcap not initialized"; return false; }
        if (pcap_inject(pcap_, buf, len) == -1) {
            err = pcap_geterr(pcap_);
            return false;
        }
        return true;
    }
    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        if (!pcap_) { err = "pcap not initialized"; return -1; }
        struct pcap_pkthdr* hdr = nullptr;
        const u_char* data = nullptr;
        int rc = pcap_next_ex(pcap_, &hdr, &data);
        if (rc == 0) return 0; // timeout
        if (rc < 0) {
            err = pcap_geterr(pcap_);
            return -1;
        }
        size_t n = std::min(max_len, static_cast<size_t>(hdr->caplen));
        std::copy(data, data + n, buf);
        return static_cast<int>(n);
    }
private:
    MacAddr mac_{};
    pcap_t* pcap_ = nullptr;
};
}

std::unique_ptr<EtherBackend> make_pcap_backend() {
    return std::unique_ptr<EtherBackend>(new PcapBackend());
}

#else // DINGUS_NET_ENABLE_PCAP
std::unique_ptr<EtherBackend> make_pcap_backend() {
    LOG_F(ERROR, "pcap backend requested but DINGUS_NET_ENABLE_PCAP not compiled in");
    return nullptr;
}
#endif
