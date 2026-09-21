#pragma once

#include <string>
#include <vector>

namespace ai_provider {

struct ToolCall {
    std::string id;
    std::string name;
    std::string argumentsJson;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::string parametersJson;
};

enum class ToolChoice {
    autoSelect,
    required
};

struct ChatMessage {
    std::string role;    // "system" | "user" | "assistant"
    std::string content;
    std::vector<ToolCall> toolCalls;
    std::string toolCallId;
};

struct ChatResponse {
    bool ok = false;
    std::string content;      // the assistant's reply, when ok
    std::string errorMessage; // human-readable failure reason, when !ok
    std::vector<ToolCall> toolCalls;
};

struct ModelListResponse {
    bool ok = false;
    std::vector<std::string> models;
    std::string errorMessage;
};

// Provider-agnostic chat interface. OpenAiProvider is the one real
// implementation right now; a new provider (Anthropic, a local model, etc.)
// is just another subclass plus a case in AiConfig::createProvider() - this
// interface itself never needs to change for that.
class AiProvider {
public:
    virtual ~AiProvider() = default;

    virtual std::string providerName() const = 0;

    // Blocking - callers on the message thread should invoke this off a
    // background thread (see AiChatPanel's usage) rather than stall the UI
    // for the length of an HTTP round trip.
    virtual ChatResponse sendChat(const std::vector<ChatMessage>& messages,
                                  const std::vector<ToolDefinition>& tools = {},
                                  ToolChoice toolChoice = ToolChoice::autoSelect) = 0;
    virtual ModelListResponse listModels() = 0;
};

} // namespace ai_provider
