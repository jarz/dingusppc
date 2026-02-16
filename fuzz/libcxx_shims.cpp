// Minimal libc++ symbol shims to make libFuzzer link on macOS (Apple Silicon)
// with toolchains that don't ship libclang_rt.fuzzer_osx.a or that lack
// newer libc++ internals (e.g., __hash_memory). These are benign fallbacks
// for local repro only; CI (Ubuntu/clang) will use the real implementation.

#include <cstddef>
#include <cstdint>

// __hash_memory appeared in newer libc++ builds. Provide a simple fallback
// implementation so libFuzzer (which references it) links successfully.
namespace std {
inline namespace __1 {
std::size_t __hash_memory(const void *ptr, std::size_t len) {
    // FNV-1a 64-bit (fast enough for fuzzing; deterministic)
    const auto *p = static_cast<const std::uint8_t *>(ptr);
    std::size_t h = 1469598103934665603ULL;
    for (std::size_t i = 0; i < len; ++i) {
        h ^= static_cast<std::size_t>(p[i]);
        h *= 1099511628211ULL;
    }
    return h;
}
} // namespace __1
} // namespace std
