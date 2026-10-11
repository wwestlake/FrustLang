#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <memory>
#include "DockTab.h"

namespace CreationDock {

class DockZone;
class DockPanel;

// The row of tabs at the top of a DockZone. Owns layout (left-to-right,
// sized to each tab's title) and the DockTab views themselves; DockZone owns
// the DockPanel content and which one is currently active.
class DockTabStrip : public juce::Component
{
public:
    static constexpr int height = 28;

    explicit DockTabStrip(DockZone& ownerZone);
    ~DockTabStrip() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    DockTab* addTabFor(DockPanel& panel);
    void removeTabFor(DockPanel* panel);
    void setActivePanel(DockPanel* panel);
    void revealPanel(DockPanel* panel);

    int getNumTabs() const { return (int) tabViews.size(); }
    DockPanel* getPanelAt(int index) const;
    int indexOfPanel(DockPanel* panel) const;

private:
    class TabContainer final : public juce::Component
    {
    public:
        explicit TabContainer(DockTabStrip& ownerStrip) : owner(ownerStrip) {}
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            owner.mouseWheelMove(e, wheel);
        }
    private:
        DockTabStrip& owner;
    };

    class NavButton final : public juce::Button
    {
    public:
        NavButton(const juce::String& name, bool isLeftArrow);
        void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    private:
        bool isLeft;
    };

    void updateLayout();
    void setScrollOffset(int newOffset);
    int getTotalTabsWidth() const;

    DockZone& zone;
    TabContainer tabContainer { *this };
    NavButton leftButton { "Scroll Left", true };
    NavButton rightButton { "Scroll Right", false };
    std::vector<std::unique_ptr<DockTab>> tabViews;
    int scrollOffset = 0;
    bool isOverflowing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockTabStrip)
};

} // namespace CreationDock
