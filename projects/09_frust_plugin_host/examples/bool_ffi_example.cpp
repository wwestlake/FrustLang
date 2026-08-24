// Verification harness for LANGUAGE_GAPS.md #9 (bool-across-FFI
// convention) - never empirically tested before this session: does a
// Frust `bool` (LLVM i1) actually cross the real C ABI boundary
// cleanly in both directions, or does it need the i64-0/1 workaround
// every prior plugin in this codebase used to avoid the question?
//
// Checks the EXACT byte pattern, not just C++ truthiness (`if (b)`
// would mask a "dirty upper bits" bug, since any nonzero byte reads
// as true) - a clean result must be exactly 0x00 or 0x01, nothing else.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" int64_t host_bool_to_i64(bool b) {
    unsigned char rawByte;
    std::memcpy(&rawByte, &b, 1);
    return (static_cast<int64_t>(rawByte) << 8) | (b ? 1 : 0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: bool_ffi_example <bool_ffi_plugin.frust>\n");
        return 1;
    }

    frust_plugin_register_host_function("host_bool_to_i64", (void*)&host_bool_to_i64);

    FrustPluginHandle h = frust_plugin_load(argv[1]);
    if (!h) { std::fprintf(stderr, "FAIL: load ('%s')\n", frust_plugin_last_error()); return 1; }

    auto returns_true = (bool(*)())frust_plugin_get_fn(h, "returns_true");
    auto returns_false = (bool(*)())frust_plugin_get_fn(h, "returns_false");
    auto round_trip_true = (int64_t(*)())frust_plugin_get_fn(h, "round_trip_true");
    auto round_trip_false = (int64_t(*)())frust_plugin_get_fn(h, "round_trip_false");
    if (!returns_true || !returns_false || !round_trip_true || !round_trip_false) {
        std::fprintf(stderr, "FAIL: get_fn returned null\n");
        return 1;
    }

    // --- C reading Frust's returned bool ---
    bool rt = returns_true();
    bool rf = returns_false();
    unsigned char rtByte, rfByte;
    std::memcpy(&rtByte, &rt, 1);
    std::memcpy(&rfByte, &rf, 1);
    std::printf("returns_true()  -> byte=0x%02x truthiness=%d\n", rtByte, (int)rt);
    std::printf("returns_false() -> byte=0x%02x truthiness=%d\n", rfByte, (int)rf);
    bool returnDirectionOk = (rtByte == 0x01) && (rt == true) && (rfByte == 0x00) && (rf == false);

    // --- Frust passing a bool argument to a real C ABI host function ---
    int64_t t1 = round_trip_true();  // expect 257 = (0x01 << 8) | 1
    int64_t t2 = round_trip_false(); // expect 0   = (0x00 << 8) | 0
    std::printf("round_trip_true()  = %lld (expect 257)\n", (long long)t1);
    std::printf("round_trip_false() = %lld (expect 0)\n", (long long)t2);
    bool argDirectionOk = (t1 == 257) && (t2 == 0);

    frust_plugin_unload(h);

    bool ok = returnDirectionOk && argDirectionOk;
    std::printf("%s\n", ok ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return ok ? 0 : 1;
}
