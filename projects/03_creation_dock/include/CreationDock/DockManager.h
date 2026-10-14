#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "DockPanel.h"
#include "DockContainer.h"
#include "DockOverlay.h"
#include "FloatingDockWindow.h"

#include <map>
#include <vector>

namespace CreationDock {

class DockManager : public juce::Component
{
public:
    explicit DockManager(juce::Component& topLevelWindow);
    ~DockManager() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    DockPanel* registerPanel(const juce::String& id, const juce::String& title, std::unique_ptr<juce::Component> contentComponent, DockTargetZone initialZone);

    void floatPanel(DockPanel* panel);
    void dockPanel(DockPanel* panel, DockTargetZone zone);
    void removePanel(DockPanel* panel);

    // Visibility and activation management (View menu / workspace)
    bool isPanelOpen(const juce::String& id) const;
    bool isPanelActive(const juce::String& id) const;
    void hidePanel(const juce::String& id);
    void showPanel(const juce::String& id);
    void activatePanel(const juce::String& id);
    void togglePanel(const juce::String& id);

    DockPanel* findPanelById(const juce::String& id) const;
    juce::String getPanelTitle(const juce::String& id) const;
    std::vector<juce::String> getRegisteredPanelIds() const;
    DockTargetZone getDefaultZone(const juce::String& id) const;
    DockTargetZone zoneContainingPanel(DockPanel* panel) const;

    // Rearranges every registered panel back to the zone it was first registered
    // into, and resets zone sizes to the library defaults.
    void resetLayout();

    // Snapshot of zone assignments, tab order/selection, zone sizes, and floating
    // window positions - suitable for round-tripping through juce::JSON.
    juce::var captureLayout() const;
    void applyLayout(const juce::var& layout);

    void saveLayoutToFile(const juce::File& file) const;
    bool loadLayoutFromFile(const juce::File& file);

private:
    juce::Component& ownerWindow;
    DockContainer container;
    DockOverlay overlay;

    std::vector<std::unique_ptr<DockPanel>> registeredPanels;
    std::vector<std::unique_ptr<FloatingDockWindow>> floatingWindows;
    std::vector<std::pair<juce::String, DockTargetZone>> defaultLayout;
    std::map<juce::String, DockTargetZone> lastKnownZones;

    void handlePanelTitleDrag(DockPanel* panel, const juce::MouseEvent& e);
    void handlePanelDragEnded(DockPanel* panel, const juce::MouseEvent& e);
    void handleFloatingWindowClosed(FloatingDockWindow* window, DockTargetZone redockZone);

    // screenPos of {-1,-1} means "no particular position" - centers the window.
    // Otherwise the window is placed near that point (used when a tab is
    // dragged out past the dock area, so it detaches under the cursor).
    void floatPanelAt(DockPanel* panel, juce::Point<int> screenPos);

    // Pulls a panel out of wherever it currently lives (the not-yet-placed list,
    // one of the container's zones, or a floating window) so it can be moved elsewhere.
    std::unique_ptr<DockPanel> extractPanel(DockPanel* panel);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockManager)
};

} // namespace CreationDock


