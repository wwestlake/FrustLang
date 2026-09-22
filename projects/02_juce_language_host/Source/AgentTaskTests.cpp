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

AgentTask::ControlResult assess(AgentTask& task)
{
    return task.executeControl(call("agent_assess_capabilities",
        R"({"required":["project files"],"available":["project files"],"missing":[],"evidence":["workspace_list found the project structure"],"options":[]})"));
}

AgentTask::ControlResult plan(AgentTask& task, const char* goal = "Update main.fr")
{
    return task.executeControl(call("agent_set_plan",
        std::string("{\"goal\":\"") + goal
        + R"(","steps":["Inspect existing code","Make focused edits","Run acceptance tests"],"constraints":["Keep the requested implementation technology"],"acceptance_tests":["The project check passes"]})"));
}
}

int main()
{
    auto task = AgentTask::begin("conversation", "Update main.fr", "execute", true, true, true);
    auto early = task.executeControl(call("agent_complete_task", R"({"summary":"done"})"));
    expect(!early.ok, "Completion is rejected before work starts");

    task.recordEngineerResult("workspace_read", { true, false, "Read src/main.fr" });
    auto prematurePlan = task.executeControl(call("agent_set_plan",
        R"({"goal":"Update main.fr","steps":["Inspect existing code","Edit the command loop"]})"));
    expect(!prematurePlan.ok, "Reading one file does not substitute for inspecting project structure");
    task.recordEngineerResult("workspace_list", { true, false, "Listed project root" });
    expect(!plan(task).ok, "Plan is rejected before capability assessment");
    expect(assess(task).ok, "Capability assessment is accepted after inspection");
    expect(plan(task).ok, "Plan is accepted after inspection and capability assessment");

    task.recordEngineerResult("workspace_replace_text", { true, true, "Updated src/main.fr" });
    auto unverified = task.executeControl(call("agent_complete_task", R"({"summary":"done"})"));
    expect(!unverified.ok, "Completion is rejected before verification");

    task.recordEngineerResult("workspace_check_frust", { true, false, "Frust check passed", true });
    ai_provider::ChatResponse usage;
    usage.inputTokens = 120;
    usage.outputTokens = 30;
    usage.totalTokens = 150;
    task.recordProviderUsage(usage);
    const auto snapshot = task.evaluationSnapshot();
    expect(static_cast<int>(snapshot.getProperty("providerCalls", 0)) == 1
           && static_cast<int>(snapshot.getProperty("totalTokens", 0)) == 150,
           "Evaluation snapshot reports provider calls and token usage");
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
    expect(assess(planOnly).ok, "Plan mode assesses prerequisites");
    expect(plan(planOnly, "Design a parser").ok,
        "Plan mode records a plan");
    expect(planOnly.executeControl(call("agent_complete_task",
        R"({"summary":"The implementation plan is ready."})")).ok,
        "Plan mode completes without writing");
    expect(planOnly.continuePlanAsExecution(true) && planOnly.canWrite(),
           "A completed plan becomes an execution task without losing its assessed plan");

    // A task that builds and opens a program changes no file, and must still be able to finish.
    AgentTask buildAndRun = AgentTask::begin("conversation", "rebuild the REPL and start it", "execute", true, true, false);
    buildAndRun.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    assess(buildAndRun);
    expect(plan(buildAndRun, "rebuild and start").ok, "the build-and-run plan is accepted");
    expect(!buildAndRun.executeControl(call("agent_complete_task", R"({"summary":"done"})")).ok,
           "it cannot finish before doing anything");
    buildAndRun.recordEngineerResult("run_command", { true, false, "Ran in PowerShell in .: cmake --build build --config Debug --target repl\nExit code 0", true });
    buildAndRun.recordEngineerResult("launch_program", { true, false, "Opened in its own window" });
    expect(buildAndRun.executeControl(call("agent_complete_task", R"({"summary":"built and opened"})")).ok,
           "a build and a launch count as doing the task, with no file changed");

    AgentTask readOnlyCommand = AgentTask::begin("conversation", "make it work", "execute", true, true, false);
    readOnlyCommand.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    assess(readOnlyCommand);
    plan(readOnlyCommand, "make it work");
    readOnlyCommand.recordEngineerResult("run_command", { true, false, "git status", false });
    expect(!readOnlyCommand.executeControl(call("agent_complete_task", R"({"summary":"x"})")).ok,
           "a command that only looked does not count as doing the task");

    auto review = AgentTask::begin("review-conversation", "Review current code", "review",
                                   false, false, false);
    review.recordEngineerResult("workspace_list", { true, false, "Listed project root" });
    review.recordEngineerResult("workspace_search", { true, false, "Found definitions" });
    expect(review.executeControl(call("agent_complete_task",
        R"({"summary":"One correctness issue found."})")).ok,
        "Review mode completes after inspection without a plan or write");

    auto blocked = AgentTask::begin("blocked", "Implement parser", "execute", true, true, true);
    blocked.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    auto missing = blocked.executeControl(call("agent_assess_capabilities",
        R"({"required":["working library linker"],"available":[],"missing":["working library linker"],"evidence":["known-good library test reproduces duplicate symbol"],"options":["repair the linker","change the library boundary"]})"));
    expect(missing.ok && missing.terminal && blocked.isResumable(),
           "Missing prerequisites pause the task before edits");
    expect(blocked.resume(), "A user continuation resumes the same task packet");

    auto converging = AgentTask::begin("converging", "Fix linker issue", "execute", true, true, true);
    converging.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    assess(converging);
    plan(converging, "Fix linker issue");
    converging.recordEngineerResult("workspace_check_frust", { false, false, "Error: duplicate symbol print_f64", true });
    converging.recordEngineerResult("workspace_read", { true, false, "Read src/lib.fr" });
    converging.recordEngineerResult("workspace_check_frust", { false, false, "Error: duplicate symbol print_f64", true });
    converging.recordEngineerResult("workspace_replace_text", { true, true, "Updated src/lib.fr" });
    converging.recordEngineerResult("workspace_check_frust", { false, false, "Error: duplicate symbol print_f64", true });
    expect(converging.isResumable(), "Three matching failures trigger a convergence stop despite intervening reads and edits");

    auto variedFailures = AgentTask::begin("varied", "Repair changing syntax errors", "execute", true, true, true);
    variedFailures.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    assess(variedFailures);
    plan(variedFailures, "Repair changing syntax errors");
    variedFailures.recordEngineerResult("workspace_check_frust", { false, false, "Error: expected semicolon", true });
    variedFailures.recordEngineerResult("workspace_replace_text", { true, true, "Updated src/main.fr" });
    variedFailures.recordEngineerResult("workspace_check_frust", { false, false, "Error: unknown function println_str", true });
    variedFailures.recordEngineerResult("workspace_replace_text", { true, true, "Updated src/main.fr" });
    variedFailures.recordEngineerResult("workspace_check_frust", { false, false, "Error: entry declaration is invalid", true });
    expect(variedFailures.isResumable(),
           "Three failed verifications stop a changing edit-and-check loop");

    AgentTask budgeted = AgentTask::begin("budgeted", "Use bounded resources", "execute", true, true, false);
    ai_provider::ChatResponse budgetUsage;
    budgetUsage.inputTokens = 900;
    budgetUsage.outputTokens = 100;
    budgetUsage.totalTokens = 1000;
    budgeted.recordProviderUsage(budgetUsage);
    expect(budgeted.budgetExceeded(1, 10, 10000).contains("model requests"),
           "Provider-call budget is enforced");
    expect(budgeted.budgetExceeded(10, 10, 1000).contains("tokens"),
           "Token budget is enforced");

    auto release = AgentTask::begin("release", "Build, test, and publish the JSON pod", "execute", true, true, true);
    release.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    assess(release);
    plan(release, "Build, test, and publish the JSON pod");
    release.recordEngineerResult("workspace_replace_text", { true, true, "Updated src/parser.fr" });
    release.recordEngineerResult("workspace_check_frust", { true, false, "Frust check passed", true });
    expect(!release.executeControl(call("agent_complete_task", R"({"summary":"done"})")).ok,
           "A syntax check does not satisfy build, test, and publish gates");
    release.recordEngineerResult("run_command", { true, false, "Ran in PowerShell in .: frate build\nExit code 0", true });
    expect(!release.executeControl(call("agent_complete_task", R"({"summary":"done"})")).ok,
           "A build does not satisfy test and publish gates");
    release.recordEngineerResult("run_command", { true, false, "Ran in PowerShell in .: frate run --test\nExit code 0", true });
    expect(!release.executeControl(call("agent_complete_task", R"({"summary":"done"})")).ok,
           "Tests do not satisfy a publish gate");
    release.recordEngineerResult("run_command", { true, false, "Ran in PowerShell in .: frate publish\nExit code 0", false });
    expect(release.executeControl(call("agent_complete_task", R"({"summary":"released"})")).ok,
           "Completion is accepted only after every requested release gate succeeds");
    folder.deleteRecursively();

    if (failures == 0) std::cout << "AgentTaskTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
