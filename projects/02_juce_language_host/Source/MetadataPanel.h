#pragma once

#include <JuceHeader.h>

class MetadataPanel : public juce::Component
{
public:
    MetadataPanel();
    ~MetadataPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void updateMetadata(const juce::String& fileName);

private:
    juce::Label headerLabel { "Header", "Metadata & Inspection" };
    juce::GroupComponent astGroup { "ASTGroup", "Abstract Syntax Tree" };
    juce::TextEditor astView;

    juce::GroupComponent typeGroup { "TypeGroup", "Refinement & Physical Units" };
    juce::TextEditor typeView;

    juce::GroupComponent portGroup { "PortGroup", "Component Ports & Invariants" };
    juce::TextEditor portView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetadataPanel)
};
