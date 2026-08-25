// The small, LLVM-free part of FRust's AOT runtime. Keeping these exports in
// their own object file lets ordinary FRate programs link without LLVM.

#include <cstdio>
#include <cstdint>
#include <iostream>

#if defined(_WIN32)
#define FRUST_RUNTIME_EXPORT extern "C" __declspec(dllexport)
#else
#define FRUST_RUNTIME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

FRUST_RUNTIME_EXPORT void frust_print_f64(double val) {
    std::cout << val << "\n";
}

FRUST_RUNTIME_EXPORT void frust_print_str(const char* val) {
    std::cout << val << "\n";
}

namespace {
constexpr int kFormatBufferCount = 16;
constexpr size_t kFormatBufferSize = 512;
thread_local char formatBufferPool[kFormatBufferCount][kFormatBufferSize];
thread_local int formatBufferIndex = 0;

char* nextFormatBuffer() {
    char* buf = formatBufferPool[formatBufferIndex];
    formatBufferIndex = (formatBufferIndex + 1) % kFormatBufferCount;
    return buf;
}
} // namespace

FRUST_RUNTIME_EXPORT const char* frust_format_i64(int64_t val) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%lld", static_cast<long long>(val));
    return buf;
}

FRUST_RUNTIME_EXPORT const char* frust_format_f64(double val) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%g", val);
    return buf;
}

FRUST_RUNTIME_EXPORT const char* frust_format_bool(bool val) {
    return val ? "true" : "false";
}

FRUST_RUNTIME_EXPORT const char* frust_str_concat(const char* a, const char* b) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%s%s", a, b);
    return buf;
}

FRUST_RUNTIME_EXPORT int64_t frust_buf_get_i64(const int64_t* base, int64_t idx) {
    return base[idx];
}

FRUST_RUNTIME_EXPORT void frust_buf_set_i64(int64_t* base, int64_t idx, int64_t val) {
    base[idx] = val;
}

FRUST_RUNTIME_EXPORT void* frust_buf_get_ptr(void* const* base, int64_t idx) {
    return base[idx];
}

FRUST_RUNTIME_EXPORT void frust_buf_set_ptr(void** base, int64_t idx, void* val) {
    base[idx] = val;
}
