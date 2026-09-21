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

enum class MutationIntent
{
    none,
    anyWrite,
    fileWrite
};

MutationIntent mutationIntentFor(const juce::String& request)
{
    const auto text = request.toLowerCase();
    const bool action = text.contains("create") || text.contains("write")
        || text.contains("update") || text.contains("edit") || text.contains("change")
        || text.contains("add") || text.contains("implement") || text.contains("fix")
        || text.contains("make") || text.contains("rename");
    if (!action) return MutationIntent::none;

    const bool directoryOnly = (text.contains("folder") || text.contains("directory"))
        && !text.contains("file") && !text.contains("code") && !text.contains("project")
        && !text.contains("source") && !text.contains("document");
    return directoryOnly ? MutationIntent::anyWrite : MutationIntent::fileWrite;
}

bool isInspectionTool(const std::string& name)
{
    return name == "workspace_list" || name == "workspace_read" || name == "workspace_search";
}

bool isWriteTool(const std::string& name)
{
    return name == "workspace_create_directory" || name == "workspace_create_file"
        || name == "workspace_replace_text";
}

bool isFileWriteTool(const std::string& name)
{
    return name == "workspace_create_file" || name == "workspace_replace_text";
}
}

AiChatPanel::AiChatPanel(juce::ApplicationProperties* properties)
    : aiConfig(getConfigFile().getFullPathName().toStdString()),
      appProperties(properties),
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

    accessBox.addItem("Observe", 1);
    accessBox.addItem("Workspace", 2);
    accessBox.setTooltip("Maximum access available to the AI Assistant");
    const auto savedAccess = appProperties != nullptr
        ? appProperties->getUserSettings()->getIntValue("aiAssistantAccess", 2) : 2;
    accessBox.setSelectedId(savedAccess == 1 ? 1 : 2, juce::dontSendNotification);
    accessBox.onChange = [this] {
        if (appProperties == nullptr) return;
        appProperties->getUserSettings()->setValue("aiAssistantAccess", accessBox.getSelectedId());
        appProperties->getUserSettings()->saveIfNeeded();
    };
    addAndMakeVisible(accessBox);

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

    transcript.onScaleChanged = [this](float scale) {
        inputBox.setFont(juce::Font("Consolas", 13.0f * scale, juce::Font::plain));
    };
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
    accessBox.setBounds(topBar.removeFromRight(100));
    topBar.removeFromRight(4);
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
    transcript.appendMessage(speaker, text);
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
        transcript.setMessages({ { "system", "Conversation integrity check failed.\n\n" + error } });
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
    std::vector<AiConversationView::Message> messages;
    messages.reserve(currentConversation.blocks.size());
    for (const auto& block : currentConversation.blocks)
    {
        history.push_back({ block.role.toStdString(), block.content.toStdString() });
        messages.push_back({ block.role, block.content });
    }
    transcript.setMessages(std::move(messages));
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
    accessBox.setEnabled(!requestInFlight);
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

    const auto projectRoot = getProjectRoot ? getProjectRoot() : juce::File();
    const auto access = accessBox.getSelectedId() == 2
        ? EngineerTools::AccessLevel::workspace : EngineerTools::AccessLevel::observe;
    EngineerTools engineerTools(projectRoot, access);
    auto toolDefinitions = projectRoot.isDirectory()
        ? engineerTools.definitions() : std::vector<ai_provider::ToolDefinition> {};
    if (!historySnapshot.empty())
    {
        historySnapshot.front().content +=
            "\n\nYou have real FrustIDE engineering tools for the project currently open in the "
            "Project Explorer. Use them whenever the user asks you to inspect, create, or edit project "
            "content. Never claim that you cannot create files or projects when the corresponding tool "
            "is available. Treat the newest user message as the only current request; do not resume an "
            "older request unless the newest message asks you to. Inspect the current project before any "
            "write. Treat the open root as the current project: when its folder name matches the requested "
            "project, initialize files directly in that root rather than creating a duplicate named folder. "
            "Never present proposed code as though it was written; report only paths confirmed by tool results. "
            "Current project root: " + projectRoot.getFullPathName().toStdString()
            + ". Current access: " + EngineerTools::accessName(access).toStdString() + ".";
    }
    const auto mutationIntent = access == EngineerTools::AccessLevel::workspace
        ? mutationIntentFor(userText) : MutationIntent::none;

    std::thread([safeThis, historySnapshot, providerPtr, engineerTools, toolDefinitions,
                 mutationIntent] () mutable {
        std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
        auto workingHistory = historySnapshot;
        ai_provider::ChatResponse response;
        bool workspaceChanged = false;
        bool inspectedProject = false;
        bool successfulWrite = false;
        bool successfulFileWrite = false;
        juce::StringArray activity;
        constexpr int maximumToolRounds = 12;
        for (int round = 0; round < maximumToolRounds; ++round)
        {
            const bool stillNeedsWrite = mutationIntent == MutationIntent::fileWrite
                ? !successfulFileWrite
                : mutationIntent == MutationIntent::anyWrite && !successfulWrite;
            response = owned->sendChat(
                workingHistory,
                toolDefinitions,
                stillNeedsWrite ? ai_provider::ToolChoice::required
                                : ai_provider::ToolChoice::autoSelect);
            if (!response.ok || response.toolCalls.empty())
                break;

            workingHistory.push_back(
                { "assistant", response.content, response.toolCalls, {} });
            for (const auto& call : response.toolCalls)
            {
                auto result = isWriteTool(call.name) && !inspectedProject
                    ? EngineerTools::Result {
                        false,
                        false,
                        "Error: Inspect the current project with workspace_list, workspace_search, or "
                        "workspace_read before writing so the target path is grounded."
                    }
                    : engineerTools.execute(call);
                if (result.ok && isInspectionTool(call.name))
                    inspectedProject = true;
                if (result.ok && isWriteTool(call.name))
                    successfulWrite = true;
                if (result.ok && isFileWriteTool(call.name))
                    successfulFileWrite = true;
                workspaceChanged = workspaceChanged || result.workspaceChanged;
                const auto summary = result.message.upToFirstOccurrenceOf("\n", false, false);
                activity.add("- `" + juce::String(call.name) + "`: " + summary);
                juce::MessageManager::callAsync([safeThis, callName = juce::String(call.name),
                                                  summary, changed = result.workspaceChanged] {
                    if (safeThis == nullptr) return;
                    safeThis->appendTranscript("tool", "`" + callName + "`: " + summary);
                    if (changed && safeThis->onFileSystemChanged)
                        safeThis->onFileSystemChanged();
                });
                workingHistory.push_back(
                    { "tool", result.message.toStdString(), {}, call.id });
            }
            if (round == maximumToolRounds - 1)
                response = { false, {}, "The Engineer reached the 12-round tool limit." };
        }

        const bool mutationSatisfied = mutationIntent == MutationIntent::none
            || (mutationIntent == MutationIntent::fileWrite ? successfulFileWrite : successfulWrite);
        juce::MessageManager::callAsync([safeThis, response, workspaceChanged, mutationIntent,
                                         mutationSatisfied, activity] {
            if (safeThis == nullptr) return;

            if (response.ok) {
                auto finalContent = response.content.empty()
                    ? std::string("The requested project operation completed.") : response.content;
                juce::String report;
                if (!activity.isEmpty())
                    report = "**Project activity**\n\n" + activity.joinIntoString("\n") + "\n\n";
                if (mutationIntent != MutationIntent::none && !mutationSatisfied)
                    report << "**No project files were changed.**\n\n";
                if (report.isNotEmpty())
                    finalContent = (report + juce::String(finalContent)).toStdString();
                safeThis->history.push_back({ "assistant", finalContent });
                safeThis->appendAndSave("assistant", juce::String(finalContent));
                safeThis->renderConversation();
            } else {
                safeThis->renderConversation();
                safeThis->appendTranscript("system", "Error: " + juce::String(response.errorMessage));
            }

            safeThis->transcript.scrollToBottom();
            if (workspaceChanged && safeThis->onFileSystemChanged)
                safeThis->onFileSystemChanged();
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
