#include <frate/FrateResolver.h>
#include <juce_core/juce_core.h>

namespace frate {

FrateResolver::FrateResolver(FrateCache& c, FrateRegistryClient& r)
    : cache(c), registryClient(r) {}

std::map<std::string, ResolveStatus> FrateResolver::resolveAll(const FrateConfig& config) {
    std::map<std::string, ResolveStatus> statuses;
    
    for (const auto& dep : config.getDependencies()) {
        statuses[dep.name] = resolve(dep.name, dep.version);
    }
    
    return statuses;
}

ResolveStatus FrateResolver::resolve(const std::string& name, const std::string& version) {
    if (cache.isCached(name, version)) {
        return ResolveStatus::ResolvedFromCache;
    }
    
    juce::String downloadUrl = registryClient.getDownloadUrl(name, version);
    if (downloadUrl.isEmpty()) {
        return ResolveStatus::UnresolvedNotFound;
    }
    
    juce::File tempFile = juce::File::createTempFile(".frpod");
    if (!registryClient.downloadFromS3(downloadUrl, tempFile)) {
        return ResolveStatus::UnresolvedNetworkError;
    }
    
    bool installed = cache.installFromPackage(tempFile, name, version);
    tempFile.deleteFile();
    
    if (installed) {
        return ResolveStatus::ResolvedFromRegistry;
    }
    
    return ResolveStatus::UnresolvedExtractError;
}

} // namespace frate
