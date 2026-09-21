#pragma once

#include <JuceHeader.h>
#include <ai_provider/AiConfig.h>
#include "ConversationStore.h"
#include "ZoomableEditors.h"
#include <memory>
#include <vector>

// Chat panel talking to whichever AI profile is selected in the dropdown
// (profiles come from AiConfig, i.e. the user's ai_config.json). Seeds
// every conversation with FRUST_LANG_SPEC.md as a system message, so the
// model actually knows Frust's syntax rather than guessing at a generic
// C-like language.
class AiChatPanel : public juce::Component
{
public:
    explicit AiChatPanel(juce::ApplicationProperties* properties);
    ~AiChatPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void sendMessage();
    void appendTranscript(const juce::String& speaker, const juce::String& text);
    void refreshProfileList();
    void refreshModelList();
    void saveSelectedModel();
    void showAiSettings();
    void showAiSettingsDialog(const juce::String& profileName,
                              const juce::String& apiKey);
    void refreshConversationList(bool loadMostRecent);
    void loadConversation(const juce::String& id);
    void startNewConversation();
    void archiveCurrentConversation();
    void showFolderMenu();
    void chooseFolder(bool archiveFolder);
    void renderConversation();
    void updateCurrentConversationListEntry();
    bool appendAndSave(const juce::String& role, const juce::String& content);
    void updateConversationControls();
    static juce::String loadFrustSystemPrompt();

    juce::Label headerLabel { "Header", "AI Assistant" };
    juce::ComboBox profileBox;
    juce::ComboBox modelBox;
    juce::TextButton aiSettingsButton { "AI Settings..." };
    juce::ComboBox conversationBox;
    juce::TextButton newButton { "New" };
    juce::TextButton archiveButton { "Archive" };
    juce::TextButton foldersButton { "Folders..." };
    ZoomableTextEditor transcript;
    juce::TextEditor inputBox;
    juce::TextButton sendButton { "Send" };

    ai_provider::AiConfig aiConfig;
    ConversationStore conversationStore;
    StoredConversation currentConversation;
    std::vector<ConversationSummary> conversationSummaries;
    std::vector<ai_provider::ChatMessage> history; // includes the leading system message
    std::unique_ptr<juce::FileChooser> folderChooser;
    bool changingConversationSelection = false;
    bool changingModelSelection = false;
    bool modelRequestInFlight = false;
    unsigned int modelRequestGeneration = 0;
    float transcriptScale = 1.0f;

    // Guards against overlapping requests; the network call runs on a
    // background std::thread and marshals its result back via
    // MessageManager::callAsync, since JUCE UI is message-thread-only.
    bool requestInFlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AiChatPanel)
};
