#include "ErrorListPanel.h"

namespace
{
juce::String severityName(frust::Diagnostic::Severity severity)
{
    return severity == frust::Diagnostic::Severity::Error ? "Error" : "Warning";
}
}

ErrorListPanel::ErrorListPanel()
{
    title.setFont(juce::Font(14.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colours::lightcyan);
    addAndMakeVisible(title);

    summary.setFont(juce::Font(12.0f));
    summary.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(summary);

    showErrors.setToggleState(true, juce::dontSendNotification);
    showWarnings.setToggleState(true, juce::dontSendNotification);
    showErrors.onClick = [this] { rebuildVisibleRows(); };
    showWarnings.onClick = [this] { rebuildVisibleRows(); };
    addAndMakeVisible(showErrors);
    addAndMakeVisible(showWarnings);

    auto& header = table.getHeader();
    header.addColumn("Severity", 1, 80, 65, 110);
    header.addColumn("File", 2, 220, 100, 600);
    header.addColumn("Line", 3, 55, 45, 80);
    header.addColumn("Column", 4, 65, 50, 90);
    header.addColumn("Message", 5, 600, 180, 1600);
    table.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff151515));
    table.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff333333));
    table.setOutlineThickness(1);
    table.setRowHeight(23);
    addAndMakeVisible(table);
    rebuildVisibleRows();
}

void ErrorListPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void ErrorListPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    auto toolbar = bounds.removeFromTop(24);
    title.setBounds(toolbar.removeFromLeft(85));
    showWarnings.setBounds(toolbar.removeFromRight(90));
    showErrors.setBounds(toolbar.removeFromRight(75));
    summary.setBounds(toolbar);
    bounds.removeFromTop(4);
    table.setBounds(bounds);
}

void ErrorListPanel::setDiagnostics(std::vector<frust::Diagnostic> newDiagnostics)
{
    diagnostics = std::move(newDiagnostics);
    rebuildVisibleRows();
}

int ErrorListPanel::getNumRows()
{
    return static_cast<int>(visibleRows.size());
}

void ErrorListPanel::paintRowBackground(juce::Graphics& g, int row, int width,
                                        int height, bool selected)
{
    juce::ignoreUnused(row, width, height);
    if (selected) g.fillAll(juce::Colour(0xff264f78));
    else if ((row & 1) != 0) g.fillAll(juce::Colour(0xff1d1d1d));
}

void ErrorListPanel::paintCell(juce::Graphics& g, int row, int columnId,
                               int width, int height, bool)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(visibleRows.size()))) return;
    const auto& diagnostic = diagnostics[visibleRows[static_cast<size_t>(row)]];
    juce::String text;
    if (columnId == 1) text = severityName(diagnostic.severity);
    if (columnId == 2) text = diagnostic.file;
    if (columnId == 3 && diagnostic.line > 0) text = juce::String(diagnostic.line);
    if (columnId == 4 && diagnostic.column > 0) text = juce::String(diagnostic.column);
    if (columnId == 5) text = diagnostic.message;

    g.setColour(diagnostic.severity == frust::Diagnostic::Severity::Error
        ? juce::Colour(0xffff6b6b) : juce::Colour(0xffffc857));
    g.setFont(juce::Font("Consolas", 12.5f, juce::Font::plain));
    g.drawText(text, 5, 0, width - 9, height, juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xff333333));
    g.drawVerticalLine(width - 1, 0.0f, static_cast<float>(height));
}

void ErrorListPanel::cellDoubleClicked(int row, int, const juce::MouseEvent&)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(visibleRows.size()))) return;
    if (onDiagnosticActivated)
        onDiagnosticActivated(diagnostics[visibleRows[static_cast<size_t>(row)]]);
}

void ErrorListPanel::rebuildVisibleRows()
{
    visibleRows.clear();
    int errors = 0;
    int warnings = 0;
    for (size_t index = 0; index < diagnostics.size(); ++index)
    {
        const auto isError = diagnostics[index].severity == frust::Diagnostic::Severity::Error;
        errors += isError ? 1 : 0;
        warnings += isError ? 0 : 1;
        if ((isError && showErrors.getToggleState())
            || (!isError && showWarnings.getToggleState()))
            visibleRows.push_back(index);
    }
    summary.setText(juce::String(errors) + " errors, " + juce::String(warnings) + " warnings",
                    juce::dontSendNotification);
    table.updateContent();
    table.repaint();
}
