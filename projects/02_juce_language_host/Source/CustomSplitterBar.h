#pragma once

#include <JuceHeader.h>
#include <functional>

class CustomVerticalSplitter : public juce::Component
{
public:
    CustomVerticalSplitter()
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    ~CustomVerticalSplitter() override = default;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff2d2d2d));
        g.setColour(isMouseOverOrDragging() ? juce::Colours::cyan : juce::Colour(0xff3f3f3f));
        g.fillRect(getLocalBounds().reduced(2, 0));
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&)  override { repaint(); }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStartWidth = currentWidth;
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        int deltaX = e.getDistanceFromDragStartX();
        int newWidth = isLeftPanel ? (dragStartWidth + deltaX) : (dragStartWidth - deltaX);
        newWidth = juce::jlimit(minWidth, maxWidth, newWidth);

        if (newWidth != currentWidth) {
            currentWidth = newWidth;
            if (onWidthChanged) onWidthChanged(currentWidth);
        }
    }

    void setLimits(int minW, int maxW, int initialW, bool isLeft)
    {
        minWidth = minW;
        maxWidth = maxW;
        currentWidth = initialW;
        isLeftPanel = isLeft;
    }

    int getCurrentWidth() const { return currentWidth; }
    std::function<void(int)> onWidthChanged;

private:
    int minWidth = 100;
    int maxWidth = 600;
    int currentWidth = 240;
    int dragStartWidth = 240;
    bool isLeftPanel = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomVerticalSplitter)
};

class CustomHorizontalSplitter : public juce::Component
{
public:
    CustomHorizontalSplitter()
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    ~CustomHorizontalSplitter() override = default;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff2d2d2d));
        g.setColour(isMouseOverOrDragging() ? juce::Colours::cyan : juce::Colour(0xff3f3f3f));
        g.fillRect(getLocalBounds().reduced(0, 2));
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&)  override { repaint(); }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStartHeight = currentHeight;
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        int deltaY = e.getDistanceFromDragStartY();
        int newHeight = dragStartHeight - deltaY;
        newHeight = juce::jlimit(minHeight, maxHeight, newHeight);

        if (newHeight != currentHeight) {
            currentHeight = newHeight;
            if (onHeightChanged) onHeightChanged(currentHeight);
        }
    }

    void setLimits(int minH, int maxH, int initialH)
    {
        minHeight = minH;
        maxHeight = maxH;
        currentHeight = initialH;
    }

    int getCurrentHeight() const { return currentHeight; }
    std::function<void(int)> onHeightChanged;

private:
    int minHeight = 80;
    int maxHeight = 600;
    int currentHeight = 200;
    int dragStartHeight = 200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomHorizontalSplitter)
};
