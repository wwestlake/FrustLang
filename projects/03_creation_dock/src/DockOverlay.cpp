#include "CreationDock/DockOverlay.h"

namespace CreationDock {

DockOverlay::DockOverlay()
{
    setInterceptsMouseClicks(false, false);
}

void DockOverlay::paint(juce::Graphics& g)
{
    if (activeZone == DockTargetZone::None) return;

    auto glowRect = highlightRect.reduced(4).toFloat();

    g.setColour(juce::Colour(0x4000aaff));
    g.fillRoundedRectangle(glowRect, 6.0f);

    g.setColour(juce::Colours::cyan);
    g.drawRoundedRectangle(glowRect, 6.0f, 2.0f);
}

void DockOverlay::resized()
{
}

void DockOverlay::updateTargetFromMouse(juce::Point<int> screenPos, juce::Rectangle<int> containerBounds)
{
    auto localPos = getLocalPoint(nullptr, screenPos);
    auto bounds = containerBounds;

    if (!bounds.contains(localPos)) {
        hideOverlay();
        return;
    }

    // DockContainer only has Left/Right/Bottom/Center edge zones (no Top), so the
    // top quarter falls through to a center-tab merge rather than a dead zone.
    if (localPos.x < bounds.getX() + bounds.getWidth() / 4) {
        activeZone = DockTargetZone::Left;
        highlightRect = bounds.removeFromLeft(bounds.getWidth() / 2);
    } else if (localPos.x > bounds.getRight() - bounds.getWidth() / 4) {
        activeZone = DockTargetZone::Right;
        highlightRect = bounds.removeFromRight(bounds.getWidth() / 2);
    } else if (localPos.y > bounds.getBottom() - bounds.getHeight() / 4) {
        activeZone = DockTargetZone::Bottom;
        highlightRect = bounds.removeFromBottom(bounds.getHeight() / 2);
    } else {
        activeZone = DockTargetZone::CenterTab;
        highlightRect = bounds;
    }

    repaint();
}

void DockOverlay::hideOverlay()
{
    activeZone = DockTargetZone::None;
    highlightRect = {};
    repaint();
}

} // namespace CreationDock
