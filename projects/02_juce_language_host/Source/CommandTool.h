#pragma once

#include <JuceHeader.h>

#include <functional>

// The Engineer's shell: how a command the model asks for is judged, approved and run.
//
// The model never runs anything itself. It asks for a command (run_command); the host judges it against rules, asks the user
// when the rules say so, and only then runs it: in PowerShell, inside the open project, with a time limit, in a Windows job so
// the whole process tree can be killed, with stdin closed so nothing can wait for input. This is how the big coding agents
// reach compilers and tests for any language, and why FrustIDE need not be Frust-only.
//
// The judging is plain functions (assess, splitCommands, prefixOf), so it is tested without running anything.
namespace command_tool
{
enum class Verdict
{
    allow,   // runs without asking
    ask,     // the user decides
    deny     // never runs; the reason goes back to the model
};

struct Assessment
{
    Verdict verdict = Verdict::ask;
    juce::String reason;          // why it was denied, or why it must be asked
    juce::String command;         // what will run: the request, with any single-core flag the host added
    juce::StringArray amendments; // what the host changed, said plainly ("added -m:1 ...")
    bool build = false;           // compiles something
    bool test = false;            // runs tests
    bool readOnly = false;        // only looks (git status, dir...)
    bool alwaysAsk = false;       // may never be allowed by a saved rule (deletions, pushes, downloads)
    juce::String rulePrefix;      // what "Always allow" would save, e.g. "dotnet build"
};

// Judges a command. `allowedPrefixes` are the user's saved "always allow" rules for this project.
Assessment assess(const juce::String& command, const juce::StringArray& allowedPrefixes);

// The separate commands in a command line: split on ; && || | and line breaks, outside quotes.
juce::StringArray splitCommands(const juce::String& command);

// The part of a command a rule is made from: the program and, when it is a subcommand, the next word ("dotnet build",
// "git status", "cmake --build").
juce::String prefixOf(const juce::String& singleCommand);

// The user's saved "always allow" rules for one project. Kept with the IDE's own settings, never inside the project.
class RuleStore
{
public:
    explicit RuleStore(juce::File projectRoot, juce::File storageFolder = {});

    juce::StringArray allowedPrefixes() const;
    bool allow(const juce::String& prefix);

private:
    juce::File file() const;

    juce::File root;
    juce::File folder;
};

struct RunResult
{
    bool started = false;
    int exitCode = -1;
    bool timedOut = false;
    juce::String output;       // what the model reads: head and tail when it was long
    juce::int64 totalBytes = 0;
    bool truncated = false;
    juce::File logFile;        // the whole output, when a log folder was given
    double seconds = 0.0;
    juce::String shell;        // "PowerShell 7" or "Windows PowerShell 5.1"
    juce::String error;        // why it did not start
};

// Runs a command in PowerShell with `workingDirectory` as its folder. Kills the whole process tree after `timeoutSeconds`.
RunResult run(const juce::String& command, const juce::File& workingDirectory, int timeoutSeconds,
              const juce::File& logFolder = {}, int headBytes = 12000, int tailBytes = 12000);

// Whether a compiler or build tool is running anywhere on this machine (the owner's rule: one build at a time), and which.
bool otherBuildRunning(juce::String& which);

// What the host needs from the application to ask the user.
struct ApprovalRequest
{
    juce::String command;
    juce::String workingDirectory;   // relative to the project
    juce::String reason;             // what the model said it is for
    juce::String whyAsked;           // why the rules did not simply allow it
    juce::String rulePrefix;         // what "Always allow" would save ("" when it may not be offered)
};

enum class Approval
{
    deny,
    once,
    always
};

using Approver = std::function<Approval(const ApprovalRequest&)>;
}
