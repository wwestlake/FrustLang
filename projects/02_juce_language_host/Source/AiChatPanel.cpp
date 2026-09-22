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

enum class AgentMode
{
    automatic = 1,
    plan = 2,
    execute = 3,
    review = 4
};

juce::String modeName(AgentMode mode)
{
    if (mode == AgentMode::plan) return "plan";
    if (mode == AgentMode::execute) return "execute";
    if (mode == AgentMode::review) return "review";
    return "auto";
}

bool isPlanContinuation(const juce::String& request)
{
    const auto text = request.trim().toLowerCase();
    return text == "do it" || text == "go ahead" || text == "proceed"
        || text == "continue" || text.contains("execute the plan")
        || text.contains("implement the plan");
}

MutationIntent mutationIntentFor(const juce::String& request)
{
    const auto text = request.trim().toLowerCase();
    const bool directRequest = text.startsWith("please ") || text.startsWith("can you ")
        || text.startsWith("could you ") || text.startsWith("would you ")
        || text.startsWith("i want you to ") || text.startsWith("let's ")
        || text.startsWith("lets ");
    const bool statusStatement = text.startsWith("we are ") || text.startsWith("we're ")
        || text.startsWith("i am ") || text.startsWith("i'm ")
        || text.startsWith("they are ") || text.startsWith("they're ")
        || text.startsWith("production is ") || text.startsWith("it is being ");
    if (statusStatement && !directRequest)
        return MutationIntent::none;

    juce::StringArray words;
    words.addTokens(text, " \t\r\n.,!?;:()[]{}<>+-=*/\\|&^%\"'", "");
    const auto hasWord = [&words](const juce::String& word) { return words.contains(word); };
    const bool action = hasWord("create") || hasWord("write") || hasWord("rewrite")
        || hasWord("update") || hasWord("edit") || hasWord("change") || hasWord("add")
        || hasWord("implement") || hasWord("fix") || hasWord("make") || hasWord("rename")
        || hasWord("remove") || hasWord("delete");
    if (!action) return MutationIntent::none;

    const bool directoryOnly = (text.contains("folder") || text.contains("directory"))
        && !text.contains("file") && !text.contains("code") && !text.contains("project")
        && !text.contains("source") && !text.contains("document");
    return directoryOnly ? MutationIntent::anyWrite : MutationIntent::fileWrite;
}

bool isWriteTool(const std::string& name)
{
    return name == "workspace_create_directory" || name == "workspace_create_file"
        || name == "workspace_write_file" || name == "workspace_replace_text";
}

bool requiresFrustVerification(const juce::String& request)
{
    const auto text = request.toLowerCase();
    if (text.contains("documentation") || text.contains("readme") || text.contains("specification"))
        return false;
    return text.contains("code") || text.contains("frust") || text.contains(".fr")
        || text.contains("function") || text.contains("compile") || text.contains("command")
        || text.contains("game loop") || text.contains("source") || text.contains("project");
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

    modeBox.addItem("Auto", static_cast<int>(AgentMode::automatic));
    modeBox.addItem("Plan", static_cast<int>(AgentMode::plan));
    modeBox.addItem("Execute", static_cast<int>(AgentMode::execute));
    modeBox.addItem("Review", static_cast<int>(AgentMode::review));
    modeBox.setTooltip("Agent workflow mode; access is controlled separately");
    const auto savedMode = appProperties != nullptr
        ? appProperties->getUserSettings()->getIntValue("aiAssistantMode", 1) : 1;
    modeBox.setSelectedId(juce::jlimit(1, 4, savedMode), juce::dontSendNotification);
    modeBox.onChange = [this] {
        if (appProperties == nullptr) return;
        appProperties->getUserSettings()->setValue("aiAssistantMode", modeBox.getSelectedId());
        appProperties->getUserSettings()->saveIfNeeded();
    };
    addAndMakeVisible(modeBox);

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

    taskStatusLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    taskStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8fded7));
    taskStatusLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff202a2d));
    taskStatusLabel.setBorderSize(juce::BorderSize<int>(0, 6, 0, 6));
    addAndMakeVisible(taskStatusLabel);

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

    auto taskBar = bounds.removeFromTop(22);
    modeBox.setBounds(taskBar.removeFromLeft(92));
    taskBar.removeFromLeft(4);
    taskStatusLabel.setBounds(taskBar);
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

bool AiChatPanel::submitExternalMessage(const juce::String& content,
                                        ExternalCompletion completion)
{
    if (requestInFlight || inputBox.getText().trim().isNotEmpty() || content.trim().isEmpty())
        return false;

    externalCompletion = std::move(completion);
    inputBox.setText(content, false);
    sendMessage();
    return true;
}

void AiChatPanel::completeExternalRequest(bool ok, const juce::String& response)
{
    if (!externalCompletion) return;
    auto completion = std::move(externalCompletion);
    externalCompletion = {};
    completion(ok, response);
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
    refreshTaskStatus();
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
    taskStatusLabel.setText("No active task", juce::dontSendNotification);
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
    modeBox.setEnabled(!requestInFlight);
    modelBox.setEnabled(!requestInFlight && !modelRequestInFlight && modelBox.getNumItems() > 0);
    sendButton.setEnabled(!requestInFlight);
}

void AiChatPanel::refreshTaskStatus()
{
    AgentTask task;
    if (AgentTask::load(conversationStore.getConversationFolder(), currentConversation.id, task))
        taskStatusLabel.setText(task.statusLine(), juce::dontSendNotification);
    else
        taskStatusLabel.setText("No active task", juce::dontSendNotification);
}

void AiChatPanel::sendMessage()
{
    if (requestInFlight) return;

    auto profileName = profileBox.getText();
    if (profileName.isEmpty()) {
        const juce::String message = "No AI profile selected - add one in AI Settings first.";
        appendTranscript("system", message);
        completeExternalRequest(false, message);
        return;
    }

    auto userText = inputBox.getText().trim();
    if (userText.isEmpty()) return;

    auto provider = aiConfig.createProvider(profileName.toStdString());
    if (!provider) {
        const auto message = "Could not create a provider for '" + profileName + "'.";
        appendTranscript("system", message);
        completeExternalRequest(false, message);
        return;
    }

    if (!appendAndSave("user", userText))
    {
        completeExternalRequest(false, "The message could not be saved.");
        return;
    }
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
    const auto projectRoot = getProjectRoot ? getProjectRoot() : juce::File();
    const auto access = accessBox.getSelectedId() == 2
        ? EngineerTools::AccessLevel::workspace : EngineerTools::AccessLevel::observe;
    const auto selectedMode = static_cast<AgentMode>(modeBox.getSelectedId());
    const auto requestedMutation = mutationIntentFor(userText);
    const bool continuation = selectedMode == AgentMode::execute && isPlanContinuation(userText);
    const bool executeRequested = (selectedMode == AgentMode::automatic
                                    && requestedMutation != MutationIntent::none)
        || (selectedMode == AgentMode::execute
            && (requestedMutation != MutationIntent::none || continuation));
    const auto effectiveAccess = executeRequested
        ? access : EngineerTools::AccessLevel::observe;
    EngineerTools engineerTools(projectRoot, effectiveAccess);
    auto toolDefinitions = engineerTools.definitions();
    if (!historySnapshot.empty())
    {
        historySnapshot.front().content +=
            "\n\nYou have real FrustIDE engineering tools for the project currently open in the "
            "Project Explorer. Use them whenever the user asks you to inspect, create, or edit project "
            "content. Never claim that you cannot create files or projects when the corresponding tool "
            "is available. The host-owned task packet is authoritative for the current goal. A new user "
            "request starts or steers work; a saved plan is resumed only when the host explicitly places "
            "it in that packet. Inspect the current project before any "
            "write. Treat the open root as the current project: when its folder name matches the requested "
            "project, initialize files directly in that root rather than creating a duplicate named folder. "
            "Never present proposed code as though it was written; report only paths confirmed by tool results. "
            "When tools write code or documentation into project files, keep the completion summary concise and "
            "do not repeat the file contents or code in chat. The IDE preserves detailed tool activity in a "
            "collapsed disclosure section. "
            "Current project root: " + projectRoot.getFullPathName().toStdString()
            + ". Current mode: " + modeName(selectedMode).toStdString()
            + ". Current access ceiling: " + EngineerTools::accessName(access).toStdString() + ".";
    }
    if ((executeRequested || selectedMode == AgentMode::plan || selectedMode == AgentMode::review)
        && !projectRoot.isDirectory())
    {
        const juce::String message = "This mode needs an open project folder. Open the target folder in "
            "Project Explorer, then send the request again.";
        appendAndSave("assistant", message);
        renderConversation();
        requestInFlight = false;
        updateConversationControls();
        completeExternalRequest(false, message);
        return;
    }
    if (executeRequested && access != EngineerTools::AccessLevel::workspace)
    {
        const juce::String message = "Execute work is blocked because access is set to Observe. "
            "Change the access control to Workspace when you want the agent to edit the open project.";
        appendAndSave("assistant", message);
        renderConversation();
        requestInFlight = false;
        updateConversationControls();
        completeExternalRequest(false, message);
        return;
    }

    const bool agentRun = projectRoot.isDirectory()
        && (executeRequested || selectedMode == AgentMode::plan || selectedMode == AgentMode::review);
    AgentTask task;
    const auto conversationFolder = conversationStore.getConversationFolder();
    if (agentRun)
    {
        juce::String goal = userText;
        juce::StringArray initialPlan;
        AgentTask previous;
        if (continuation && AgentTask::load(conversationFolder, currentConversation.id, previous)
            && previous.isCompleted() && previous.taskMode() == "plan")
        {
            goal = previous.taskGoal();
            initialPlan = previous.planSteps();
        }
        const bool planRequired = selectedMode != AgentMode::review;
        const bool writeRequired = executeRequested;
        task = AgentTask::begin(currentConversation.id, goal,
                                executeRequested ? "execute" : modeName(selectedMode),
                                planRequired, writeRequired,
                                writeRequired && requiresFrustVerification(goal), initialPlan);
        const auto controlTools = task.controlDefinitions();
        toolDefinitions.insert(toolDefinitions.end(), controlTools.begin(), controlTools.end());
        historySnapshot.push_back({ "system", task.contextMessage().toStdString() });
        task.save(conversationFolder);
        taskStatusLabel.setText(task.statusLine(), juce::dontSendNotification);
    }

    auto* providerPtr = provider.release();

    std::thread([safeThis, historySnapshot, providerPtr, engineerTools, toolDefinitions,
                 agentRun, task, conversationFolder] () mutable {
        std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
        auto workingHistory = historySnapshot;
        ai_provider::ChatResponse response;
        bool workspaceChanged = false;
        juce::StringArray activity;
        constexpr int maximumToolRounds = 24;
        for (int round = 0; round < maximumToolRounds; ++round)
        {
            response = owned->sendChat(
                workingHistory,
                toolDefinitions,
                agentRun ? ai_provider::ToolChoice::required
                         : ai_provider::ToolChoice::autoSelect);
            if (!response.ok)
                break;

            if (response.toolCalls.empty())
            {
                if (agentRun && response.hostedToolUsed)
                {
                    workingHistory.push_back(
                        { "assistant", response.content, {}, {}, response.providerItemsJson });
                    workingHistory.push_back({ "system",
                        (task.contextMessage() + "\nContinue the assigned task after using web search. "
                         "Use the project and task-control tools; do not stop at a prose answer.")
                            .toStdString() });
                    continue;
                }
                break;
            }

            workingHistory.push_back(
                { "assistant", response.content, response.toolCalls, {}, response.providerItemsJson });
            for (const auto& call : response.toolCalls)
            {
                EngineerTools::Result result;
                auto control = agentRun ? task.executeControl(call) : AgentTask::ControlResult {};
                if (control.handled)
                {
                    result = { control.ok, false, control.message };
                }
                else if (agentRun && isWriteTool(call.name) && !task.canWrite())
                {
                    result = { false, false,
                        "Error: The host requires a successful project inspection and an explicit "
                        "agent_set_plan call before any project write." };
                }
                else
                {
                    result = engineerTools.execute(call);
                    if (agentRun) task.recordEngineerResult(call.name, result);
                }
                workspaceChanged = workspaceChanged || result.workspaceChanged;
                const auto summary = result.message.upToFirstOccurrenceOf("\n", false, false);
                activity.add("- `" + juce::String(call.name) + "`: " + summary);
                const auto taskLine = agentRun ? task.statusLine() : juce::String();
                juce::MessageManager::callAsync([safeThis,
                                                  changed = result.workspaceChanged, taskLine] {
                    if (safeThis == nullptr) return;
                    if (taskLine.isNotEmpty())
                        safeThis->taskStatusLabel.setText(taskLine, juce::dontSendNotification);
                    if (changed && safeThis->onFileSystemChanged)
                        safeThis->onFileSystemChanged();
                });
                workingHistory.push_back(
                    { "tool", result.message.toStdString(), {}, call.id });
            }
            if (agentRun)
            {
                task.save(conversationFolder);
                if (task.isTerminal()) break;
                workingHistory.push_back({ "system", task.contextMessage().toStdString() });
            }
            if (round == maximumToolRounds - 1)
                response = { false, {}, "The Engineer reached the 24-round tool limit." };
        }

        if (agentRun && !task.isTerminal())
        {
            if (!response.ok)
                task.fail("Provider error: " + juce::String(response.errorMessage));
            else if (response.toolCalls.empty())
                task.fail("The model stopped without completing the assigned task.");
            else
                task.fail("The agent run ended before the host accepted completion.");
            task.save(conversationFolder);
        }

        juce::MessageManager::callAsync([safeThis, response, workspaceChanged,
                                         agentRun, task, activity] {
            if (safeThis == nullptr) return;

            if (response.ok || agentRun) {
                auto finalContent = agentRun ? task.finalMessage().toStdString()
                    : (response.content.empty() ? std::string("The requested operation completed.")
                                                : response.content);
                juce::String report;
                if (!activity.isEmpty())
                    report = "\n\n:::details Project activity ("
                        + juce::String(activity.size()) + " steps)\n"
                        + activity.joinIntoString("\n") + "\n:::";
                if (report.isNotEmpty())
                    finalContent = (juce::String(finalContent) + report).toStdString();
                safeThis->history.push_back({ "assistant", finalContent });
                safeThis->appendAndSave("assistant", juce::String(finalContent));
                safeThis->renderConversation();
                safeThis->completeExternalRequest(true, juce::String(finalContent));
                if (agentRun)
                    safeThis->taskStatusLabel.setText(task.statusLine(), juce::dontSendNotification);
            } else {
                safeThis->renderConversation();
                const auto message = "Error: " + juce::String(response.errorMessage);
                safeThis->appendTranscript("system", message);
                safeThis->completeExternalRequest(false, message);
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
