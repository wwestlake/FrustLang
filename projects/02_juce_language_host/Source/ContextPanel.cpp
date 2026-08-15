#include "ContextPanel.h"

#include <algorithm>

ContextPanel::ContextPanel()
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
    addAndMakeVisible(headerLabel);

    searchBox.setMultiLine(false);
    searchBox.setTextToShowWhenEmpty("Search names...", juce::Colours::grey);
    searchBox.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    searchBox.onTextChange = [this] { applyFilterAndSort(); };
    addAndMakeVisible(searchBox);

    table.getHeader().addColumn("Name", 1, 150, 60, 400);
    table.getHeader().addColumn("Type", 2, 80, 50, 200);
    table.getHeader().setSortColumnId(sortColumnId, sortForwards);
    table.setColour(juce::TableListBox::backgroundColourId, juce::Colour(0xff141414));
    table.setOutlineThickness(1);
    addAndMakeVisible(table);

    detailGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xff444444));
    detailGroup.setColour(juce::GroupComponent::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(detailGroup);

    detailView.setMultiLine(true);
    detailView.setReadOnly(true);
    detailView.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    detailView.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141414));
    detailView.setColour(juce::TextEditor::textColourId, juce::Colour(0xff00ff66));
    detailView.setText("(select a name to see its value)");
    addAndMakeVisible(detailView);
}

void ContextPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void ContextPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    headerLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(4);

    searchBox.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(4);

    auto detailArea = bounds.removeFromBottom(bounds.getHeight() / 3);
    bounds.removeFromBottom(6);

    table.setBounds(bounds);

    detailGroup.setBounds(detailArea);
    detailView.setBounds(detailArea.reduced(6, 20));
}

void ContextPanel::refresh()
{
    allBindings = getBindings ? getBindings() : std::vector<frust::ReplSession::Binding>{};
    applyFilterAndSort();
}

void ContextPanel::applyFilterAndSort()
{
    auto query = searchBox.getText();

    visibleIndices.clear();
    for (int i = 0; i < (int) allBindings.size(); ++i) {
        if (query.isEmpty() || juce::String(allBindings[(size_t) i].name).containsIgnoreCase(query))
            visibleIndices.push_back(i);
    }

    std::sort(visibleIndices.begin(), visibleIndices.end(), [this](int a, int b) {
        // Only Name (1) and Type (2) columns exist, and every binding's
        // type is "f64" today, so a Type sort is a no-op until real types
        // exist - still correctly implemented for when they do.
        bool less;
        if (sortColumnId == 2)
            less = allBindings[(size_t) a].typeName < allBindings[(size_t) b].typeName;
        else
            less = allBindings[(size_t) a].name < allBindings[(size_t) b].name;
        return sortForwards ? less : !less;
    });

    table.updateContent();
    updateDetailPane();
}

void ContextPanel::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    sortColumnId = newSortColumnId;
    sortForwards = isForwards;
    applyFilterAndSort();
}

void ContextPanel::selectedRowsChanged(int)
{
    updateDetailPane();
}

void ContextPanel::updateDetailPane()
{
    auto row = table.getSelectedRow();
    if (row < 0 || row >= (int) visibleIndices.size()) {
        detailView.setText("(select a name to see its value)");
        return;
    }

    auto& binding = allBindings[(size_t) visibleIndices[(size_t) row]];
    detailView.setText(juce::String(binding.name) + " : " + juce::String(binding.typeName)
        + "\n\n" + juce::String(binding.value));
}

int ContextPanel::getNumRows()
{
    return (int) visibleIndices.size();
}

void ContextPanel::paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected) g.fillAll(juce::Colour(0xff2d5f7c));
    else g.fillAll(rowNumber % 2 == 0 ? juce::Colour(0xff181818) : juce::Colour(0xff141414));
}

void ContextPanel::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool)
{
    if (rowNumber < 0 || rowNumber >= (int) visibleIndices.size()) return;
    auto& binding = allBindings[(size_t) visibleIndices[(size_t) rowNumber]];

    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));

    juce::String text = columnId == 1 ? juce::String(binding.name) : juce::String(binding.typeName);
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}
