#pragma once

#include <JuceHeader.h>
#include <memory>

class TerminalInstance : public juce::Component, private juce::Timer, private juce::KeyListener
{
public:
    TerminalInstance(const juce::String& shellType, std::function<juce::File()> getRoot);
    ~TerminalInstance() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void setProjectRoot(const juce::File& root);
    juce::String getShellType() const { return currentShell; }

private:
    void executeCommand();
    void logMessage(const juce::String& message);
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void updatePromptText();

    juce::TextEditor consoleText;

    std::function<juce::File()> getProjectRoot;
    juce::File currentWorkingDirectory;
    juce::String promptText;
    juce::String currentShell;
    
    std::unique_ptr<juce::ChildProcess> activeProcess;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TerminalInstance)
};
