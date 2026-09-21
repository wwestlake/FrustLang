#include "AiChatPanel.h"
#include "RAGQuery.h"
#include <ai_provider/OpenAiProvider.h>

#include <thread>

namespace
{
juce::File getConfigFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LagDaemonResearchIDE")
        .getChildFile("ai_config.json");
}

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
                insertStyled(editor, line.substring(position + 2, end),
                             baseFont.boldened(), baseColour);
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
                             juce::Colour(0xfff0c674));
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
                                 linkFont, juce::Colour(0xff70b7ff));
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

void insertMarkdown(juce::TextEditor& editor, const juce::String& markdown,
                    juce::Colour baseColour)
{
    const juce::Font bodyFont(14.0f);
    const juce::Font codeFont("Consolas", 13.0f, juce::Font::plain);
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
                             juce::Font(11.0f, juce::Font::bold), juce::Colour(0xff7f929c));
            continue;
        }

        if (inCodeBlock)
        {
            insertStyled(editor, "  " + sourceLine + "\n", codeFont, juce::Colour(0xffd7e4e8));
            continue;
        }

        int headingLevel = 0;
        while (headingLevel < trimmed.length() && trimmed[headingLevel] == '#')
            ++headingLevel;
        if (headingLevel > 0 && headingLevel <= 6
            && headingLevel < trimmed.length() && trimmed[headingLevel] == ' ')
        {
            const auto size = headingLevel == 1 ? 21.0f : headingLevel == 2 ? 18.0f : 16.0f;
            insertInlineMarkdown(editor, trimmed.substring(headingLevel + 1),
                                 juce::Font(size, juce::Font::bold), juce::Colour(0xffe6f4f1));
            insertStyled(editor, "\n", bodyFont, baseColour);
            continue;
        }

        if (trimmed == "---" || trimmed == "***" || trimmed == "___")
        {
            insertStyled(editor, "----------------------------------------\n",
                         bodyFont, juce::Colour(0xff52626a));
            continue;
        }

        juce::String prefix;
        juce::String content = trimmed;
        if (trimmed.startsWith("> "))
        {
            prefix = "| ";
            content = trimmed.substring(2);
        }
        else if (trimmed.startsWith("- ") || trimmed.startsWith("* ") || trimmed.startsWith("+ "))
        {
            prefix = "  - ";
            content = trimmed.substring(2);
        }
        else
        {
            int digitCount = 0;
            while (digitCount < trimmed.length() && juce::CharacterFunctions::isDigit(trimmed[digitCount]))
                ++digitCount;
            if (digitCount > 0 && trimmed.substring(digitCount).startsWith(". "))
            {
                prefix = "  " + trimmed.substring(0, digitCount + 2);
                content = trimmed.substring(digitCount + 2);
            }
        }

        if (prefix.isNotEmpty())
            insertStyled(editor, prefix, bodyFont.boldened(), juce::Colour(0xff70c7b5));
        insertInlineMarkdown(editor, content, bodyFont, baseColour);
        insertStyled(editor, "\n", bodyFont, baseColour);
    }
}

void insertMessage(juce::TextEditor& editor, const juce::String& role, const juce::String& text)
{
    const auto isUser = role == "user" || role == "you";
    const auto isAssistant = role == "assistant";
    const auto roleColour = isUser ? juce::Colour(0xff72d6c1)
                                   : isAssistant ? juce::Colour(0xff79bfff)
                                                 : juce::Colour(0xffc4a86b);
    const auto textColour = role == "system" ? juce::Colour(0xffb8aa88)
                                               : juce::Colour(0xffd9e1e3);
    insertStyled(editor, "\n" + role.toUpperCase() + "\n",
                 juce::Font(12.0f, juce::Font::bold), roleColour);
    insertMarkdown(editor, text, textColour);
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
    profileBox.setTooltip("AI account/profile");
    profileBox.onChange = [this] { refreshModelList(); };

    addAndMakeVisible(modelBox);
    modelBox.setTooltip("Model used by the AI Assistant");
    modelBox.setTextWhenNothingSelected("Loading models...");
    modelBox.onChange = [this] { saveSelectedModel(); };

    aiSettingsButton.setTooltip("Configure the selected AI provider");
    aiSettingsButton.onClick = [this] { showAiSettings(); };
    addAndMakeVisible(aiSettingsButton);

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

    refreshProfileList();
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
    headerLabel.setBounds(topBar.removeFromLeft(96));
    aiSettingsButton.setBounds(topBar.removeFromRight(88));
    topBar.removeFromRight(4);
    profileBox.setBounds(topBar.removeFromLeft(150));
    topBar.removeFromLeft(4);
    modelBox.setBounds(topBar);
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

    if (profileBox.getNumItems() > 0)
    {
        profileBox.setSelectedItemIndex(0, juce::dontSendNotification);
        refreshModelList();
    }
    else profileBox.setTextWhenNoChoicesAvailable("No AI profiles configured");
}

void AiChatPanel::refreshModelList()
{
    const auto profileName = profileBox.getText();
    const ai_provider::AiProfile* selectedProfile = nullptr;
    for (const auto& profile : aiConfig.profiles())
        if (profile.name == profileName.toStdString())
            selectedProfile = &profile;

    changingModelSelection = true;
    modelBox.clear(juce::dontSendNotification);
    if (selectedProfile == nullptr)
    {
        modelBox.setTextWhenNothingSelected("No AI profile selected");
        modelBox.setEnabled(false);
        changingModelSelection = false;
        return;
    }

    const auto savedModel = juce::String(selectedProfile->model);
    if (savedModel.isNotEmpty())
    {
        modelBox.addItem(savedModel, 1);
        modelBox.setSelectedItemIndex(0, juce::dontSendNotification);
    }
    modelBox.setTextWhenNothingSelected("Loading models...");
    modelBox.setEnabled(false);
    changingModelSelection = false;
    modelRequestInFlight = true;
    const auto requestGeneration = ++modelRequestGeneration;

    auto provider = aiConfig.createProvider(profileName.toStdString());
    if (provider == nullptr)
    {
        modelRequestInFlight = false;
        modelBox.setTextWhenNothingSelected("Provider unavailable");
        return;
    }

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    auto* providerPtr = provider.release();
    std::thread([safeThis, providerPtr, profileName, savedModel, requestGeneration] {
        std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
        auto response = owned->listModels();
        juce::MessageManager::callAsync(
            [safeThis, profileName, savedModel, requestGeneration, response = std::move(response)] {
                if (safeThis == nullptr || requestGeneration != safeThis->modelRequestGeneration)
                    return;

                safeThis->modelRequestInFlight = false;
                safeThis->changingModelSelection = true;
                safeThis->modelBox.clear(juce::dontSendNotification);
                juce::StringArray models;
                if (savedModel.isNotEmpty()) models.add(savedModel);
                for (const auto& model : response.models)
                    models.addIfNotAlreadyThere(juce::String(model));
                for (int index = 0; index < models.size(); ++index)
                    safeThis->modelBox.addItem(models[index], index + 1);

                if (savedModel.isNotEmpty())
                    safeThis->modelBox.setText(savedModel, juce::dontSendNotification);
                safeThis->modelBox.setTextWhenNothingSelected(
                    response.ok ? "Select a model" : "Models unavailable");
                safeThis->changingModelSelection = false;
                safeThis->modelBox.setEnabled(!safeThis->requestInFlight && !models.isEmpty());

                if (!response.ok)
                    safeThis->appendTranscript("system", "Could not load models for "
                        + profileName + ": " + juce::String(response.errorMessage));
            });
    }).detach();
}

void AiChatPanel::saveSelectedModel()
{
    if (changingModelSelection) return;

    const auto profileName = profileBox.getText();
    const auto model = modelBox.getText();
    if (profileName.isEmpty() || model.isEmpty()) return;

    for (const auto& profile : aiConfig.profiles())
    {
        if (profile.name != profileName.toStdString()) continue;
        std::string error;
        if (!aiConfig.updateProfileCredentials(profile.name, profile.apiKey,
                                                model.toStdString(), error))
            appendTranscript("system", "Could not save model selection: " + juce::String(error));
        return;
    }
}

void AiChatPanel::showAiSettings()
{
    const auto profileName = profileBox.getText();
    const ai_provider::AiProfile* selectedProfile = nullptr;
    for (const auto& profile : aiConfig.profiles())
        if (profile.name == profileName.toStdString())
            selectedProfile = &profile;

    if (selectedProfile == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "AI Settings",
                                               "Select an AI profile first.");
        return;
    }

    showAiSettingsDialog(profileName,
                         juce::String(selectedProfile->apiKey));
}

void AiChatPanel::showAiSettingsDialog(const juce::String& profileName,
                                       const juce::String& apiKey)
{
    auto* dialog = new juce::AlertWindow("AI Settings",
                                         "Profile: " + profileName,
                                         juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("apiKey", apiKey, "API key:", true);

    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis, safeDialog, profileName] (int result) {
            if (result == 0 || safeThis == nullptr || safeDialog == nullptr)
                return;

            const auto apiKey = safeDialog->getTextEditorContents("apiKey").trim();
            if (apiKey.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "AI Settings",
                                                       "An API key is required.");
                return;
            }

            juce::String model = safeThis->modelBox.getText();
            if (model.isEmpty()) model = "gpt-4o-mini";

            std::string error;
            if (!safeThis->aiConfig.updateProfileCredentials(profileName.toStdString(),
                                                               apiKey.toStdString(),
                                                               model.toStdString(),
                                                               error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "AI Settings",
                                                       juce::String(error));
                return;
            }

            safeThis->appendTranscript("system", "API key saved for " + profileName + ".");
            safeThis->refreshModelList();
        }), true);
}

void AiChatPanel::appendTranscript(const juce::String& speaker, const juce::String& text)
{
    transcript.moveCaretToEnd();
    insertMessage(transcript, speaker, text);
    transcript.moveCaretToEnd();
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
    transcript.clear();
    insertStyled(transcript, "Ask me anything about writing Frust code.\n",
                 juce::Font(13.0f), juce::Colour(0xff92a0a4));
    for (const auto& block : currentConversation.blocks)
    {
        history.push_back({ block.role.toStdString(), block.content.toStdString() });
        insertMessage(transcript, block.role == "user" ? "you" : block.role, block.content);
    }
    transcript.moveCaretToEnd();
}

void AiChatPanel::updateCurrentConversationListEntry()
{
    changingConversationSelection = true;
    for (size_t i = 0; i < conversationSummaries.size(); ++i)
    {
        auto& summary = conversationSummaries[i];
        if (summary.id != currentConversation.id)
            continue;

        summary.title = currentConversation.title;
        summary.updatedAt = currentConversation.updatedAt;
        summary.integrityValid = true;
        conversationBox.changeItemText(static_cast<int>(i) + 1, currentConversation.title);
        conversationBox.setSelectedItemIndex(static_cast<int>(i), juce::dontSendNotification);
        changingConversationSelection = false;
        return;
    }

    conversationSummaries.push_back({ currentConversation.id,
                                      currentConversation.title,
                                      currentConversation.updatedAt,
                                      true });
    const auto itemId = static_cast<int>(conversationSummaries.size());
    conversationBox.addItem(currentConversation.title, itemId);
    conversationBox.setSelectedItemIndex(itemId - 1, juce::dontSendNotification);
    changingConversationSelection = false;
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

    updateCurrentConversationListEntry();
    updateConversationControls();
    return true;
}

void AiChatPanel::updateConversationControls()
{
    conversationBox.setEnabled(!requestInFlight);
    newButton.setEnabled(!requestInFlight);
    archiveButton.setEnabled(!requestInFlight && !currentConversation.blocks.empty());
    foldersButton.setEnabled(!requestInFlight);
    profileBox.setEnabled(!requestInFlight);
    modelBox.setEnabled(!requestInFlight && !modelRequestInFlight && modelBox.getNumItems() > 0);
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

    if (!appendAndSave("user", userText)) return;
    history.push_back({ "user", userText.toStdString() });
    inputBox.clear();
    renderConversation();

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

            if (response.ok) {
                safeThis->history.push_back({ "assistant", response.content });
                safeThis->appendAndSave("assistant", juce::String(response.content));
                safeThis->renderConversation();
            } else {
                safeThis->renderConversation();
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
