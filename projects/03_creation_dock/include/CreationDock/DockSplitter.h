#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace CreationDock {

enum class SplitterOrientation { Vertical, Horizontal };

class DockSplitter : public juce::Component
{
public:
    explicit DockSplitter(SplitterOrientation orientation);
    ~DockSplitter() override = default;

    void paint(juce::Graphics& g) override;

    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    // Fired once when a drag gesture begins, before any onDragDelta calls.
    std::function<void()> onDragStart;

    // Fired on every mouse move during a drag with the pixel offset from
    // where the drag started (not since the last call).
    std::function<void(int)> onDragDelta;

private:
    SplitterOrientation orientation;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockSplitter)
};

} // namespace CreationDock
