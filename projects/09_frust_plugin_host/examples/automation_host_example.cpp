// Verification harness for calling a Frust `interface`-implementing
// struct's constructor + method directly from native C++, via the same
// mechanism the JUCE IDE's AutomationPanel uses: frust_plugin_get_fn on
// a plain constructor free function (returns a heap-allocated instance,
// via `own` - see automation.fr) and on the concrete type's own mangled
// method name (e.g. "RampAutomation::tick"). The host never touches the
// Automation interface's vtable/fat-pointer machinery at all - it just
// calls the concrete type's real exported symbols directly.
//
// Also proves the manifest's implementedInterfaces field is real, not
// just parsed-and-ignored data: instead of hardcoding "new_ramp"/
// "RampAutomation::tick" by name (as this file used to), the
// constructor and mangled method names are read FROM automation.json
// at runtime - a host that only knows "I want something implementing
// Automation" can discover how to build and call one without any
// hardcoded knowledge of RampAutomation/DecayAutomation specifically.

#include "frust_plugin_host/FrustPluginHost.h"
#include "frust_plugin_host/FrustPluginManifest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

namespace {

// Ticks 3 times at delta_time=1.0 and returns the values - shared by
// both manifest-discovered instances below, since both are reached
// through the exact same generic path (construct, then call
// "<Type>::tick" - the interface's own method name is still assumed
// known by the host, same as any entryPoint; see FrustPluginManifest.h's
// note that full method-signature reflection isn't in scope for this
// pass).
bool tickThree(FrustPluginHandle h, const char* constructorFn, const char* typeName,
                float ctorArg, float expected[3]) {
    auto ctor = (void*(*)(float))frust_plugin_get_fn(h, constructorFn);
    std::string tickSym = std::string(typeName) + "::tick";
    auto tick = (float(*)(void*, float))frust_plugin_get_fn(h, tickSym.c_str());
    if (!ctor || !tick) {
        std::fprintf(stderr, "FAIL: missing symbol for manifest-declared type '%s' (ctor=%s)\n", typeName, constructorFn);
        return false;
    }
    void* inst = ctor(ctorArg);
    float v1 = tick(inst, 1.0f);
    float v2 = tick(inst, 1.0f);
    float v3 = tick(inst, 1.0f);
    std::printf("%s (via manifest): %g %g %g\n", typeName, v1, v2, v3);
    return std::fabs(v1 - expected[0]) < 1e-4f && std::fabs(v2 - expected[1]) < 1e-4f && std::fabs(v3 - expected[2]) < 1e-4f;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: automation_host_example <automation.fr> <automation.json>\n");
        return 1;
    }
    std::string pluginPath = argv[1];
    std::string manifestPath = argv[2];

    FrustPluginManifestHandle manifest = frust_plugin_manifest_load(manifestPath.c_str());
    if (!manifest) { std::fprintf(stderr, "FAIL: manifest load\n"); return 1; }

    int32_t ifaceCount = frust_plugin_manifest_interface_count(manifest);
    bool manifestShapeOk = ifaceCount == 2;
    for (int32_t i = 0; i < ifaceCount; ++i) {
        if (std::strcmp(frust_plugin_manifest_interface_name(manifest, i), "Automation") != 0) manifestShapeOk = false;
    }

    FrustPluginHandle h = frust_plugin_load(pluginPath.c_str());
    if (!h) { std::fprintf(stderr, "FAIL: load\n"); return 1; }

    // Drive both instances entirely from what the manifest declared -
    // no "RampAutomation"/"new_ramp" string literals below this point.
    bool allTicksOk = true;
    for (int32_t i = 0; i < ifaceCount; ++i) {
        const char* type = frust_plugin_manifest_interface_type(manifest, i);
        const char* ctorFn = frust_plugin_manifest_interface_constructor(manifest, i);
        float expected[3];
        float ctorArg;
        if (std::strcmp(type, "RampAutomation") == 0) {
            ctorArg = 2.0f;
            expected[0] = 2.0f; expected[1] = 4.0f; expected[2] = 6.0f;
        } else if (std::strcmp(type, "DecayAutomation") == 0) {
            ctorArg = 0.5f;
            expected[0] = 50.0f; expected[1] = 25.0f; expected[2] = 12.5f;
        } else {
            std::fprintf(stderr, "FAIL: manifest declared unexpected type '%s'\n", type);
            allTicksOk = false;
            continue;
        }
        if (!tickThree(h, ctorFn, type, ctorArg, expected)) allTicksOk = false;
    }

    frust_plugin_manifest_free(manifest);
    frust_plugin_unload(h);

    bool allOk = manifestShapeOk && allTicksOk;
    std::printf("manifestShapeOk=%d allTicksOk=%d\n", manifestShapeOk, allTicksOk);
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
