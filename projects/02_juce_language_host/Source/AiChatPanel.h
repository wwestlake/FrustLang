#pragma once

#include <JuceHeader.h>
#include <ai_provider/AiConfig.h>
#include <memory>
#include <vector>

// Chat panel talking to whichever AI profile is selected in the dropdown
// (profiles come from AiConfig, i.e. the user's ai_config.json). Seeds
// every conversation with FRUST_LANG_SPEC.md as a system message, so the
// model actually knows Frust's syntax rather than guessing at a generic
// C-like language.
class AiChatPanel : public juce::Component
{
public:
    AiChatPanel();
    ~AiChatPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void sendMessage();
    void appendTranscript(const juce::String& speaker, const juce::String& text);
    void refreshProfileList();
    static juce::String loadFrustSystemPrompt();

    juce::Label headerLabel { "Header", "AI Assistant" };
    juce::ComboBox profileBox;
    juce::TextEditor transcript;
    juce::TextEditor inputBox;
    juce::TextButton sendButton { "Send" };

    ai_provider::AiConfig aiConfig;
    std::vector<ai_provider::ChatMessage> history; // includes the leading system message

    // Guards against overlapping requests; the network call runs on a
    // background std::thread and marshals its result back via
    // MessageManager::callAsync, since JUCE UI is message-thread-only.
    bool requestInFlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AiChatPanel)
};
