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
#include <chrono>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace {

// Minimal timer handle used by the libslirp timer callbacks.
struct SlirpTimer {
    SlirpTimerCb cb       = nullptr;
    void*        cb_opaque = nullptr;
    int64_t      expire_ms = INT64_MAX;
    bool         active    = false;
};

class SlirpBackend final : public EtherBackend {
public:
    SlirpBackend() = default;
    ~SlirpBackend() override { close(); }

    bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) override {
        (void)cfg;
        mac_ = mac;

        // Wire up all required SlirpCb fields for libslirp 4.x
        SlirpCb cb{};
        cb.send_packet        = &SlirpBackend::cb_send_packet;
        cb.guest_error        = &SlirpBackend::cb_guest_error;
        cb.clock_get_ns       = &SlirpBackend::cb_clock_get_ns;
        cb.timer_new          = &SlirpBackend::cb_timer_new;
        cb.timer_free         = &SlirpBackend::cb_timer_free;
        cb.timer_mod          = &SlirpBackend::cb_timer_mod;
        cb.register_poll_fd   = &SlirpBackend::cb_register_poll_fd;
        cb.unregister_poll_fd = &SlirpBackend::cb_unregister_poll_fd;
        cb.notify             = &SlirpBackend::cb_notify;

        // Minimal IPv4-only config (version 1 fields), same address scheme as QEMU
        SlirpConfig c{};
        c.version     = 1;
        c.in_enabled  = true;
        c.in6_enabled = false;
        inet_pton(AF_INET, "10.0.2.0",   &c.vnetwork);
        inet_pton(AF_INET, "255.255.255.0", &c.vnetmask);
        inet_pton(AF_INET, "10.0.2.2",   &c.vhost);
        inet_pton(AF_INET, "10.0.2.15",  &c.vdhcp_start);
        inet_pton(AF_INET, "10.0.2.3",   &c.vnameserver);

        slirp_ = slirp_new(&c, &cb, this);
        if (!slirp_) {
            err = "slirp_new returned null";
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
        poll_fds_.clear();
        timers_.clear();
    }

    // Guest → slirp: hand the Ethernet frame to the userspace TCP/IP stack.
    bool send(const uint8_t* buf, size_t len, std::string& err) override {
        if (!slirp_) { err = "slirp not initialized"; return false; }
        slirp_input(slirp_, buf, static_cast<int>(len));
        return true;
    }

    // Drive the slirp event loop then return the next pending frame for the guest.
    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        if (!slirp_) { err = "slirp not initialized"; return -1; }
        pump();
        std::lock_guard<std::mutex> _{mu_};
        if (out_q_.empty()) return 0;
        auto frame = std::move(out_q_.front());
        out_q_.pop_front();
        size_t n = std::min(max_len, frame.size());
        std::copy(frame.begin(), frame.begin() + n, buf);
        return static_cast<int>(n);
    }

private:
    // Called by slirp when it wants to send a frame to the guest.
    void enqueue_out(const void* pkt, size_t len) {
        if (!pkt || !len) return;
        const auto* p = reinterpret_cast<const uint8_t*>(pkt);
        std::lock_guard<std::mutex> _{mu_};
        out_q_.emplace_back(p, p + len);
    }

    // Drive slirp's internal event loop (poll registered fds + fire timers).
    // Called single-threaded from recv(); add_poll_vec_ is not shared.
    void pump() {
        uint32_t timeout = 0;

        // Let slirp register which fds it wants to watch.
        slirp_pollfds_fill(slirp_, &timeout,
            [](int fd, int events, void* opaque) -> int {
                auto* self = reinterpret_cast<SlirpBackend*>(opaque);
                self->add_poll_vec_.push_back({fd, (short)slirp_to_poll(events), 0});
                return static_cast<int>(self->add_poll_vec_.size() - 1);
            }, this);

        // Non-blocking poll (timeout=0; we're called periodically from the device loop).
        if (!add_poll_vec_.empty())
            ::poll(add_poll_vec_.data(), (nfds_t)add_poll_vec_.size(), 0);

        // Report results back to slirp.
        slirp_pollfds_poll(slirp_, 0,
            [](int idx, void* opaque) -> int {
                auto* self = reinterpret_cast<SlirpBackend*>(opaque);
                if (idx < 0 || (size_t)idx >= self->add_poll_vec_.size()) return 0;
                return poll_to_slirp(self->add_poll_vec_[idx].revents);
            }, this);

        add_poll_vec_.clear();

        // Fire any expired timers.
        int64_t now_ms = cb_clock_get_ns(this) / 1'000'000LL;
        for (auto& t : timers_) {
            if (t && t->active && t->expire_ms <= now_ms) {
                t->active = false;
                if (t->cb) t->cb(t->cb_opaque);
            }
        }
    }

    static int slirp_to_poll(int e) {
        int r = 0;
        if (e & SLIRP_POLL_IN)  r |= POLLIN;
        if (e & SLIRP_POLL_OUT) r |= POLLOUT;
        if (e & SLIRP_POLL_PRI) r |= POLLPRI;
        if (e & SLIRP_POLL_ERR) r |= POLLERR;
        if (e & SLIRP_POLL_HUP) r |= POLLHUP;
        return r;
    }
    static int poll_to_slirp(short r) {
        int e = 0;
        if (r & POLLIN)  e |= SLIRP_POLL_IN;
        if (r & POLLOUT) e |= SLIRP_POLL_OUT;
        if (r & POLLPRI) e |= SLIRP_POLL_PRI;
        if (r & POLLERR) e |= SLIRP_POLL_ERR;
        if (r & POLLHUP) e |= SLIRP_POLL_HUP;
        return e;
    }

    // ---- Static SlirpCb callbacks ----
    static slirp_ssize_t cb_send_packet(const void* pkt, size_t len, void* opaque) {
        reinterpret_cast<SlirpBackend*>(opaque)->enqueue_out(pkt, len);
        return static_cast<slirp_ssize_t>(len);
    }
    static void cb_guest_error(const char* msg, void* /*opaque*/) {
        LOG_F(WARNING, "slirp guest error: %s", msg);
    }
    static int64_t cb_clock_get_ns(void* /*opaque*/) {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }
    static void* cb_timer_new(SlirpTimerCb cb, void* cb_opaque, void* opaque) {
        auto* self = reinterpret_cast<SlirpBackend*>(opaque);
        auto t = std::make_unique<SlirpTimer>();
        t->cb        = cb;
        t->cb_opaque = cb_opaque;
        SlirpTimer* raw = t.get();
        self->timers_.push_back(std::move(t));
        return raw;
    }
    static void cb_timer_free(void* timer, void* opaque) {
        auto* self = reinterpret_cast<SlirpBackend*>(opaque);
        auto& v = self->timers_;
        v.erase(std::remove_if(v.begin(), v.end(),
            [timer](const std::unique_ptr<SlirpTimer>& t){ return t.get() == timer; }),
            v.end());
    }
    static void cb_timer_mod(void* timer, int64_t expire_time_ms, void* /*opaque*/) {
        auto* t = reinterpret_cast<SlirpTimer*>(timer);
        t->expire_ms = expire_time_ms;
        t->active    = true;
    }
    static void cb_register_poll_fd(int fd, void* opaque) {
        auto* self = reinterpret_cast<SlirpBackend*>(opaque);
        std::lock_guard<std::mutex> _{self->mu_};
        self->poll_fds_.push_back(fd);
    }
    static void cb_unregister_poll_fd(int fd, void* opaque) {
        auto* self = reinterpret_cast<SlirpBackend*>(opaque);
        std::lock_guard<std::mutex> _{self->mu_};
        auto& v = self->poll_fds_;
        v.erase(std::remove(v.begin(), v.end(), fd), v.end());
    }
    static void cb_notify(void* /*opaque*/) {}

    MacAddr mac_{};
    Slirp*  slirp_ = nullptr;
    std::mutex mu_;
    std::deque<std::vector<uint8_t>>        out_q_;
    std::vector<int>                        poll_fds_;  // guarded by mu_
    std::vector<struct pollfd>              add_poll_vec_; // temp per pump(); single-threaded
    std::vector<std::unique_ptr<SlirpTimer>> timers_;
};

} // namespace

std::unique_ptr<EtherBackend> make_slirp_backend() {
    return std::make_unique<SlirpBackend>();
}

#else // DINGUS_NET_ENABLE_SLIRP
std::unique_ptr<EtherBackend> make_slirp_backend() {
    LOG_F(ERROR, "libslirp backend requested but DINGUS_NET_ENABLE_SLIRP not compiled in");
    return nullptr;
}
#endif

