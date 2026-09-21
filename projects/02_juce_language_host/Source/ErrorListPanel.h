#pragma once

#include <JuceHeader.h>
#include <CompilerApi.h>

#include <functional>
#include <vector>

class ErrorListPanel : public juce::Component,
                       private juce::TableListBoxModel
{
public:
    ErrorListPanel();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void setDiagnostics(std::vector<frust::Diagnostic> newDiagnostics);

    std::function<void(const frust::Diagnostic&)> onDiagnosticActivated;

private:
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int, int, int, bool) override;
    void paintCell(juce::Graphics&, int, int, int, int, bool) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void rebuildVisibleRows();

    juce::Label title { "Title", "Error List" };
    juce::Label summary;
    juce::ToggleButton showErrors { "Errors" };
    juce::ToggleButton showWarnings { "Warnings" };
    juce::TableListBox table { "Compiler diagnostics", this };
    std::vector<frust::Diagnostic> diagnostics;
    std::vector<size_t> visibleRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ErrorListPanel)
};
