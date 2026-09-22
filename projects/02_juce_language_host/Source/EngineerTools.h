#pragma once

#include <JuceHeader.h>
#include <ai_provider/AiProvider.h>

#include "CommandTool.h"

#include <vector>

class EngineerTools
{
public:
    enum class AccessLevel
    {
        observe = 0,
        workspace = 1
    };

    struct Result
    {
        bool ok = false;
        bool workspaceChanged = false;
        juce::String message;
        bool verificationPerformed = false;
    };

    EngineerTools(juce::File projectRoot, AccessLevel accessLevel);

    // What run_command needs from the application: a way to ask the user (without one, every command the rules do not
    // simply allow is refused), where to keep full command logs, and where the user's "always allow" rules live (empty =
    // the IDE's own settings folder).
    struct CommandServices
    {
        command_tool::Approver approve;
        juce::File logFolder;
        juce::File rulesFolder;
        std::function<bool()> shouldStop;                    // true once the user presses Stop or the IDE is closing
        std::function<void(const juce::String&)> progress;   // a short live status line ("Running dotnet build (0:14): ...")
    };
    void setCommandServices(CommandServices services) { commands = std::move(services); }

    std::vector<ai_provider::ToolDefinition> definitions() const;
    Result execute(const ai_provider::ToolCall& call) const;

    static juce::String accessName(AccessLevel level);

private:
    juce::var loadCatalog() const;
    juce::File resolveProjectPath(const juce::String& suppliedPath, juce::String& error) const;
    bool toolIsAvailable(const juce::String& name) const;

    Result list(const juce::var& arguments) const;
    Result read(const juce::var& arguments) const;
    Result search(const juce::var& arguments) const;
    Result searchRegistry(const juce::var& arguments) const;
    Result createDirectory(const juce::var& arguments) const;
    Result createFile(const juce::var& arguments) const;
    Result writeFile(const juce::var& arguments) const;
    Result replaceText(const juce::var& arguments) const;
    Result checkFrust(const juce::var& arguments) const;
    Result runCommand(const juce::var& arguments) const;
    Result launchProgram(const juce::var& arguments) const;
    Result stopProgram(const juce::var& arguments) const;

    juce::File root;
    AccessLevel access;
    CommandServices commands;
};
