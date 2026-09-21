#include "EngineerTools.h"

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
    const auto base = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("frust-engineer-tools", {}, true);
    expect(base.createDirectory().wasOk(), "temporary project is created");

    EngineerTools observe(base, EngineerTools::AccessLevel::observe);
    EngineerTools workspace(base, EngineerTools::AccessLevel::workspace);
    expect(observe.definitions().size() == 4, "Observe exposes read-only and verification tools");
    expect(workspace.definitions().size() == 7, "Workspace exposes all seven project tools");

    auto denied = observe.execute(call(
        "workspace_create_file", R"({"path":"src/main.fr","content":"fn main() = 42"})"));
    expect(!denied.ok, "Observe cannot create a file");

    auto created = workspace.execute(call(
        "workspace_create_file", R"({"path":"src/main.fr","content":"fn main() = 42"})"));
    expect(created.ok && created.workspaceChanged, "Workspace creates a source file");
    expect(base.getChildFile("src/main.fr").existsAsFile(), "Created source exists on disk");

    auto read = workspace.execute(call(
        "workspace_read", R"({"path":"src/main.fr"})"));
    expect(read.ok && read.message.contains("fn main() = 42"), "Read returns source content");

    auto edited = workspace.execute(call(
        "workspace_replace_text",
        R"({"path":"src/main.fr","old_text":"42","new_text":"84"})"));
    expect(edited.ok, "Exact source edit succeeds");
    expect(base.getChildFile("src/main.fr").loadFileAsString().contains("84"),
           "Exact source edit reaches disk");

    auto checked = workspace.execute(call(
        "workspace_check_frust", R"({"path":"src/main.fr"})"));
    expect(checked.ok && checked.verificationPerformed, "Valid Frust source passes verification");

    auto broken = workspace.execute(call(
        "workspace_replace_text",
        R"({"path":"src/main.fr","old_text":"84","new_text":"{"})"));
    expect(broken.ok, "Test can introduce invalid Frust source");
    checked = workspace.execute(call(
        "workspace_check_frust", R"({"path":"src/main.fr"})"));
    expect(!checked.ok && checked.verificationPerformed,
           "Invalid Frust source returns a failed verification");

    auto escaped = workspace.execute(call(
        "workspace_create_file", R"({"path":"../outside.txt","content":"no"})"));
    expect(!escaped.ok, "Workspace path cannot escape the project root");

    base.deleteRecursively();
    if (failures == 0)
        std::cout << "EngineerToolsTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
