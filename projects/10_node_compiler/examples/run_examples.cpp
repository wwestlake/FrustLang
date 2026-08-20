// Real end-to-end verification: graph JSON -> generated .frust source ->
// loaded via frust_plugin_host -> called -> checked against the
// mathematically expected result. Not "does it compile" alone - "does
// it produce the right answer."

#include "node_compiler/NodeCompiler.h"
#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

extern "C" int64_t host_add_ten(int64_t x) {
    return x + 10;
}

// frust_print_str/frust_format_i64 are runtime helpers exported from
// frust_compiler's/the IDE's own Main.cpp - not something every host
// process defines. This test harness is a generic host (like a real
// Creation Suite integration would be), so a `print` node's generated
// `extern fn`s need these registered explicitly via
// frust_plugin_register_host_function(), the same way any other host-
// provided function would be. Minimal but real implementations, not
// stubs - genuinely format/print, matching the real ones' semantics.
static thread_local char g_fmtBuf[64];
extern "C" const char* local_frust_format_i64(int64_t val) {
    std::snprintf(g_fmtBuf, sizeof(g_fmtBuf), "%lld", (long long)val);
    return g_fmtBuf;
}
extern "C" void local_frust_print_str(const char* val) {
    std::printf("[plugin print] %s\n", val);
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool CompileLoadAndCheck(const std::string& jsonPath, const std::string& scratchDir, int64_t arg, int64_t expected) {
    std::string json = ReadFile(jsonPath);
    if (json.empty()) { std::fprintf(stderr, "FAIL: could not read %s\n", jsonPath.c_str()); return false; }

    char* source = node_compiler_compile(json.c_str());
    if (!source) {
        std::fprintf(stderr, "FAIL: node_compiler_compile(%s): %s\n", jsonPath.c_str(), node_compiler_last_error());
        return false;
    }
    std::printf("--- generated from %s ---\n%s\n", jsonPath.c_str(), source);

    std::string outPath = scratchDir + "/generated.frust";
    std::ofstream out(outPath);
    out << source;
    out.close();
    node_compiler_free_string(source);

    FrustPluginHandle h = frust_plugin_load(outPath.c_str());
    if (!h) { std::fprintf(stderr, "FAIL: frust_plugin_load(%s)\n", outPath.c_str()); return false; }

    auto fn = (int64_t(*)(int64_t))frust_plugin_get_fn(h, "compute");
    if (!fn) { std::fprintf(stderr, "FAIL: get_fn(compute)\n"); frust_plugin_unload(h); return false; }

    int64_t actual = fn(arg);
    frust_plugin_unload(h);

    bool ok = actual == expected;
    std::printf("%s: compute(%lld) = %lld (expected %lld) - %s\n\n",
        jsonPath.c_str(), (long long)arg, (long long)actual, (long long)expected, ok ? "OK" : "MISMATCH");
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: run_examples <scratch_dir>\n"); return 1; }
    std::string scratchDir = argv[1];
    std::string examplesDir = "D:/000 Tech Research/projects/10_node_compiler/examples";

    frust_plugin_register_host_function("host_add_ten", (void*)&host_add_ten);
    frust_plugin_register_host_function("frust_format_i64", (void*)&local_frust_format_i64);
    frust_plugin_register_host_function("frust_print_str", (void*)&local_frust_print_str);

    bool ok = true;
    // (x + 10) * 2
    ok &= CompileLoadAndCheck(examplesDir + "/simple_arithmetic.json", scratchDir, 5, 30);
    // if x > 10 { x*2 } else { x+1 }
    ok &= CompileLoadAndCheck(examplesDir + "/branch.json", scratchDir, 20, 40);
    ok &= CompileLoadAndCheck(examplesDir + "/branch.json", scratchDir, 5, 6);
    // host_add_ten(x), plus a print node
    ok &= CompileLoadAndCheck(examplesDir + "/call_and_print.json", scratchDir, 7, 17);

    std::printf("%s\n", ok ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return ok ? 0 : 1;
}
