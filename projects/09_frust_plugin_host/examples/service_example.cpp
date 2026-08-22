// Verification harness for plugin-to-plugin service discovery
// (docs/17-Plugin-Automation-Layer.md section 4.3): TWO SEPARATELY
// LOADED plugins - service_provider.frust registers a "doubler"
// service in its own on_init, service_consumer.frust (a different
// plugin, a different JITDylib) looks that service up by name and
// calls it, entirely from ITS OWN Frust code - no host mediation in
// the middle beyond the registry itself. Proves this is genuinely
// plugin-to-plugin, not just host-relayed calls.
//
// Also proves the real safety property, same as the event system:
// after the PROVIDING plugin is unloaded, looking the service up again
// must return NULL, not a stale pointer into freed JIT code.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdint>

namespace {
int64_t g_reportedResult = -1;

extern "C" int64_t test_report_result(int64_t val) {
    g_reportedResult = val;
    return val;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: service_example <service_provider.frust> <service_consumer.frust>\n");
        return 1;
    }

    frust_plugin_register_host_function("test_report_result", (void*)&test_report_result);

    FrustPluginHandle provider = frust_plugin_load(argv[1]);
    if (!provider) { std::fprintf(stderr, "FAIL: load provider\n"); return 1; }
    int64_t providerInit = frust_plugin_call_on_init(provider); // registers "doubler"
    bool providerInitOk = providerInit == 111;

    // Before the consumer even loads, the service should already be
    // visible to a direct C++ lookup too - it's not consumer-specific.
    bool visibleBeforeConsumerOk = frust_lookup_service("doubler") != nullptr;

    FrustPluginHandle consumer = frust_plugin_load(argv[2]);
    if (!consumer) { std::fprintf(stderr, "FAIL: load consumer\n"); return 1; }
    int64_t consumerInit = frust_plugin_call_on_init(consumer); // looks up + calls "doubler" itself
    bool consumerInitOk = consumerInit == 222;

    // The actual proof: service_consumer.frust called service_provider.
    // frust's double_it(21) ENTIRELY on its own, through the registry -
    // this harness never called double_it directly.
    bool crossPluginCallOk = (g_reportedResult == 42);

    frust_plugin_unload(provider);

    // Safety test: the registry must not still point at now-freed JIT
    // code after the plugin that registered it is gone.
    bool goneAfterUnloadOk = frust_lookup_service("doubler") == nullptr;

    frust_plugin_unload(consumer);

    std::printf("providerInit=%lld consumerInit=%lld reportedResult=%lld\n",
        (long long)providerInit, (long long)consumerInit, (long long)g_reportedResult);
    std::printf("visibleBeforeConsumer=%d crossPluginCall=%d goneAfterUnload=%d\n",
        visibleBeforeConsumerOk, crossPluginCallOk, goneAfterUnloadOk);

    bool allOk = providerInitOk && visibleBeforeConsumerOk && consumerInitOk && crossPluginCallOk && goneAfterUnloadOk;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
