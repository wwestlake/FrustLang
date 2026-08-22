// Completion-plan item D: never tested with more than 2 plugins loaded
// at once. Loads FOUR real, previously-independently-verified plugins
// SIMULTANEOUSLY (automation.fr's RampAutomation/DecayAutomation,
// event_plugin.frust, service_provider.frust, service_consumer.frust),
// confirms they all still work together, then deliberately unloads and
// reloads plugins ONE AT A TIME mid-session to prove cross-plugin
// isolation: unloading/reloading one plugin must not disturb any other
// still-loaded plugin's state.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdint>
#include <cmath>

namespace {
int64_t g_eventFireCount = 0;
int64_t g_serviceCallResult = -1;

extern "C" int64_t test_mark_fired(int64_t id) {
    g_eventFireCount += id;
    return g_eventFireCount;
}
extern "C" int64_t test_report_result(int64_t val) {
    g_serviceCallResult = val;
    return val;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: multi_plugin_stress_example <automation.fr> <event_plugin.frust> <service_provider.frust> <service_consumer.frust>\n");
        return 1;
    }

    frust_plugin_register_host_function("test_mark_fired", (void*)&test_mark_fired);
    frust_plugin_register_host_function("test_report_result", (void*)&test_report_result);

    // --- Load all four simultaneously ---
    FrustPluginHandle automation = frust_plugin_load(argv[1]);
    FrustPluginHandle eventPlugin = frust_plugin_load(argv[2]);
    FrustPluginHandle serviceProvider = frust_plugin_load(argv[3]);
    FrustPluginHandle serviceConsumer = frust_plugin_load(argv[4]); // on_init calls the provider's service itself

    bool allLoaded = automation && eventPlugin && serviceProvider && serviceConsumer;
    if (!allLoaded) { std::fprintf(stderr, "FAIL: not all four loaded\n"); return 1; }

    frust_plugin_call_on_init(eventPlugin);       // registers "something_happened" handler
    frust_plugin_call_on_init(serviceProvider);   // registers "doubler" service
    frust_plugin_call_on_init(serviceConsumer);   // looks up "doubler", calls it, reports 42

    // --- Phase 1: everything works together ---
    auto newRamp = (void*(*)(float))frust_plugin_get_fn(automation, "new_ramp");
    auto rampTick = (float(*)(void*, float))frust_plugin_get_fn(automation, "RampAutomation::tick");
    void* ramp = newRamp(2.0f);
    float r1 = rampTick(ramp, 1.0f);

    frust_fire_event("something_happened", nullptr);

    bool phase1Ok = std::fabs(r1 - 2.0f) < 1e-4f && g_eventFireCount == 1 && g_serviceCallResult == 42;
    std::printf("phase1: rampTick=%g eventFireCount=%lld serviceCallResult=%lld ok=%d\n",
        r1, (long long)g_eventFireCount, (long long)g_serviceCallResult, phase1Ok);

    // --- Phase 2: unload eventPlugin mid-session; everything else must survive untouched ---
    frust_plugin_unload(eventPlugin);
    frust_fire_event("something_happened", nullptr); // must now be a no-op - no crash, no count change

    float r2 = rampTick(ramp, 1.0f); // automation must be completely unaffected
    void* doublerAfterEventUnload = frust_lookup_service("doubler"); // service must be completely unaffected

    bool phase2Ok = g_eventFireCount == 1 /* unchanged */ && std::fabs(r2 - 4.0f) < 1e-4f && doublerAfterEventUnload != nullptr;
    std::printf("phase2 (after unloading event plugin): eventFireCount=%lld rampTick=%g doublerStillThere=%d ok=%d\n",
        (long long)g_eventFireCount, r2, doublerAfterEventUnload != nullptr, phase2Ok);

    // --- Phase 3: reload serviceProvider; doubler must survive (re-registered via the reload fix), automation must be unaffected ---
    FrustPluginHandle newProvider = frust_plugin_reload(serviceProvider);
    bool reloadOk = newProvider != nullptr;

    void* doublerAfterReload = frust_lookup_service("doubler");
    float r3 = rampTick(ramp, 1.0f);

    bool phase3Ok = reloadOk && doublerAfterReload != nullptr && std::fabs(r3 - 6.0f) < 1e-4f;
    std::printf("phase3 (after reloading service provider): reloadOk=%d doublerStillThere=%d rampTick=%g ok=%d\n",
        reloadOk, doublerAfterReload != nullptr, r3, phase3Ok);

    frust_plugin_unload(automation);
    frust_plugin_unload(newProvider);
    frust_plugin_unload(serviceConsumer);

    bool allOk = phase1Ok && phase2Ok && phase3Ok;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
