#pragma once

#include <JuceHeader.h>

#include "AiConversationView.h"

class PlanReviewPanel : public juce::Component
{
public:
    PlanReviewPanel();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setPlan(const juce::String& conversationId, const juce::String& markdown);
    void clearPlan();

    std::function<void(const juce::String& conversationId, const juce::String& markdown)> onApprove;
    std::function<void(const juce::String& conversationId, const juce::String& reason)> onDeny;

private:
    void refreshPreview();

    juce::String activeConversationId;
    juce::Label title { "Title", "Plan Review" };
    juce::TextButton approveButton { "Approve" };
    juce::TextButton denyButton { "Deny" };
    AiConversationView preview;
    juce::TextEditor editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlanReviewPanel)
};
