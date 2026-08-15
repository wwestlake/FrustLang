#pragma once

#include <frate/PodMetadata.h>
#include <juce_core/juce_core.h>

namespace frate {

class PodMetadataJson {
public:
    static PodMetadata fromJson(const juce::var& jsonVar);
    static juce::var toJson(const PodMetadata& metadata);
    static juce::var toJson(const PodDependency& dep);
};

} // namespace frate
