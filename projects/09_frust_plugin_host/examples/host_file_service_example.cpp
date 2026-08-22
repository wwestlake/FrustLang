// Verification harness for a HOST advertising real capabilities to
// plugins (docs/17-Plugin-Automation-Layer.md) - the same service
// registry built for plugin-to-plugin discovery, used the other
// direction: the host registers "file_dialog_open"/"file_open"/
// "file_read_line"/"file_write_line"/"file_close" via
// frust_register_service (called directly from host startup code, not
// from inside a plugin's on_init - the host itself never unloads, so
// there's no ownership window to track), and a loaded plugin discovers
// and calls them via lookup_service, entirely on its own.
//
// Matches the user's own example: a plugin asks for a file, the host
// (in a real GUI app) would show an open-file dialog and hand back a
// path; here file_dialog_open just returns a fixed test CSV path,
// standing in for that click. The host owns the REAL OS file handle
// (an fstream in a host-side map) - the plugin only ever holds a
// lightweight int64 id it passes back for every operation. The host
// looks up the real handle from that id and performs the operation -
// exactly the design the user described directly.
//
// Known, named limitation (not hidden): no automatic handle cleanup on
// plugin unload here, unlike the event/service registries. File
// operations can happen at any point during a plugin's execution, not
// just during the tracked on_init window frust_register_event_handler/
// frust_register_service use for ownership - a broader "which plugin is
// currently executing" tracker would be needed for automatic cleanup,
// which isn't built yet. A plugin that opens a file is responsible for
// closing it, same as any other file API's normal contract.

#include "frust_plugin_host/FrustPluginHost.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct HostFileRegistry {
    std::mutex mutex;
    std::unordered_map<int64_t, std::fstream> handles;
    int64_t nextId = 1;
};
HostFileRegistry& fileRegistry() {
    static HostFileRegistry r;
    return r;
}

std::string g_testCsvPath;
std::vector<std::string> g_reportedLines;

// --- Host capabilities, registered under these names ---

extern "C" const char* host_file_dialog_open() {
    // Stand-in for a real GUI file-open dialog - see header comment.
    return g_testCsvPath.c_str();
}

// mode: 0 = read, 1 = write. Returns 0 on failure (never a valid id -
// nextId starts at 1).
extern "C" int64_t host_file_open(const char* path, int64_t mode) {
    auto& r = fileRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    int64_t id = r.nextId++;
    std::ios::openmode m = (mode == 1) ? std::ios::out : std::ios::in;
    auto& stream = r.handles[id];
    stream.open(path, m);
    if (!stream.is_open()) {
        r.handles.erase(id);
        return 0;
    }
    return id;
}

extern "C" const char* host_file_read_line(int64_t id) {
    static thread_local std::string buf; // caller must copy before the next call, same convention as Main.cpp's format buffer pool
    auto& r = fileRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.handles.find(id);
    if (it == r.handles.end() || !std::getline(it->second, buf)) {
        return nullptr; // bad handle or EOF
    }
    return buf.c_str();
}

extern "C" int64_t host_file_write_line(int64_t id, const char* line) {
    auto& r = fileRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.handles.find(id);
    if (it == r.handles.end()) return 0;
    it->second << line << "\n";
    return 1;
}

extern "C" int64_t host_file_close(int64_t id) {
    auto& r = fileRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.handles.find(id);
    if (it == r.handles.end()) return 0;
    it->second.close();
    r.handles.erase(it);
    return 1;
}

// The plugin reports each line it read back here, so this harness can
// verify what the plugin actually saw - it never reads the CSV itself.
extern "C" int64_t test_report_line(const char* line) {
    g_reportedLines.push_back(line ? line : "");
    return (int64_t)g_reportedLines.size();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: host_file_service_example <plugin.frust> <sample_data.csv>\n");
        return 1;
    }
    g_testCsvPath = argv[2];

    frust_plugin_register_host_function("test_report_line", (void*)&test_report_line);

    // Advertise host capabilities BEFORE loading any plugin - this is
    // the host equivalent of a plugin's own_init registration, just not
    // tied to any plugin's lifetime since the host isn't unloaded.
    frust_register_service("file_dialog_open", (void*)&host_file_dialog_open);
    frust_register_service("file_open", (void*)&host_file_open);
    frust_register_service("file_read_line", (void*)&host_file_read_line);
    frust_register_service("file_write_line", (void*)&host_file_write_line);
    frust_register_service("file_close", (void*)&host_file_close);

    FrustPluginHandle h = frust_plugin_load(argv[1]);
    if (!h) { std::fprintf(stderr, "FAIL: load\n"); return 1; }

    int64_t initResult = frust_plugin_call_on_init(h);
    bool initOk = initResult == 111;

    std::printf("plugin reported %zu line(s):\n", g_reportedLines.size());
    for (auto& l : g_reportedLines) std::printf("  '%s'\n", l.c_str());

    std::vector<std::string> expected = {"name,value", "alpha,10", "beta,20", "gamma,30"};
    bool linesOk = g_reportedLines == expected;

    frust_plugin_unload(h);

    bool allOk = initOk && linesOk;
    std::printf("initOk=%d linesOk=%d\n", initOk, linesOk);
    std::printf("%s\n", allOk ? "ALL_CHECKS_PASSED" : "CHECKS_FAILED");
    return allOk ? 0 : 1;
}
