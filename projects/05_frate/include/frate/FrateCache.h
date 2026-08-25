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

    // Copies a pod bundled with a packaged FRust release's stdlib folder into
    // the writable cache. Returns false when this is a source build, the pod
    // is not bundled, or its manifest does not match the requested version.
    bool installBundledPodIfAvailable(const std::string& name, const std::string& version);

    // Where the cache root actually lives, checked in this order:
    //   1. FRATE_CACHE_DIR environment variable, if set (highest priority,
    //      always available - scripting/CI escape hatch).
    //   2. A path saved in frate_settings.json, beside a from-source
    //      executable or under %APPDATA%\FRust for a packaged install.
    //   3. Otherwise, <the running executable's own directory>/cache -
    //      for a from-source build this naturally lands under the repo's
    //      own bin/Debug or bin/Release. A packaged install uses
    //      %APPDATA%\FRust\cache so it remains writable under Program Files.
    static juce::File resolveDefaultCacheRoot();

    // CLI-only: if no cache location has ever been chosen (no
    // frate_settings.json next to the exe, and FRATE_CACHE_DIR isn't
    // set), prompts the user once via stdin/stdout for a folder, then
    // persists the choice (an empty answer accepts the resolveDefault-
    // CacheRoot() fallback, persisted explicitly so this never prompts
    // again). Never call this from a GUI context - it blocks on
    // std::cin, which a windowed app has no way to satisfy.
    static void promptForCacheRootIfUnset();

    // Explicitly sets and persists the cache root (the `frate cache-dir
    // <path>` command). Creates the directory if it doesn't exist yet.
    // Returns false only if frate_settings.json itself couldn't be
    // written - the
    // directory creation succeeding or not is independent of that.
    static bool setCacheRoot(const juce::File& newRoot);

private:
    static juce::File settingsFilePath();

    juce::File cacheRoot;
};

} // namespace frate
