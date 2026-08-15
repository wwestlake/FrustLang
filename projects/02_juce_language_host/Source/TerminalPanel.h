#pragma once

#include <JuceHeader.h>
#include <memory>
#include "TerminalInstance.h"

class TerminalPanel : public juce::Component
{
public:
    TerminalPanel(juce::ApplicationProperties* props = nullptr);
    ~TerminalPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void setProjectRoot(const juce::File& root);
    std::function<juce::File()> getProjectRoot;

private:
    void addNewTerminal(const juce::String& type);
    void closeActiveTerminal();

    juce::ApplicationProperties* appProperties;
    juce::String defaultShell;
    int terminalCounter = 1;

    juce::Label headerLabel { "header", "Terminals" };
    juce::ComboBox shellSelector;
    juce::TextButton newTerminalButton { "+" };
    juce::TextButton closeTerminalButton { "X" };
    
    juce::TabbedComponent tabbedComponent { juce::TabbedButtonBar::TabsAtBottom };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TerminalPanel)
};
