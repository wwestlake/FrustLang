#include "CreationDock/DockSplitter.h"

namespace CreationDock {

DockSplitter::DockSplitter(SplitterOrientation orientationIn)
    : orientation(orientationIn)
{
    setMouseCursor(orientation == SplitterOrientation::Vertical
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::UpDownResizeCursor);
}

void DockSplitter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2d2d2d));
    g.setColour(isMouseOverOrDragging() ? juce::Colours::cyan : juce::Colour(0xff3f3f3f));

    g.fillRect(orientation == SplitterOrientation::Vertical
        ? getLocalBounds().reduced(2, 0)
        : getLocalBounds().reduced(0, 2));
}

void DockSplitter::mouseEnter(const juce::MouseEvent&) { repaint(); }
void DockSplitter::mouseExit(const juce::MouseEvent&)  { repaint(); }

void DockSplitter::mouseDown(const juce::MouseEvent&)
{
    repaint();
    if (onDragStart) onDragStart();
}

void DockSplitter::mouseUp(const juce::MouseEvent&)
{
    repaint();
}

void DockSplitter::mouseDrag(const juce::MouseEvent& e)
{
    auto delta = orientation == SplitterOrientation::Vertical
        ? e.getDistanceFromDragStartX()
        : e.getDistanceFromDragStartY();

    if (onDragDelta) onDragDelta(delta);
}

} // namespace CreationDock
