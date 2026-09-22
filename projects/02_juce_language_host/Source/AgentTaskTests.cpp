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
    expect(AgentTask::proposedWriteHasUnresolvedImplementation(call(
               "workspace_create_file",
               R"({"path":"src/parser.fr","content":"fn parse() { // Logic goes here\n}"})")),
           "A comment-only Frust function is rejected as unresolved implementation");
    expect(!AgentTask::proposedWriteHasUnresolvedImplementation(call(
               "workspace_create_file",
               R"({"path":"src/parser.fr","content":"fn parse() { return 1; }"})")),
           "A Frust function with executable code is accepted");
    expect(!AgentTask::proposedWriteHasUnresolvedImplementation(call(
               "workspace_create_file",
               R"({"path":"src/types.fr","content":"struct Item { value: i64, }"})")),
           "A declaration-only Frust source file is accepted");

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

    auto externalEvidence = AgentTask::begin(
        "external-evidence", "Build the analyzer using D:\\FrustLang\\projects\\frust_json", "execute",
        true, true, true);
    externalEvidence.recordEngineerResult("workspace_list", { true, false, "Listed 0 entries." });
    expect(externalEvidence.currentPhase() == "inspect",
           "An empty project does not finish inspection when the goal names external evidence");
    externalEvidence.recordEngineerResult("workspace_read", { true, false, "D:/FrustLang/projects/frust_json/frate.json" });
    expect(externalEvidence.currentPhase() == "assess",
           "Reading named external evidence advances the task to capability assessment");

    auto populatedExternal = AgentTask::begin(
        "populated-external", "Build using D:\\reference\\format.json", "execute",
        true, true, true);
    populatedExternal.recordEngineerResult("workspace_list", { true, false, "Listed 1 entries." });
    expect(populatedExternal.currentPhase() == "inspect",
           "A non-empty project listing does not substitute for named external evidence");

    auto registryRequired = AgentTask::begin(
        "registry-required", "Inspect the pod registry and build the parser", "execute",
        true, true, true);
    registryRequired.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    const auto prematureAssessment = assess(registryRequired);
    expect(!prematureAssessment.ok && registryRequired.currentPhase() == "inspect",
           "Premature capability assessment returns to inspection when registry evidence was requested");
    registryRequired.recordEngineerResult("registry_search", { true, false, "Found JSON pods" });
    expect(assess(registryRequired).ok,
           "Capability assessment proceeds after the requested registry inspection");

    auto blocked = AgentTask::begin("blocked", "Implement parser", "execute", true, true, true);
    blocked.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    blocked.recordEngineerResult("workspace_search", { true, false, "No linker implementation found" });
    blocked.recordEngineerResult("registry_search", { true, false, "No linker pod found" });
    auto missing = blocked.executeControl(call("agent_assess_capabilities",
        R"({"required":["working library linker"],"available":[],"missing":["working library linker"],"evidence":["known-good library test reproduces duplicate symbol"],"options":["repair the linker","change the library boundary"]})"));
    expect(missing.ok && missing.terminal && blocked.isResumable(),
           "Missing prerequisites pause the task before edits");
    expect(blocked.resume(), "A user continuation resumes the same task packet");

    auto unsupportedMissing = AgentTask::begin(
        "unsupported-missing", "Implement file input", "execute", true, true, true);
    unsupportedMissing.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    const auto unsupportedClaim = unsupportedMissing.executeControl(call("agent_assess_capabilities",
        R"({"required":["file I/O"],"available":[],"missing":["file I/O"],"evidence":["I did not see it"],"options":["build it"]})"));
    expect(!unsupportedClaim.ok && unsupportedMissing.currentPhase() == "inspect",
           "A missing-capability claim without local and registry searches returns to inspection");

    auto falsePermissionBlock = AgentTask::begin("permission", "Update main.fr", "execute", true, true, true);
    falsePermissionBlock.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    const auto rejectedPermission = falsePermissionBlock.executeControl(call("agent_assess_capabilities",
        R"({"required":["Write access to project files"],"available":[],"missing":["Write access to project files"],"evidence":["A write was rejected before planning"],"options":["ask user"]})"));
    expect(rejectedPermission.ok && !rejectedPermission.terminal && !falsePermissionBlock.isTerminal(),
           "Host-granted workspace access is normalized to available in one assessment call");

    auto hiddenToolBlock = AgentTask::begin("hidden-tool", "Create NOTES.md", "execute", true, true, false);
    hiddenToolBlock.recordEngineerResult("workspace_list", { true, false, "Listed 0 entries." });
    const auto rejectedHiddenTool = hiddenToolBlock.executeControl(call("agent_assess_capabilities",
        R"({"required":["workspace_create_file"],"available":[],"missing":["workspace_create_file"],"evidence":["Tool is not shown during assessment"],"options":["Create a file"]})"));
    expect(rejectedHiddenTool.ok && !hiddenToolBlock.isTerminal(),
           "A tool hidden by phase gating is normalized to available in one assessment call");

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

    AgentTask predictiveBudget = AgentTask::begin("predictive", "Avoid an overrun", "execute", true, true, false);
    ai_provider::ChatResponse costlyRound;
    costlyRound.inputTokens = 24000;
    costlyRound.outputTokens = 500;
    costlyRound.totalTokens = 24500;
    predictiveBudget.recordProviderUsage(costlyRound);
    expect(predictiveBudget.budgetBeforeProviderCall(64, 128, 50000).contains("before another model request"),
           "The host reserves room before a provider call that would likely exceed the token budget");
    expect(predictiveBudget.budgetBeforeProviderCall(64, 128, 100000).isEmpty(),
           "A provider call proceeds when the remaining token budget is sufficient");

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

    auto noPublish = AgentTask::begin("no-publish", "Build and test the pod, but do not publish it",
                                      "execute", true, true, true);
    noPublish.recordEngineerResult("workspace_list", { true, false, "Listed project" });
    assess(noPublish);
    plan(noPublish, "Build and test without publishing");
    noPublish.recordEngineerResult("workspace_replace_text", { true, true, "Updated src/lib.fr" });
    noPublish.recordEngineerResult("workspace_check_frust", { true, false, "Frust check passed", true });
    noPublish.recordEngineerResult("run_command", { true, false, "Ran in PowerShell in .: frate build\nExit code 0", true });
    noPublish.recordEngineerResult("run_command", { true, false, "Ran in PowerShell in .: frate test\nExit code 0", true });
    expect(noPublish.executeControl(call("agent_complete_task", R"({"summary":"ready for review"})")).ok,
           "A negated publication request does not create a publication completion gate");
    folder.deleteRecursively();

    if (failures == 0) std::cout << "AgentTaskTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
