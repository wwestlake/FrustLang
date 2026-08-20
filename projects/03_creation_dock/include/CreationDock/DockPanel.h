#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace CreationDock {

// The content wrapper docked into a DockZone (or floated in its own window).
// Owns the id/title and the caller-supplied content component. All tab UI -
// drag, float, close, activate - lives in DockTab now; DockPanel is just a
// thin, stable handle that host apps and DockManager hold onto across
// redocks, so the public registerPanel()/DockPanel contract stays the same
// no matter how the tab strip underneath it is implemented.
class DockPanel : public juce::Component
{
public:
    DockPanel(const juce::String& panelID, const juce::String& title, std::unique_ptr<juce::Component> contentComponent);
    ~DockPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::String getPanelID() const { return id; }
    juce::String getTitle() const { return panelTitle; }
    juce::Component* getContent() const { return content.get(); }

    void setTitle(const juce::String& newTitle);

    std::function<void(DockPanel*, const juce::MouseEvent&)> onTitleBarDragged;
    std::function<void(DockPanel*, const juce::MouseEvent&)> onTitleBarDragEnded;
    std::function<void(DockPanel*)> onFloatRequested;
    std::function<void(DockPanel*)> onCloseRequested;
    std::function<void()> onTitleChanged;

private:
    juce::String id;
    juce::String panelTitle;
    std::unique_ptr<juce::Component> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockPanel)
};

} // namespace CreationDock
