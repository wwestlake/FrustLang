// Completion-plan item E: every registry (events, services) is
// mutex-guarded, but nothing has ever actually exercised it from more
// than one thread. Spawns N threads that concurrently register their
// own uniquely-named service, look it up back, and fire a SHARED event
// many times each - hammering the exact same HostState the JIT-loaded-
// plugin tests use, just driven directly instead of through compiled
// Frust code (the registries themselves don't care which side calls
// them). A crash, a wrong final count, or a thread getting back a
// DIFFERENT thread's service under its own name would all mean the
// existing std::mutex guarding isn't actually sufficient.
//
// Expect a wall of "service '...' registered again" stderr warnings
// when running this - each thread deliberately re-registers its OWN
// name every loop iteration, which correctly re-triggers that warning
// every time. Noisy, but correct behavior, not a bug.

#include "frust_plugin_host/FrustPluginHost.h"

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<int64_t> g_totalFires{0};

extern "C" void shared_handler(void*) {
    g_totalFires.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

int main() {
    constexpr int kThreads = 8;
    constexpr int kFiresPerThread = 500;
    constexpr int kServiceOpsPerThread = 500;

    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &mismatches]() {
            std::string myServiceName = "svc_thread_" + std::to_string(t);
            // A distinct, real pointer per thread (its own stack address
            // is fine here - never dereferenced, only compared).
            int marker = t;

            for (int i = 0; i < kServiceOpsPerThread; ++i) {
                frust_register_service(myServiceName.c_str(), (void*)&marker);
                void* got = frust_lookup_service(myServiceName.c_str());
                if (got != (void*)&marker) {
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                }
            }
            for (int i = 0; i < kFiresPerThread; ++i) {
                frust_fire_event("stress_event", nullptr);
            }
        });
    }

    // Every thread's handler must be registered before any thread fires,
    // or early fires would legitimately not reach threads that haven't
    // registered yet - register all handlers up front, single-threaded,
    // then let the real concurrent stress (service register/lookup +
    // event fire) happen across threads.
    for (int t = 0; t < kThreads; ++t) {
        frust_register_event_handler("stress_event", &shared_handler);
    }

    for (auto& th : threads) th.join();

    int64_t expectedFires = (int64_t)kThreads * kFiresPerThread * kThreads; // each of kThreads handlers fires on every one of kThreads*kFiresPerThread total fires
    bool firesOk = g_totalFires.load() == expectedFires;
    bool noMismatches = mismatches.load() == 0;

    std::printf("totalFires=%lld expected=%lld mismatches=%d\n",
        (long long)g_totalFires.load(), (long long)expectedFires, mismatches.load());

    bool allOk = firesOk && noMismatches;
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
