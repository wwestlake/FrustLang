#include "AgentModeRouter.h"

namespace
{
juce::String property(const juce::var& object, const juce::Identifier& name)
{
    return object.getProperty(name, {}).toString().trim();
}
}

juce::String AgentModeRouter::modeName(AgentMode mode)
{
    if (mode == AgentMode::plan) return "plan";
    if (mode == AgentMode::execute) return "execute";
    if (mode == AgentMode::review) return "review";
    if (mode == AgentMode::answer) return "answer";
    return "auto";
}

std::vector<ai_provider::ChatMessage> AgentModeRouter::messagesFor(
    const juce::String& userPrompt,
    const juce::String& previousTaskSummary)
{
    const juce::String system =
        "You route one user request for a coding assistant. Do not answer the request and do not use tools. "
        "Choose exactly one mode:\n"
        "- answer: conversation, explanation, a question about prior behavior, or a request needing no project inspection.\n"
        "- review: inspect, research, read, diagnose, compare, or report without changing project files.\n"
        "- plan: produce or revise a concrete implementation plan without changing project files.\n"
        "- execute: create, edit, delete, build, run, test, launch, or otherwise act on the project.\n"
        "A question such as 'why did you do that?' is answer, not execute. A request to read a file is review. "
        "Use continuation=true only when the request clearly asks to carry out the saved plan, such as 'execute the plan' "
        "or 'go ahead'. Return only one JSON object with this exact shape and no Markdown: "
        "{\"mode\":\"answer|review|plan|execute\",\"continuation\":false,\"confidence\":0.0,\"reason\":\"short reason\"}.";

    juce::String user = "ORIGINAL USER REQUEST:\n" + userPrompt;
    if (previousTaskSummary.isNotEmpty())
        user << "\n\nSAVED TASK STATE:\n" << previousTaskSummary;
    else
        user << "\n\nSAVED TASK STATE:\n(none)";

    return { { "system", system.toStdString() }, { "user", user.toStdString() } };
}

AgentModeDecision AgentModeRouter::parse(const juce::String& response)
{
    AgentModeDecision decision;
    auto json = response.trim();
    if (json.startsWith("```"))
    {
        const auto firstNewline = json.indexOfChar('\n');
        if (firstNewline >= 0) json = json.substring(firstNewline + 1);
        if (json.endsWith("```")) json = json.dropLastCharacters(3).trimEnd();
    }
    const auto objectStart = json.indexOfChar('{');
    const auto objectEnd = json.lastIndexOfChar('}');
    if (objectStart < 0 || objectEnd < objectStart)
    {
        decision.error = "The mode router did not return a JSON object.";
        return decision;
    }

    const auto value = juce::JSON::parse(json.substring(objectStart, objectEnd + 1));
    if (!value.isObject())
    {
        decision.error = "The mode router returned invalid JSON.";
        return decision;
    }

    const auto mode = property(value, "mode").toLowerCase();
    if (mode == "answer") decision.mode = AgentMode::answer;
    else if (mode == "review") decision.mode = AgentMode::review;
    else if (mode == "plan") decision.mode = AgentMode::plan;
    else if (mode == "execute") decision.mode = AgentMode::execute;
    else
    {
        decision.error = "The mode router returned an unknown mode: " + mode;
        return decision;
    }

    decision.continuation = static_cast<bool>(value.getProperty("continuation", false));
    decision.confidence = juce::jlimit(0.0, 1.0, static_cast<double>(value.getProperty("confidence", 0.0)));
    decision.reason = property(value, "reason");
    decision.ok = true;
    return decision;
}
