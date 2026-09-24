#include "AgentModeRouter.h"

#include <iostream>

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
}
}

int main()
{
    auto review = AgentModeRouter::parse(
        R"({"mode":"review","continuation":false,"confidence":0.96,"reason":"read-only inspection"})");
    expect(review.ok && review.mode == AgentMode::review, "Parses review mode");
    expect(!review.continuation, "Parses a non-continuation");
    expect(review.confidence > 0.95, "Parses confidence");

    auto execute = AgentModeRouter::parse(
        "```json\n{\"mode\":\"execute\",\"continuation\":true,\"confidence\":1,\"reason\":\"saved plan\"}\n```");
    expect(execute.ok && execute.mode == AgentMode::execute, "Accepts fenced provider JSON");
    expect(execute.continuation, "Parses plan continuation");

    auto conversation = AgentModeRouter::parse(
        R"({"mode":"conversation","continuation":false,"confidence":0.91,"reason":"requirements discussion"})");
    expect(conversation.ok && conversation.mode == AgentMode::conversation, "Parses conversation mode");

    auto architect = AgentModeRouter::parse(
        R"({"mode":"architect","continuation":false,"confidence":0.93,"reason":"business architecture discussion"})");
    expect(architect.ok && architect.mode == AgentMode::architect, "Parses architect mode");

    expect(!AgentModeRouter::parse("I would use execute mode.").ok, "Rejects prose instead of JSON");
    expect(!AgentModeRouter::parse(R"({"mode":"destroy","continuation":false})").ok,
           "Rejects unknown modes");

    const auto obviousBuild = AgentModeRouter::obviousDecision(
        "Build the Conversation Analyzer. Inspect the project first, then implement and test it.");
    expect(obviousBuild.ok && obviousBuild.mode == AgentMode::execute,
           "An explicit build request bypasses ambiguous model routing");
    const auto politeFix = AgentModeRouter::obviousDecision("Please fix the parser and run its tests.");
    expect(politeFix.ok && politeFix.mode == AgentMode::execute,
           "A polite explicit edit request is deterministically Execute");
    expect(!AgentModeRouter::obviousDecision("How does the parser build its AST?").ok,
           "A question containing build terminology is not forced to Execute");

    const auto messages = AgentModeRouter::messagesFor("execute the plan", "Previous mode: plan");
    expect(messages.size() == 2, "Router uses a minimal two-message context");
    expect(messages[0].content.find("conversation|architect|answer|review|plan|execute") != std::string::npos,
           "Router advertises architect as a valid mode");
    expect(messages[1].content.find("execute the plan") != std::string::npos,
           "Router preserves the original request");
    expect(messages[1].content.find("Previous mode: plan") != std::string::npos,
           "Router receives compact saved task state");

    if (failures == 0) std::cout << "AgentModeRouterTests passed\n";
    return failures == 0 ? 0 : 1;
}
