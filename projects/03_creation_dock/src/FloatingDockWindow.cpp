#include "CreationDock/FloatingDockWindow.h"

namespace CreationDock {

FloatingDockWindow::FloatingDockWindow(const juce::String& name, std::unique_ptr<DockPanel> panel)
    : juce::DocumentWindow(name, juce::Colour(0xff181818), juce::DocumentWindow::allButtons)
{
    panelPtr = panel.get();
    ownedPanel = std::move(panel);

    // Non-owned: ownedPanel keeps the panel alive so it can be pulled back out
    // and redocked without the window deleting it out from under us.
    setContentNonOwned(ownedPanel.get(), true);
    setResizable(true, true);
    setUsingNativeTitleBar(true);
    centreWithSize(600, 450);
    setVisible(true);
}

void FloatingDockWindow::closeButtonPressed()
{
    if (onWindowClosed) {
        onWindowClosed(this);
    }
}

std::unique_ptr<DockPanel> FloatingDockWindow::detachPanel()
{
    clearContentComponent();
    panelPtr = nullptr;
    return std::move(ownedPanel);
}

} // namespace CreationDock
