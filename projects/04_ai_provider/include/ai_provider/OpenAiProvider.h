#pragma once

#include "AiProvider.h"

namespace ai_provider {

class OpenAiProvider : public AiProvider {
public:
    OpenAiProvider(std::string apiKey, std::string model);

    std::string providerName() const override { return "openai"; }
    ChatResponse sendChat(const std::vector<ChatMessage>& messages) override;

private:
    std::string apiKey;
    std::string model;
};

} // namespace ai_provider
