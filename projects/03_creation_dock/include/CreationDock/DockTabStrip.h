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

    DockTab* addTabFor(DockPanel& panel);
    void removeTabFor(DockPanel* panel);
    void setActivePanel(DockPanel* panel);

    int getNumTabs() const { return (int) tabViews.size(); }
    DockPanel* getPanelAt(int index) const;
    int indexOfPanel(DockPanel* panel) const;

private:
    DockZone& zone;
    std::vector<std::unique_ptr<DockTab>> tabViews;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockTabStrip)
};

} // namespace CreationDock
