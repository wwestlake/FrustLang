#include "AgentTask.h"

#include <iostream>

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
}

ai_provider::ToolCall call(const std::string& name, const std::string& arguments)
{
    return { "test-call", name, arguments };
}
}

int main()
{
    auto task = AgentTask::begin("conversation", "Update main.fr", "execute", true, true, true);
    auto early = task.executeControl(call("agent_complete_task", R"({"summary":"done"})"));
    expect(!early.ok, "Completion is rejected before work starts");

    task.recordEngineerResult("workspace_read", { true, false, "Read src/main.fr" });
    auto plan = task.executeControl(call("agent_set_plan",
        R"({"goal":"Update main.fr","steps":["Inspect existing code","Edit the command loop","Compile-check the result"]})"));
    expect(plan.ok, "Plan is accepted after inspection");

    task.recordEngineerResult("workspace_replace_text", { true, true, "Updated src/main.fr" });
    auto unverified = task.executeControl(call("agent_complete_task", R"({"summary":"done"})"));
    expect(!unverified.ok, "Completion is rejected before verification");

    task.recordEngineerResult("workspace_check_frust", { true, false, "Frust check passed", true });
    auto complete = task.executeControl(call("agent_complete_task", R"({"summary":"Updated the command loop."})"));
    expect(complete.ok && complete.terminal && task.isCompleted(),
           "Completion is accepted after inspection, edit, and verification");

    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("frust-agent-task", {}, true);
    expect(folder.createDirectory().wasOk(), "Temporary state folder is created");
    expect(task.save(folder), "Task state is saved");
    AgentTask loaded;
    expect(AgentTask::load(folder, "conversation", loaded) && loaded.isCompleted(),
           "Task state survives a save and reload");

    auto planOnly = AgentTask::begin("plan-conversation", "Design a parser", "plan",
                                     true, false, false);
    planOnly.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    expect(planOnly.executeControl(call("agent_set_plan",
        R"({"goal":"Design a parser","steps":["Read grammar","Describe implementation"]})")).ok,
        "Plan mode records a plan");
    expect(planOnly.executeControl(call("agent_complete_task",
        R"({"summary":"The implementation plan is ready."})")).ok,
        "Plan mode completes without writing");

    auto review = AgentTask::begin("review-conversation", "Review current code", "review",
                                   false, false, false);
    review.recordEngineerResult("workspace_search", { true, false, "Found definitions" });
    expect(review.executeControl(call("agent_complete_task",
        R"({"summary":"One correctness issue found."})")).ok,
        "Review mode completes after inspection without a plan or write");
    folder.deleteRecursively();

    if (failures == 0) std::cout << "AgentTaskTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
