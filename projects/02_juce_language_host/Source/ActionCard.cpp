#include "ActionCard.h"

namespace
{
const juce::Colour cardBackground(0xff23272e);
const juce::Colour cardText(0xffe0e0e0);
constexpr int padding = 10;
constexpr int titleHeight = 20;
constexpr int buttonHeight = 26;
constexpr int commentHeight = 48;
}

ActionCard::ActionCard(Request requestToShow, std::function<void(int, const juce::String&)> onAnswer)
    : request(std::move(requestToShow)), answer(std::move(onAnswer))
{
    titleLabel.setText(request.title, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, request.accent.brighter(0.3f));
    addAndMakeVisible(titleLabel);

    // Read-only but selectable, so a command can be copied.
    bodyText.setMultiLine(true, true);
    bodyText.setReadOnly(true);
    bodyText.setCaretVisible(false);
    bodyText.setScrollbarsShown(true);
    bodyText.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
    bodyText.setColour(juce::TextEditor::backgroundColourId, cardBackground.darker(0.25f));
    bodyText.setColour(juce::TextEditor::textColourId, cardText);
    bodyText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    bodyText.setText(request.body, false);
    addAndMakeVisible(bodyText);

    if (request.wantsComment)
    {
        commentBox.setMultiLine(true, true);
        commentBox.setReturnKeyStartsNewLine(true);
        commentBox.setTextToShowWhenEmpty(request.commentHint, juce::Colours::grey);
        commentBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1b1e23));
        commentBox.setColour(juce::TextEditor::textColourId, cardText);
        addAndMakeVisible(commentBox);
    }

    for (int i = 0; i < request.buttons.size(); ++i)
    {
        auto* button = buttons.add(new juce::TextButton(request.buttons[i]));
        button->setColour(juce::TextButton::buttonColourId, i == 0 ? request.accent.withAlpha(0.85f) : juce::Colour(0xff3a3f47));
        button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button->onClick = [this, i] {
            if (answered)
                return;
            answered = true;
            for (auto* b : buttons)
                b->setEnabled(false);
            if (answer)
                answer(i, commentBox.getText().trim());
        };
        addAndMakeVisible(button);
    }
}

int ActionCard::preferredHeight(int width) const
{
    const juce::Font font(juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain);
    juce::AttributedString text;
    text.append(request.body, font);
    juce::TextLayout layout;
    layout.createLayout(text, (float) juce::jmax(100, width - 2 * padding - 16));
    const int bodyHeight = juce::jlimit(22, 150, (int) std::ceil(layout.getHeight()) + 10);
    return padding + titleHeight + 4 + bodyHeight + 6 + (request.wantsComment ? commentHeight + 6 : 0) + buttonHeight + padding;
}

void ActionCard::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(cardBackground);
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(request.accent.withAlpha(0.7f));
    g.drawRoundedRectangle(area, 6.0f, 1.2f);
    g.fillRoundedRectangle(area.withWidth(4.0f), 2.0f);
}

void ActionCard::resized()
{
    auto area = getLocalBounds().reduced(padding);
    area.removeFromLeft(4);
    titleLabel.setBounds(area.removeFromTop(titleHeight));
    area.removeFromTop(4);

    auto buttonRow = area.removeFromBottom(buttonHeight);
    area.removeFromBottom(6);
    if (request.wantsComment)
    {
        commentBox.setBounds(area.removeFromBottom(commentHeight));
        area.removeFromBottom(6);
    }
    bodyText.setBounds(area);

    for (int i = buttons.size(); --i >= 0;)
    {
        auto* b = buttons[i];
        const int w = juce::jmin(260, b->getBestWidthForHeight(buttonHeight) + 24);
        b->setBounds(buttonRow.removeFromRight(w));
        buttonRow.removeFromRight(6);
    }
}
