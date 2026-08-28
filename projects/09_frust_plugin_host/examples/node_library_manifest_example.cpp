#include <frust_plugin_host/FrustPluginManifest.h>

#include <cstring>
#include <iostream>

int main() {
    const auto manifest = frust_plugin_host::ParseManifestJson(
        R"({"name":"node_library_example","version":"0.1.0","nodeLibraries":[{"id":"engine.motion","target":"behavior","nodes":[{"typeName":"engine.move"}]}]})",
        "node library manifest example");
    if (!manifest || manifest->nodeLibraries.size() != 1 || manifest->nodeLibraries.front().id != "engine.motion" ||
        manifest->nodeLibraries.front().descriptorJson.find("engine.move") == std::string::npos) {
        std::cerr << "C++ node-library manifest parse failed.\n";
        return 1;
    }

    auto handle = frust_plugin_host::WrapManifest(*manifest);
    const bool apiShapeIsCorrect = frust_plugin_manifest_node_library_count(handle) == 1 &&
        std::strcmp(frust_plugin_manifest_node_library_id(handle, 0), "engine.motion") == 0 &&
        std::strstr(frust_plugin_manifest_node_library_json(handle, 0), "engine.move") != nullptr &&
        frust_plugin_manifest_node_library_id(handle, 1) == nullptr;
    frust_plugin_manifest_free(handle);

    if (!apiShapeIsCorrect) {
        std::cerr << "C node-library manifest API failed.\n";
        return 1;
    }

    std::cout << "FRust plugin node-library manifest passed.\n";
    return 0;
}
