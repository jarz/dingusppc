#include <utils/net/ether_backend.h>
#include <loguru.hpp>
#include <memory>

#if defined(__APPLE__) && defined(DINGUS_NET_ENABLE_VMNET)
#if __has_include(<vmnet/vmnet.h>)
#include <vmnet/vmnet.h> // for types/constants only
#define DINGUS_HAVE_VMNET_HEADERS 1
#endif
#include <xpc/xpc.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <sys/uio.h>

/**
 * Dynamic loader helper
 */
template<typename T>
static bool load_sym(void* handle, const char* name, T& out) {
    out = reinterpret_cast<T>(dlsym(handle, name));
    return out != nullptr;
}

// Use decltype to get correct signatures when headers are available
#if defined(DINGUS_HAVE_VMNET_HEADERS)
using start_fn_t = decltype(&vmnet_start_interface);
using stop_fn_t  = decltype(&vmnet_stop_interface);
using write_fn_t = decltype(&vmnet_write);
using read_fn_t  = decltype(&vmnet_read);
#else
// Fallback shapes (best effort) to keep the dynamic loader working even
// when vmnet headers are not present at compile time.
typedef void* interface_ref;
typedef long vmnet_return_t;
using start_fn_t = interface_ref (*)(xpc_object_t, dispatch_queue_t, void*);
using stop_fn_t  = vmnet_return_t (*)(interface_ref, dispatch_queue_t, void*);
using write_fn_t = vmnet_return_t (*)(interface_ref, struct vmpktdesc*, int*);
using read_fn_t  = vmnet_return_t (*)(interface_ref, struct vmpktdesc*, int*);
#endif

typedef interface_ref (*create_fn_t)(xpc_object_t, vmnet_return_t*);

struct VmnetDynSyms {
    create_fn_t   create_interface = nullptr;
    start_fn_t    start_interface  = nullptr;
    stop_fn_t     stop_interface   = nullptr;
    void        (*release)(interface_ref) = nullptr;
    write_fn_t    write            = nullptr;
    read_fn_t     read             = nullptr;
    const char*   op_mode_key      = nullptr;
};

static VmnetDynSyms vmnet_dyn;
static bool vmnet_loaded = false;
static bool vmnet_tried  = false;

static bool vmnet_load_syms(std::string& err) {
    if (vmnet_loaded) return true;
    if (vmnet_tried) return false;
    vmnet_tried = true;
    void* h = dlopen("/System/Library/Frameworks/vmnet.framework/vmnet", RTLD_LAZY);
    if (!h) { err = dlerror(); return false; }

    bool ok = true;
    ok &= load_sym(h, "vmnet_start_interface",  vmnet_dyn.start_interface);
    ok &= load_sym(h, "vmnet_stop_interface",   vmnet_dyn.stop_interface);
    ok &= load_sym(h, "vmnet_release",          vmnet_dyn.release);
    ok &= load_sym(h, "vmnet_write",            vmnet_dyn.write);
    ok &= load_sym(h, "vmnet_read",             vmnet_dyn.read);
    vmnet_dyn.op_mode_key = reinterpret_cast<const char*>(dlsym(h, "vmnet_operation_mode_key"));

    if (!ok || !vmnet_dyn.op_mode_key) {
        err = "vmnet symbols missing";
        return false;
    }
    vmnet_loaded = true;
    return true;
}

namespace {
class VmnetBackend final : public EtherBackend {
public:
    VmnetBackend() = default;
    ~VmnetBackend() override { close(); }
    bool open(const MacAddr& mac, const EtherConfig& cfg, std::string& err) override {
        (void)cfg;
        mac_ = mac;
        if (!vmnet_load_syms(err)) {
            LOG_F(ERROR, "vmnet dlopen failed: %s", err.c_str());
            return false;
        }
        dispatch_queue_ = dispatch_queue_create("dingusppc.vmnet", DISPATCH_QUEUE_SERIAL);
        xpc_object_t params = xpc_dictionary_create(nullptr, nullptr, 0);
        // Shared NAT by default
        xpc_dictionary_set_uint64(params, vmnet_dyn.op_mode_key, VMNET_SHARED_MODE);
        vmnet_return_t status = VMNET_SUCCESS;
        // vmnet_start_interface both creates and starts; signature matches header (xpc desc -> interface_ref)
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        iface_ = vmnet_dyn.start_interface(params, dispatch_queue_, ^(vmnet_return_t st, xpc_object_t){ start_status_ = st; dispatch_semaphore_signal(sem); });
        xpc_release(params);
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        dispatch_release(sem);
        if (!iface_ || start_status_ != VMNET_SUCCESS) {
            err = "vmnet_start_interface failed";
            iface_ = nullptr;
            return false;
        }
        return true;
    }
    void close() override {
        if (iface_) {
            vmnet_dyn.stop_interface(iface_, dispatch_queue_, ^(vmnet_return_t){ });
            vmnet_dyn.release(iface_);
            iface_ = nullptr;
        }
        if (dispatch_queue_) { dispatch_release(dispatch_queue_); dispatch_queue_ = nullptr; }
    }
    bool send(const uint8_t* buf, size_t len, std::string& err) override {
        if (!iface_) { err = "vmnet iface null"; return false; }
        struct vmpktdesc pkt{};
        struct iovec iov{};
        iov.iov_base = const_cast<uint8_t*>(buf);
        iov.iov_len  = len;
        pkt.vm_pkt_size   = static_cast<uint32_t>(len);
        pkt.vm_pkt_iovcnt = 1;
        pkt.vm_pkt_iov    = &iov;
        int pktcnt = 1;
        vmnet_return_t rc = vmnet_dyn.write(iface_, &pkt, &pktcnt);
        if (rc != VMNET_SUCCESS || pktcnt <= 0) {
            err = "vmnet_write failed";
            return false;
        }
        return true;
    }
    int recv(uint8_t* buf, size_t max_len, std::string& err) override {
        if (!iface_) { err = "vmnet iface null"; return -1; }
        struct vmpktdesc pkt{};
        struct iovec iov{};
        iov.iov_base = buf;
        iov.iov_len  = max_len;
        pkt.vm_pkt_iovcnt = 1;
        pkt.vm_pkt_iov    = &iov;
        int pktcnt = 1;
        vmnet_return_t rc = vmnet_dyn.read(iface_, &pkt, &pktcnt);
        if (rc != VMNET_SUCCESS || pktcnt <= 0) return 0;
        return static_cast<int>(pkt.vm_pkt_size);
    }
private:
    MacAddr mac_{};
    vmnet_return_t start_status_ = VMNET_SUCCESS;
    interface_ref iface_ = nullptr;
    dispatch_queue_t dispatch_queue_ = nullptr;
};
} // namespace

std::unique_ptr<EtherBackend> make_vmnet_backend() {
#if defined(__APPLE__) && defined(DINGUS_NET_ENABLE_VMNET)
    return std::unique_ptr<EtherBackend>(new VmnetBackend());
#else
    LOG_F(ERROR, "vmnet backend requested but vmnet.framework is not enabled/available");
    return nullptr;
#endif
}

#else // not enabled
std::unique_ptr<EtherBackend> make_vmnet_backend() {
    LOG_F(ERROR, "vmnet backend requested but DINGUS_NET_ENABLE_VMNET not compiled in or not on macOS");
    return nullptr;
}
#endif
