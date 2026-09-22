#include "AiConversationView.h"
#include "ZoomableEditors.h"

namespace
{
void insertStyled(juce::TextEditor& editor, const juce::String& text,
                  const juce::Font& font, juce::Colour colour)
{
    editor.setFont(font);
    editor.setColour(juce::TextEditor::textColourId, colour);
    editor.insertTextAtCaret(text);
}

void insertInlineMarkdown(juce::TextEditor& editor, const juce::String& line,
                          const juce::Font& baseFont, juce::Colour baseColour)
{
    int position = 0;
    while (position < line.length())
    {
        if (line.substring(position).startsWith("**"))
        {
            const auto end = line.indexOf(position + 2, "**");
            if (end >= 0)
            {
                insertStyled(editor, line.substring(position + 2, end), baseFont.boldened(), baseColour);
                position = end + 2;
                continue;
            }
        }

        if (line[position] == '`')
        {
            const auto end = line.indexOfChar(position + 1, '`');
            if (end >= 0)
            {
                insertStyled(editor, line.substring(position + 1, end),
                             juce::Font("Consolas", baseFont.getHeight(), juce::Font::plain),
                             juce::Colour(0xfff2c879));
                position = end + 1;
                continue;
            }
        }

        if (line[position] == '[')
        {
            const auto labelEnd = line.indexOfChar(position + 1, ']');
            if (labelEnd >= 0 && line.substring(labelEnd).startsWith("]("))
            {
                const auto urlEnd = line.indexOfChar(labelEnd + 2, ')');
                if (urlEnd >= 0)
                {
                    auto linkFont = baseFont;
                    linkFont.setUnderline(true);
                    insertStyled(editor, line.substring(position + 1, labelEnd),
                                 linkFont, juce::Colour(0xff78bfff));
                    insertStyled(editor, " (" + line.substring(labelEnd + 2, urlEnd) + ")",
                                 baseFont, juce::Colour(0xff8fa3ad));
                    position = urlEnd + 1;
                    continue;
                }
            }
        }

        if (line[position] == '*' || line[position] == '_')
        {
            const auto marker = line[position];
            const auto end = line.indexOfChar(position + 1, marker);
            if (end > position + 1)
            {
                insertStyled(editor, line.substring(position + 1, end),
                             baseFont.italicised(), baseColour);
                position = end + 1;
                continue;
            }
        }

        auto next = position + 1;
        while (next < line.length() && line[next] != '*' && line[next] != '_'
               && line[next] != '`' && line[next] != '[')
            ++next;
        insertStyled(editor, line.substring(position, next), baseFont, baseColour);
        position = next;
    }
}

void renderMarkdown(juce::TextEditor& editor, const juce::String& markdown,
                    juce::Colour baseColour, float scale)
{
    editor.clear();
    const juce::Font bodyFont(14.0f * scale);
    const juce::Font codeFont("Consolas", 13.0f * scale, juce::Font::plain);
    const auto lines = juce::StringArray::fromLines(markdown.replace("\r\n", "\n"));
    bool inCodeBlock = false;

    for (const auto& sourceLine : lines)
    {
        const auto trimmed = sourceLine.trimStart();
        if (trimmed.startsWith("```"))
        {
            inCodeBlock = !inCodeBlock;
            const auto language = trimmed.substring(3).trim();
            if (inCodeBlock && language.isNotEmpty())
                insertStyled(editor, language.toUpperCase() + "\n",
                             juce::Font(11.0f * scale, juce::Font::bold), juce::Colour(0xff91a4ae));
            continue;
        }

        if (inCodeBlock)
        {
            insertStyled(editor, "  " + sourceLine + "\n", codeFont, juce::Colour(0xffdce8eb));
            continue;
        }

        int headingLevel = 0;
        while (headingLevel < trimmed.length() && trimmed[headingLevel] == '#')
            ++headingLevel;
        if (headingLevel > 0 && headingLevel <= 6
            && headingLevel < trimmed.length() && trimmed[headingLevel] == ' ')
        {
            const auto size = (headingLevel == 1 ? 21.0f : headingLevel == 2 ? 18.0f : 16.0f) * scale;
            insertInlineMarkdown(editor, trimmed.substring(headingLevel + 1),
                                 juce::Font(size, juce::Font::bold), juce::Colour(0xfff0f7f6));
            insertStyled(editor, "\n", bodyFont, baseColour);
            continue;
        }

        if (trimmed == "---" || trimmed == "***" || trimmed == "___")
        {
            insertStyled(editor, "----------------------------------------\n",
                         bodyFont, juce::Colour(0xff536674));
            continue;
        }

        juce::String prefix;
        juce::String line = trimmed;
        if (trimmed.startsWith("> "))
        {
            prefix = "| ";
            line = trimmed.substring(2);
        }
        else if (trimmed.startsWith("- ") || trimmed.startsWith("* ") || trimmed.startsWith("+ "))
        {
            prefix = "  - ";
            line = trimmed.substring(2);
        }
        else
        {
            int digitCount = 0;
            while (digitCount < trimmed.length() && juce::CharacterFunctions::isDigit(trimmed[digitCount]))
                ++digitCount;
            if (digitCount > 0 && trimmed.substring(digitCount).startsWith(". "))
            {
                prefix = "  " + trimmed.substring(0, digitCount + 2);
                line = trimmed.substring(digitCount + 2);
            }
        }

        if (prefix.isNotEmpty())
            insertStyled(editor, prefix, bodyFont.boldened(), juce::Colour(0xff78d7c2));
        insertInlineMarkdown(editor, line, bodyFont, baseColour);
        insertStyled(editor, "\n", bodyFont, baseColour);
    }
    editor.setCaretPosition(0);
}

class MessageBubble : public juce::Component
{
public:
    MessageBubble(AiConversationView::Message value, std::function<void(float)> zoomCallback,
                  std::function<void()> layoutCallbackIn)
        : message(std::move(value)), layoutCallback(std::move(layoutCallbackIn))
    {
        const auto marker = message.content.indexOf("\n\n:::details ");
        if (marker >= 0)
        {
            const auto titleStart = marker + 13;
            const auto titleEnd = message.content.indexOfChar(titleStart, '\n');
            const auto detailsEnd = message.content.lastIndexOf("\n:::");
            if (titleEnd > titleStart && detailsEnd > titleEnd)
            {
                detailsTitle = message.content.substring(titleStart, titleEnd).trim();
                detailsContent = message.content.substring(titleEnd + 1, detailsEnd).trim();
                message.content = message.content.substring(0, marker).trimEnd();
            }
        }

        text.setMultiLine(true);
        text.setReadOnly(true);
        text.setScrollbarsShown(false);
        text.setBorder(juce::BorderSize<int>());
        text.setIndents(0, 0);
        text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        text.onZoom = std::move(zoomCallback);
        addAndMakeVisible(text);

        detailsText.setMultiLine(true);
        detailsText.setReadOnly(true);
        detailsText.setScrollbarsShown(false);
        detailsText.setBorder(juce::BorderSize<int>());
        detailsText.setIndents(0, 0);
        detailsText.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff101721));
        detailsText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        detailsText.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        detailsText.onZoom = [this](float direction) {
            if (text.onZoom) text.onZoom(direction);
        };

        detailsButton.setButtonText("> " + detailsTitle);
        detailsButton.setTooltip("Show project activity");
        detailsButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        detailsButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        detailsButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff91b7c9));
        detailsButton.onClick = [this] {
            detailsExpanded = !detailsExpanded;
            detailsButton.setButtonText((detailsExpanded ? "v " : "> ") + detailsTitle);
            detailsButton.setTooltip(detailsExpanded ? "Hide project activity" : "Show project activity");
            detailsText.setVisible(detailsExpanded);
            if (layoutCallback) layoutCallback();
        };
        if (detailsTitle.isNotEmpty())
            addAndMakeVisible(detailsButton);
        addChildComponent(detailsText);
    }

    bool isUser() const { return message.role == "user" || message.role == "you"; }
    bool isSystem() const { return message.role == "system"; }

    int layoutForWidth(int availableWidth, float scale)
    {
        const auto proportion = isUser() ? 0.82f : (isSystem() ? 0.88f : 0.94f);
        const auto bubbleWidth = juce::jlimit(180, availableWidth,
                                              juce::roundToInt(static_cast<float>(availableWidth) * proportion));
        setSize(bubbleWidth, 80);
        text.setBounds(14, 27, bubbleWidth - 28, 20);
        const auto textColour = isSystem() ? juce::Colour(0xffc8b98e) : juce::Colour(0xffedf3f5);
        renderMarkdown(text, message.content, textColour, scale);
        const auto bodyHeight = juce::jmax(20, text.getTextHeight() + 5);
        int totalHeight = bodyHeight + 39;
        setSize(bubbleWidth, totalHeight);
        text.setBounds(14, 27, bubbleWidth - 28, bodyHeight);

        if (detailsTitle.isNotEmpty())
        {
            detailsButton.setBounds(11, 29 + bodyHeight, bubbleWidth - 22, 24);
            totalHeight += 28;
            if (detailsExpanded)
            {
                renderMarkdown(detailsText, detailsContent, juce::Colour(0xffc7d6dc), scale * 0.92f);
                const auto detailsHeight = juce::jmax(24, detailsText.getTextHeight() + 12);
                detailsText.setBounds(14, 55 + bodyHeight, bubbleWidth - 28, detailsHeight);
                detailsText.setVisible(true);
                totalHeight += detailsHeight + 4;
            }
        }
        setSize(bubbleWidth, totalHeight);
        return totalHeight;
    }

    void paint(juce::Graphics& g) override
    {
        const auto fill = isUser() ? juce::Colour(0xff244e78)
                         : isSystem() ? juce::Colour(0xff25231e)
                                      : juce::Colour(0xff171e28);
        const auto outline = isUser() ? juce::Colour(0xff4c91cf)
                            : isSystem() ? juce::Colour(0xff5c5645)
                                         : juce::Colour(0xff304154);
        const auto roleColour = isUser() ? juce::Colour(0xffa9d8ff)
                               : isSystem() ? juce::Colour(0xffd6bd78)
                                            : juce::Colour(0xff79c8ee);
        auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(fill);
        g.fillRoundedRectangle(area, 8.0f);
        g.setColour(outline);
        g.drawRoundedRectangle(area, 8.0f, 1.0f);
        g.setColour(roleColour);
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        const auto roleName = isUser() ? "You" : isSystem() ? "System" : "Assistant";
        g.drawText(roleName, 14, 6, getWidth() - 28, 17, juce::Justification::centredLeft, false);
    }

private:
    AiConversationView::Message message;
    ZoomableTextEditor text;
    ZoomableTextEditor detailsText;
    juce::TextButton detailsButton;
    juce::String detailsTitle;
    juce::String detailsContent;
    std::function<void()> layoutCallback;
    bool detailsExpanded = false;
};
}

class AiConversationView::Content : public juce::Component
{
public:
    explicit Content(AiConversationView& ownerIn) : owner(ownerIn) {}

    void rebuild(const std::vector<Message>& messages, float scale)
    {
        bubbles.clear();
        for (const auto& message : messages)
        {
            auto bubble = std::make_unique<MessageBubble>(
                message,
                [this](float direction) { owner.changeScale(direction); },
                [this] { layout(getWidth()); });
            addAndMakeVisible(*bubble);
            bubbles.push_back(std::move(bubble));
        }
        currentScale = scale;
        layout(getWidth());
    }

    void layout(int width)
    {
        const auto availableWidth = juce::jmax(180, width - 24);
        int y = 14;
        for (auto& bubble : bubbles)
        {
            const auto height = bubble->layoutForWidth(availableWidth, currentScale);
            const auto x = bubble->isUser() ? width - 12 - bubble->getWidth() : 12;
            bubble->setTopLeftPosition(juce::jmax(0, x), y);
            y += height + 12;
        }
        setSize(width, juce::jmax(y + 4, getParentHeight()));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (!bubbles.empty()) return;
        g.setColour(juce::Colour(0xff7f8e94));
        g.setFont(juce::Font(13.0f));
        g.drawText("Ask me anything about writing Frust code.",
                   getLocalBounds().reduced(16), juce::Justification::topLeft, true);
    }

private:
    AiConversationView& owner;
    std::vector<std::unique_ptr<MessageBubble>> bubbles;
    float currentScale = 1.0f;
};

AiConversationView::AiConversationView()
    : content(std::make_unique<Content>(*this))
{
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(8);
    viewport.setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff397f92));
    viewport.setColour(juce::ScrollBar::trackColourId, juce::Colour(0xff11161c));
    addAndMakeVisible(viewport);
}

AiConversationView::~AiConversationView()
{
    viewport.setViewedComponent(nullptr, false);
}

void AiConversationView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10151c));
    g.setColour(juce::Colour(0xff34434c));
    g.drawRect(getLocalBounds(), 1);
}

void AiConversationView::resized()
{
    viewport.setBounds(getLocalBounds());
    content->layout(juce::jmax(1, viewport.getMaximumVisibleWidth()));
}

void AiConversationView::setMessages(std::vector<Message> newMessages)
{
    messages = std::move(newMessages);
    content->rebuild(messages, scale);
    scrollToBottom();
}

void AiConversationView::appendMessage(const juce::String& role, const juce::String& message)
{
    messages.push_back({ role, message });
    content->rebuild(messages, scale);
    scrollToBottom();
}

void AiConversationView::scrollToBottom()
{
    viewport.setViewPosition(0, juce::jmax(0, content->getHeight() - viewport.getMaximumVisibleHeight()));
}

void AiConversationView::changeScale(float direction)
{
    scale = juce::jlimit(0.7f, 2.0f, scale + direction * 0.1f);
    content->rebuild(messages, scale);
    scrollToBottom();
    if (onScaleChanged) onScaleChanged(scale);
}
