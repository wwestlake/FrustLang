#pragma once

#include <JuceHeader.h>
#include <vector>

struct ConversationBlock
{
    int index = 0;
    juce::String timestamp;
    juce::String role;
    juce::String content;
    juce::String previousHash;
    juce::String hash;
};

struct StoredConversation
{
    juce::String id;
    juce::String title;
    juce::String createdAt;
    juce::String updatedAt;
    std::vector<ConversationBlock> blocks;
};

struct ConversationSummary
{
    juce::String id;
    juce::String title;
    juce::String updatedAt;
    bool integrityValid = false;
};

class ConversationStore
{
public:
    explicit ConversationStore(juce::ApplicationProperties* properties,
                               juce::File conversationFolderOverride = {},
                               juce::File archiveFolderOverride = {});

    StoredConversation createConversation() const;
    bool append(StoredConversation& conversation,
                const juce::String& role,
                const juce::String& content) const;

    std::vector<ConversationSummary> listConversations() const;
    bool load(const juce::String& id, StoredConversation& conversation, juce::String& error) const;
    bool save(const StoredConversation& conversation, juce::String& error) const;
    bool archive(const StoredConversation& conversation, juce::String& error) const;
    bool verify(const StoredConversation& conversation, juce::String& error) const;

    juce::File getConversationFolder() const;
    juce::File getArchiveFolder() const;
    void setConversationFolder(const juce::File& folder);
    void setArchiveFolder(const juce::File& folder);

private:
    static juce::String now();
    static juce::String calculateHash(const StoredConversation& conversation,
                                      const ConversationBlock& block);
    static juce::String makeTitle(const juce::String& content);
    static bool parseConversation(const juce::File& file,
                                  StoredConversation& conversation,
                                  juce::String& error);
    static juce::var serialiseConversation(const StoredConversation& conversation);

    juce::ApplicationProperties* appProperties = nullptr;
    juce::File conversationFolderOverride;
    juce::File archiveFolderOverride;
};
