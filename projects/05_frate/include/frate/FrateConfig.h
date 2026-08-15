#pragma once

#include <frate/PodMetadata.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace frate {

class FrateConfig {
public:
    FrateConfig() = default;
    
    bool load(const juce::File& frateJsonFile);
    bool save(const juce::File& frateJsonFile) const;

    const PodMetadata& getMetadata() const { return metadata; }
    void setMetadata(const PodMetadata& meta) { metadata = meta; }

    const std::vector<PodDependency>& getDependencies() const;
    void addDependency(const PodDependency& dep);
    void removeDependency(const std::string& name);

private:
    PodMetadata metadata;
};

} // namespace frate
