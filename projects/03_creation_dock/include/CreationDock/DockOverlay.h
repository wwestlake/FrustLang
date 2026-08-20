#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace CreationDock {

enum class DockTargetZone { None, CenterTab, Left, Right, Top, Bottom };

class DockOverlay : public juce::Component
{
public:
    DockOverlay();
    ~DockOverlay() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void updateTargetFromMouse(juce::Point<int> screenPos, juce::Rectangle<int> containerBounds);
    DockTargetZone getActiveZone() const { return activeZone; }

    void hideOverlay();

private:
    DockTargetZone activeZone = DockTargetZone::None;
    juce::Rectangle<int> highlightRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockOverlay)
};

} // namespace CreationDock
