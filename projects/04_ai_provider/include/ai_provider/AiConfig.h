#pragma once

#include "AiProvider.h"

#include <memory>
#include <string>
#include <vector>

namespace ai_provider {

struct AiProfile {
    std::string name;     // user-facing label, e.g. "OpenAI (work)"
    std::string provider; // "openai" today; add a case in createProvider() for more
    std::string apiKey;
    std::string model;
};

// Loads named AI profiles from a JSON config file (see this project's
// README/CMakeLists comment: always somewhere outside the repo, e.g. user
// app-data, so a key can never end up committed). If the file doesn't
// exist yet, creates it with one placeholder profile for the user to fill
// in themselves - this class never receives or handles a real key value
// itself, it only reads whatever's already on disk.
class AiConfig {
public:
    explicit AiConfig(const std::string& configFilePath);

    const std::vector<AiProfile>& profiles() const { return profiles_; }

    // Builds a provider for the named profile, or nullptr if no such
    // profile exists or its provider type isn't recognized.
    std::unique_ptr<AiProvider> createProvider(const std::string& profileName) const;

private:
    std::vector<AiProfile> profiles_;
};

} // namespace ai_provider
