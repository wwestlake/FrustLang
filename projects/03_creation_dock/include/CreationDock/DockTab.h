#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace CreationDock {

class DockZone;
class DockPanel;

// A single, fully custom tab button: title text, close glyph, click-to-
// activate, drag-to-relocate, double-click / drag-past-the-edge / right-click
// to float. Deliberately NOT built on juce::TabBarButton or TabbedComponent -
// we own layout, hit-testing and paint here so docking behavior is never at
// the mercy of a widget we didn't write.
class DockTab : public juce::Component
{
public:
    DockTab(DockZone& ownerZone, DockPanel& ownedPanel);
    ~DockTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    DockPanel& getPanel() const { return panel; }

    void setActive(bool shouldBeActive);
    bool isActive() const { return active; }

    // Ideal width for the current title text; used by DockTabStrip's layout.
    int getPreferredWidth() const;

private:
    DockZone& zone;
    DockPanel& panel;

    bool active = false;
    bool isBeingDragged = false;
    bool closeHovered = false;

    juce::Rectangle<int> closeButtonBounds;

    void showContextMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockTab)
};

} // namespace CreationDock
