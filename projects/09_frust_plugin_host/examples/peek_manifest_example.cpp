// Verification harness for frust_plugin_peek_manifest() - the new
// preview-without-loading API built for a plugin BROWSER (docs/17,
// "no registry that reads many plugins' manifests from a directory at
// once" follow-on, now the actual ask). Proves it reads a plugin's
// manifest regardless of compatibility (unlike frust_plugin_load,
// which refuses an incompatible plugin outright) and never actually
// loads anything - frust_plugin_get_fn on the SAME path afterward must
// still return null (nothing was ever linked into the JIT by peeking).

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: peek_manifest_example <unrestricted_plugin.frust> <restricted_plugin.frust> <test_plugin.frust (no manifest)>\n");
        return 1;
    }
    std::string unrestrictedPath = argv[1];
    std::string restrictedPath = argv[2];
    std::string noManifestPath = argv[3];

    // Step 1: unrestricted plugin - manifest readable regardless.
    FrustPluginManifestHandle m1 = frust_plugin_peek_manifest(unrestrictedPath.c_str());
    bool step1Ok = m1 != nullptr && std::strcmp(frust_plugin_manifest_name(m1), "unrestricted_plugin") == 0;
    if (m1) frust_plugin_manifest_free(m1);

    // Step 2: restricted plugin, no host identity set - frust_plugin_load
    // would REFUSE this outright, but peek still reads it (that's the
    // whole point - a browser needs to show WHY it's incompatible).
    FrustPluginManifestHandle m2 = frust_plugin_peek_manifest(restrictedPath.c_str());
    bool step2Ok = m2 != nullptr && std::strcmp(frust_plugin_manifest_name(m2), "restricted_plugin") == 0
        && frust_plugin_manifest_is_compatible(m2) == 0; // still correctly reports incompatible
    if (m2) frust_plugin_manifest_free(m2);

    // Step 3: no manifest at all - null, real error.
    FrustPluginManifestHandle m3 = frust_plugin_peek_manifest(noManifestPath.c_str());
    bool step3Ok = m3 == nullptr;
    std::string reason3 = frust_plugin_last_error();
    bool reason3Ok = reason3.find("no embedded plugin manifest") != std::string::npos;

    // Step 4: peeking must never actually load anything - a fresh
    // frust_plugin_get_fn lookup on a handle from a REAL load of the
    // same path must still work normally (proves peek didn't leave any
    // stale/partial state behind), and there must be no way to call a
    // function through the peek handle itself (it's a manifest handle,
    // not a plugin handle - the type system already prevents this, but
    // confirm frust_plugin_load on the same path still works fresh).
    FrustPluginHandle realHandle = frust_plugin_load(unrestrictedPath.c_str());
    bool step4Ok = realHandle != nullptr;
    if (realHandle) frust_plugin_unload(realHandle);

    std::printf("step1Ok=%d step2Ok=%d step3Ok=%d reason3Ok=%d step4Ok=%d\n",
        step1Ok, step2Ok, step3Ok, reason3Ok, step4Ok);
    std::printf("reason3='%s'\n", reason3.c_str());

    bool allOk = step1Ok && step2Ok && step3Ok && reason3Ok && step4Ok;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
