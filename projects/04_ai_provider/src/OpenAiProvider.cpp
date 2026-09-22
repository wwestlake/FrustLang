#include "ai_provider/OpenAiProvider.h"

#include <juce_core/juce_core.h>
#include <algorithm>

namespace ai_provider {

namespace {

bool shouldEnableWebSearch(const std::vector<ChatMessage>& messages)
{
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role != "user") continue;
        const auto text = juce::String(it->content).toLowerCase();
        return text.contains("web") || text.contains("internet") || text.contains("search")
            || text.contains("research") || text.contains("look up") || text.contains("latest")
            || text.contains("current information") || text.contains("online");
    }
    return false;
}

int retryDelayMs(const juce::StringPairArray& headers, const juce::String& message, int attempt)
{
    auto retryAfter = headers.getValue("retry-after", {}).getDoubleValue();
    if (retryAfter <= 0.0) {
        const auto marker = message.indexOfIgnoreCase("try again in ");
        if (marker >= 0)
            retryAfter = message.substring(marker + 13).upToFirstOccurrenceOf("s", false, false)
                .getDoubleValue();
    }
    if (retryAfter <= 0.0) retryAfter = 1.5 * static_cast<double>(attempt + 1);
    return juce::jlimit(500, 10000, static_cast<int>(retryAfter * 1000.0) + 250);
}

void appendMessageItem(juce::Array<juce::var>& input, const ChatMessage& message)
{
    auto* item = new juce::DynamicObject();
    item->setProperty("role", juce::String(message.role));
    item->setProperty("content", juce::String(message.content));
    input.add(juce::var(item));
}

} // namespace

OpenAiProvider::OpenAiProvider(std::string apiKeyIn, std::string modelIn)
    : apiKey(std::move(apiKeyIn)), model(std::move(modelIn)) {}

ChatResponse OpenAiProvider::sendChat(const std::vector<ChatMessage>& messages,
                                      const std::vector<ToolDefinition>& tools,
                                      ToolChoice toolChoice) {
    if (apiKey.empty() || apiKey == "PASTE_YOUR_OPENAI_API_KEY_HERE") {
        return { false, {}, "No OpenAI API key set for this profile (open AI Settings)." };
    }

    juce::Array<juce::var> input;
    for (const auto& message : messages) {
        if (!message.providerItemsJson.empty()) {
            const auto savedItems = juce::JSON::parse(juce::String(message.providerItemsJson));
            if (auto* items = savedItems.getArray())
                for (const auto& item : *items) input.add(item);
            continue;
        }
        if (message.role == "tool") {
            auto* output = new juce::DynamicObject();
            output->setProperty("type", "function_call_output");
            output->setProperty("call_id", juce::String(message.toolCallId));
            output->setProperty("output", juce::String(message.content));
            input.add(juce::var(output));
            continue;
        }
        if (!message.content.empty()) appendMessageItem(input, message);
        for (const auto& call : message.toolCalls) {
            auto* item = new juce::DynamicObject();
            item->setProperty("type", "function_call");
            item->setProperty("call_id", juce::String(call.id));
            item->setProperty("name", juce::String(call.name));
            item->setProperty("arguments", juce::String(call.argumentsJson));
            input.add(juce::var(item));
        }
    }

    auto* bodyObj = new juce::DynamicObject();
    bodyObj->setProperty("model", juce::String(model.empty() ? "gpt-4o-mini" : model));
    bodyObj->setProperty("input", input);

    juce::Array<juce::var> toolArray;
    const bool webSearchEnabled = shouldEnableWebSearch(messages);
    if (webSearchEnabled) {
        auto* webSearch = new juce::DynamicObject();
        webSearch->setProperty("type", "web_search");
        toolArray.add(juce::var(webSearch));
    }
    for (const auto& tool : tools) {
        auto parameters = juce::JSON::parse(juce::String(tool.parametersJson));
        if (!parameters.isObject())
            return { false, {}, "Tool schema is not a JSON object: " + tool.name };
        auto* function = new juce::DynamicObject();
        function->setProperty("type", "function");
        function->setProperty("name", juce::String(tool.name));
        function->setProperty("description", juce::String(tool.description));
        function->setProperty("parameters", parameters);
        toolArray.add(juce::var(function));
    }
    if (!toolArray.isEmpty()) {
        bodyObj->setProperty("tools", toolArray);
    }
    if (webSearchEnabled) {
        bodyObj->setProperty("include", juce::Array<juce::var> { "web_search_call.action.sources" });
    }
    if (!tools.empty()) {
        bodyObj->setProperty("tool_choice",
            toolChoice == ToolChoice::required ? "required" : "auto");
    }

    auto bodyText = juce::JSON::toString(juce::var(bodyObj), true);
    juce::MemoryBlock postData(bodyText.toRawUTF8(), bodyText.getNumBytesAsUTF8());

    juce::String headers = "Content-Type: application/json\r\nAuthorization: Bearer " + juce::String(apiKey);
    int statusCode = 0;
    juce::String responseText;
    juce::var parsed;
    for (int attempt = 0; attempt < 3; ++attempt) {
        juce::StringPairArray responseHeaders;
        auto url = juce::URL("https://api.openai.com/v1/responses").withPOSTData(postData);
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders(headers)
                .withConnectionTimeoutMs(60000)
                .withHttpRequestCmd("POST")
                .withStatusCode(&statusCode)
                .withResponseHeaders(&responseHeaders));
        if (stream == nullptr)
            return { false, {}, "Could not reach api.openai.com (network/DNS failure)." };
        responseText = stream->readEntireStreamAsString();
        parsed = juce::JSON::parse(responseText);
        if (statusCode == 200) break;

        const auto error = parsed.getProperty("error", {});
        const auto message = error.isObject() ? error.getProperty("message", {}).toString()
                                               : responseText;
        if ((statusCode == 429 || statusCode >= 500) && attempt < 2) {
            juce::Thread::sleep(retryDelayMs(responseHeaders, message, attempt));
            continue;
        }
        if (statusCode == 429)
            return { false, {}, "The selected model is temporarily rate-limited. FrustIDE retried "
                "automatically, but the account still needs a moment. Please send again shortly." };
        return { false, {}, "OpenAI request failed (HTTP " + std::to_string(statusCode)
            + "): " + message.toStdString() };
    }

    auto* output = parsed.getProperty("output", {}).getArray();
    if (output == nullptr)
        return { false, {}, "OpenAI response did not contain an output array." };

    juce::String content;
    juce::StringArray sourceUrls;
    juce::StringArray sourceTitles;
    std::vector<ToolCall> toolCalls;
    bool hostedToolUsed = false;
    for (const auto& item : *output) {
        const auto type = item.getProperty("type", {}).toString();
        if (type == "web_search_call") {
            hostedToolUsed = true;
        } else if (type == "function_call") {
            ToolCall call;
            call.id = item.getProperty("call_id", {}).toString().toStdString();
            call.name = item.getProperty("name", {}).toString().toStdString();
            call.argumentsJson = item.getProperty("arguments", {}).toString().toStdString();
            if (!call.id.empty() && !call.name.empty())
                toolCalls.push_back(std::move(call));
        } else if (type == "message") {
            if (auto* parts = item.getProperty("content", {}).getArray())
                for (const auto& part : *parts)
                    if (part.getProperty("type", {}).toString() == "output_text")
                    {
                        content << part.getProperty("text", {}).toString();
                        if (auto* annotations = part.getProperty("annotations", {}).getArray())
                        {
                            for (const auto& annotation : *annotations)
                            {
                                if (annotation.getProperty("type", {}).toString() != "url_citation")
                                    continue;
                                const auto url = annotation.getProperty("url", {}).toString();
                                if (url.isEmpty() || sourceUrls.contains(url)) continue;
                                sourceUrls.add(url);
                                auto title = annotation.getProperty("title", {}).toString().trim();
                                sourceTitles.add(title.isEmpty() ? url : title);
                            }
                        }
                    }
        }
    }
    if (!sourceUrls.isEmpty())
    {
        content << "\n\n**Sources**\n";
        for (int index = 0; index < sourceUrls.size(); ++index)
            content << "- [" << sourceTitles[index] << "](" << sourceUrls[index] << ")\n";
    }
    return { true, content.toStdString(), {}, std::move(toolCalls),
             juce::JSON::toString(parsed.getProperty("output", {}), false).toStdString(),
             hostedToolUsed };
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
