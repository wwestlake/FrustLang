#include "MetadataPanel.h"

MetadataPanel::MetadataPanel()
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
    addAndMakeVisible(headerLabel);

    // AST View
    astGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xff444444));
    astGroup.setColour(juce::GroupComponent::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(astGroup);

    astView.setMultiLine(true);
    astView.setReadOnly(true);
    astView.setFont(juce::Font("Consolas", 12.0f, juce::Font::plain));
    astView.setText("ProgramNode\n ├── FunctionDecl: calculate_gain\n │    ├── Param: sample (f32)\n │    └── Param: gain (f32)\n └── BinaryExpr (*)");
    addAndMakeVisible(astView);

    // Type View
    typeGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xff444444));
    typeGroup.setColour(juce::GroupComponent::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(typeGroup);

    typeView.setMultiLine(true);
    typeView.setReadOnly(true);
    typeView.setFont(juce::Font("Consolas", 12.0f, juce::Font::plain));
    typeView.setText("sample: f32[-1.0 .. 1.0]\ngain:   f32[Meter / Second]\nunit:   Meter");
    addAndMakeVisible(typeView);

    // Port View
    portGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xff444444));
    portGroup.setColour(juce::GroupComponent::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(portGroup);

    portView.setMultiLine(true);
    portView.setReadOnly(true);
    portView.setFont(juce::Font("Consolas", 12.0f, juce::Font::plain));
    portView.setText("in  signal:  Stream<f32>\nout output:  Stream<f32>\nwhere { latency <= 64 }");
    addAndMakeVisible(portView);
}

void MetadataPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void MetadataPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    headerLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(6);

    auto thirdHeight = bounds.getHeight() / 3;

    // AST Section
    auto astArea = bounds.removeFromTop(thirdHeight);
    astGroup.setBounds(astArea);
    astView.setBounds(astArea.reduced(6, 20));

    bounds.removeFromTop(6);

    // Type Section
    auto typeArea = bounds.removeFromTop(thirdHeight);
    typeGroup.setBounds(typeArea);
    typeView.setBounds(typeArea.reduced(6, 20));

    bounds.removeFromTop(6);

    // Port Section
    portGroup.setBounds(bounds);
    portView.setBounds(bounds.reduced(6, 20));
}

void MetadataPanel::updateMetadata(const juce::String& fileName)
{
    headerLabel.setText("Metadata: " + fileName, juce::dontSendNotification);
}
