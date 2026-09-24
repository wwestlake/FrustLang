#include <ai_provider/AiConfig.h>

#include <juce_core/juce_core.h>
#include <iostream>

int main()
{
    const auto testFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("frust-ai-config-" + juce::Uuid().toString());
    const auto configFile = testFolder.getChildFile("ai_config.json");

    auto fail = [&testFolder] (const std::string& message) {
        testFolder.deleteRecursively();
        std::cerr << message << '\n';
        return 1;
    };

    ai_provider::AiConfig config(configFile.getFullPathName().toStdString());
    if (config.profiles().empty())
        return fail("Default profile was not created.");

    std::string error;
    const auto profileName = config.profiles().front().name;
    if (!config.updateProfileCredentials(profileName, "test-api-key", "test-model", error))
        return fail("Credential update failed: " + error);

    ai_provider::AiConfig reloaded(configFile.getFullPathName().toStdString());
    if (reloaded.profiles().empty()
        || reloaded.profiles().front().apiKey != "test-api-key"
        || reloaded.profiles().front().model != "test-model")
        return fail("Saved credentials did not reload correctly.");

    if (reloaded.updateProfileCredentials("missing-profile", "key", "model", error))
        return fail("A missing profile was unexpectedly updated.");

    testFolder.deleteRecursively();
    std::cout << "AiConfig: save, reload, and missing-profile checks passed.\n";
    return 0;
}
