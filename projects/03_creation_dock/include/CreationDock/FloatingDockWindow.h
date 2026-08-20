#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "DockPanel.h"

namespace CreationDock {

class FloatingDockWindow : public juce::DocumentWindow
{
public:
    FloatingDockWindow(const juce::String& name, std::unique_ptr<DockPanel> panel);
    ~FloatingDockWindow() override = default;

    void closeButtonPressed() override;

    DockPanel* getPanel() const { return panelPtr; }
    std::unique_ptr<DockPanel> detachPanel();

    std::function<void(FloatingDockWindow*)> onWindowClosed;

private:
    std::unique_ptr<DockPanel> ownedPanel;
    DockPanel* panelPtr = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingDockWindow)
};

} // namespace CreationDock
