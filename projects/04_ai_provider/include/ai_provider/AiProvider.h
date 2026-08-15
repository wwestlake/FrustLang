#pragma once

#include <string>
#include <vector>

namespace ai_provider {

struct ChatMessage {
    std::string role;    // "system" | "user" | "assistant"
    std::string content;
};

struct ChatResponse {
    bool ok = false;
    std::string content;      // the assistant's reply, when ok
    std::string errorMessage; // human-readable failure reason, when !ok
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
    virtual ChatResponse sendChat(const std::vector<ChatMessage>& messages) = 0;
};

} // namespace ai_provider
