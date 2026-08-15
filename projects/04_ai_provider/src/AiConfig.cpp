#include "ai_provider/AiConfig.h"
#include "ai_provider/OpenAiProvider.h"

#include <juce_core/juce_core.h>

namespace ai_provider {

namespace {

void writeDefaultConfig(const juce::File& file) {
    auto* profile = new juce::DynamicObject();
    profile->setProperty("name", "OpenAI (default)");
    profile->setProperty("provider", "openai");
    profile->setProperty("apiKey", "PASTE_YOUR_OPENAI_API_KEY_HERE");
    profile->setProperty("model", "gpt-4o-mini");

    juce::Array<juce::var> profileArray;
    profileArray.add(juce::var(profile));

    auto* root = new juce::DynamicObject();
    root->setProperty("profiles", profileArray);

    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(root), true));
}

} // namespace

AiConfig::AiConfig(const std::string& configFilePath) {
    juce::File file(configFilePath);
    if (!file.existsAsFile()) writeDefaultConfig(file);

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    auto* profilesArray = parsed.getProperty("profiles", {}).getArray();
    if (profilesArray == nullptr) return;

    for (auto& p : *profilesArray) {
        if (!p.isObject()) continue;
        AiProfile profile;
        profile.name = p.getProperty("name", {}).toString().toStdString();
        profile.provider = p.getProperty("provider", {}).toString().toStdString();
        profile.apiKey = p.getProperty("apiKey", {}).toString().toStdString();
        profile.model = p.getProperty("model", {}).toString().toStdString();
        if (!profile.name.empty()) profiles_.push_back(std::move(profile));
    }
}

std::unique_ptr<AiProvider> AiConfig::createProvider(const std::string& profileName) const {
    for (auto& p : profiles_) {
        if (p.name != profileName) continue;

        if (p.provider == "openai") return std::make_unique<OpenAiProvider>(p.apiKey, p.model);

        return nullptr; // unrecognized provider type
    }
    return nullptr; // no such profile
}

} // namespace ai_provider
