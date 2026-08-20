#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "DockZone.h"
#include "DockSplitter.h"

namespace CreationDock {

class DockContainer : public juce::Component
{
public:
    static constexpr int defaultLeftWidth = 260;
    static constexpr int defaultRightWidth = 280;
    static constexpr int defaultBottomHeight = 220;

    DockContainer();
    ~DockContainer() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    DockZone* getCenterZone() const { return centerZone.get(); }
    DockZone* getLeftZone() const { return leftZone.get(); }
    DockZone* getRightZone() const { return rightZone.get(); }
    DockZone* getBottomZone() const { return bottomZone.get(); }

    int getLeftWidth() const { return leftWidth; }
    int getRightWidth() const { return rightWidth; }
    int getBottomHeight() const { return bottomHeight; }

    void setLeftWidth(int width);
    void setRightWidth(int width);
    void setBottomHeight(int height);

private:
    std::unique_ptr<DockZone> leftZone;
    std::unique_ptr<DockZone> centerZone;
    std::unique_ptr<DockZone> rightZone;
    std::unique_ptr<DockZone> bottomZone;

    DockSplitter leftSplitter { SplitterOrientation::Vertical };
    DockSplitter rightSplitter { SplitterOrientation::Vertical };
    DockSplitter bottomSplitter { SplitterOrientation::Horizontal };

    int leftWidth = defaultLeftWidth;
    int rightWidth = defaultRightWidth;
    int bottomHeight = defaultBottomHeight;

    int leftWidthAtDragStart = leftWidth;
    int rightWidthAtDragStart = rightWidth;
    int bottomHeightAtDragStart = bottomHeight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockContainer)
};

} // namespace CreationDock
