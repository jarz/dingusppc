#include <utils/net/ether_backend.h>
#include "mock_error_backend.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

extern "C" int test_ether_backends();
extern "C" int test_tap_backend();

static int test_null_backend() {
    int failures = 0;
    auto be = make_ether_backend("null");
    if (!be) {
        std::cerr << "FAIL: make_ether_backend(\"null\") returned nullptr" << std::endl;
        return 1;
    }
    MacAddr mac = {{0x02,0x00,0xDE,0xAD,0xBE,0xEF}};
    EtherConfig cfg{};
    std::string err;
    if (!be->open(mac, cfg, err)) {
        std::cerr << "FAIL: null backend open failed: " << err << std::endl;
        return 1;
    }
    uint8_t frame[64]{};
    // send should succeed silently
    if (!be->send(frame, sizeof(frame), err)) {
        std::cerr << "FAIL: null backend send failed" << std::endl;
        ++failures;
    }
    // recv should always return 0 (no data)
    uint8_t buf[128]{};
    int n = be->recv(buf, sizeof(buf), err);
    if (n != 0) {
        std::cerr << "FAIL: null backend recv returned " << n << " (expected 0)" << std::endl;
        ++failures;
    }
    be->close();
    return failures;
}

static int test_loopback_roundtrip() {
    int failures = 0;
    auto be = make_loopback_backend();
    if (!be) {
        std::cerr << "FAIL: make_loopback_backend returned nullptr" << std::endl;
        return 1;
    }
    MacAddr mac = {{0x02,0x00,0xDE,0xAD,0xBE,0xEF}};
    EtherConfig cfg{};
    std::string err;
    if (!be->open(mac, cfg, err)) {
        std::cerr << "FAIL: loopback open failed: " << err << std::endl;
        return 1;
    }

    // send a frame, recv it back
    uint8_t frame[64];
    for (int i = 0; i < 64; ++i) frame[i] = static_cast<uint8_t>(i ^ 0xAA);
    if (!be->send(frame, sizeof(frame), err)) {
        std::cerr << "FAIL: loopback send failed" << std::endl;
        ++failures;
    }
    uint8_t buf[128]{};
    int n = be->recv(buf, sizeof(buf), err);
    if (n != 64) {
        std::cerr << "FAIL: loopback recv returned " << n << " (expected 64)" << std::endl;
        ++failures;
    } else if (std::memcmp(buf, frame, 64) != 0) {
        std::cerr << "FAIL: loopback recv data mismatch" << std::endl;
        ++failures;
    }

    // recv when empty
    n = be->recv(buf, sizeof(buf), err);
    if (n != 0) {
        std::cerr << "FAIL: loopback recv should return 0 when empty, got " << n << std::endl;
        ++failures;
    }

    // multiple frames queued in order
    for (int f = 0; f < 5; ++f) {
        uint8_t pkt[32];
        std::memset(pkt, f, sizeof(pkt));
        be->send(pkt, sizeof(pkt), err);
    }
    for (int f = 0; f < 5; ++f) {
        uint8_t pkt[32]{};
        n = be->recv(pkt, sizeof(pkt), err);
        if (n != 32) {
            std::cerr << "FAIL: loopback multi-frame recv " << f << " returned " << n << std::endl;
            ++failures;
            break;
        }
        if (pkt[0] != static_cast<uint8_t>(f)) {
            std::cerr << "FAIL: loopback multi-frame ordering wrong at frame " << f
                      << " got " << (int)pkt[0] << std::endl;
            ++failures;
            break;
        }
    }

    // double close is safe
    be->close();
    be->close();
    return failures;
}

static int test_factory_unknown_name() {
    int failures = 0;
    // unknown backend name should fall back to null (not crash)
    auto be = make_ether_backend("nonexistent_backend_xyz");
    if (!be) {
        std::cerr << "FAIL: unknown backend name returned nullptr instead of fallback" << std::endl;
        ++failures;
    } else {
        MacAddr mac{};
        EtherConfig cfg{};
        std::string err;
        be->open(mac, cfg, err);
        // should behave like null
        uint8_t buf[64]{};
        int n = be->recv(buf, sizeof(buf), err);
        if (n != 0) {
            std::cerr << "FAIL: fallback backend recv returned non-zero" << std::endl;
            ++failures;
        }
        be->close();
    }
    return failures;
}

static int test_loopback_large_frame() {
    int failures = 0;
    auto be = make_loopback_backend();
    MacAddr mac{};
    EtherConfig cfg{};
    std::string err;
    be->open(mac, cfg, err);

    // send 1514-byte frame (max Ethernet without FCS)
    std::vector<uint8_t> big_frame(1514);
    for (size_t i = 0; i < big_frame.size(); ++i)
        big_frame[i] = static_cast<uint8_t>(i & 0xFF);
    if (!be->send(big_frame.data(), big_frame.size(), err)) {
        std::cerr << "FAIL: loopback send large frame failed" << std::endl;
        ++failures;
    }
    std::vector<uint8_t> recv_buf(2048);
    int n = be->recv(recv_buf.data(), recv_buf.size(), err);
    if (n != 1514) {
        std::cerr << "FAIL: loopback large frame recv returned " << n << std::endl;
        ++failures;
    } else if (std::memcmp(recv_buf.data(), big_frame.data(), 1514) != 0) {
        std::cerr << "FAIL: loopback large frame data mismatch" << std::endl;
        ++failures;
    }

    be->close();
    return failures;
}

static int test_loopback_zero_length() {
    int failures = 0;
    auto be = make_loopback_backend();
    MacAddr mac{};
    EtherConfig cfg{};
    std::string err;
    be->open(mac, cfg, err);

    // send zero-length: should not crash, returns true
    if (!be->send(nullptr, 0, err)) {
        std::cerr << "FAIL: loopback send zero-length failed" << std::endl;
        ++failures;
    }
    // recv should still return 0 (zero-len sends are no-ops)
    uint8_t buf[64]{};
    int n = be->recv(buf, sizeof(buf), err);
    if (n != 0) {
        std::cerr << "FAIL: loopback recv after zero-length send returned " << n << std::endl;
        ++failures;
    }

    be->close();
    return failures;
}

int test_ether_backends() {
    int failures = 0;
    failures += test_null_backend();
    failures += test_loopback_roundtrip();
    failures += test_factory_unknown_name();
    failures += test_loopback_large_frame();
    failures += test_loopback_zero_length();
    return failures;
}

// ---------------------------------------------------------------------------
// TAP backend tests (Linux /dev/net/tun)
// All tests are safe without root; privilege-gated scenarios are skipped.
// ---------------------------------------------------------------------------

// 1. Factory registration: make_ether_backend("tap") must not return nullptr
//    when the TAP backend is compiled in.
static int test_tap_factory_registration() {
    int failures = 0;
    auto be = make_ether_backend("tap");
#if defined(DINGUS_NET_ENABLE_TAP) && defined(__linux__)
    if (!be) {
        std::cerr << "FAIL: make_ether_backend(\"tap\") returned nullptr "
                     "(DINGUS_NET_ENABLE_TAP is defined)" << std::endl;
        ++failures;
    }
#else
    // Backend not compiled in; nullptr is expected.
    (void)be;
#endif
    return failures;
}

// 2. Overlong interface name must be rejected before touching the kernel.
static int test_tap_open_overlong_ifname() {
    int failures = 0;
#if defined(DINGUS_NET_ENABLE_TAP) && defined(__linux__)
    auto be = make_tap_backend();
    if (!be) {
        std::cerr << "FAIL: make_tap_backend() returned nullptr" << std::endl;
        return 1;
    }
    MacAddr mac{};
    EtherConfig cfg{};
    cfg.ifname = std::string(64, 'x'); // 64-chars > IFNAMSIZ (16)
    std::string err;
    bool ok = be->open(mac, cfg, err);
    if (ok) {
        std::cerr << "FAIL: tap open with overlong ifname should return false" << std::endl;
        ++failures;
        be->close();
    } else if (err.empty()) {
        std::cerr << "FAIL: tap open with overlong ifname should set err" << std::endl;
        ++failures;
    }
#endif
    return failures;
}

// 3. send() / recv() on a never-opened backend must return error rather than crash.
static int test_tap_send_recv_before_open() {
    int failures = 0;
#if defined(DINGUS_NET_ENABLE_TAP) && defined(__linux__)
    auto be = make_tap_backend();
    if (!be) {
        std::cerr << "FAIL: make_tap_backend() returned nullptr" << std::endl;
        return 1;
    }

    std::string err;
    uint8_t buf[64]{};
    bool sent = be->send(buf, sizeof(buf), err);
    if (sent) {
        std::cerr << "FAIL: tap send before open should return false" << std::endl;
        ++failures;
    } else if (err.empty()) {
        std::cerr << "FAIL: tap send before open should set err" << std::endl;
        ++failures;
    }

    err.clear();
    int n = be->recv(buf, sizeof(buf), err);
    if (n >= 0) {
        std::cerr << "FAIL: tap recv before open should return <0, got " << n << std::endl;
        ++failures;
    } else if (err.empty()) {
        std::cerr << "FAIL: tap recv before open should set err" << std::endl;
        ++failures;
    }
#endif
    return failures;
}

// 4. Double-close must be safe (no crash, no assertion failure).
static int test_tap_double_close() {
#if defined(DINGUS_NET_ENABLE_TAP) && defined(__linux__)
    auto be = make_tap_backend();
    if (!be) {
        std::cerr << "FAIL: make_tap_backend() returned nullptr" << std::endl;
        return 1;
    }
    be->close(); // close without ever opening
    be->close(); // second close must not crash
#endif
    return 0;
}

// 5. Runtime open smoke-test: attempt to open a TAP device; skip (not fail)
//    if /dev/net/tun is inaccessible (no CAP_NET_ADMIN / not root).
//    When it does succeed, exercise a minimal send→recv round trip via the
//    kernel (the kernel echoes nothing, but send must return true and recv 0).
static int test_tap_open_smoke() {
    int failures = 0;
#if defined(DINGUS_NET_ENABLE_TAP) && defined(__linux__)
    // Skip silently if we cannot access /dev/net/tun at all.
    if (access("/dev/net/tun", R_OK | W_OK) != 0) {
        std::cout << "  [tap smoke] skipped: /dev/net/tun not accessible" << std::endl;
        return 0;
    }

    auto be = make_tap_backend();
    if (!be) {
        std::cerr << "FAIL: make_tap_backend() returned nullptr" << std::endl;
        return 1;
    }

    MacAddr mac = {{0x02,0xDE,0xAD,0xBE,0xEF,0x01}};
    EtherConfig cfg{};
    // Leave cfg.ifname empty → kernel picks the next free tapN.
    std::string err;
    if (!be->open(mac, cfg, err)) {
        // TUNSETIFF can fail if we have read access to /dev/net/tun but lack
        // CAP_NET_ADMIN for TUNSETIFF itself.  Skip rather than fail.
        std::cout << "  [tap smoke] skipped: open failed (" << err << ")" << std::endl;
        return 0;
    }

    // send: a minimal 60-byte Ethernet frame (all zeros → broadcast dest, our
    // mac as src, ARP ether-type, zero payload).  Must not crash or error.
    uint8_t frame[60]{};
    frame[12] = 0x08; frame[13] = 0x06; // EtherType = ARP (arbitrary)
    if (!be->send(frame, sizeof(frame), err)) {
        std::cerr << "FAIL: tap send failed: " << err << std::endl;
        ++failures;
    }

    // recv: non-blocking; 0 = no incoming frame is fine.
    uint8_t buf[2048]{};
    int n = be->recv(buf, sizeof(buf), err);
    if (n < 0) {
        std::cerr << "FAIL: tap recv returned error: " << err << std::endl;
        ++failures;
    }

    be->close();
#endif
    return failures;
}

int test_tap_backend() {
    int failures = 0;
    failures += test_tap_factory_registration();
    failures += test_tap_open_overlong_ifname();
    failures += test_tap_send_recv_before_open();
    failures += test_tap_double_close();
    failures += test_tap_open_smoke();
    return failures;
}

