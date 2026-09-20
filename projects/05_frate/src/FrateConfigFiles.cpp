// The file half of FrateConfig: reading and writing frate.json on a real disk. Its own object file so an
// application that keeps pods in the VFS (and uses loadFromString / toJsonString) does not link it.

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

} // namespace frate
