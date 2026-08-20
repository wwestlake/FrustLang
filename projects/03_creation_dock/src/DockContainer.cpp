#include "CreationDock/DockContainer.h"

namespace CreationDock {

namespace {
    constexpr int splitterThickness = 6;
    constexpr int minZoneSize = 120;
}

DockContainer::DockContainer()
{
    leftZone = std::make_unique<DockZone>();
    centerZone = std::make_unique<DockZone>();
    rightZone = std::make_unique<DockZone>();
    bottomZone = std::make_unique<DockZone>();

    addAndMakeVisible(*leftZone);
    addAndMakeVisible(*centerZone);
    addAndMakeVisible(*rightZone);
    addAndMakeVisible(*bottomZone);

    addChildComponent(leftSplitter);
    addChildComponent(rightSplitter);
    addChildComponent(bottomSplitter);

    leftSplitter.onDragStart = [this] { leftWidthAtDragStart = leftWidth; };
    leftSplitter.onDragDelta = [this](int delta) { setLeftWidth(leftWidthAtDragStart + delta); };

    rightSplitter.onDragStart = [this] { rightWidthAtDragStart = rightWidth; };
    rightSplitter.onDragDelta = [this](int delta) { setRightWidth(rightWidthAtDragStart - delta); };

    bottomSplitter.onDragStart = [this] { bottomHeightAtDragStart = bottomHeight; };
    bottomSplitter.onDragDelta = [this](int delta) { setBottomHeight(bottomHeightAtDragStart - delta); };
}

void DockContainer::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818));
}

void DockContainer::setLeftWidth(int width)
{
    leftWidth = width;
    resized();
}

void DockContainer::setRightWidth(int width)
{
    rightWidth = width;
    resized();
}

void DockContainer::setBottomHeight(int height)
{
    bottomHeight = height;
    resized();
}

void DockContainer::resized()
{
    // leftWidth/rightWidth/bottomHeight are the user's *desired* sizes (only
    // changed by dragging a splitter or loading a saved layout). Clamping here
    // uses local copies so a transient tiny container size - e.g. while the
    // window is still being constructed - can never permanently shrink them.
    auto bounds = getLocalBounds();

    // Bottom zone spans the full width, so it's carved out first.
    if (bottomZone->getNumPanels() > 0) {
        int maxBottom = juce::jmax(minZoneSize, bounds.getHeight() - minZoneSize - splitterThickness);
        int clampedBottom = juce::jlimit(minZoneSize, maxBottom, bottomHeight);

        bottomZone->setBounds(bounds.removeFromBottom(clampedBottom));
        bottomSplitter.setBounds(bounds.removeFromBottom(splitterThickness));
        bottomSplitter.setVisible(true);
    } else {
        bottomZone->setBounds({});
        bottomSplitter.setVisible(false);
    }

    bool hasRight = rightZone->getNumPanels() > 0;

    if (leftZone->getNumPanels() > 0) {
        int reserveForRight = hasRight ? (minZoneSize + splitterThickness) : 0;
        int maxLeft = juce::jmax(minZoneSize, bounds.getWidth() - minZoneSize - splitterThickness - reserveForRight);
        int clampedLeft = juce::jlimit(minZoneSize, maxLeft, leftWidth);

        leftZone->setBounds(bounds.removeFromLeft(clampedLeft));
        leftSplitter.setBounds(bounds.removeFromLeft(splitterThickness));
        leftSplitter.setVisible(true);
    } else {
        leftZone->setBounds({});
        leftSplitter.setVisible(false);
    }

    if (hasRight) {
        int maxRight = juce::jmax(minZoneSize, bounds.getWidth() - minZoneSize - splitterThickness);
        int clampedRight = juce::jlimit(minZoneSize, maxRight, rightWidth);

        rightZone->setBounds(bounds.removeFromRight(clampedRight));
        rightSplitter.setBounds(bounds.removeFromRight(splitterThickness));
        rightSplitter.setVisible(true);
    } else {
        rightZone->setBounds({});
        rightSplitter.setVisible(false);
    }

    centerZone->setBounds(bounds);
}

} // namespace CreationDock
