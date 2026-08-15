#pragma once

#include <juce_core/juce_core.h>
#include <frate/PodMetadata.h>

namespace frate {

class FratePodBuilder {
public:
    static bool scaffoldPod(const juce::File& targetDir, const PodMetadata& metadata);
    
    // Returns the path to the created .frpod file, or juce::File() on failure.
    static juce::File packagePod(const juce::File& podDir);
};

} // namespace frate
