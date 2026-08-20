#include "CreationDock/DockTab.h"
#include "CreationDock/DockZone.h"
#include "CreationDock/DockPanel.h"

namespace CreationDock {

DockTab::DockTab(DockZone& ownerZone, DockPanel& ownedPanel)
    : zone(ownerZone), panel(ownedPanel)
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void DockTab::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    juce::Colour bg = active ? juce::Colour(0xff2d2d2d) : juce::Colour(0xff232323);
    if (isBeingDragged) bg = juce::Colour(0xff3a3a3a);
    g.fillAll(bg);

    g.setColour(isBeingDragged ? juce::Colours::cyan.withAlpha(0.8f)
                                : (active ? juce::Colours::cyan.withAlpha(0.4f) : juce::Colour(0xff333333)));
    g.drawRect(bounds, 1);

    auto textArea = bounds.reduced(8, 0);
    textArea.removeFromRight(16);

    g.setColour(active ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont(juce::Font(13.0f, active ? juce::Font::bold : juce::Font::plain));
    g.drawText(panel.getTitle(), textArea, juce::Justification::centredLeft, true);

    g.setColour(closeHovered ? juce::Colours::white : juce::Colours::grey);
    g.drawText("x", closeButtonBounds, juce::Justification::centred, false);
}

void DockTab::resized()
{
    closeButtonBounds = getLocalBounds().removeFromRight(20).reduced(2);
}

void DockTab::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) {
        showContextMenu();
        return;
    }

    if (closeButtonBounds.contains(e.getPosition())) {
        if (panel.onCloseRequested) panel.onCloseRequested(&panel);
        return;
    }

    zone.setActivePanel(&panel);

    isBeingDragged = true;
    repaint();
}

void DockTab::mouseDrag(const juce::MouseEvent& e)
{
    if (panel.onTitleBarDragged) panel.onTitleBarDragged(&panel, e);
}

void DockTab::mouseUp(const juce::MouseEvent& e)
{
    isBeingDragged = false;
    repaint();

    if (panel.onTitleBarDragEnded) panel.onTitleBarDragEnded(&panel, e);
}

void DockTab::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (closeButtonBounds.contains(e.getPosition())) return;
    if (panel.onFloatRequested) panel.onFloatRequested(&panel);
}

void DockTab::mouseMove(const juce::MouseEvent& e)
{
    bool hoveredNow = closeButtonBounds.contains(e.getPosition());
    if (hoveredNow != closeHovered) {
        closeHovered = hoveredNow;
        repaint();
    }
}

void DockTab::mouseExit(const juce::MouseEvent&)
{
    if (closeHovered) {
        closeHovered = false;
        repaint();
    }
}

void DockTab::setActive(bool shouldBeActive)
{
    if (active == shouldBeActive) return;
    active = shouldBeActive;
    repaint();
}

int DockTab::getPreferredWidth() const
{
    juce::Font font(13.0f, juce::Font::bold);
    int textWidth = font.getStringWidth(panel.getTitle());
    return juce::jlimit(80, 240, textWidth + 40);
}

void DockTab::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Float");
    menu.addItem(2, "Close");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result == 1) {
            if (panel.onFloatRequested) panel.onFloatRequested(&panel);
        } else if (result == 2) {
            if (panel.onCloseRequested) panel.onCloseRequested(&panel);
        }
    });
}

} // namespace CreationDock
