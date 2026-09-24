#include "PlanReviewPanel.h"

PlanReviewPanel::PlanReviewPanel()
{
    title.setFont(juce::Font(14.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(title);

    approveButton.setTooltip("Approve the markdown currently shown in the editor");
    approveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6a2d));
    approveButton.onClick = [this] {
        if (activeConversationId.isNotEmpty() && onApprove)
            onApprove(activeConversationId, editor.getText().trim());
    };
    addAndMakeVisible(approveButton);

    denyButton.setTooltip("Deny this plan and send feedback back to the assistant");
    denyButton.onClick = [this] {
        if (activeConversationId.isEmpty() || !onDeny)
            return;
        auto* dialog = new juce::AlertWindow("Deny Plan",
                                             "Tell Frusty what must change before this plan can be approved.",
                                             juce::MessageBoxIconType::QuestionIcon);
        dialog->addTextEditor("reason", {}, "Reason:");
        dialog->addButton("Deny", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PlanReviewPanel> safeThis(this);
        juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
        dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, safeDialog] (int result) {
                if (result == 0 || safeThis == nullptr || safeDialog == nullptr)
                    return;
                if (safeThis->onDeny)
                    safeThis->onDeny(safeThis->activeConversationId,
                                     safeDialog->getTextEditorContents("reason").trim());
            }), true);
    };
    addAndMakeVisible(denyButton);

    preview.setInterceptsMouseClicks(true, true);
    addAndMakeVisible(preview);

    editor.setMultiLine(true, true);
    editor.setReturnKeyStartsNewLine(true);
    editor.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    editor.setTextToShowWhenEmpty("A plan awaiting review will appear here.", juce::Colours::grey);
    editor.onTextChange = [this] { refreshPreview(); };
    addAndMakeVisible(editor);

    clearPlan();
}

void PlanReviewPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff141719));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void PlanReviewPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    auto header = bounds.removeFromTop(26);
    denyButton.setBounds(header.removeFromRight(70));
    header.removeFromRight(4);
    approveButton.setBounds(header.removeFromRight(86));
    title.setBounds(header);
    bounds.removeFromTop(6);

    const auto editorHeight = juce::jlimit(120, juce::jmax(120, bounds.getHeight() / 2), 260);
    editor.setBounds(bounds.removeFromBottom(editorHeight));
    bounds.removeFromBottom(6);
    preview.setBounds(bounds);
}

void PlanReviewPanel::setPlan(const juce::String& conversationId, const juce::String& markdown)
{
    activeConversationId = conversationId;
    editor.setText(markdown, false);
    approveButton.setEnabled(true);
    denyButton.setEnabled(true);
    title.setText("Plan Review: " + conversationId.substring(0, 8), juce::dontSendNotification);
    refreshPreview();
}

void PlanReviewPanel::clearPlan()
{
    activeConversationId.clear();
    editor.clear();
    approveButton.setEnabled(false);
    denyButton.setEnabled(false);
    title.setText("Plan Review", juce::dontSendNotification);
    preview.setMessages({ { "system", "No plan awaiting review." } });
}

void PlanReviewPanel::refreshPreview()
{
    const auto markdown = editor.getText().trim();
    if (markdown.isEmpty())
        preview.setMessages({ { "system", "No plan awaiting review." } });
    else
        preview.setMessages({ { "assistant", markdown } });
}
