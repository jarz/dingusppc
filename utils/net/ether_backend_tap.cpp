/*
 * Linux TUN/TAP backend for DingusPPC Ethernet emulation.
 *
 * Uses /dev/net/tun (IFF_TAP | IFF_NO_PI) to create a persistent
 * Layer-2 Ethernet tap interface on the host.  Packets written by the
 * guest machine appear on the tap interface and vice-versa, making
 * bridged or routed networking possible without libpcap or libslirp.
 *
 * Permissions: opening /dev/net/tun requires either root or
 * CAP_NET_ADMIN.  Bringing the interface up ("SIOCSIFFLAGS +IFF_UP")
 * also needs CAP_NET_ADMIN.  Both operations are attempted and log a
 * warning when they fail rather than aborting, so that pre-configured
 * interfaces (e.g. created with `ip tuntap add`) still work without
 * re-issuing TUNSETIFF as root.
 */

#include <utils/net/ether_backend.h>
#include <loguru.hpp>
#include <memory>

#if defined(DINGUS_NET_ENABLE_TAP) && defined(__linux__)

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
/* linux/if.h and net/if.h define overlapping structures; include only the
 * Linux kernel headers we actually need for TUN/TAP ioctls. */
#include <linux/if.h>
#include <linux/if_tun.h>

namespace {

class TapBackend final : public EtherBackend {
public:
    TapBackend() = default;
    ~TapBackend() override { close(); }

    bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) override {
        mac_ = mac;

        fd_ = ::open("/dev/net/tun", O_RDWR | O_CLOEXEC);
        if (fd_ < 0) {
            err = std::string("open /dev/net/tun: ") + strerror(errno);
            return false;
        }

        // IFF_TAP  → Layer-2 Ethernet (vs IFF_TUN which is Layer-3 IP)
        // IFF_NO_PI → no prepended packet-info header; raw Ethernet frames
        struct ifreq ifr{};
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

        // Honour explicit interface name from env or config
        std::string ifname;
        if (const char* env = std::getenv("DINGUS_NET_IFNAME")) ifname = env;
        if (!cfg.ifname.empty())                                 ifname = cfg.ifname;
        if (!ifname.empty()) {
            if (ifname.size() >= IFNAMSIZ) {
                err = "interface name too long";
                ::close(fd_); fd_ = -1;
                return false;
            }
            strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        }
        // If left blank the kernel assigns the next free "tapN" name.

        if (ioctl(fd_, TUNSETIFF, &ifr) < 0) {
            err = std::string("TUNSETIFF: ") + strerror(errno);
            ::close(fd_); fd_ = -1;
            return false;
        }

        // Record the kernel-assigned name (may differ from request when blank)
        ifname_.assign(ifr.ifr_name);
        LOG_F(INFO, "TapBackend: opened tap interface %s", ifname_.c_str());

        // Make reads non-blocking; writes may still block momentarily but
        // the guest poll loop is fine with that.
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            err = std::string("fcntl O_NONBLOCK: ") + strerror(errno);
            ::close(fd_); fd_ = -1;
            return false;
        }

        bring_up();
        return true;
    }

    void close() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // Guest → host: write raw Ethernet frame to the tap fd.
    bool send(const uint8_t* buf, size_t len, std::string& err) override {
        if (fd_ < 0) { err = "tap not open"; return false; }
        ssize_t n = ::write(fd_, buf, len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true; // temporary congestion; drop frame
            err = std::string("tap write: ") + strerror(errno);
            return false;
        }
        return true;
    }

    // Host → guest: read one raw Ethernet frame from the tap fd.
    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        if (fd_ < 0) { err = "tap not open"; return -1; }
        ssize_t n = ::read(fd_, buf, max_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // no frame available
            err = std::string("tap read: ") + strerror(errno);
            return -1;
        }
        return static_cast<int>(n);
    }

private:
    // Attempt to bring the tap interface up (IFF_UP).
    // Requires CAP_NET_ADMIN; logs a warning if the caller lacks privileges
    // but does not abort — a pre-configured interface is still usable.
    void bring_up() {
        int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (s < 0) {
            LOG_F(WARNING, "TapBackend: socket for SIOCGIFFLAGS: %s", strerror(errno));
            return;
        }

        struct ifreq ifr{};
        strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);

        if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
            LOG_F(WARNING, "TapBackend: SIOCGIFFLAGS %s: %s", ifname_.c_str(), strerror(errno));
            ::close(s);
            return;
        }

        if (ifr.ifr_flags & IFF_UP) {
            // Already up; nothing to do.
            ::close(s);
            return;
        }

        ifr.ifr_flags |= IFF_UP;
        if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) {
            LOG_F(WARNING,
                  "TapBackend: SIOCSIFFLAGS +IFF_UP on %s failed: %s "
                  "(run as root or grant CAP_NET_ADMIN, or pre-configure with "
                  "`ip tuntap add mode tap %s && ip link set %s up`)",
                  ifname_.c_str(), strerror(errno), ifname_.c_str(), ifname_.c_str());
        } else {
            LOG_F(INFO, "TapBackend: %s is up", ifname_.c_str());
        }

        ::close(s);
    }

    MacAddr mac_{};
    int fd_ = -1;
    std::string ifname_;
};

} // anonymous namespace

std::unique_ptr<EtherBackend> make_tap_backend() {
    return std::make_unique<TapBackend>();
}

#else // !(DINGUS_NET_ENABLE_TAP && __linux__)

std::unique_ptr<EtherBackend> make_tap_backend() {
    LOG_F(ERROR, "tap backend requested but DINGUS_NET_ENABLE_TAP not compiled (Linux only)");
    return nullptr;
}

#endif // DINGUS_NET_ENABLE_TAP && __linux__
