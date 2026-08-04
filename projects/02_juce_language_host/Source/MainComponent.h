#pragma once

#include <JuceHeader.h>

class MainComponent  : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextEditor codeEditor;
    juce::TextEditor consoleOutput;
    juce::TextButton executeButton { "Execute (LLVM OrcJIT)" };
    juce::TextButton clearButton { "Clear Console" };

    juce::Label headerLabel { "HeaderLabel", "LagDaemon Multi-Paradigm Language Host" };
    juce::Label statusLabel { "StatusLabel", "LLVM OrcJIT Engine: Ready" };

    void onExecuteClicked();
    void onClearClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
