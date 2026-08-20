#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "DockPanel.h"
#include "DockTabStrip.h"

namespace CreationDock {

class DockZone : public juce::Component
{
public:
    DockZone();
    ~DockZone() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void addPanel(std::unique_ptr<DockPanel> panel);
    std::unique_ptr<DockPanel> removePanel(DockPanel* panel);

    int getNumPanels() const;
    DockPanel* getPanel(int index) const;

    bool containsPanel(DockPanel* panel) const;

    int getCurrentTabIndex() const;
    void setCurrentTabIndex(int index);

    // Called by a DockTab when it's clicked - makes its panel the visible
    // one in this zone and hides the rest.
    void setActivePanel(DockPanel* panel);

    // Re-measures/repaints the tab strip (e.g. after a panel's title changed).
    void refreshLayout();

private:
    DockTabStrip tabStrip { *this };
    std::vector<std::unique_ptr<DockPanel>> ownedPanels;
    DockPanel* activePanel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockZone)
};

} // namespace CreationDock
