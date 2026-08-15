#include "PodMetadataJson.h"

namespace frate {

PodMetadata PodMetadataJson::fromJson(const juce::var& jsonVar) {
    PodMetadata metadata;
    
    if (jsonVar.isObject()) {
        auto* obj = jsonVar.getDynamicObject();
        
        metadata.name = obj->getProperty("name").toString().toStdString();
        metadata.version = obj->getProperty("version").toString().toStdString();
        
        if (obj->hasProperty("type")) {
            metadata.type = obj->getProperty("type").toString().toStdString();
        } else {
            metadata.type = "bin"; // Default to bin
        }
        
        if (obj->hasProperty("description")) {
            metadata.description = obj->getProperty("description").toString().toStdString();
        }
        
        if (obj->hasProperty("exports") && obj->getProperty("exports").isArray()) {
            auto* exportsArray = obj->getProperty("exports").getArray();
            for (auto& item : *exportsArray) {
                metadata.exports.push_back(item.toString().toStdString());
            }
        }
        
        if (obj->hasProperty("dependencies") && obj->getProperty("dependencies").isArray()) {
            auto* depsArray = obj->getProperty("dependencies").getArray();
            for (auto& item : *depsArray) {
                if (item.isObject()) {
                    auto* depObj = item.getDynamicObject();
                    PodDependency dep;
                    dep.name = depObj->getProperty("name").toString().toStdString();
                    dep.version = depObj->getProperty("version").toString().toStdString();
                    metadata.dependencies.push_back(dep);
                }
            }
        }
        
        if (obj->hasProperty("workspace") && obj->getProperty("workspace").isObject()) {
            metadata.isWorkspace = true;
            auto* wsObj = obj->getProperty("workspace").getDynamicObject();
            if (wsObj->hasProperty("members") && wsObj->getProperty("members").isArray()) {
                auto* membersArray = wsObj->getProperty("members").getArray();
                for (auto& item : *membersArray) {
                    metadata.workspaceMembers.push_back(item.toString().toStdString());
                }
            }
        }
    }
    
    return metadata;
}

juce::var PodMetadataJson::toJson(const PodDependency& dep) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("name", juce::String(dep.name));
    obj->setProperty("version", juce::String(dep.version));
    return juce::var(obj);
}

juce::var PodMetadataJson::toJson(const PodMetadata& metadata) {
    auto* obj = new juce::DynamicObject();
    
    obj->setProperty("name", juce::String(metadata.name));
    obj->setProperty("version", juce::String(metadata.version));
    
    if (!metadata.type.empty()) {
        obj->setProperty("type", juce::String(metadata.type));
    }
    
    if (!metadata.description.empty()) {
        obj->setProperty("description", juce::String(metadata.description));
    }
    
    juce::Array<juce::var> exportsArray;
    for (const auto& exp : metadata.exports) {
        exportsArray.add(juce::var(juce::String(exp)));
    }
    obj->setProperty("exports", exportsArray);
    
    juce::Array<juce::var> depsArray;
    for (const auto& dep : metadata.dependencies) {
        depsArray.add(toJson(dep));
    }
    obj->setProperty("dependencies", depsArray);
    
    if (metadata.isWorkspace) {
        auto* wsObj = new juce::DynamicObject();
        juce::Array<juce::var> membersArray;
        for (const auto& member : metadata.workspaceMembers) {
            membersArray.add(juce::var(juce::String(member)));
        }
        wsObj->setProperty("members", membersArray);
        obj->setProperty("workspace", juce::var(wsObj));
    }
    
    return juce::var(obj);
}

} // namespace frate
