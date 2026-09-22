#include "AiChatPanel.h"
#include "RAGQuery.h"
#include <ai_provider/OpenAiProvider.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
juce::File getConfigFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LagDaemonResearchIDE")
        .getChildFile("ai_config.json");
}

bool isPlanContinuation(const juce::String& request)
{
    const auto text = request.trim().toLowerCase();
    return text == "do it" || text == "go ahead" || text == "proceed"
        || text == "continue" || text.contains("execute the plan")
        || text.contains("implement the plan");
}

// Shows a card and waits for the user's answer, from the assistant's worker thread. Returns the button pressed and the comment,
// or -1 when the run was stopped, the panel is gone, or nobody answered in thirty minutes (the card is then taken away).
std::pair<int, juce::String> askWithCard(juce::Component::SafePointer<AiChatPanel> panel, const ActionCard::Request& request,
                                         const std::function<bool()>& shouldStop)
{
    struct Wait
    {
        std::mutex mutex;
        std::condition_variable answered;
        bool done = false;
        int button = -1;
        juce::String comment;
    };
    auto wait = std::make_shared<Wait>();

    juce::MessageManager::callAsync([panel, request, wait] {
        if (panel == nullptr)
        {
            std::lock_guard<std::mutex> lock(wait->mutex);
            wait->done = true;
            wait->answered.notify_all();
            return;
        }
        panel->showCard(request, [panel, wait](int button, const juce::String& comment) {
            {
                std::lock_guard<std::mutex> lock(wait->mutex);
                wait->done = true;
                wait->button = button;
                wait->comment = comment;
                wait->answered.notify_all();
            }
            if (panel != nullptr)
                panel->closeCard();
        });
    });

    const auto giveUpAt = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    std::unique_lock<std::mutex> lock(wait->mutex);
    while (!wait->done)
    {
        if ((shouldStop && shouldStop()) || std::chrono::steady_clock::now() > giveUpAt)
        {
            juce::MessageManager::callAsync([panel] { if (panel != nullptr) panel->closeCard(); });
            return { -1, {} };
        }
        wait->answered.wait_for(lock, std::chrono::milliseconds(200));
    }
    return { wait->button, wait->comment };
}

// Whether a command may run: a card with Run once, Always allow (when it can be offered) and Don't run.
command_tool::Approval askUserToRunCommand(juce::Component::SafePointer<AiChatPanel> panel,
                                           const command_tool::ApprovalRequest& request,
                                           std::function<bool()> shouldStop)
{
    ActionCard::Request card;
    const bool opens = request.whyAsked.startsWith("It opens a program");
    card.title = opens ? "Open this program?" : "Run this command?";
    card.body << request.command << "\n\nin: " << request.workingDirectory;
    if (request.reason.isNotEmpty())
        card.body << "\nwhy: " << request.reason;
    if (request.whyAsked.isNotEmpty() && !opens)
        card.body << "\n(asked because: " << request.whyAsked << ")";
    card.buttons.add(opens ? "Open it" : "Run once");
    const bool offerAlways = request.rulePrefix.isNotEmpty();
    if (offerAlways)
        card.buttons.add("Always allow \"" + request.rulePrefix + "\" here");
    card.buttons.add(opens ? "Don't open" : "Don't run");
    card.accent = juce::Colour(0xffe0a040);

    const auto [button, comment] = askWithCard(panel, card, shouldStop);
    juce::ignoreUnused(comment);
    if (button == 0) return command_tool::Approval::once;
    if (offerAlways && button == 1) return command_tool::Approval::always;
    return command_tool::Approval::deny;
}

EngineerTools::ExternalReadDecision askUserToReadExternal(
    juce::Component::SafePointer<AiChatPanel> panel,
    const juce::File& requested,
    std::function<bool()> shouldStop)
{
    const auto folder = requested.isDirectory() ? requested : requested.getParentDirectory();
    ActionCard::Request card;
    card.title = "Open external reference?";
    card.body = "The Engineer wants to read:\n" + requested.getFullPathName()
        + "\n\nThis location is outside the open workspace. Allow one read, or open the folder as a read-only reference tree.";
    card.buttons = { "Allow once", "Open read-only", "Deny" };
    card.accent = juce::Colour(0xff4ea1ff);

    const auto [button, comment] = askWithCard(panel, card, std::move(shouldStop));
    juce::ignoreUnused(comment);
    if (button == 0) return EngineerTools::ExternalReadDecision::allowOnce;
    if (button == 1)
    {
        juce::MessageManager::callAsync([panel, folder] {
            if (panel != nullptr && panel->openReadOnlyRoot)
                panel->openReadOnlyRoot(folder);
        });
        return EngineerTools::ExternalReadDecision::openReadOnly;
    }
    return EngineerTools::ExternalReadDecision::deny;
}

// A program the Engineer opened for the user to try: a card with what to check, a comment box, Pass and Fail.
command_tool::TestVerdict askUserToTest(juce::Component::SafePointer<AiChatPanel> panel, const command_tool::TestRequest& request,
                                        std::function<bool()> shouldStop)
{
    ActionCard::Request card;
    card.title = "Test this program";
    card.body << "It is open in its own window: " << request.command << "\n\nPlease check:\n" << request.instructions;
    card.buttons = { "Pass", "Fail" };
    card.wantsComment = true;
    card.commentHint = "What happened? (especially on Fail)";
    card.accent = juce::Colour(0xff4ec27a);

    const auto [button, comment] = askWithCard(panel, card, shouldStop);
    command_tool::TestVerdict verdict;
    verdict.comment = comment;
    verdict.outcome = button == 0 ? command_tool::TestVerdict::Outcome::pass
                    : button == 1 ? command_tool::TestVerdict::Outcome::fail
                                  : command_tool::TestVerdict::Outcome::noAnswer;
    return verdict;
}

// What a tool call is doing, in a few words for the live status ("Reading src/main.cpp").
juce::String describeCall(const ai_provider::ToolCall& call)
{
    const auto arguments = juce::JSON::parse(juce::String(call.argumentsJson));
    auto arg = [&arguments](const char* name) {
        auto text = arguments.getProperty(name, {}).toString();
        return text.length() > 80 ? text.substring(0, 77) + "..." : text;
    };
    const juce::String name(call.name);
    if (name == "workspace_list") return "Looking at the project" + (arg("path").isNotEmpty() && arg("path") != "." ? " (" + arg("path") + ")" : juce::String());
    if (name == "workspace_read") return "Reading " + arg("path");
    if (name == "workspace_search") return "Searching the project for \"" + arg("query") + "\"";
    if (name == "registry_search") return "Checking the pod registry" + (arg("query").isNotEmpty() ? " for \"" + arg("query") + "\"" : juce::String());
    if (name == "workspace_create_directory") return "Creating the folder " + arg("path");
    if (name == "workspace_create_file") return "Creating " + arg("path");
    if (name == "workspace_write_file") return "Writing " + arg("path");
    if (name == "workspace_replace_text") return "Editing " + arg("path");
    if (name == "workspace_check_frust") return "Checking the Frust code in " + arg("path");
    if (name == "run_command") return "Running " + arg("command");
    if (name == "launch_program") return "Opening " + arg("command") + " in its own window";
    if (name == "stop_program") return "Closing a program it opened";
    if (name == "user_test") return "Opening " + arg("command") + " for you to test";
    if (name == "agent_assess_capabilities") return "Checking required capabilities";
    if (name == "agent_set_plan")
    {
        const auto* steps = arguments.getProperty("steps", {}).getArray();
        return "Planning (" + juce::String(steps != nullptr ? steps->size() : 0) + " steps)";
    }
    if (name == "agent_complete_task") return "Finishing up";
    if (name == "agent_request_user") return "Preparing a question for you";
    return "Using " + name;
}

// Shows the live status in the reply, from any thread.
void postLiveStatus(juce::Component::SafePointer<AiChatPanel> panel, const std::shared_ptr<AiChatPanel::RunControl>& run,
                    const juce::String& status, const juce::String& finishedStep = {})
{
    juce::StringArray steps;
    {
        std::lock_guard<std::mutex> lock(run->mutex);
        if (status.isNotEmpty())
            run->status = status;
        if (finishedStep.isNotEmpty())
        {
            run->steps.add(finishedStep);
            while (run->steps.size() > 12)
                run->steps.remove(0);
        }
        steps = run->steps;
    }
    juce::MessageManager::callAsync([panel, run, steps] {
        if (panel == nullptr)
            return;
        juce::String shown;
        {
            std::lock_guard<std::mutex> lock(run->mutex);
            shown = run->status;
        }
        panel->showLiveStatus(shown, steps);
    });
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
    accessBox.addItem("Full Access", 3);
    accessBox.setTooltip("Maximum access available to the AI Assistant");
    const auto savedAccess = appProperties != nullptr
        ? appProperties->getUserSettings()->getIntValue("aiAssistantAccess", 2) : 2;
    accessBox.setSelectedId(juce::jlimit(1, 3, savedAccess), juce::dontSendNotification);
    accessBox.onChange = [this] {
        if (accessBox.getSelectedId() == 3)
            frusty.showMood(FrustyComponent::Mood::fullAccess, 1400);
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

    outputBox.addItem("Brief", 1);
    outputBox.addItem("Standard", 2);
    outputBox.addItem("Detailed", 3);
    outputBox.setTooltip("How much detail the assistant includes in its visible response");
    const auto savedOutput = appProperties != nullptr
        ? appProperties->getUserSettings()->getIntValue("aiAssistantOutputDetail", 2) : 2;
    outputBox.setSelectedId(juce::jlimit(1, 3, savedOutput), juce::dontSendNotification);
    outputBox.onChange = [this] {
        if (appProperties == nullptr) return;
        appProperties->getUserSettings()->setValue("aiAssistantOutputDetail", outputBox.getSelectedId());
        appProperties->getUserSettings()->saveIfNeeded();
    };
    addAndMakeVisible(outputBox);

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

    sendButton.onClick = [this] {
        if (requestInFlight)
        {
            requestStop("Stopped by the user.");
            sendButton.setEnabled(false);
            if (runControl != nullptr)
                postLiveStatus(juce::Component::SafePointer<AiChatPanel>(this), runControl, "Stopping...");
            return;
        }
        sendMessage();
    };
    addAndMakeVisible(sendButton);

    addAndMakeVisible(frusty);

    refreshProfileList();
    refreshConversationList(true);
}

bool AiChatPanel::requestStop(const juce::String& reason)
{
    if (!requestInFlight || runControl == nullptr) return false;
    {
        std::lock_guard lock(runControl->mutex);
        runControl->stopReason = reason.isNotEmpty() ? reason : "Stopped by the user.";
    }
    runControl->stop = true;
    postLiveStatus(juce::Component::SafePointer<AiChatPanel>(this), runControl, "Stopping...");
    return true;
}

AiChatPanel::~AiChatPanel()
{
    // A run still going: tell it to stop (a running command is ended at once) and give its thread a moment to finish, so
    // nothing is left working on a panel that is gone.
    if (runControl != nullptr && runControl->running)
    {
        runControl->stop = true;
        for (int i = 0; i < 60 && runControl->running; ++i)
            juce::Thread::sleep(50);
    }
}

void AiChatPanel::showCard(ActionCard::Request request, std::function<void(int, const juce::String&)> onAnswer)
{
    frusty.showMood(FrustyComponent::Mood::approval);
    card = std::make_unique<ActionCard>(std::move(request), std::move(onAnswer));
    addAndMakeVisible(*card);
    resized();
}

void AiChatPanel::closeCard()
{
    if (card == nullptr)
        return;
    // Not deleted here: this is usually called from the card's own button. It goes on the next message loop.
    auto* old = card.release();
    old->setVisible(false);
    juce::MessageManager::callAsync([old] { delete old; });
    frusty.showMood(requestInFlight ? FrustyComponent::Mood::planning : FrustyComponent::Mood::idle);
    resized();
}

void AiChatPanel::showLiveStatus(const juce::String& status, const juce::StringArray& steps)
{
    if (!requestInFlight)
        return;
    juce::String text = status.isNotEmpty() ? status : juce::String("Working...");
    const auto lowerStatus = text.toLowerCase();
    if (lowerStatus.contains("stopping") || lowerStatus.contains("round limit"))
        frusty.showMood(FrustyComponent::Mood::exhausted);
    else if (lowerStatus.contains("failed") || lowerStatus.contains("error"))
        frusty.showMood(FrustyComponent::Mood::compilerError);
    else if (lowerStatus.contains("running") || lowerStatus.contains("building")
             || lowerStatus.contains("creating") || lowerStatus.contains("updating"))
        frusty.showMood(FrustyComponent::Mood::working);
    else
        frusty.showMood(FrustyComponent::Mood::planning);
    if (!steps.isEmpty())
    {
        text << "\n\nSo far:";
        for (const auto& step : steps)
            text << "\n- " << step;
    }
    transcript.replaceLastMessage("assistant", text);
}

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
    outputBox.setBounds(taskBar.removeFromLeft(92));
    taskBar.removeFromLeft(4);
    taskStatusLabel.setBounds(taskBar);
    bounds.removeFromTop(4);

    auto inputArea = bounds.removeFromBottom(70);
    bounds.removeFromBottom(4);
    if (card != nullptr)
    {
        const int height = juce::jmin(card->preferredHeight(bounds.getWidth()), bounds.getHeight() / 2);
        card->setBounds(bounds.removeFromBottom(height));
        bounds.removeFromBottom(4);
    }
    transcript.setBounds(bounds);
    sendButton.setBounds(inputArea.removeFromRight(60));
    inputArea.removeFromRight(4);
    frusty.setBounds(inputArea.removeFromLeft(62));
    inputArea.removeFromLeft(4);
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

bool AiChatPanel::configureExternalSession(const juce::var& options, juce::String& error)
{
    if (requestInFlight)
    {
        error = "The AI Assistant is busy.";
        return false;
    }
    if (!options.isObject())
    {
        error = "Session options must be a JSON object.";
        return false;
    }

    auto selectNamed = [&error](juce::ComboBox& box, const juce::String& requested,
                                std::initializer_list<std::pair<const char*, int>> names,
                                const char* field) {
        if (requested.isEmpty()) return true;
        for (const auto& [name, id] : names)
            if (requested.equalsIgnoreCase(name))
            {
                box.setSelectedId(id, juce::sendNotificationSync);
                return true;
            }
        error = "Unknown " + juce::String(field) + ": " + requested;
        return false;
    };

    if (!selectNamed(modeBox, options.getProperty("mode", {}).toString(),
                     { { "auto", 1 }, { "plan", 2 }, { "execute", 3 }, { "review", 4 } }, "mode")
        || !selectNamed(accessBox, options.getProperty("access", {}).toString(),
                       { { "observe", 1 }, { "workspace", 2 }, { "full", 3 }, { "full access", 3 } }, "access")
        || !selectNamed(outputBox, options.getProperty("outputDetail", {}).toString(),
                       { { "brief", 1 }, { "standard", 2 }, { "detailed", 3 } }, "output detail"))
        return false;

    const auto requestedProfile = options.getProperty("profile", {}).toString().trim();
    if (requestedProfile.isNotEmpty())
    {
        int match = -1;
        for (int index = 0; index < profileBox.getNumItems(); ++index)
            if (profileBox.getItemText(index) == requestedProfile) { match = index; break; }
        if (match < 0)
        {
            error = "Unknown AI profile: " + requestedProfile;
            return false;
        }
        profileBox.setSelectedItemIndex(match, juce::sendNotificationSync);
    }

    const auto requestedModel = options.getProperty("model", {}).toString().trim();
    if (requestedModel.isNotEmpty())
    {
        int match = -1;
        for (int index = 0; index < modelBox.getNumItems(); ++index)
            if (modelBox.getItemText(index) == requestedModel) { match = index; break; }
        if (match < 0)
        {
            error = "Model is not available in the selected profile: " + requestedModel;
            return false;
        }
        modelBox.setSelectedItemIndex(match, juce::sendNotificationSync);
    }

    auto configureBudget = [&options, &error](const char* name, int& target,
                                               int minimum, int maximum) {
        if (!options.hasProperty(name)) return true;
        const auto value = static_cast<int>(options.getProperty(name, 0));
        if (value < minimum || value > maximum)
        {
            error = juce::String(name) + " must be between " + juce::String(minimum)
                + " and " + juce::String(maximum) + ".";
            return false;
        }
        target = value;
        return true;
    };
    if (!configureBudget("maxProviderCalls", maxProviderCalls, 1, 500)
        || !configureBudget("maxToolCalls", maxToolCalls, 1, 2000)
        || !configureBudget("maxTotalTokens", maxTotalTokens, 1000, 10000000))
        return false;

    if (static_cast<bool>(options.getProperty("newConversation", false)))
        startNewConversation();
    return true;
}

juce::var AiChatPanel::externalSessionSnapshot() const
{
    auto* snapshot = new juce::DynamicObject();
    snapshot->setProperty("profile", profileBox.getText());
    snapshot->setProperty("model", modelBox.getText());
    snapshot->setProperty("mode", modeBox.getText().toLowerCase());
    snapshot->setProperty("access", accessBox.getText().toLowerCase());
    snapshot->setProperty("outputDetail", outputBox.getText().toLowerCase());
    snapshot->setProperty("conversationId", currentConversation.id);
    snapshot->setProperty("busy", requestInFlight);
    snapshot->setProperty("maxProviderCalls", maxProviderCalls);
    snapshot->setProperty("maxToolCalls", maxToolCalls);
    snapshot->setProperty("maxTotalTokens", maxTotalTokens);
    if (getProjectRoot)
        snapshot->setProperty("projectRoot", getProjectRoot().getFullPathName());
    AgentTask task;
    if (AgentTask::load(conversationStore.getConversationFolder(), currentConversation.id, task))
        snapshot->setProperty("task", task.evaluationSnapshot());
    return juce::var(snapshot);
}

void AiChatPanel::completeExternalRequest(bool ok, const juce::String& response)
{
    if (!externalCompletion) return;
    auto completion = std::move(externalCompletion);
    externalCompletion = {};
    completion(ok, response, externalSessionSnapshot());
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
    outputBox.setEnabled(!requestInFlight);
    modelBox.setEnabled(!requestInFlight && !modelRequestInFlight && modelBox.getNumItems() > 0);
    // While the Engineer works, Send becomes Stop.
    sendButton.setButtonText(requestInFlight ? "Stop" : "Send");
    sendButton.setTooltip(requestInFlight ? "Stop the assistant (it ends what it is running and reports where it got to)" : "Send");
    sendButton.setEnabled(true);
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

    if (!appendAndSave("user", userText))
    {
        completeExternalRequest(false, "The message could not be saved.");
        return;
    }
    history.push_back({ "user", userText.toStdString() });
    inputBox.clear();
    renderConversation();

    requestInFlight = true;
    runControl = std::make_shared<RunControl>();
    runControl->status = "Thinking...";
    updateConversationControls();
    appendTranscript("assistant", "Thinking...");
    frusty.showMood(FrustyComponent::Mood::planning);

    const auto selectedMode = static_cast<AgentMode>(modeBox.getSelectedId());
    if (selectedMode != AgentMode::automatic)
    {
        startResolvedMessage(userText, selectedMode,
                             selectedMode == AgentMode::execute && isPlanContinuation(userText), {});
        return;
    }

    auto provider = aiConfig.createProvider(profileName.toStdString());
    if (!provider)
    {
        const auto message = "Could not create a provider for '" + profileName + "'.";
        appendTranscript("system", message);
        requestInFlight = false;
        updateConversationControls();
        completeExternalRequest(false, message);
        return;
    }

    juce::String previousTaskSummary;
    AgentTask previous;
    if (AgentTask::load(conversationStore.getConversationFolder(), currentConversation.id, previous))
    {
        previousTaskSummary << "Previous mode: " << previous.taskMode()
                            << "\nPrevious status: " << (previous.isCompleted() ? "completed" : "not completed")
                            << "\nPrevious goal: " << previous.taskGoal();
        if (!previous.planSteps().isEmpty())
            previousTaskSummary << "\nSaved plan:\n" << previous.planSteps().joinIntoString("\n");
    }

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    auto* providerPtr = provider.release();
    auto run = runControl;
    std::thread([safeThis, providerPtr, userText, previousTaskSummary, run] {
        run->running = true;
        struct Finished { std::shared_ptr<RunControl> run; ~Finished() { run->running = false; } } finished { run };
        postLiveStatus(safeThis, run, "Choosing the right mode...");
        std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
        const auto response = owned->sendChat(AgentModeRouter::messagesFor(userText, previousTaskSummary));
        auto decision = response.ok
            ? AgentModeRouter::parse(juce::String(response.content)) : AgentModeDecision {};
        if (!response.ok) decision.error = juce::String(response.errorMessage);
        if (run->stop) decision = { false, AgentMode::answer, false, 0.0, {}, "Stopped by the user." };

        juce::MessageManager::callAsync([safeThis, userText, decision] {
            if (safeThis == nullptr) return;
            if (!decision.ok)
            {
                const auto message = "Auto could not determine a mode: " + decision.error
                    + " Select a mode explicitly and send the request again.";
                safeThis->appendAndSave("assistant", message);
                safeThis->renderConversation();
                safeThis->completeExternalRequest(false, message);
                safeThis->requestInFlight = false;
                safeThis->updateConversationControls();
                return;
            }
            safeThis->taskStatusLabel.setText(
                "Auto selected " + AgentModeRouter::modeName(decision.mode), juce::dontSendNotification);
            safeThis->startResolvedMessage(userText, decision.mode, decision.continuation, decision.reason);
        });
    }).detach();
}

void AiChatPanel::startResolvedMessage(const juce::String& userText, AgentMode selectedMode,
                                       bool continuation, const juce::String& routeReason)
{
    const auto profileName = profileBox.getText();
    juce::String providerIdentity = "unknown";
    for (const auto& profile : aiConfig.profiles())
        if (profile.name == profileName.toStdString())
        {
            providerIdentity = profile.provider;
            break;
        }
    const auto modelIdentity = modelBox.getText().isNotEmpty() ? modelBox.getText() : juce::String("unknown");
    auto provider = aiConfig.createProvider(profileName.toStdString());
    if (!provider)
    {
        const auto message = "Could not create a provider for '" + profileName + "'.";
        appendAndSave("assistant", message);
        renderConversation();
        completeExternalRequest(false, message);
        requestInFlight = false;
        updateConversationControls();
        return;
    }

    juce::Component::SafePointer<AiChatPanel> safeThis(this);
    auto historySnapshot = history;
    auto ragContext = rag::getContextForQuery(userText);
    if (ragContext.isNotEmpty() && !historySnapshot.empty())
        historySnapshot.back().content =
            (userText + "\n\n---\nRetrieved context for this request:\n" + ragContext).toStdString();
    const auto projectRoot = getProjectRoot ? getProjectRoot() : juce::File();
    const auto access = accessBox.getSelectedId() == 3 ? EngineerTools::AccessLevel::full
        : accessBox.getSelectedId() == 2 ? EngineerTools::AccessLevel::workspace
                                         : EngineerTools::AccessLevel::observe;
    const auto outputDetail = outputBox.getSelectedId() == 1 ? juce::String("brief")
        : outputBox.getSelectedId() == 3 ? juce::String("detailed") : juce::String("standard");
    const bool executeRequested = selectedMode == AgentMode::execute;
    EngineerTools engineerTools(projectRoot, access, executeRequested);
    const auto readOnlyRoots = getReadOnlyRoots ? getReadOnlyRoots() : std::vector<juce::File> {};
    engineerTools.setReferenceRoots(readOnlyRoots);
    {
        EngineerTools::CommandServices services;
        auto run = runControl;
        services.shouldStop = [run] { return run->stop.load(); };
        services.approve = [panel = juce::Component::SafePointer<AiChatPanel>(this), run](const command_tool::ApprovalRequest& request) {
            return askUserToRunCommand(panel, request, [run] { return run->stop.load(); });
        };
        services.askTest = [panel = juce::Component::SafePointer<AiChatPanel>(this), run](const command_tool::TestRequest& request) {
            return askUserToTest(panel, request, [run] { return run->stop.load(); });
        };
        services.approveExternalRead = [panel = juce::Component::SafePointer<AiChatPanel>(this), run](const juce::File& path) {
            return askUserToReadExternal(panel, path, [run] { return run->stop.load(); });
        };
        services.progress = [panel = juce::Component::SafePointer<AiChatPanel>(this), run](const juce::String& status) {
            postLiveStatus(panel, run, status);
        };
        services.logFolder = conversationStore.getConversationFolder().getChildFile(".agent-state").getChildFile("command-logs");
        engineerTools.setCommandServices(std::move(services));
    }
    auto toolDefinitions = engineerTools.definitions();
    if (!historySnapshot.empty())
    {
        historySnapshot.front().content +=
            "\n\nHOST-SUPPLIED RUNTIME IDENTITY (authoritative): Provider: "
            + providerIdentity.toStdString() + ". Exact model ID: " + modelIdentity.toStdString()
            + ". Host application: FrustIDE. Assigned role: Virtual Engineer. Application branding, the project "
            "name, retrieved documents, and conversational style do not change the provider or model identity. "
            "When asked what model you are, report this exact host-supplied provider and model ID; do not infer "
            "another identity from the surrounding application.\n\n"
            "You have real FrustIDE engineering tools for the project currently open in the "
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
            + ". Current mode: " + AgentModeRouter::modeName(selectedMode).toStdString()
            + ". Current access ceiling: " + EngineerTools::accessName(access).toStdString()
            + ". Visible response detail: " + outputDetail.toStdString()
            + ". Brief means state only the result and essential caveats. Standard means a balanced explanation. "
              "Detailed means include reasoning, evidence, and relevant implementation detail. "
              "Output detail changes presentation only; it does not change the task, mode, access, or approval rules.";
        if (!readOnlyRoots.empty())
        {
            historySnapshot.front().content += " Open read-only reference roots:";
            for (const auto& reference : readOnlyRoots)
                historySnapshot.front().content += " " + reference.getFullPathName().toStdString() + ";";
        }
        if (routeReason.isNotEmpty())
            historySnapshot.front().content += " Auto routing note: " + routeReason.toStdString() + ".";
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
    if (executeRequested && access == EngineerTools::AccessLevel::observe)
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
        bool resumed = false;
        if (continuation && AgentTask::load(conversationFolder, currentConversation.id, previous))
        {
            if (previous.isResumable()
                && (executeRequested || previous.taskMode() == AgentModeRouter::modeName(selectedMode)))
            {
                resumed = previous.resume();
                if (resumed) task = previous;
            }
            else if (executeRequested && previous.isCompleted() && previous.taskMode() == "plan")
            {
                resumed = previous.continuePlanAsExecution(requiresFrustVerification(previous.taskGoal()));
                if (resumed) task = previous;
            }
        }
        if (!resumed)
        {
            const bool planRequired = selectedMode != AgentMode::review;
            const bool writeRequired = executeRequested;
            task = AgentTask::begin(currentConversation.id, goal,
                                    AgentModeRouter::modeName(selectedMode),
                                    planRequired, writeRequired,
                                    writeRequired && requiresFrustVerification(goal), initialPlan);
        }
        const auto controlTools = task.controlDefinitions();
        toolDefinitions.insert(toolDefinitions.end(), controlTools.begin(), controlTools.end());
        historySnapshot.push_back({ "system", task.contextMessage().toStdString() });
        task.save(conversationFolder);
        taskStatusLabel.setText(task.statusLine(), juce::dontSendNotification);
    }

    auto* providerPtr = provider.release();

    const auto providerCallBudget = maxProviderCalls;
    const auto toolCallBudget = maxToolCalls;
    const auto tokenBudget = maxTotalTokens;
    std::thread([safeThis, historySnapshot, providerPtr, engineerTools, toolDefinitions,
                 agentRun, task, conversationFolder, run = runControl,
                 providerCallBudget, toolCallBudget, tokenBudget] () mutable {
        run->running = true;
        struct Finished { std::shared_ptr<RunControl> run; ~Finished() { run->running = false; } } finished { run };
        bool stopped = false;
        std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
        auto workingHistory = historySnapshot;
        ai_provider::ChatResponse response;
        bool workspaceChanged = false;
        juce::StringArray activity;
        for (int round = 0; !stopped; ++round)
        {
            if (run->stop)
            {
                stopped = true;
                break;
            }
            if (agentRun)
            {
                const auto reason = task.budgetExceeded(providerCallBudget, toolCallBudget, tokenBudget);
                if (reason.isNotEmpty())
                {
                    task.fail(reason);
                    task.save(conversationFolder);
                    break;
                }
            }
            postLiveStatus(safeThis, run, round == 0 ? juce::String("Thinking...")
                                                     : "Thinking about the next step (step " + juce::String(round + 1) + ")...");
            response = owned->sendChat(
                workingHistory,
                toolDefinitions,
                agentRun ? ai_provider::ToolChoice::required
                         : ai_provider::ToolChoice::autoSelect);
            if (agentRun) task.recordProviderUsage(response);
            if (agentRun)
            {
                const auto reason = task.budgetExceeded(providerCallBudget, toolCallBudget, tokenBudget);
                if (reason.isNotEmpty())
                {
                    task.fail(reason);
                    task.save(conversationFolder);
                    break;
                }
            }
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
                if (run->stop)
                {
                    stopped = true;
                    break;
                }
                const auto doing = describeCall(call);
                postLiveStatus(safeThis, run, doing);
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
                    if (agentRun)
                    {
                        task.recordEngineerResult(call.name, result);
                        const auto reason = task.budgetExceeded(
                            providerCallBudget, toolCallBudget, tokenBudget);
                        if (reason.isNotEmpty())
                            task.fail(reason);
                    }
                }
                workspaceChanged = workspaceChanged || result.workspaceChanged;
                const auto summary = result.message.upToFirstOccurrenceOf("\n", false, false);
                {
                    auto brief = summary.fromFirstOccurrenceOf("Error: ", false, false);
                    if (brief.isEmpty()) brief = summary;
                    if (brief.length() > 110) brief = brief.substring(0, 107) + "...";
                    postLiveStatus(safeThis, run, {}, result.ok ? doing : doing + ": failed (" + brief + ")");
                }
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
                if (agentRun && task.isTerminal())
                {
                    break;
                }
            }
            if (agentRun)
            {
                task.save(conversationFolder);
                if (task.isTerminal()) break;
                workingHistory.push_back({ "system", task.contextMessage().toStdString() });
            }
        }

        if (stopped)
        {
            juce::String reason;
            {
                std::lock_guard lock(run->mutex);
                reason = run->stopReason;
            }
            response = { false, {}, reason.toStdString() };
        }

        if (agentRun && !task.isTerminal())
        {
            if (stopped)
                task.fail(juce::String(response.errorMessage));
            else if (!response.ok)
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
                safeThis->frusty.showMood(agentRun && !task.isCompleted()
                    ? FrustyComponent::Mood::compilerError
                    : FrustyComponent::Mood::success, 1800);
            } else {
                safeThis->renderConversation();
                const auto message = "Error: " + juce::String(response.errorMessage);
                safeThis->appendTranscript("system", message);
                safeThis->completeExternalRequest(false, message);
                safeThis->frusty.showMood(FrustyComponent::Mood::linkerFailure, 2200);
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
