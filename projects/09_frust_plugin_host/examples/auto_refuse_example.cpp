// Verification harness for frust_plugin_load()'s "no manifest, no load"
// gate (docs/17-Plugin-Automation-Layer.md section 4.2).
//
// History: this started as an auto-refuse-when-incompatible check with a
// permissive fallback ("if it doesn't know [because there's no manifest
// at all], let the app decide"). The user then asked whether the
// manifest could be embedded directly in the plugin's own source instead
// of living in a separate companion file - Frust compiles through LLVM
// IR, so a plugin's own module can carry its own metadata as a
// discoverable global constant, readable before the module is ever
// linked into the JIT or executed. Once that was built, the user's
// instruction sharpened the rule: "no manifest, no load at all" - there
// is no more permissive fallback. A plugin with no `manifest "...";`
// declaration in its own source cannot load, period.
//
// Walks through every real combination: a restricted plugin
// (restricted_plugin.frust, whose embedded manifest declares
// intendedApplications + a requiredHostFunctions entry) is refused with
// no host identity set; still refused once the identity matches but the
// required function isn't registered yet; loads once both hold; refused
// again if the host's identity later changes to something that doesn't
// match (proves the check runs fresh on every load call, not cached); an
// unrestricted plugin (unrestricted_plugin.frust, an embedded manifest
// with no restrictions) always loads; and a plugin with NO embedded
// manifest at all (test_plugin.frust, deliberately left without one) is
// ALWAYS refused, regardless of host identity - there is no longer any
// case where a manifest-less plugin loads.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" int64_t some_fn() { return 0; }

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: auto_refuse_example <restricted_plugin.frust> <unrestricted_plugin.frust> <test_plugin.frust (no manifest)>\n");
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

    // Step 6: a plugin with NO embedded manifest at all is ALWAYS
    // refused now - "no manifest, no load" has no permissive fallback.
    FrustPluginHandle h6 = frust_plugin_load(noManifestPath.c_str());
    bool step6Refused = (h6 == nullptr);
    std::string reason6 = frust_plugin_last_error();
    bool reason6MentionsManifest = reason6.find("no embedded plugin manifest") != std::string::npos;

    if (h2) frust_plugin_unload(h2);
    if (h4) frust_plugin_unload(h4);

    std::printf("step1Refused=%d reason1Ok=%d step2Loaded=%d step3Refused=%d step4Loaded=%d step5Refused=%d step6Refused=%d reason6MentionsManifest=%d\n",
        step1Refused, reason1Ok, step2Loaded, step3Refused, step4Loaded, step5Refused, step6Refused, reason6MentionsManifest);
    std::printf("reason1='%s'\n", reason1.c_str());
    std::printf("reason6='%s'\n", reason6.c_str());

    bool allOk = step1Refused && reason1Ok && step2Loaded && step3Refused && step4Loaded && step5Refused
        && step6Refused && reason6MentionsManifest;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
