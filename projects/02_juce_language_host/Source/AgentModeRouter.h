#pragma once

#include <JuceHeader.h>
#include <ai_provider/AiProvider.h>

#include <vector>

enum class AgentMode
{
    automatic = 1,
    plan = 2,
    execute = 3,
    review = 4,
    answer = 5
};

struct AgentModeDecision
{
    bool ok = false;
    AgentMode mode = AgentMode::answer;
    bool continuation = false;
    double confidence = 0.0;
    juce::String reason;
    juce::String error;
};

class AgentModeRouter
{
public:
    static juce::String modeName(AgentMode mode);
    static std::vector<ai_provider::ChatMessage> messagesFor(
        const juce::String& userPrompt,
        const juce::String& previousTaskSummary);
    static AgentModeDecision parse(const juce::String& response);
};
