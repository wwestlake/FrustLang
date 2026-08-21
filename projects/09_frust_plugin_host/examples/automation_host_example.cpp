// Verification harness for calling a Frust `interface`-implementing
// struct's constructor + method directly from native C++, via the same
// mechanism the JUCE IDE's AutomationPanel uses: frust_plugin_get_fn on
// a plain constructor free function (returns a heap-allocated instance,
// via `own` - see automation.fr) and on the concrete type's own mangled
// method name (e.g. "RampAutomation::tick"). The host never touches the
// Automation interface's vtable/fat-pointer machinery at all - it just
// calls the concrete type's real exported symbols directly.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: automation_host_example <automation.fr>\n");
        return 1;
    }

    FrustPluginHandle h = frust_plugin_load(argv[1]);
    if (!h) { std::fprintf(stderr, "FAIL: load\n"); return 1; }

    auto newRamp = (void*(*)(float))frust_plugin_get_fn(h, "new_ramp");
    auto rampTick = (float(*)(void*, float))frust_plugin_get_fn(h, "RampAutomation::tick");
    auto newDecay = (void*(*)(float))frust_plugin_get_fn(h, "new_decay");
    auto decayTick = (float(*)(void*, float))frust_plugin_get_fn(h, "DecayAutomation::tick");

    if (!newRamp || !rampTick || !newDecay || !decayTick) {
        std::fprintf(stderr, "FAIL: missing symbol(s)\n");
        return 1;
    }

    void* ramp = newRamp(2.0f);
    float r1 = rampTick(ramp, 1.0f);
    float r2 = rampTick(ramp, 1.0f);
    float r3 = rampTick(ramp, 1.0f);

    void* decay = newDecay(0.5f);
    float d1 = decayTick(decay, 1.0f);
    float d2 = decayTick(decay, 1.0f);
    float d3 = decayTick(decay, 1.0f);

    std::printf("ramp: %g %g %g\n", r1, r2, r3);
    std::printf("decay: %g %g %g\n", d1, d2, d3);

    bool ok = std::fabs(r1 - 2.0f) < 1e-4f && std::fabs(r2 - 4.0f) < 1e-4f && std::fabs(r3 - 6.0f) < 1e-4f
        && std::fabs(d1 - 50.0f) < 1e-4f && std::fabs(d2 - 25.0f) < 1e-4f && std::fabs(d3 - 12.5f) < 1e-4f;

    frust_plugin_unload(h);

    std::printf("%s\n", ok ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return ok ? 0 : 1;
}
