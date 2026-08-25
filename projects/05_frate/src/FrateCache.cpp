#include <frate/FrateCache.h>
#include <iostream>

namespace frate {

juce::File FrateCache::settingsFilePath() {
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("frate_settings.json");
}

juce::File FrateCache::resolveDefaultCacheRoot() {
    juce::String envOverride = juce::SystemStats::getEnvironmentVariable("FRATE_CACHE_DIR", {});
    if (envOverride.isNotEmpty()) {
        return juce::File(envOverride);
    }

    juce::File settingsFile = settingsFilePath();
    if (settingsFile.existsAsFile()) {
        auto json = juce::JSON::parse(settingsFile);
        juce::String saved = json.getProperty("cacheRoot", {}).toString();
        if (saved.isNotEmpty()) {
            return juce::File(saved);
        }
    }

    // Deliberately NOT %APPDATA% - see FrateCache.h's doc comment. Lands
    // under the repo's own bin/Debug|Release for a from-source build, or
    // wherever a release zip was extracted, never a fixed spot on C:.
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("cache");
}

bool FrateCache::setCacheRoot(const juce::File& newRoot) {
    newRoot.createDirectory();

    juce::File settingsFile = settingsFilePath();
    settingsFile.deleteFile();
    auto stream = settingsFile.createOutputStream();
    if (!stream) return false;

    auto* settings = new juce::DynamicObject();
    settings->setProperty("cacheRoot", newRoot.getFullPathName());
    juce::JSON::writeToStream(*stream, juce::var(settings));
    return true;
}

void FrateCache::promptForCacheRootIfUnset() {
    if (juce::SystemStats::getEnvironmentVariable("FRATE_CACHE_DIR", {}).isNotEmpty()) return;
    if (settingsFilePath().existsAsFile()) return;

    juce::File defaultRoot = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                  .getParentDirectory()
                                  .getChildFile("cache");

    std::cout << "Frate hasn't been configured with a cache folder yet.\n"
              << "This is where downloaded/built pod dependencies are stored - shared\n"
              << "across every Frust project on this machine, not per-project.\n\n"
              << "Cache folder [" << defaultRoot.getFullPathName().toStdString() << "]\n"
              << "(or run `frate cache-dir <path>` later to change this): ";

    std::string chosen;
    std::getline(std::cin, chosen);

    juce::File cacheRoot = chosen.empty() ? defaultRoot : juce::File(juce::String(chosen));
    if (setCacheRoot(cacheRoot)) {
        std::cout << "Using " << cacheRoot.getFullPathName().toStdString()
                  << " - saved to frate_settings.json, won't ask again.\n\n";
    } else {
        std::cout << "Using " << cacheRoot.getFullPathName().toStdString()
                  << " for this run, but couldn't save that choice to "
                  << settingsFilePath().getFullPathName().toStdString() << " - will ask again next time.\n\n";
    }
}

FrateCache::FrateCache() {
    cacheRoot = resolveDefaultCacheRoot();
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
