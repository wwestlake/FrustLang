#pragma once

#include <JuceHeader.h>
#include <ai_provider/AiProvider.h>

#include "EngineerTools.h"

#include <vector>

class AgentTask
{
public:
    AgentTask() = default;

    struct ControlResult
    {
        bool handled = false;
        bool ok = false;
        bool terminal = false;
        juce::String message;
    };

    static AgentTask begin(const juce::String& conversationId,
                           const juce::String& goal,
                           const juce::String& mode,
                           bool requiresPlan,
                           bool requiresWrite,
                           bool requiresVerification,
                           const juce::StringArray& initialPlan = {});
    static bool load(const juce::File& conversationFolder,
                     const juce::String& conversationId,
                     AgentTask& task);
    bool resume();

    bool save(const juce::File& conversationFolder) const;
    std::vector<ai_provider::ToolDefinition> controlDefinitions() const;
    ControlResult executeControl(const ai_provider::ToolCall& call);
    void recordEngineerResult(const std::string& toolName, const EngineerTools::Result& result);
    void recordProviderUsage(const ai_provider::ChatResponse& response);
    juce::String budgetExceeded(int maxProviderCalls, int maxToolCalls,
                                int maxTotalTokens) const;
    juce::String budgetBeforeProviderCall(int maxProviderCalls, int maxToolCalls,
                                          int maxTotalTokens) const;
    void fail(const juce::String& reason);
    bool continuePlanAsExecution(bool verificationRequired);

    juce::String contextMessage() const;
    juce::String statusLine() const;
    juce::String finalMessage() const;
    bool isTerminal() const;
    bool isCompleted() const;
    bool isResumable() const;
    bool canWrite() const;
    const juce::StringArray& planSteps() const;
    const juce::String& taskGoal() const;
    const juce::String& taskMode() const;
    juce::String currentPhase() const;
    juce::var evaluationSnapshot() const;
    static bool proposedWriteHasUnresolvedImplementation(const ai_provider::ToolCall& call);

private:
    juce::String phase() const;
    juce::String completionBlocker() const;
    static juce::String failureFingerprint(const juce::String& message);
    void recordCommandEvidence(const juce::String& message);
    juce::var toJson() const;
    static bool fromJson(const juce::var&, AgentTask&);

    juce::String taskId;
    juce::String conversationId;
    juce::String goal;
    juce::String mode { "execute" };
    juce::String status { "running" };
    juce::String summary;
    juce::String pendingQuestion;
    juce::String latestVerification;
    juce::String repeatedFailure;
    juce::StringArray plan;
    juce::StringArray constraints;
    juce::StringArray acceptanceTests;
    juce::StringArray requiredCapabilities;
    juce::StringArray availableCapabilities;
    juce::StringArray missingCapabilities;
    juce::StringArray capabilityEvidence;
    juce::StringArray observations;
    bool requiresWrite = false;
    bool requiresPlan = true;
    bool requiresVerification = false;
    bool requiresBuildEvidence = false;
    bool requiresTestEvidence = false;
    bool requiresPublishEvidence = false;
    bool inspected = false;
    bool capabilitiesAssessed = false;
    bool changed = false;
    // Did the task DO something: a file change, a build or test, a command that acted, or opening a program. A task like
    // "rebuild it and start it" changes no file; counting only file changes trapped it, unable to finish.
    bool acted = false;
    bool verified = false;
    bool buildVerified = false;
    bool testsVerified = false;
    bool published = false;
    int repeatedFailureCount = 0;
    int verificationFailureCount = 0;
    int toolCalls = 0;
    int providerCalls = 0;
    int inputTokens = 0;
    int outputTokens = 0;
    int totalTokens = 0;
    int lastInputTokens = 0;
};
