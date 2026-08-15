#include <frate/FrateCache.h>

namespace frate {

FrateCache::FrateCache() {
    cacheRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Frate")
                    .getChildFile("cache");
}

FrateCache::FrateCache(const juce::File& customCacheRoot) 
    : cacheRoot(customCacheRoot) {
}

bool FrateCache::isCached(const std::string& name, const std::string& version) const {
    juce::File podDir = getCachedPodDir(name, version);
    return podDir.exists() && podDir.isDirectory() && podDir.getChildFile("frate.json").existsAsFile();
}

juce::File FrateCache::getCachedPodDir(const std::string& name, const std::string& version) const {
    return cacheRoot.getChildFile(name).getChildFile(version);
}

bool FrateCache::installFromPackage(const juce::File& frpodFile, const std::string& name, const std::string& version) {
    if (!frpodFile.existsAsFile()) return false;

    juce::File targetDir = getCachedPodDir(name, version);
    if (!targetDir.exists()) {
        targetDir.createDirectory();
    }

    juce::ZipFile zip(frpodFile);
    auto result = zip.uncompressTo(targetDir);
    return result.wasOk();
}

} // namespace frate
