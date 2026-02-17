#include <utils/net/ether_backend.h>
#include <deque>
#include <mutex>
#include <vector>
#include <loguru.hpp>

namespace {
class LoopbackBackend final : public EtherBackend {
public:
    bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) override {
        (void)cfg; (void)err;
        mac_ = mac;
        return true;
    }
    void close() override {}
    bool send(const uint8_t* buf, size_t len, std::string& err) override {
        (void)err;
        if (!buf || !len) return true;
        std::lock_guard<std::mutex> lock(mu_);
        queue_.emplace_back(buf, buf + len);
        LOG_F(9, "LoopbackBackend: queued %zu bytes", len);
        return true;
    }
    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        (void)err;
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.empty()) return 0;
        auto frame = std::move(queue_.front());
        queue_.pop_front();
        size_t n = std::min(max_len, frame.size());
        std::copy(frame.begin(), frame.begin() + n, buf);
        return static_cast<int>(n);
    }
private:
    MacAddr mac_{};
    std::mutex mu_;
    std::deque<std::vector<uint8_t>> queue_;
};
} // namespace

std::unique_ptr<EtherBackend> make_loopback_backend() {
    return std::unique_ptr<EtherBackend>(new LoopbackBackend());
}
