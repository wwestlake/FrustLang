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
    if (obj->hasProperty("sourceFile")) {
        m.sourceFile = obj->getProperty("sourceFile").toString().toStdString();
    }
    if (obj->hasProperty("implementedInterfaces") && obj->getProperty("implementedInterfaces").isArray()) {
        auto* arr = obj->getProperty("implementedInterfaces").getArray();
        for (auto& item : *arr) {
            if (!item.isObject()) continue;
            auto* ifaceObj = item.getDynamicObject();
            PluginInterfaceImpl impl;
            impl.interfaceName = ifaceObj->getProperty("interface").toString().toStdString();
            impl.typeName = ifaceObj->getProperty("type").toString().toStdString();
            impl.constructorFn = ifaceObj->getProperty("constructor").toString().toStdString();
            m.implementedInterfaces.push_back(impl);
        }
    }
    if (obj->hasProperty("firedEvents") && obj->getProperty("firedEvents").isArray()) {
        auto* arr = obj->getProperty("firedEvents").getArray();
        for (auto& item : *arr) m.firedEvents.push_back(item.toString().toStdString());
    }
    if (obj->hasProperty("listenedEvents") && obj->getProperty("listenedEvents").isArray()) {
        auto* arr = obj->getProperty("listenedEvents").getArray();
        for (auto& item : *arr) m.listenedEvents.push_back(item.toString().toStdString());
    }
    if (obj->hasProperty("requiredHostFunctions") && obj->getProperty("requiredHostFunctions").isArray()) {
        auto* arr = obj->getProperty("requiredHostFunctions").getArray();
        for (auto& item : *arr) {
            if (!item.isObject()) continue;
            auto* fnObj = item.getDynamicObject();
            RequiredHostFunction fn;
            fn.name = fnObj->getProperty("name").toString().toStdString();
            if (fnObj->hasProperty("description")) {
                fn.description = fnObj->getProperty("description").toString().toStdString();
            }
            m.requiredHostFunctions.push_back(fn);
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

FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_source_file(FrustPluginManifestHandle handle) {
    return handle ? handle->manifest.sourceFile.c_str() : nullptr;
}

FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_interface_count(FrustPluginManifestHandle handle) {
    return handle ? static_cast<int32_t>(handle->manifest.implementedInterfaces.size()) : 0;
}
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_interface_name(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.implementedInterfaces.size()) return nullptr;
    return handle->manifest.implementedInterfaces[static_cast<size_t>(index)].interfaceName.c_str();
}
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_interface_type(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.implementedInterfaces.size()) return nullptr;
    return handle->manifest.implementedInterfaces[static_cast<size_t>(index)].typeName.c_str();
}
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_interface_constructor(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.implementedInterfaces.size()) return nullptr;
    return handle->manifest.implementedInterfaces[static_cast<size_t>(index)].constructorFn.c_str();
}

FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_fired_event_count(FrustPluginManifestHandle handle) {
    return handle ? static_cast<int32_t>(handle->manifest.firedEvents.size()) : 0;
}
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_fired_event(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.firedEvents.size()) return nullptr;
    return handle->manifest.firedEvents[static_cast<size_t>(index)].c_str();
}
FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_listened_event_count(FrustPluginManifestHandle handle) {
    return handle ? static_cast<int32_t>(handle->manifest.listenedEvents.size()) : 0;
}
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_listened_event(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.listenedEvents.size()) return nullptr;
    return handle->manifest.listenedEvents[static_cast<size_t>(index)].c_str();
}

FRUST_PLUGIN_HOST_API int32_t frust_plugin_manifest_required_host_function_count(FrustPluginManifestHandle handle) {
    return handle ? static_cast<int32_t>(handle->manifest.requiredHostFunctions.size()) : 0;
}
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_required_host_function_name(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.requiredHostFunctions.size()) return nullptr;
    return handle->manifest.requiredHostFunctions[static_cast<size_t>(index)].name.c_str();
}
FRUST_PLUGIN_HOST_API const char* frust_plugin_manifest_required_host_function_description(FrustPluginManifestHandle handle, int32_t index) {
    if (!handle || index < 0 || static_cast<size_t>(index) >= handle->manifest.requiredHostFunctions.size()) return nullptr;
    return handle->manifest.requiredHostFunctions[static_cast<size_t>(index)].description.c_str();
}

} // extern "C"
