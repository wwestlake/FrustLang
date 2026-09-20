#include "AiChatPanel.h"
#include "RAGQuery.h"

#include <thread>

namespace
{
juce::File getConfigFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LagDaemonResearchIDE")
        .getChildFile("ai_config.json");
}
}

AiChatPanel::AiChatPanel(juce::ApplicationProperties* properties)
    : aiConfig(getConfigFile().getFullPathName().toStdString()),
      conversationStore(properties)
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::lightcyan);
    addAndMakeVisible(headerLabel);

    addAndMakeVisible(profileBox);
    refreshProfileList();

    conversationBox.setTextWhenNothingSelected("New conversation");
    conversationBox.onChange = [this] {
        const auto index = conversationBox.getSelectedItemIndex();
        if (!changingConversationSelection && index >= 0)
            loadConversation(conversationSummaries[static_cast<size_t>(index)].id);
    };
    addAndMakeVisible(conversationBox);

    newButton.setTooltip("Start a new conversation");
    newButton.onClick = [this] { startNewConversation(); };
    addAndMakeVisible(newButton);

    archiveButton.setTooltip("Move this conversation to the configured archive folder");
    archiveButton.onClick = [this] { archiveCurrentConversation(); };
    addAndMakeVisible(archiveButton);

    foldersButton.setTooltip("Configure or open conversation folders");
    foldersButton.onClick = [this] { showFolderMenu(); };
    addAndMakeVisible(foldersButton);

    transcript.setMultiLine(true);
    transcript.setReadOnly(true);
    transcript.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    transcript.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141414));
    transcript.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd4d4d4));
    addAndMakeVisible(transcript);

    inputBox.setMultiLine(true, true);
    inputBox.setReturnKeyStartsNewLine(false);
    inputBox.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    inputBox.setTextToShowWhenEmpty("Ask about Frust...", juce::Colours::grey);
    inputBox.onReturnKey = [this] { sendMessage(); };
    addAndMakeVisible(inputBox);

    sendButton.onClick = [this] { sendMessage(); };
    addAndMakeVisible(sendButton);

    refreshConversationList(true);
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

    auto conversationBar = bounds.removeFromTop(24);
    foldersButton.setBounds(conversationBar.removeFromRight(76));
    conversationBar.removeFromRight(4);
    archiveButton.setBounds(conversationBar.removeFromRight(64));
    conversationBar.removeFromRight(4);
    newButton.setBounds(conversationBar.removeFromRight(48));
    conversationBar.removeFromRight(4);
    conversationBox.setBounds(conversationBar);
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

void AiChatPanel::refreshConversationList(bool loadMostRecent)
{
    conversationSummaries = conversationStore.listConversations();
    changingConversationSelection = true;
    conversationBox.clear();
    int itemId = 1;
    for (const auto& summary : conversationSummaries)
        conversationBox.addItem(summary.integrityValid ? summary.title : "[ALTERED] " + summary.title, itemId++);
    changingConversationSelection = false;

    if (loadMostRecent && !conversationSummaries.empty())
        loadConversation(conversationSummaries.front().id);
    else if (loadMostRecent)
        startNewConversation();
    else
        updateConversationControls();
}

void AiChatPanel::loadConversation(const juce::String& id)
{
    if (requestInFlight) return;

    StoredConversation loaded;
    juce::String error;
    if (!conversationStore.load(id, loaded, error))
    {
        currentConversation = conversationStore.createConversation();
        history.clear();
        history.push_back({ "system", loadFrustSystemPrompt().toStdString() });
        transcript.setText("Conversation integrity check failed.\n\n" + error);
        updateConversationControls();
        return;
    }

    currentConversation = std::move(loaded);
    renderConversation();
    changingConversationSelection = true;
    for (size_t i = 0; i < conversationSummaries.size(); ++i)
        if (conversationSummaries[i].id == currentConversation.id)
            conversationBox.setSelectedItemIndex(static_cast<int>(i), juce::dontSendNotification);
    changingConversationSelection = false;
    updateConversationControls();
}

void AiChatPanel::startNewConversation()
{
    if (requestInFlight) return;

    currentConversation = conversationStore.createConversation();
    changingConversationSelection = true;
    conversationBox.setSelectedItemIndex(-1, juce::dontSendNotification);
    conversationBox.setText("New conversation", juce::dontSendNotification);
    changingConversationSelection = false;
    renderConversation();
    updateConversationControls();
    inputBox.grabKeyboardFocus();
}

void AiChatPanel::archiveCurrentConversation()
{
    if (requestInFlight || currentConversation.blocks.empty()) return;

    juce::String error;
    if (!conversationStore.archive(currentConversation, error))
    {
        appendTranscript("system", "Archive failed: " + error);
        return;
    }
    refreshConversationList(true);
}

void AiChatPanel::showFolderMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Choose Conversations Folder...");
    menu.addItem(2, "Choose Archive Folder...");
    menu.addSeparator();
    menu.addItem(3, "Open Conversations Folder");
    menu.addItem(4, "Open Archive Folder");

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&foldersButton),
                       [safeThis] (int result) {
        if (safeThis == nullptr) return;
        if (result == 1) safeThis->chooseFolder(false);
        if (result == 2) safeThis->chooseFolder(true);
        if (result == 3) safeThis->conversationStore.getConversationFolder().revealToUser();
        if (result == 4) safeThis->conversationStore.getArchiveFolder().revealToUser();
    });
}

void AiChatPanel::chooseFolder(bool archiveFolder)
{
    const auto currentFolder = archiveFolder
        ? conversationStore.getArchiveFolder() : conversationStore.getConversationFolder();
    folderChooser = std::make_unique<juce::FileChooser>(
        archiveFolder ? "Choose the conversation archive folder" : "Choose the conversation folder",
        currentFolder);

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    folderChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectDirectories,
                               [safeThis, archiveFolder] (const juce::FileChooser& chooser) {
        if (safeThis == nullptr) return;
        const auto folder = chooser.getResult();
        if (folder == juce::File()) return;

        folder.createDirectory();
        if (archiveFolder)
            safeThis->conversationStore.setArchiveFolder(folder);
        else
        {
            safeThis->conversationStore.setConversationFolder(folder);
            safeThis->refreshConversationList(true);
        }
    });
}

void AiChatPanel::renderConversation()
{
    history.clear();
    history.push_back({ "system", loadFrustSystemPrompt().toStdString() });
    transcript.setText("Ask me anything about writing Frust code.\n");
    for (const auto& block : currentConversation.blocks)
    {
        history.push_back({ block.role.toStdString(), block.content.toStdString() });
        appendTranscript(block.role == "user" ? "you" : "assistant", block.content);
    }
    transcript.moveCaretToEnd();
}

bool AiChatPanel::appendAndSave(const juce::String& role, const juce::String& content)
{
    auto previous = currentConversation;
    juce::String error;
    if (!conversationStore.append(currentConversation, role, content)
        || !conversationStore.save(currentConversation, error))
    {
        currentConversation = std::move(previous);
        appendTranscript("system", "Conversation was not saved: " + error);
        return false;
    }

    refreshConversationList(false);
    changingConversationSelection = true;
    for (size_t i = 0; i < conversationSummaries.size(); ++i)
        if (conversationSummaries[i].id == currentConversation.id)
            conversationBox.setSelectedItemIndex(static_cast<int>(i), juce::dontSendNotification);
    changingConversationSelection = false;
    updateConversationControls();
    return true;
}

void AiChatPanel::updateConversationControls()
{
    conversationBox.setEnabled(!requestInFlight);
    newButton.setEnabled(!requestInFlight);
    archiveButton.setEnabled(!requestInFlight && !currentConversation.blocks.empty());
    foldersButton.setEnabled(!requestInFlight);
    sendButton.setEnabled(!requestInFlight);
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
    if (!appendAndSave("user", userText)) return;
    history.push_back({ "user", userText.toStdString() });
    inputBox.clear();

    requestInFlight = true;
    updateConversationControls();
    appendTranscript("assistant", "(thinking...)");

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    auto historySnapshot = history;
    auto ragContext = rag::getContextForQuery(userText);
    if (ragContext.isNotEmpty() && !historySnapshot.empty())
        historySnapshot.back().content =
            (userText + "\n\n---\nRetrieved context for this request:\n" + ragContext).toStdString();
    auto* providerPtr = provider.release();

    std::thread([safeThis, historySnapshot, providerPtr] {
        std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
        auto response = owned->sendChat(historySnapshot);

        juce::MessageManager::callAsync([safeThis, response] {
            if (safeThis == nullptr) return;

            auto text = safeThis->transcript.getText();
            const juce::String placeholder = "\nassistant: (thinking...)\n";
            auto idx = text.lastIndexOf(placeholder);
            if (idx >= 0) text = text.substring(0, idx) + text.substring(idx + placeholder.length());
            safeThis->transcript.setText(text);

            if (response.ok) {
                safeThis->appendTranscript("assistant", juce::String(response.content));
                safeThis->history.push_back({ "assistant", response.content });
                safeThis->appendAndSave("assistant", juce::String(response.content));
            } else {
                safeThis->appendTranscript("system", "Error: " + juce::String(response.errorMessage));
            }

            safeThis->transcript.moveCaretToEnd();
            safeThis->requestInFlight = false;
            safeThis->updateConversationControls();
        });
    }).detach();
}

juce::String AiChatPanel::loadFrustSystemPrompt()
{
    auto repoRoot = juce::File(FRUST_REPO_ROOT_DIR);
    auto agentContextFile = repoRoot.getChildFile("projects").getChildFile("frust-ide-agent")
        .getChildFile("FRUST_AI_CONTEXT.md");
    auto specFile = repoRoot.getChildFile("projects").getChildFile("01_language_paradigms")
        .getChildFile("02_functional").getChildFile("FRUST_LANG_SPEC.md");

    juce::String agentContext = agentContextFile.existsAsFile()
        ? agentContextFile.loadFileAsString() : juce::String("(FRUST_AI_CONTEXT.md not found.)");
    juce::String specLocation = specFile.existsAsFile()
        ? specFile.getFullPathName() : juce::String("(FRUST_LANG_SPEC.md not found in this repo.)");

    return "You are an assistant embedded in the LagDaemon IDE, helping the user write code in Frust, "
           "a language they are actively designing and implementing (lexer/parser/codegen already exist; "
           "not every language feature is wired to codegen yet). Use the compact context brief below and "
           "the LiteSemRAG context attached to individual user requests as the source of truth for Frust. "
           "Do not assume Frust works like Rust, C++, or any other language where they differ. If the "
           "retrieved context is not enough, say exactly what needs to be checked in the grammar, compiler, "
           "or library sources.\n\nAuthoritative spec path: " + specLocation + "\n\n---\n\n" + agentContext;
}
