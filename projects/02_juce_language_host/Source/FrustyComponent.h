#pragma once

#include <JuceHeader.h>

class FrustyComponent : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::Timer
{
public:
    enum class Mood
    {
        idle = 0,
        glassesAdjust = 1,
        approval = 2,
        satisfied = 3,
        planning = 4,
        compilerError = 5,
        working = 6,
        linkerFailure = 7,
        fullAccess = 8,
        success = 9,
        sigh = 10,
        exhausted = 11
    };

    FrustyComponent();

    void paint(juce::Graphics& g) override;
    void showMood(Mood mood, int holdMilliseconds = 0);

private:
    void timerCallback() override;

    juce::Image atlas;
    Mood mood = Mood::idle;
    int transientTicks = 0;
    int idleTicks = 0;
    int nextIdleGesture = 120;
    bool bobUp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrustyComponent)
};
