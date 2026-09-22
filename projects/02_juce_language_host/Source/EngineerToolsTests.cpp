#include "EngineerTools.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

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
    const auto observeDefinitions = observe.definitions();
    expect(observeDefinitions.size() == 5, "Observe exposes read-only, registry, and verification tools");
    expect(workspace.definitions().size() == 12, "Workspace exposes all twelve engineering tools: run_command, launch_program and stop_program included");
    expect(std::none_of(observeDefinitions.begin(), observeDefinitions.end(), [](const auto& tool) {
        return tool.name == "run_command";
    }), "Observe cannot run commands");
    expect(std::any_of(observeDefinitions.begin(), observeDefinitions.end(), [](const auto& tool) {
        return tool.name == "registry_search";
    }), "Registry search is available at Observe access");
    EngineerTools noProject({}, EngineerTools::AccessLevel::observe);
    expect(noProject.definitions().size() == 1
           && noProject.definitions().front().name == "registry_search",
           "Registry search remains available without an open project");

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

    const auto currentText = base.getChildFile("src/main.fr").loadFileAsString();
    const auto currentHash = juce::SHA256(currentText.toRawUTF8(),
        static_cast<size_t>(currentText.getNumBytesAsUTF8())).toHexString();
    auto staleWrite = workspace.execute(call(
        "workspace_write_file",
        R"({"path":"src/main.fr","content":"fn main() = 21","expected_sha256":"0000000000000000000000000000000000000000000000000000000000000000"})"));
    expect(!staleWrite.ok, "Whole-file write rejects a stale revision");
    auto rewritten = workspace.execute(call(
        "workspace_write_file",
        "{\"path\":\"src/main.fr\",\"content\":\"fn main() = 84\",\"expected_sha256\":\""
            + currentHash.toStdString() + "\"}"));
    expect(rewritten.ok && rewritten.workspaceChanged, "Whole-file write accepts the current revision");

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

    // ---- run_command: the rules (nothing is run here) ----
    {
        using command_tool::Verdict;
        auto verdictOf = [](const char* command, const juce::StringArray& allowed = {}) {
            return command_tool::assess(command, allowed);
        };
        expect(verdictOf("git status").verdict == Verdict::allow, "git status runs without asking");
        expect(verdictOf("git status").readOnly, "git status is read-only");
        expect(verdictOf("dotnet build").verdict == Verdict::ask, "a build is asked the first time");
        expect(verdictOf("dotnet build", { "dotnet build" }).verdict == Verdict::allow, "a saved rule allows it after that");
        expect(verdictOf("dotnet build src/App.csproj", { "dotnet build" }).verdict == Verdict::allow, "a rule covers its arguments");
        expect(verdictOf("dotnet test", { "dotnet build" }).verdict == Verdict::ask, "a rule for build does not cover test");
        expect(verdictOf("dotnet build").command == "dotnet build -m:1", "the host adds -m:1 to a dotnet build");
        expect(verdictOf("dotnet build -m:1").command == "dotnet build -m:1", "and not twice");
        expect(verdictOf("dotnet build -m:8").verdict == Verdict::deny, "a parallel dotnet build is refused");
        expect(verdictOf("cargo build").command == "cargo build -j 1", "the host adds -j 1 to cargo");
        expect(verdictOf("cargo build -j 1").verdict != Verdict::deny && verdictOf("cargo build -j 1").command == "cargo build -j 1",
               "-j 1 written as two words is single-core, not refused and not doubled");
        expect(verdictOf("cmake --build build --config Debug").verdict == Verdict::deny, "cmake --build without a target is refused");
        expect(verdictOf("cmake --build build --config Debug --target app").verdict == Verdict::ask, "with a target it is asked");
        expect(verdictOf("cmake --build build --target app --parallel 8").verdict == Verdict::deny, "a parallel cmake build is refused");
        expect(verdictOf("msbuild app.sln /t:app /m").verdict == Verdict::deny, "msbuild /m is refused");
        expect(verdictOf("msbuild app.sln /p:Configuration=Debug").verdict == Verdict::deny, "msbuild without /t: is refused");
        expect(verdictOf("vcpkg install llvm").verdict == Verdict::deny, "vcpkg install is refused");
        expect(verdictOf("cmake --build D:/llvm/build --target install").verdict == Verdict::deny, "building LLVM is refused");
        expect(verdictOf("git push --force").verdict == Verdict::deny, "a force push is refused");
        expect(verdictOf("git reset --hard HEAD~1").verdict == Verdict::deny, "git reset --hard is refused");
        expect(verdictOf("shutdown /s").verdict == Verdict::deny, "shutting the machine down is refused");
        expect(verdictOf("powershell -EncodedCommand AAAA").verdict == Verdict::deny, "an encoded command is refused");
        expect(verdictOf("Remove-Item C:\\Windows\\foo").verdict == Verdict::deny, "deleting outside the project is refused");
        expect(verdictOf("Remove-Item build\\obj -Recurse").verdict == Verdict::ask, "deleting inside the project is asked");
        expect(verdictOf("Remove-Item build", { "remove-item" }).verdict == Verdict::ask, "and a saved rule never allows a deletion");
        expect(verdictOf("Remove-Item build").rulePrefix.isEmpty(), "so Always allow is not offered for it");
        expect(verdictOf("git push", { "git push" }).verdict == Verdict::ask, "a push is always asked");
        expect(verdictOf("type C:\\Users\\someone\\secrets.txt").verdict == Verdict::ask, "reading outside the project is asked");
        expect(verdictOf("type C:\\Users\\someone\\secrets.txt", { "type" }).verdict == Verdict::ask, "even with a rule");
        expect(verdictOf("git status && dotnet build").verdict == Verdict::ask, "a chain is only allowed when every part is");
        expect(verdictOf("git status && dotnet build").command == "git status && dotnet build -m:1",
               "an added flag lands on its own part and && keeps its meaning");
        expect(verdictOf("git status; vcpkg install zlib").verdict == Verdict::deny, "one refused part refuses the chain");
        expect(verdictOf("echo \"a; vcpkg install x\"").verdict == Verdict::allow, "separators inside quotes are not split");
        expect(verdictOf("dotnet build $(Get-Secret)", { "dotnet build" }).verdict == Verdict::ask, "a sub-expression is always asked");
        expect(command_tool::prefixOf("dotnet build src/App.csproj") == "dotnet build", "the rule for a dotnet build");
        expect(command_tool::prefixOf("cmake --build build --target x") == "cmake --build", "the rule for a cmake build");
        expect(command_tool::prefixOf("C:\\tools\\ninja.exe -C out") == "ninja", "a path to a program is its name");
        expect(command_tool::splitCommands("a && b || c; d | e").size() == 5, "a chain splits into its commands");
        expect(verdictOf("dotnet run").runsProgram && !verdictOf("dotnet run").build, "dotnet run starts a program; it is not a build");
        expect(verdictOf("dotnet run").command == "dotnet run", "and gets no build flag");
        expect(verdictOf("cargo run").runsProgram && verdictOf("cargo build").build, "cargo run starts a program, cargo build builds");
    }

    // ---- run_command: really running (PowerShell, headless, inside a temporary project) ----
    {
        const auto rulesFolder = base.getChildFile("rules");
        const auto logs = base.getChildFile("logs");
        int asked = 0;
        auto approving = workspace;
        approving.setCommandServices({ [&asked](const command_tool::ApprovalRequest&) { ++asked; return command_tool::Approval::once; },
                                       logs, rulesFolder });

        auto echoed = approving.execute(call("run_command", R"({"command":"Write-Output hello-from-the-shell","reason":"test"})"));
        expect(echoed.ok && echoed.message.contains("hello-from-the-shell") && asked == 0,
               "a read-only command runs without asking and its output comes back");
        expect(!echoed.workspaceChanged && !echoed.verificationPerformed, "a read-only command is neither a change nor verification");

        auto failing = approving.execute(call("run_command", R"({"command":"cmd /c exit 3","reason":"test exit codes"})"));
        expect(!failing.ok && failing.message.contains("Exit code 3") && asked == 1, "an unknown command is asked, and its exit code comes back");

        base.getChildFile("sub").createDirectory();
        auto located = approving.execute(call("run_command", R"({"command":"Get-Location","cwd":"sub","reason":"test"})"));
        expect(located.ok && located.message.contains("sub"), "it runs in the folder asked for");

        auto outside = approving.execute(call("run_command", R"({"command":"Get-Location","cwd":"..","reason":"test"})"));
        expect(!outside.ok, "it cannot run outside the project");

        auto slow = approving.execute(call("run_command", R"({"command":"Start-Sleep -Seconds 30","reason":"test","timeout_seconds":5})"));
        expect(!slow.ok && slow.message.contains("time limit"), "a command that runs too long is stopped");

        auto chatty = approving.execute(call("run_command", R"({"command":"1..20000 | ForEach-Object { 'line ' + $_ }","reason":"test"})"));
        expect(chatty.ok && chatty.message.contains("line 1") && chatty.message.contains("line 20000") && chatty.message.contains("left out"),
               "long output keeps its start and end and says what was left out");
        expect(logs.getNumberOfChildFiles(juce::File::findFiles) > 0, "the full output is kept in a log");

        auto refusing = workspace;
        refusing.setCommandServices({ [](const command_tool::ApprovalRequest&) { return command_tool::Approval::deny; }, logs, rulesFolder });
        auto refused = refusing.execute(call("run_command", R"({"command":"cmd /c echo no","reason":"test"})"));
        expect(!refused.ok && refused.message.contains("did not allow"), "when the user says no, it does not run");

        auto always = workspace;
        int alwaysAsked = 0;
        always.setCommandServices({ [&alwaysAsked](const command_tool::ApprovalRequest&) { ++alwaysAsked; return command_tool::Approval::always; },
                                    logs, rulesFolder });
        always.execute(call("run_command", R"({"command":"cmd /c echo first","reason":"test"})"));
        always.execute(call("run_command", R"({"command":"cmd /c echo second","reason":"test"})"));
        expect(alwaysAsked == 1, "after Always allow, the same kind of command is not asked again");

        auto noOneToAsk = workspace.execute(call("run_command", R"({"command":"cmd /c echo x","reason":"test"})"));
        expect(!noOneToAsk.ok, "with no one to ask, a command that needs approval does not run");

        // Stop ends a running command at once, and progress is reported while it runs.
        std::atomic<bool> stopNow { false };
        std::atomic<int> progressCalls { 0 };
        juce::String lastProgress;
        std::mutex progressLock;
        auto stoppable = workspace;
        EngineerTools::CommandServices services;
        services.approve = [](const command_tool::ApprovalRequest&) { return command_tool::Approval::once; };
        services.logFolder = logs;
        services.rulesFolder = rulesFolder;
        services.shouldStop = [&stopNow] { return stopNow.load(); };
        services.progress = [&](const juce::String& line) {
            ++progressCalls;
            std::lock_guard<std::mutex> lock(progressLock);
            lastProgress = line;
        };
        stoppable.setCommandServices(services);
        std::thread stopper([&stopNow] {
            std::this_thread::sleep_for(std::chrono::milliseconds(3500));
            stopNow = true;
        });
        const auto startedAt = juce::Time::getMillisecondCounterHiRes();
        auto stoppedRun = stoppable.execute(call("run_command",
            R"({"command":"Write-Output ticking; Start-Sleep -Seconds 60","reason":"test stop","timeout_seconds":120})"));
        const auto took = (juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0;
        stopper.join();
        expect(!stoppedRun.ok && stoppedRun.message.contains("stopped it"), "Stop ends a running command, and says so");
        expect(took < 15.0, "and promptly, not at the time limit");
        expect(progressCalls >= 2, "progress is reported while it runs");
        {
            std::lock_guard<std::mutex> lock(progressLock);
            expect(lastProgress.contains("Running") && lastProgress.contains("ticking"), "with the elapsed time and the last line it printed");
        }
    }

    base.deleteRecursively();
    if (failures == 0)
        std::cout << "EngineerToolsTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
