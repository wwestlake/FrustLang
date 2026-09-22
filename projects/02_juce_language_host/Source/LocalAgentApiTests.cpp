#include "LocalAgentApi.h"

#include <iostream>

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
}

juce::var request(const juce::String& url, const juce::String& token,
                  const juce::String& method, const juce::String& body = {})
{
    auto target = juce::URL(url);
    if (body.isNotEmpty()) target = target.withPOSTData(body);
    int statusCode = 0;
    auto stream = target.createInputStream(
        juce::URL::InputStreamOptions(body.isNotEmpty()
                ? juce::URL::ParameterHandling::inPostData
                : juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd(method)
            .withExtraHeaders("Authorization: Bearer " + token + "\r\nContent-Type: application/json")
            .withConnectionTimeoutMs(5000)
            .withStatusCode(&statusCode));
    if (stream == nullptr || statusCode < 200 || statusCode >= 300) return {};
    return juce::JSON::parse(stream->readEntireStreamAsString());
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const auto testFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("frustide-agent-api-test", {}, true);
    const auto discoveryFile = testFolder.getChildFile("agent-api.json");
    LocalAgentApi api(discoveryFile);
    api.onMessage = [](const juce::String& content, LocalAgentApi::Completion completion) {
        completion(true, "Echo: " + content);
    };

    expect(api.start(), "API starts on a loopback port");
    const auto discovery = juce::JSON::parse(discoveryFile.loadFileAsString());
    const auto baseUrl = discovery.getProperty("baseUrl", {}).toString();
    const auto token = discovery.getProperty("token", {}).toString();
    expect(baseUrl.startsWith("http://127.0.0.1:"), "Discovery uses a loopback URL");
    expect(token.isNotEmpty(), "Discovery contains a bearer token");

    const auto status = request(baseUrl + "/v1/status", token, "GET");
    expect(status.getProperty("status", {}).toString() == "ready", "Authenticated status succeeds");

    const auto submitted = request(baseUrl + "/v1/messages", token, "POST",
                                   R"({"content":"hello agent"})");
    const auto requestId = submitted.getProperty("requestId", {}).toString();
    expect(requestId.isNotEmpty(), "Message submission returns a request id");

    bool completed = false;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        const auto result = request(baseUrl + "/v1/requests/" + requestId, token, "GET");
        if (result.getProperty("status", {}).toString() != "completed") continue;
        expect(result.getProperty("response", {}).toString() == "Echo: hello agent",
               "Completed request returns the assistant response");
        completed = true;
        break;
    }
    expect(completed, "Submitted request reaches completion");

    api.stop();
    expect(!discoveryFile.existsAsFile(), "Discovery file is removed when the API stops");
    testFolder.deleteRecursively();
    if (failures == 0)
        std::cout << "LocalAgentApiTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
