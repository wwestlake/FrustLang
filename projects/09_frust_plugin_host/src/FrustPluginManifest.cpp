#include "frust_plugin_host/FrustPluginManifest.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <juce_core/juce_core.h>

namespace frust_plugin_host {

std::optional<PluginManifest> LoadPluginManifest(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "frust_plugin_host: cannot open manifest '" << path << "'\n";
        return std::nullopt;
    }
    std::stringstream buf;
    buf << file.rdbuf();

    juce::var parsed;
    auto parseResult = juce::JSON::parse(juce::String(buf.str()), parsed);
    if (parseResult.failed() || !parsed.isObject()) {
        std::cerr << "frust_plugin_host: failed to parse manifest '" << path << "': "
                   << parseResult.getErrorMessage() << "\n";
        return std::nullopt;
    }

    auto* obj = parsed.getDynamicObject();
    PluginManifest m;
    m.name = obj->getProperty("name").toString().toStdString();
    m.version = obj->getProperty("version").toString().toStdString();
    if (obj->hasProperty("description")) {
        m.description = obj->getProperty("description").toString().toStdString();
    }
    if (obj->hasProperty("requiredHostApiVersion")) {
        m.requiredHostApiVersion = obj->getProperty("requiredHostApiVersion").toString().toStdString();
    }
    if (obj->hasProperty("entryPoints") && obj->getProperty("entryPoints").isArray()) {
        auto* arr = obj->getProperty("entryPoints").getArray();
        for (auto& item : *arr) {
            m.entryPoints.push_back(item.toString().toStdString());
        }
    }
    return m;
}

} // namespace frust_plugin_host

namespace {
struct ManifestHolder {
    frust_plugin_host::PluginManifest manifest;
};
} // namespace

struct FrustPluginManifestHandleImpl : ManifestHolder {};

extern "C" {

FRUST_PLUGIN_HOST_API FrustPluginManifestHandle frust_plugin_manifest_load(const char* path) {
    auto parsed = frust_plugin_host::LoadPluginManifest(path);
    if (!parsed) return nullptr;
    auto* handle = new FrustPluginManifestHandleImpl();
    handle->manifest = std::move(*parsed);
    return handle;
}

FRUST_PLUGIN_HOST_API void frust_plugin_manifest_free(FrustPluginManifestHandle handle) {
    delete handle;
}

FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_name(FrustPluginManifestHandle handle) {
    return handle ? handle->manifest.name.c_str() : nullptr;
}

FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_version(FrustPluginManifestHandle handle) {
    return handle ? handle->manifest.version.c_str() : nullptr;
}

FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_description(FrustPluginManifestHandle handle) {
    return handle ? handle->manifest.description.c_str() : nullptr;
}

FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_entry_point_count(FrustPluginManifestHandle handle) {
    return handle ? static_cast<int32_t>(handle->manifest.entryPoints.size()) : 0;
}

FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_entry_point(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.entryPoints.size()) return nullptr;
    return handle->manifest.entryPoints[static_cast<size_t>(index)].c_str();
}

} // extern "C"
