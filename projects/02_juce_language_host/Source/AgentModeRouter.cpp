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
    if (mode == AgentMode::conversation) return "conversation";
    if (mode == AgentMode::architect) return "architect";
    return "auto";
}

AgentModeDecision AgentModeRouter::obviousDecision(const juce::String& userPrompt)
{
    auto prompt = userPrompt.trim().toLowerCase();
    while (prompt.startsWith("please ")) prompt = prompt.substring(7).trimStart();

    const juce::StringArray executeStarts {
        "add ", "build ", "change ", "create ", "delete ", "fix ", "implement ",
        "launch ", "make ", "remove ", "run ", "start ", "test ", "update ", "write "
    };
    bool execute = prompt.contains("this is an execute task")
        || prompt.startsWith("execute the plan") || prompt.startsWith("go ahead")
        || prompt.startsWith("go fix ");
    for (const auto& prefix : executeStarts)
        execute = execute || prompt.startsWith(prefix)
            || prompt.startsWith("i want you to " + prefix)
            || prompt.startsWith("i need you to " + prefix);

    if (!execute) return {};
    AgentModeDecision decision;
    decision.ok = true;
    decision.mode = AgentMode::execute;
    decision.continuation = prompt.startsWith("execute the plan") || prompt.startsWith("go ahead");
    decision.confidence = 1.0;
    decision.reason = "explicit project action";
    return decision;
}

std::vector<ai_provider::ChatMessage> AgentModeRouter::messagesFor(
    const juce::String& userPrompt,
    const juce::String& previousTaskSummary)
{
    const juce::String system =
        "You route one user request for a coding assistant. Do not answer the request and do not use tools. "
        "Choose exactly one mode:\n"
        "- conversation: casual chat, praise, personality, requirements discussion, brainstorming, or exploratory back-and-forth where no project inspection is needed yet.\n"
        "- architect: abstract product, business, domain, workflow, requirements, use-case, constraint, risk, trust, or system-shape discussion that should stay above implementation.\n"
        "- answer: a direct factual explanation, a question about prior behavior, or a request needing no project inspection.\n"
        "- review: inspect, research, read, diagnose, compare, or report without changing project files.\n"
        "- plan: produce or revise a concrete implementation plan without changing project files.\n"
        "- execute: create, edit, delete, build, run, test, launch, or otherwise act on the project.\n"
        "Choose execute when inspection or planning is requested as preparation for an implementation in the same request; "
        "the requested final outcome controls the mode. "
        "A question such as 'why did you do that?' is answer, not execute. A request to read a file is review. "
        "A request to reason about business objects, user purposes, requirements, architecture, or use cases is architect, not plan. "
        "Use continuation=true only when the request clearly asks to carry out the saved plan, such as 'execute the plan' "
        "or 'go ahead'. Return only one JSON object with this exact shape and no Markdown: "
        "{\"mode\":\"conversation|architect|answer|review|plan|execute\",\"continuation\":false,\"confidence\":0.0,\"reason\":\"short reason\"}.";

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
    if (mode == "conversation") decision.mode = AgentMode::conversation;
    else if (mode == "architect") decision.mode = AgentMode::architect;
    else if (mode == "answer") decision.mode = AgentMode::answer;
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
