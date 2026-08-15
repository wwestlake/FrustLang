#pragma once

#include <JuceHeader.h>
#include <ReplSession.h>
#include <functional>
#include <vector>

// Live view of the console's REPL context: a sortable/searchable Name/Type
// list with a detail pane showing the selected binding's value. Doesn't
// own any session state itself - purely reads it via getBindings, which
// WorkbenchComponent wires to the one real ReplSession that ConsolePanel
// owns, and repaints when told to via refresh().
class ContextPanel : public juce::Component,
                      private juce::TableListBoxModel
{
public:
    ContextPanel();
    ~ContextPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Re-pulls from getBindings and redraws. Call after anything that may
    // have changed the session (ConsolePanel's onSessionChanged is meant
    // to drive this).
    void refresh();

    std::function<std::vector<frust::ReplSession::Binding>()> getBindings;

private:
    // juce::TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void applyFilterAndSort();
    void updateDetailPane();

    juce::Label headerLabel { "Header", "Frust Context" };
    juce::TextEditor searchBox;
    juce::TableListBox table { "ContextTable", this };

    juce::GroupComponent detailGroup { "DetailGroup", "Value" };
    juce::TextEditor detailView;

    std::vector<frust::ReplSession::Binding> allBindings; // full set from the last refresh()
    std::vector<int> visibleIndices;                       // indices into allBindings, post filter+sort

    int sortColumnId = 1;
    bool sortForwards = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContextPanel)
};
