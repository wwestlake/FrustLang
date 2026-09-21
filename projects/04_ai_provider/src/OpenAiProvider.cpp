#include "ai_provider/OpenAiProvider.h"

#include <juce_core/juce_core.h>
#include <algorithm>

namespace ai_provider {

OpenAiProvider::OpenAiProvider(std::string apiKeyIn, std::string modelIn)
    : apiKey(std::move(apiKeyIn)), model(std::move(modelIn)) {}

ChatResponse OpenAiProvider::sendChat(const std::vector<ChatMessage>& messages) {
    if (apiKey.empty() || apiKey == "PASTE_YOUR_OPENAI_API_KEY_HERE") {
        return { false, {}, "No OpenAI API key set for this profile (open AI Settings)." };
    }

    juce::Array<juce::var> messagesArray;
    for (auto& m : messages) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("role", juce::String(m.role));
        obj->setProperty("content", juce::String(m.content));
        messagesArray.add(juce::var(obj));
    }

    auto* bodyObj = new juce::DynamicObject();
    bodyObj->setProperty("model", juce::String(model.empty() ? "gpt-4o-mini" : model));
    bodyObj->setProperty("messages", messagesArray);

    auto bodyText = juce::JSON::toString(juce::var(bodyObj), true);
    juce::MemoryBlock postData(bodyText.toRawUTF8(), bodyText.getNumBytesAsUTF8());

    juce::URL url("https://api.openai.com/v1/chat/completions");
    url = url.withPOSTData(postData);

    juce::String headers = "Content-Type: application/json\r\nAuthorization: Bearer " + juce::String(apiKey);
    int statusCode = 0;

    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withExtraHeaders(headers)
            .withConnectionTimeoutMs(30000)
            .withHttpRequestCmd("POST")
            .withStatusCode(&statusCode));

    if (stream == nullptr) return { false, {}, "Could not reach api.openai.com (network/DNS failure)." };

    auto responseText = stream->readEntireStreamAsString();
    auto parsed = juce::JSON::parse(responseText);

    if (statusCode != 200) {
        auto errObj = parsed.getProperty("error", {});
        auto message = errObj.isObject() ? errObj.getProperty("message", {}).toString() : responseText;
        return { false, {}, "OpenAI request failed (HTTP " + std::to_string(statusCode) + "): " + message.toStdString() };
    }

    auto* choices = parsed.getProperty("choices", {}).getArray();
    if (choices == nullptr || choices->isEmpty())
        return { false, {}, "OpenAI response had no choices: " + responseText.toStdString() };

    auto content = choices->getReference(0).getProperty("message", {}).getProperty("content", {}).toString();
    return { true, content.toStdString(), {} };
}

ModelListResponse OpenAiProvider::listModels() {
    if (apiKey.empty() || apiKey == "PASTE_YOUR_OPENAI_API_KEY_HERE")
        return { false, {}, "Enter an OpenAI API key before refreshing models." };

    juce::URL url("https://api.openai.com/v1/models");
    const auto headers = "Authorization: Bearer " + juce::String(apiKey);
    int statusCode = 0;
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders(headers)
            .withConnectionTimeoutMs(30000)
            .withHttpRequestCmd("GET")
            .withStatusCode(&statusCode));

    if (stream == nullptr)
        return { false, {}, "Could not reach api.openai.com (network/DNS failure)." };

    const auto responseText = stream->readEntireStreamAsString();
    const auto parsed = juce::JSON::parse(responseText);
    if (statusCode != 200) {
        const auto error = parsed.getProperty("error", {});
        const auto message = error.isObject()
            ? error.getProperty("message", {}).toString()
            : responseText;
        return { false, {}, "OpenAI model request failed (HTTP "
            + std::to_string(statusCode) + "): " + message.toStdString() };
    }

    auto* data = parsed.getProperty("data", {}).getArray();
    if (data == nullptr)
        return { false, {}, "OpenAI model response did not contain a data array." };

    std::vector<std::string> models;
    models.reserve(static_cast<size_t>(data->size()));
    for (const auto& item : *data) {
        const auto id = item.getProperty("id", {}).toString().trim();
        if (id.isNotEmpty()) models.push_back(id.toStdString());
    }

    std::sort(models.begin(), models.end());
    models.erase(std::unique(models.begin(), models.end()), models.end());
    return { true, std::move(models), {} };
}

} // namespace ai_provider
