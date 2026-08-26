#pragma once

#include <JuceHeader.h>

#include "PersistentShellSession.h"
#include "ShellTokeniser.h"

class TerminalInstance final : public juce::Component,
                               private juce::Timer,
                               private juce::KeyListener
{
public:
    TerminalInstance(const juce::String& shellType, std::function<juce::File()> getRoot);
    ~TerminalInstance() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void setProjectRoot(const juce::File& root);
    juce::String getShellType() const { return currentShell; }

private:
    void executeCommand();
    void appendOutput(const juce::String& text);
    void updatePrompt();
    void updateWorkingDirectoryFromCommand(const juce::String& command);
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* source) override;

    std::function<juce::File()> getProjectRoot;
    juce::String currentShell;
    juce::File currentWorkingDirectory;
    PersistentShellSession session;
    juce::TextEditor output;
    juce::Label prompt;
    juce::CodeDocument inputDocument;
    ShellTokeniser tokeniser;
    juce::CodeEditorComponent inputEditor { inputDocument, &tokeniser };
    juce::StringArray commandHistory;
    int historyIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TerminalInstance)
};
