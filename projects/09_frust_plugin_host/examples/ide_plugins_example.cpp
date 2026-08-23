// Verification harness for the four REAL plugins shipping with the
// JUCE IDE (projects/02_juce_language_host/plugins/) - linter,
// plugin_generator, metrics, symbol_finder. Not demos - real,
// permanent plugins meant to actually be used, each verified here
// against real, hand-predicted input/output before ever being wired
// into the IDE's plugin browser.
//
// Provides host_count_occurrences, the one host-side primitive
// metrics.frust needs (Frust has no pointer arithmetic to count
// repeated substring occurrences in pure Frust - see that file's
// header for the full reasoning) - a real C++ substring-count loop,
// not a stub.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" int64_t host_count_occurrences(const char* text, const char* needle) {
    if (!text || !needle || !*needle) return 0;
    std::string t(text), n(needle);
    int64_t count = 0;
    size_t pos = 0;
    while ((pos = t.find(n, pos)) != std::string::npos) {
        ++count;
        pos += n.size();
    }
    return count;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: ide_plugins_example <linter.frust> <plugin_generator.frust> <metrics.frust> <symbol_finder.frust>\n");
        return 1;
    }

    frust_plugin_register_host_function("host_count_occurrences", (void*)&host_count_occurrences);

    bool allOk = true;

    // --- linter.frust ---
    {
        FrustPluginHandle h = frust_plugin_load(argv[1]);
        if (!h) { std::fprintf(stderr, "FAIL: load linter ('%s')\n", frust_plugin_last_error()); return 1; }

        const char* text = "manifest \"{}\";\n// TODO: fix this\npub fn foo() -> i64 = { 1 }\n";

        auto hasTodo = (int64_t(*)(const char*))frust_plugin_get_fn(h, "has_todo");
        auto hasFixme = (int64_t(*)(const char*))frust_plugin_get_fn(h, "has_fixme");
        auto hasHack = (int64_t(*)(const char*))frust_plugin_get_fn(h, "has_hack");
        auto hasManifest = (int64_t(*)(const char*))frust_plugin_get_fn(h, "has_manifest_decl");
        auto hasPubFn = (int64_t(*)(const char*))frust_plugin_get_fn(h, "has_pub_fn");
        if (!hasTodo || !hasFixme || !hasHack || !hasManifest || !hasPubFn) {
            std::fprintf(stderr, "FAIL: linter get_fn returned null\n");
            return 1;
        }

        bool ok = hasTodo(text) == 1 && hasFixme(text) == 0 && hasHack(text) == 0
            && hasManifest(text) == 1 && hasPubFn(text) == 1;
        std::printf("linter: has_todo=%lld has_fixme=%lld has_hack=%lld has_manifest=%lld has_pub_fn=%lld ok=%d\n",
            (long long)hasTodo(text), (long long)hasFixme(text), (long long)hasHack(text),
            (long long)hasManifest(text), (long long)hasPubFn(text), ok);
        allOk = allOk && ok;
        frust_plugin_unload(h);
    }

    // --- plugin_generator.frust ---
    {
        FrustPluginHandle h = frust_plugin_load(argv[2]);
        if (!h) { std::fprintf(stderr, "FAIL: load plugin_generator ('%s')\n", frust_plugin_last_error()); return 1; }

        auto generate = (const char*(*)(const char*))frust_plugin_get_fn(h, "generate_skeleton");
        if (!generate) { std::fprintf(stderr, "FAIL: plugin_generator get_fn returned null\n"); return 1; }

        const char* result = generate("my_plugin");
        std::string resultStr = result ? result : "";
        // The generated TEXT must contain a real backslash-quote before
        // "name" (it's the JSON *inside* the generated manifest string
        // literal) - so this C++ comparison string needs \\\" (real
        // backslash + real quote), not just \" (a bare quote). Got this
        // wrong on the first pass here, in this exact same way the
        // plugin's own source had to get it right - the plugin was
        // correct; this comparison string wasn't.
        bool ok = resultStr.find("manifest \"{\\\"name\\\":\\\"my_plugin\\\"}\";") != std::string::npos
            && resultStr.find("pub fn on_init() -> i64 = {") != std::string::npos
            && resultStr.find("111") != std::string::npos;
        std::printf("plugin_generator output:\n%s\n", resultStr.c_str());
        std::printf("plugin_generator: ok=%d\n", ok);
        allOk = allOk && ok;
        frust_plugin_unload(h);
    }

    // --- metrics.frust ---
    {
        FrustPluginHandle h = frust_plugin_load(argv[3]);
        if (!h) { std::fprintf(stderr, "FAIL: load metrics ('%s')\n", frust_plugin_last_error()); return 1; }

        const char* text = "fn a() {}\nfn b() {}\nstruct S1 {}\nstruct S2 {}\nstruct S3 {}\ninterface I1 {}\n";

        auto totalLength = (int64_t(*)(const char*))frust_plugin_get_fn(h, "total_length");
        auto countFns = (int64_t(*)(const char*))frust_plugin_get_fn(h, "count_functions");
        auto countStructs = (int64_t(*)(const char*))frust_plugin_get_fn(h, "count_structs");
        auto countInterfaces = (int64_t(*)(const char*))frust_plugin_get_fn(h, "count_interfaces");
        auto hasManifest = (int64_t(*)(const char*))frust_plugin_get_fn(h, "has_manifest_decl");
        if (!totalLength || !countFns || !countStructs || !countInterfaces || !hasManifest) {
            std::fprintf(stderr, "FAIL: metrics get_fn returned null\n");
            return 1;
        }

        int64_t len = totalLength(text);
        int64_t fns = countFns(text);
        int64_t structs = countStructs(text);
        int64_t ifaces = countInterfaces(text);
        int64_t manifestFound = hasManifest(text);

        bool ok = len == (int64_t)std::strlen(text) && fns == 2 && structs == 3 && ifaces == 1 && manifestFound == 0;
        std::printf("metrics: total_length=%lld count_functions=%lld count_structs=%lld count_interfaces=%lld has_manifest=%lld ok=%d\n",
            (long long)len, (long long)fns, (long long)structs, (long long)ifaces, (long long)manifestFound, ok);
        allOk = allOk && ok;
        frust_plugin_unload(h);
    }

    // --- symbol_finder.frust ---
    {
        FrustPluginHandle h = frust_plugin_load(argv[4]);
        if (!h) { std::fprintf(stderr, "FAIL: load symbol_finder ('%s')\n", frust_plugin_last_error()); return 1; }

        const char* text =
            "pub fn foo() -> i64 = { 1 }\n"
            "struct Bar {\n    x: i64\n}\n"
            "interface Baz {\n    fn m(&mut self) -> i64\n}\n";

        auto hasFunction = (int64_t(*)(const char*, const char*))frust_plugin_get_fn(h, "has_function");
        auto hasStruct = (int64_t(*)(const char*, const char*))frust_plugin_get_fn(h, "has_struct");
        auto hasInterface = (int64_t(*)(const char*, const char*))frust_plugin_get_fn(h, "has_interface");
        if (!hasFunction || !hasStruct || !hasInterface) {
            std::fprintf(stderr, "FAIL: symbol_finder get_fn returned null\n");
            return 1;
        }

        bool ok = hasFunction(text, "foo") == 1 && hasFunction(text, "nonexistent") == 0
            && hasStruct(text, "Bar") == 1 && hasStruct(text, "Nope") == 0
            && hasInterface(text, "Baz") == 1 && hasInterface(text, "Missing") == 0;
        std::printf("symbol_finder: has_function(foo)=%lld has_function(nonexistent)=%lld has_struct(Bar)=%lld has_struct(Nope)=%lld has_interface(Baz)=%lld has_interface(Missing)=%lld ok=%d\n",
            (long long)hasFunction(text, "foo"), (long long)hasFunction(text, "nonexistent"),
            (long long)hasStruct(text, "Bar"), (long long)hasStruct(text, "Nope"),
            (long long)hasInterface(text, "Baz"), (long long)hasInterface(text, "Missing"), ok);
        allOk = allOk && ok;
        frust_plugin_unload(h);
    }

    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
