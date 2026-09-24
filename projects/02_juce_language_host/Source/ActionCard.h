#pragma once

#include <JuceHeader.h>

#include <functional>

// A card in the assistant panel, above the message box, for something only the user can decide while the Engineer waits:
// whether a command may run (Run once / Always allow / Don't run), or whether a program the Engineer opened works
// (Pass / Fail, with a comment). It is part of the panel, not a pop-up, so the user can look at the program and answer at the
// same time.
class ActionCard final : public juce::Component
{
public:
    struct Request
    {
        juce::String title;              // "Run this command?", "Test this program"
        juce::String body;               // what it is and why, several lines
        juce::StringArray buttons;       // left to right; the first is the main one
        bool wantsComment = false;       // a comment box (for a test verdict)
        juce::String commentHint;        // grey text in the empty comment box
        juce::Colour accent { 0xff4ea1ff };
    };

    // `onAnswer` is called once, on the message thread, with the index of the button pressed and the comment.
    ActionCard(Request request, std::function<void(int button, const juce::String& comment)> onAnswer);

    // The height the card wants at a given width.
    int preferredHeight(int width) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Request request;
    std::function<void(int, const juce::String&)> answer;
    juce::Label titleLabel;
    juce::TextEditor bodyText;
    juce::TextEditor commentBox;
    juce::OwnedArray<juce::TextButton> buttons;
    bool answered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActionCard)
};
