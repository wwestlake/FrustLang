#pragma once

#include <JuceHeader.h>
#include <ai_provider/AiProvider.h>

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

    juce::File root;
    AccessLevel access;
};
