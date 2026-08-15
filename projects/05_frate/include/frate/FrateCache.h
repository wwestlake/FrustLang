#pragma once

#include <juce_core/juce_core.h>
#include <string>

namespace frate {

class FrateCache {
public:
    FrateCache();
    FrateCache(const juce::File& customCacheRoot);

    bool isCached(const std::string& name, const std::string& version) const;
    juce::File getCachedPodDir(const std::string& name, const std::string& version) const;
    
    // Extracts a .frpod zip file into the cache directory for its name and version
    bool installFromPackage(const juce::File& frpodFile, const std::string& name, const std::string& version);

private:
    juce::File cacheRoot;
};

} // namespace frate
