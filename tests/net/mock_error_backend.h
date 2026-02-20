#pragma once

#include <utils/net/ether_backend.h>
#include <cstring>
#include <vector>

// MockErrorBackend — injectable backend that can simulate send/recv failures
class MockErrorBackend : public EtherBackend {
public:
    bool fail_send = false;
    bool fail_recv = false;
    std::vector<uint8_t> rx_frame; // queued frame for recv()

    bool open(const MacAddr&, const EtherConfig&, std::string&) override { return true; }
    void close() override {}

    bool send(const uint8_t*, size_t, std::string& err) override {
        if (fail_send) { err = "mock send error"; return false; }
        return true;
    }

    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        if (fail_recv) { err = "mock recv error"; return -1; }
        if (rx_frame.empty()) return 0;
        size_t n = std::min(max_len, rx_frame.size());
        std::memcpy(buf, rx_frame.data(), n);
        rx_frame.clear();
        return static_cast<int>(n);
    }
};
