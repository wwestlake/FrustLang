#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

class AiConversationView : public juce::Component
{
public:
    struct Message
    {
        juce::String role;
        juce::String content;
    };

    AiConversationView();
    ~AiConversationView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setMessages(std::vector<Message> newMessages);
    void appendMessage(const juce::String& role, const juce::String& content);
    void scrollToBottom();
    float getScale() const { return scale; }

    std::function<void(float)> onScaleChanged;

private:
    class Content;
    void changeScale(float direction);

    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    std::vector<Message> messages;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AiConversationView)
};
