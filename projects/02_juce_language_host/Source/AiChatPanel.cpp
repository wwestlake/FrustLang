#include "AiChatPanel.h"

#include <thread>

namespace {
juce::File getConfigFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LagDaemonResearchIDE")
        .getChildFile("ai_config.json");
}
} // namespace

AiChatPanel::AiChatPanel()
    : aiConfig(getConfigFile().getFullPathName().toStdString())
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::lightcyan);
    addAndMakeVisible(headerLabel);

    addAndMakeVisible(profileBox);
    refreshProfileList();

    transcript.setMultiLine(true);
    transcript.setReadOnly(true);
    transcript.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    transcript.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141414));
    transcript.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd4d4d4));
    transcript.setText("Ask me anything about writing Frust code.\n");
    addAndMakeVisible(transcript);

    inputBox.setMultiLine(true, true);
    inputBox.setReturnKeyStartsNewLine(false); // Return sends; Shift+Return isn't wired separately yet
    inputBox.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    inputBox.setTextToShowWhenEmpty("Ask about Frust...", juce::Colours::grey);
    inputBox.onReturnKey = [this] { sendMessage(); };
    addAndMakeVisible(inputBox);

    sendButton.onClick = [this] { sendMessage(); };
    addAndMakeVisible(sendButton);

    history.push_back({ "system", loadFrustSystemPrompt().toStdString() });
}

AiChatPanel::~AiChatPanel() = default;

void AiChatPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void AiChatPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto topBar = bounds.removeFromTop(24);
    headerLabel.setBounds(topBar.removeFromLeft(topBar.getWidth() / 2));
    profileBox.setBounds(topBar);

    bounds.removeFromTop(4);

    auto inputArea = bounds.removeFromBottom(70);
    bounds.removeFromBottom(4);
    transcript.setBounds(bounds);

    sendButton.setBounds(inputArea.removeFromRight(60));
    inputArea.removeFromRight(4);
    inputBox.setBounds(inputArea);
}

void AiChatPanel::refreshProfileList()
{
    profileBox.clear();
    int id = 1;
    for (auto& profile : aiConfig.profiles())
        profileBox.addItem(juce::String(profile.name), id++);

    if (profileBox.getNumItems() > 0) profileBox.setSelectedItemIndex(0);
    else profileBox.setTextWhenNoChoicesAvailable("No AI profiles configured");
}

void AiChatPanel::appendTranscript(const juce::String& speaker, const juce::String& text)
{
    transcript.moveCaretToEnd();
    transcript.insertTextAtCaret("\n" + speaker + ": " + text + "\n");
}

void AiChatPanel::sendMessage()
{
    if (requestInFlight) return;

    auto profileName = profileBox.getText();
    if (profileName.isEmpty()) {
        appendTranscript("system", "No AI profile selected - add one to ai_config.json first.");
        return;
    }

    auto userText = inputBox.getText().trim();
    if (userText.isEmpty()) return;

    auto provider = aiConfig.createProvider(profileName.toStdString());
    if (!provider) {
        appendTranscript("system", "Could not create a provider for '" + profileName + "'.");
        return;
    }

    appendTranscript("you", userText);
    history.push_back({ "user", userText.toStdString() });
    inputBox.clear();

    requestInFlight = true;
    appendTranscript("assistant", "(thinking...)");

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    auto historySnapshot = history; // sendChat runs off-thread; copy so it doesn't race the message thread
    auto* providerPtr = provider.release(); // ownership moves into the thread below

    std::thread([safeThis, historySnapshot, providerPtr] {
        std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
        auto response = owned->sendChat(historySnapshot);

        juce::MessageManager::callAsync([safeThis, response] {
            if (safeThis == nullptr) return; // panel (or the whole app) was closed mid-request

            // Replace the "(thinking...)" placeholder rather than just
            // appending, so the transcript doesn't accumulate stale ones.
            auto text = safeThis->transcript.getText();
            juce::String placeholder = "\nassistant: (thinking...)\n";
            auto idx = text.lastIndexOf(placeholder);
            if (idx >= 0) text = text.substring(0, idx) + text.substring(idx + placeholder.length());
            safeThis->transcript.setText(text);

            if (response.ok) {
                safeThis->appendTranscript("assistant", juce::String(response.content));
                safeThis->history.push_back({ "assistant", response.content });
            } else {
                safeThis->appendTranscript("system", "Error: " + juce::String(response.errorMessage));
            }

            safeThis->transcript.moveCaretToEnd();
            safeThis->requestInFlight = false;
        });
    }).detach();
}

juce::String AiChatPanel::loadFrustSystemPrompt()
{
    juce::File specFile("D:/000 Tech Research/projects/01_language_paradigms/02_functional/FRUST_LANG_SPEC.md");

    juce::String spec = specFile.existsAsFile()
        ? specFile.loadFileAsString()
        : juce::String("(FRUST_LANG_SPEC.md not found - answer from general programming-language knowledge instead.)");

    return "You are an assistant embedded in the LagDaemon IDE, helping the user write code in Frust, "
           "a language they are actively designing and implementing (lexer/parser/codegen already exist; "
           "not every language feature is wired to codegen yet). Use the specification below as the "
           "source of truth for Frust's syntax and semantics - don't assume it works like Rust, C++, or "
           "any other language where they differ.\n\n---\n\n" + spec;
}
