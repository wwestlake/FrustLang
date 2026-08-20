// Real end-to-end verification, not just a compile check:
// - a host-provided native function callable from a plugin's `extern fn`
// - a plugin function callable from the host via frust_plugin_get_fn
// - unload
// - hot-reload: rewrite the SAME plugin source file on disk with
//   different behavior, reload, and confirm the NEW behavior actually
//   runs (not stale cached code)

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

extern "C" int64_t host_add_ten(int64_t x) {
    return x + 10;
}

static void write_plugin_v1(const std::string& path) {
    std::ofstream f(path);
    f << "extern fn host_add_ten(x: i64) -> i64;\n"
      << "pub fn plugin_double(x: i64) -> i64 = { x * 2 }\n"
      << "pub fn plugin_call_host(x: i64) -> i64 = { host_add_ten(x) }\n";
}

static void write_plugin_v2(const std::string& path) {
    std::ofstream f(path);
    f << "extern fn host_add_ten(x: i64) -> i64;\n"
      << "pub fn plugin_double(x: i64) -> i64 = { x * 100 }\n" // deliberately different
      << "pub fn plugin_call_host(x: i64) -> i64 = { host_add_ten(x) }\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: host_example <scratch_plugin_path>\n");
        return 1;
    }
    std::string path = argv[1];

    frust_plugin_register_host_function("host_add_ten", (void*)&host_add_ten);

    write_plugin_v1(path);
    FrustPluginHandle h = frust_plugin_load(path.c_str());
    if (!h) { std::fprintf(stderr, "FAIL: initial load\n"); return 1; }

    auto double_fn = (int64_t(*)(int64_t))frust_plugin_get_fn(h, "plugin_double");
    auto call_host_fn = (int64_t(*)(int64_t))frust_plugin_get_fn(h, "plugin_call_host");
    if (!double_fn || !call_host_fn) { std::fprintf(stderr, "FAIL: get_fn returned null\n"); return 1; }

    int64_t r1 = double_fn(21);       // expect 42
    int64_t r2 = call_host_fn(5);     // expect 15 (host_add_ten(5))
    std::printf("v1: double(21)=%lld call_host(5)=%lld\n", (long long)r1, (long long)r2);

    frust_plugin_unload(h);
    h = nullptr;

    // Hot-reload: change the source on disk, then reload from a FRESH
    // load() (reload() itself would re-read the same still-v1 file if we
    // hadn't rewritten it first - this proves the new content is what
    // actually runs, not a cached compilation).
    write_plugin_v2(path);
    h = frust_plugin_load(path.c_str());
    if (!h) { std::fprintf(stderr, "FAIL: reload after rewrite\n"); return 1; }

    double_fn = (int64_t(*)(int64_t))frust_plugin_get_fn(h, "plugin_double");
    if (!double_fn) { std::fprintf(stderr, "FAIL: get_fn after reload returned null\n"); return 1; }
    int64_t r3 = double_fn(21); // expect 2100 (v2's x*100), NOT 42
    std::printf("v2: double(21)=%lld\n", (long long)r3);

    frust_plugin_unload(h);

    bool ok = (r1 == 42) && (r2 == 15) && (r3 == 2100);
    std::printf("%s\n", ok ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return ok ? 0 : 1;
}
