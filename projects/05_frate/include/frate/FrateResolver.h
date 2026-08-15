#pragma once

#include <frate/FrateConfig.h>
#include <frate/FrateCache.h>
#include <frate/FrateRegistryClient.h>
#include <map>
#include <string>

namespace frate {

enum class ResolveStatus {
    ResolvedFromCache,
    ResolvedFromRegistry,
    UnresolvedNotFound,
    UnresolvedNetworkError,
    UnresolvedExtractError
};

class FrateResolver {
public:
    FrateResolver(FrateCache& cache, FrateRegistryClient& client);

    std::map<std::string, ResolveStatus> resolveAll(const FrateConfig& config);
    ResolveStatus resolve(const std::string& name, const std::string& version);

private:
    FrateCache& cache;
    FrateRegistryClient& registryClient;
};

} // namespace frate
