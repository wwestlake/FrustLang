// Verification harness for the real, built-in compatibility check
// (docs/17-Plugin-Automation-Layer.md section 4.2) - the user's own
// direct requirement: "an application should be able to know what
// plugins are valid for it... some metadata about the application its
// intended for is essential." Previously requiredHostFunctions was
// advisory-only (a host had to manually re-derive the check itself);
// this proves frust_plugin_manifest_is_compatible() is a real, built-in
// check, not just readable data.
//
// Walks through every real combination: an unrestricted manifest is
// always compatible; a restricted one is incompatible with no host
// identity set (conservative default - silence plus an explicit
// restriction is exactly the "might be the wrong app" case worth
// catching); still incompatible once the identity matches but a
// required host function is missing (proves BOTH checks are real, not
// just the first one short-circuiting success); compatible once both
// hold; and incompatible again if the host's identity changes to
// something that doesn't match, even with every required function
// already registered (proves the identity check isn't bypassed once
// satisfied once).

#include "frust_plugin_host/FrustPluginHost.h"
#include "frust_plugin_host/FrustPluginManifest.h"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" int64_t some_fn() { return 0; }

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: app_identity_example <restricted_plugin.json> <unrestricted_plugin.json>\n");
        return 1;
    }

    FrustPluginManifestHandle restricted = frust_plugin_manifest_load(argv[1]);
    FrustPluginManifestHandle unrestricted = frust_plugin_manifest_load(argv[2]);
    if (!restricted || !unrestricted) { std::fprintf(stderr, "FAIL: manifest load\n"); return 1; }

    bool intendedAppsOk = frust_plugin_manifest_intended_application_count(restricted) == 2
        && std::strcmp(frust_plugin_manifest_intended_application(restricted, 0), "test-app-a") == 0
        && std::strcmp(frust_plugin_manifest_intended_application(restricted, 1), "test-app-b") == 0;

    // Step 1: unrestricted is always compatible, no identity set at all.
    bool unrestrictedOk = frust_plugin_manifest_is_compatible(unrestricted) == 1;

    // Step 2: restricted, no host identity declared yet -> incompatible.
    bool step2Incompatible = frust_plugin_manifest_is_compatible(restricted) == 0;
    std::string reason2 = frust_plugin_manifest_incompatibility_reason();
    bool reason2Ok = !reason2.empty();

    // Step 3: set a MATCHING identity, but the required host function
    // still isn't registered -> still incompatible (proves this is a
    // real second check, not skipped once the identity matches).
    frust_plugin_host_set_application_identity("test-app-a");
    bool step3Incompatible = frust_plugin_manifest_is_compatible(restricted) == 0;
    std::string reason3 = frust_plugin_manifest_incompatibility_reason();
    bool reason3MentionsFn = reason3.find("some_fn") != std::string::npos;

    // Step 4: register the missing function -> now compatible.
    frust_plugin_register_host_function("some_fn", (void*)&some_fn);
    bool step4Compatible = frust_plugin_manifest_is_compatible(restricted) == 1;

    // Step 5: change the host's identity to something that doesn't
    // match, even though every required function is now registered ->
    // incompatible again. Proves the identity check isn't a one-time
    // gate that gets bypassed once satisfied once.
    frust_plugin_host_set_application_identity("wrong-app");
    bool step5Incompatible = frust_plugin_manifest_is_compatible(restricted) == 0;

    frust_plugin_manifest_free(restricted);
    frust_plugin_manifest_free(unrestricted);

    std::printf("intendedAppsOk=%d unrestrictedOk=%d step2Incompatible=%d reason2Ok=%d step3Incompatible=%d reason3MentionsFn=%d step4Compatible=%d step5Incompatible=%d\n",
        intendedAppsOk, unrestrictedOk, step2Incompatible, reason2Ok, step3Incompatible, reason3MentionsFn, step4Compatible, step5Incompatible);
    std::printf("reason2='%s'\n", reason2.c_str());
    std::printf("reason3='%s'\n", reason3.c_str());

    bool allOk = intendedAppsOk && unrestrictedOk && step2Incompatible && reason2Ok
        && step3Incompatible && reason3MentionsFn && step4Compatible && step5Incompatible;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
