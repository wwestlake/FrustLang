#include <frate/FrateConfig.h>
#include "../src/PodMetadataJson.h"

namespace frate {

bool FrateConfig::load(const juce::File& frateJsonFile) {
    if (!frateJsonFile.existsAsFile()) {
        return false;
    }

    auto jsonVar = juce::JSON::parse(frateJsonFile);
    if (!jsonVar.isObject()) {
        return false;
    }

    metadata = PodMetadataJson::fromJson(jsonVar);
    return true;
}

bool FrateConfig::save(const juce::File& frateJsonFile) const {
    auto jsonVar = PodMetadataJson::toJson(metadata);
    juce::String jsonStr = juce::JSON::toString(jsonVar);
    return frateJsonFile.replaceWithText(jsonStr);
}

const std::vector<PodDependency>& FrateConfig::getDependencies() const {
    return metadata.dependencies;
}

void FrateConfig::addDependency(const PodDependency& dep) {
    for (auto& existing : metadata.dependencies) {
        if (existing.name == dep.name) {
            existing.version = dep.version;
            return;
        }
    }
    metadata.dependencies.push_back(dep);
}

void FrateConfig::removeDependency(const std::string& name) {
    metadata.dependencies.erase(std::remove_if(metadata.dependencies.begin(), metadata.dependencies.end(),
        [&](const PodDependency& dep) { return dep.name == name; }), metadata.dependencies.end());
}

} // namespace frate
