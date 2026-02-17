#include <utils/net/ether_backend.h>
#include <loguru.hpp>
#include <memory>

// Optional libslirp backend. This is a stub unless DINGUS_NET_ENABLE_SLIRP is defined
// and libslirp headers are available. We isolate the dependency to keep builds light.

#if defined(DINGUS_NET_ENABLE_SLIRP)
extern "C" {
#if __has_include(<slirp/libslirp.h>)
#include <slirp/libslirp.h>
#elif __has_include(<libslirp.h>)
#include <libslirp.h>
#else
#error "libslirp headers not found"
#endif
}

#include <mutex>
#include <deque>
#include <vector>
#include <algorithm>
#include <poll.h>
#include <chrono>

namespace {
class SlirpBackend final : public EtherBackend {
public:
    SlirpBackend() = default;
    ~SlirpBackend() override { close(); }

    bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) override {
        (void)cfg;
        mac_ = mac;
        SlirpConfig c{};
#if !defined(slirp_init_config)
        // Older libslirp might not define slirp_init_config; zero-init is acceptable.
#else
        slirp_init_config(&c);
#endif
        // Provide reasonable defaults (user-mode NAT)
        // c.version defaults are fine; caller can tweak in future via config props.

        // Prepare callbacks
        SlirpCb cb{};
        // send_packet signature changed across libslirp versions; support both.
#if defined(SLIRP_VERSION_MAJOR) && (SLIRP_VERSION_MAJOR >= 4)
        cb.send_packet = [](Slirp* /*s*/, const void* pkt, size_t len, void* opaque) -> ssize_t {
            auto* self = reinterpret_cast<SlirpBackend*>(opaque);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(pkt);
            self->enqueue_out(p, len);
            return static_cast<ssize_t>(len);
        };
#else
        cb.send_packet = [](const void* pkt, size_t len, void* opaque) {
            auto* self = reinterpret_cast<SlirpBackend*>(opaque);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(pkt);
            self->enqueue_out(p, len);
        };
#endif
        cb.guest_error = [](const char* msg, void* opaque) {
            (void)opaque; LOG_F(WARNING, "slirp guest error: %s", msg);
        };
#if defined(SLIRP_VERSION_MAJOR)
        cb.clock_get_ns = []() -> int64_t {
            using namespace std::chrono;
            return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        };
#endif

        // Some libslirp versions don't export slirp_init_config; guard accordingly.
#if defined(slirp_init_config)
        slirp_init_config(&c);
#endif

        slirp_ = slirp_new(&c, &cb, this);
        if (!slirp_) {
            err = "slirp_new failed";
            return false;
        }
        return true;
    }

    void close() override {
        if (slirp_) {
            slirp_cleanup(slirp_);
            slirp_ = nullptr;
        }
        std::lock_guard<std::mutex> _{mu_};
        out_q_.clear();
    }

    bool send(const uint8_t* buf, size_t len, std::string& err) override {
        if (!slirp_) { err = "slirp not initialized"; return false; }
        slirp_input(slirp_, buf, static_cast<int>(len));
        return true;
    }

    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        if (!slirp_) { err = "slirp not initialized"; return -1; }
        // Drive slirp timers/poll; this mirrors qemu's slirp integration.
        pump_poll();
        std::vector<uint8_t> frame;
        {
            std::lock_guard<std::mutex> _{mu_};
            if (out_q_.empty()) return 0;
            frame = std::move(out_q_.front());
            out_q_.pop_front();
        }
        size_t n = std::min(max_len, frame.size());
        std::copy(frame.begin(), frame.begin() + n, buf);
        return static_cast<int>(n);
    }

private:
    void enqueue_out(const uint8_t* p, size_t len) {
        if (!p || !len) return;
        std::lock_guard<std::mutex> _{mu_};
        out_q_.emplace_back(p, p + len);
    }

    void pump_poll() {
#if defined(SLIRP_VERSION_MAJOR) && (SLIRP_VERSION_MAJOR >= 4)
        // Modern libslirp exposes poll helpers (used by qemu >= 5.0)
        struct pollfd pfds[8];
        int nfds = 0;
        int timeout = 0;
        if (slirp_pollfds_fill(slirp_, pfds, &nfds, &timeout) == 0) {
            slirp_pollfds_poll(slirp_, pfds, nfds, 0);
        }
#elif defined(slirp_poll)
        // Fallback: tickle timers; older libslirp may expose slirp_poll
        slirp_poll(slirp_, 0);
#else
        // No-op fallback; recv will just see empty queue.
#endif
    }

    MacAddr mac_{};
    Slirp* slirp_ = nullptr;
    std::mutex mu_;
    std::deque<std::vector<uint8_t>> out_q_;
};
} // namespace

std::unique_ptr<EtherBackend> make_slirp_backend() {
    return std::unique_ptr<EtherBackend>(new SlirpBackend());
}

#else // DINGUS_NET_ENABLE_SLIRP
std::unique_ptr<EtherBackend> make_slirp_backend() {
    LOG_F(ERROR, "libslirp backend requested but DINGUS_NET_ENABLE_SLIRP not compiled in");
    return nullptr;
}
#endif
