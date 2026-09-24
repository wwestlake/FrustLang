#pragma once

#include "AiProvider.h"

namespace ai_provider {

class OpenAiProvider : public AiProvider {
public:
    OpenAiProvider(std::string apiKey, std::string model);

    std::string providerName() const override { return "openai"; }
    ChatResponse sendChat(const std::vector<ChatMessage>& messages,
                          const std::vector<ToolDefinition>& tools = {},
                          ToolChoice toolChoice = ToolChoice::autoSelect) override;
    ModelListResponse listModels() override;

private:
    std::string apiKey;
    std::string model;
};

} // namespace ai_provider
