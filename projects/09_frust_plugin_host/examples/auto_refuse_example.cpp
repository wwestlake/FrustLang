// Verification harness for frust_plugin_load() auto-refusing an
// incompatible plugin (docs/17-Plugin-Automation-Layer.md section 4.2).
//
// Prior state: frust_plugin_manifest_is_compatible() was a real check,
// but nothing in frust_plugin_load() itself ever called it - a host had
// to remember to check compatibility BEFORE calling load, or an
// incompatible plugin would load anyway. User's direct instruction:
// "if the framework can know that the plugin is not compatible it
// should refuse it" - low-level, but this one crosses into user-visible
// behavior (a bad plugin can no longer load silently), so it's not left
// as a host-side opt-in. Countervailing instruction, same message: "if
// it doesn't know, let the app decide" - a plugin with no manifest at
// all must still load exactly as before; the framework only acts when
// it genuinely has the information.
//
// Walks through every real combination: a restricted plugin
// (restricted_plugin.frust + restricted_plugin.json, which declares
// intendedApplications + a requiredHostFunctions entry) is refused by
// frust_plugin_load itself with no host identity set; still refused
// once the identity matches but the required function isn't registered
// yet; loads successfully once both hold; refused again if the host's
// identity later changes to something that doesn't match (proves the
// check runs fresh on every load call, not cached); an unrestricted
// plugin (unrestricted_plugin.frust + unrestricted_plugin.json, no
// restrictions declared) always loads; and a plugin with NO manifest at
// all (test_plugin.frust, no sibling .json) always loads regardless of
// host identity - the "let the app decide" case.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" int64_t some_fn() { return 0; }

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: auto_refuse_example <restricted_plugin.frust> <unrestricted_plugin.frust> <test_plugin.frust>\n");
        return 1;
    }
    std::string restrictedPath = argv[1];
    std::string unrestrictedPath = argv[2];
    std::string noManifestPath = argv[3];

    // Step 1: restricted plugin, no host identity set at all -> the
    // framework itself refuses the load (nullptr), not just advisory.
    FrustPluginHandle h1 = frust_plugin_load(restrictedPath.c_str());
    bool step1Refused = (h1 == nullptr);
    std::string reason1 = frust_plugin_last_error();
    bool reason1Ok = reason1.find("refusing to load") != std::string::npos;

    // Step 2: unrestricted plugin loads fine regardless.
    FrustPluginHandle h2 = frust_plugin_load(unrestrictedPath.c_str());
    bool step2Loaded = (h2 != nullptr);

    // Step 3: matching identity, but the required function still isn't
    // registered -> still refused (proves the fn-availability half of
    // the check is real too, not short-circuited by the identity match).
    frust_plugin_host_set_application_identity("test-app-a");
    FrustPluginHandle h3 = frust_plugin_load(restrictedPath.c_str());
    bool step3Refused = (h3 == nullptr);

    // Step 4: register the missing function -> now loads.
    frust_plugin_register_host_function("some_fn", (void*)&some_fn);
    FrustPluginHandle h4 = frust_plugin_load(restrictedPath.c_str());
    bool step4Loaded = (h4 != nullptr);

    // Step 5: identity changes to something that no longer matches,
    // even though the required function is still registered -> refused
    // again. Proves this isn't a one-time gate cached from step 4.
    frust_plugin_host_set_application_identity("wrong-app");
    FrustPluginHandle h5 = frust_plugin_load(restrictedPath.c_str());
    bool step5Refused = (h5 == nullptr);

    // Step 6: a plugin with no manifest at all loads regardless of
    // identity - the framework has no way to know, so it defers to the
    // app rather than refusing.
    FrustPluginHandle h6 = frust_plugin_load(noManifestPath.c_str());
    bool step6Loaded = (h6 != nullptr);

    if (h2) frust_plugin_unload(h2);
    if (h4) frust_plugin_unload(h4);
    if (h6) frust_plugin_unload(h6);

    std::printf("step1Refused=%d reason1Ok=%d step2Loaded=%d step3Refused=%d step4Loaded=%d step5Refused=%d step6Loaded=%d\n",
        step1Refused, reason1Ok, step2Loaded, step3Refused, step4Loaded, step5Refused, step6Loaded);
    std::printf("reason1='%s'\n", reason1.c_str());

    bool allOk = step1Refused && reason1Ok && step2Loaded && step3Refused && step4Loaded && step5Refused && step6Loaded;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
