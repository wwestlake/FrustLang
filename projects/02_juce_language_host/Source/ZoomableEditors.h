#pragma once

#include <JuceHeader.h>
#include <functional>

class ZoomableCodeEditor : public juce::CodeEditorComponent
{
public:
    ZoomableCodeEditor(juce::CodeDocument& document, juce::CodeTokeniser* tokeniser)
        : juce::CodeEditorComponent(document, tokeniser) {}

    std::function<void(float)> onZoom;

    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (event.mods.isCommandDown() && wheel.deltaY != 0.0f && onZoom)
        {
            onZoom(wheel.deltaY > 0.0f ? 1.0f : -1.0f);
            return;
        }
        juce::CodeEditorComponent::mouseWheelMove(event, wheel);
    }
};

class ZoomableTextEditor : public juce::TextEditor
{
public:
    std::function<void(float)> onZoom;

    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (event.mods.isCommandDown() && wheel.deltaY != 0.0f && onZoom)
        {
            onZoom(wheel.deltaY > 0.0f ? 1.0f : -1.0f);
            return;
        }
        juce::TextEditor::mouseWheelMove(event, wheel);
    }
};
