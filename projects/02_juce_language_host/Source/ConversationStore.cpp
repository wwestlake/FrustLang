#include "ConversationStore.h"

#include <juce_cryptography/juce_cryptography.h>

namespace
{
constexpr auto conversationFolderKey = "aiConversationFolder";
constexpr auto archiveFolderKey = "aiConversationArchiveFolder";
constexpr auto schemaName = "djehuti-conversation-chain";
constexpr int schemaVersion = 1;

juce::String zeroHash()
{
    return juce::String::repeatedString("0", 64);
}

void appendHashField(juce::MemoryOutputStream& stream, const juce::String& value)
{
    auto utf8 = value.toUTF8();
    const auto byteCount = static_cast<size_t>(utf8.sizeInBytes() - 1);
    stream << juce::String(byteCount) << ":";
    stream.write(utf8.getAddress(), byteCount);
}
}

ConversationStore::ConversationStore(juce::ApplicationProperties* properties,
                                     juce::File conversationOverride,
                                     juce::File archiveOverride)
    : appProperties(properties),
      conversationFolderOverride(std::move(conversationOverride)),
      archiveFolderOverride(std::move(archiveOverride))
{
}

StoredConversation ConversationStore::createConversation() const
{
    StoredConversation conversation;
    conversation.id = juce::Uuid().toString();
    conversation.title = "New conversation";
    conversation.createdAt = now();
    conversation.updatedAt = conversation.createdAt;
    return conversation;
}

bool ConversationStore::append(StoredConversation& conversation,
                               const juce::String& role,
                               const juce::String& content) const
{
    if (role != "user" && role != "assistant")
        return false;

    ConversationBlock block;
    block.index = static_cast<int>(conversation.blocks.size());
    block.timestamp = now();
    block.role = role;
    block.content = content;
    block.previousHash = conversation.blocks.empty() ? zeroHash() : conversation.blocks.back().hash;
    block.hash = calculateHash(conversation, block);
    conversation.blocks.push_back(std::move(block));
    conversation.updatedAt = conversation.blocks.back().timestamp;

    if (role == "user" && conversation.title == "New conversation")
        conversation.title = makeTitle(content);

    return true;
}

std::vector<ConversationSummary> ConversationStore::listConversations() const
{
    std::vector<ConversationSummary> summaries;
    auto folder = getConversationFolder();
    if (!folder.isDirectory())
        return summaries;

    juce::Array<juce::File> files;
    folder.findChildFiles(files, juce::File::findFiles, false, "*.json");

    for (const auto& file : files)
    {
        StoredConversation conversation;
        juce::String error;
        const auto parsed = parseConversation(file, conversation, error);
        const auto valid = parsed && verify(conversation, error);
        summaries.push_back({ parsed ? conversation.id : file.getFileNameWithoutExtension(),
                              parsed ? conversation.title : file.getFileNameWithoutExtension(),
                              parsed ? conversation.updatedAt : juce::String(),
                              valid });
    }

    std::sort(summaries.begin(), summaries.end(), [] (const auto& left, const auto& right) {
        return left.updatedAt > right.updatedAt;
    });
    return summaries;
}

bool ConversationStore::load(const juce::String& id,
                             StoredConversation& conversation,
                             juce::String& error) const
{
    const auto file = getConversationFolder().getChildFile(id + ".json");
    if (!parseConversation(file, conversation, error))
        return false;

    return verify(conversation, error);
}

bool ConversationStore::save(const StoredConversation& conversation, juce::String& error) const
{
    if (!verify(conversation, error))
        return false;

    auto folder = getConversationFolder();
    if (!folder.createDirectory())
    {
        error = "Could not create the conversation folder: " + folder.getFullPathName();
        return false;
    }

    const auto file = folder.getChildFile(conversation.id + ".json");
    const auto json = juce::JSON::toString(serialiseConversation(conversation), true);
    if (!file.replaceWithText(json))
    {
        error = "Could not write conversation: " + file.getFullPathName();
        return false;
    }

    return true;
}

bool ConversationStore::archive(const StoredConversation& conversation, juce::String& error) const
{
    if (!verify(conversation, error))
        return false;

    auto archiveFolder = getArchiveFolder();
    if (!archiveFolder.createDirectory())
    {
        error = "Could not create the archive folder: " + archiveFolder.getFullPathName();
        return false;
    }

    auto source = getConversationFolder().getChildFile(conversation.id + ".json");
    if (!source.existsAsFile() && !save(conversation, error))
        return false;

    auto destination = archiveFolder.getChildFile(conversation.id + ".json");
    if (destination.existsAsFile())
        destination = archiveFolder.getNonexistentChildFile(conversation.id, ".json", false);

    if (!source.moveFileTo(destination))
    {
        error = "Could not move the conversation to: " + destination.getFullPathName();
        return false;
    }

    return true;
}

bool ConversationStore::verify(const StoredConversation& conversation, juce::String& error) const
{
    if (conversation.id.isEmpty())
    {
        error = "Conversation has no identifier.";
        return false;
    }

    auto expectedPreviousHash = zeroHash();
    juce::String expectedTitle = "New conversation";
    for (size_t i = 0; i < conversation.blocks.size(); ++i)
    {
        const auto& block = conversation.blocks[i];
        if (block.index != static_cast<int>(i))
        {
            error = "Conversation block index " + juce::String(block.index)
                + " is out of sequence.";
            return false;
        }

        if ((block.role != "user" && block.role != "assistant") || block.timestamp.isEmpty())
        {
            error = "Conversation block " + juce::String(block.index) + " has invalid data.";
            return false;
        }

        if (block.previousHash != expectedPreviousHash)
        {
            error = "Conversation chain is broken at block " + juce::String(block.index) + ".";
            return false;
        }

        if (block.hash != calculateHash(conversation, block))
        {
            error = "Conversation block " + juce::String(block.index) + " has been altered.";
            return false;
        }

        expectedPreviousHash = block.hash;
        if (expectedTitle == "New conversation" && block.role == "user")
            expectedTitle = makeTitle(block.content);
    }

    if (conversation.title != expectedTitle)
    {
        error = "Conversation title does not match its chained content.";
        return false;
    }

    const auto expectedUpdatedAt = conversation.blocks.empty()
        ? conversation.createdAt : conversation.blocks.back().timestamp;
    if (conversation.createdAt.isEmpty() || conversation.updatedAt != expectedUpdatedAt)
    {
        error = "Conversation timestamps do not match its chained content.";
        return false;
    }

    return true;
}

juce::File ConversationStore::getConversationFolder() const
{
    if (conversationFolderOverride != juce::File())
        return conversationFolderOverride;

    auto defaultFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("LagDaemon Research IDE")
        .getChildFile("Conversations");
    if (appProperties == nullptr)
        return defaultFolder;

    auto path = appProperties->getUserSettings()->getValue(conversationFolderKey);
    return path.isNotEmpty() ? juce::File(path) : defaultFolder;
}

juce::File ConversationStore::getArchiveFolder() const
{
    if (archiveFolderOverride != juce::File())
        return archiveFolderOverride;

    auto defaultFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("LagDaemon Research IDE")
        .getChildFile("Conversation Archive");
    if (appProperties == nullptr)
        return defaultFolder;

    auto path = appProperties->getUserSettings()->getValue(archiveFolderKey);
    return path.isNotEmpty() ? juce::File(path) : defaultFolder;
}

void ConversationStore::setConversationFolder(const juce::File& folder)
{
    if (appProperties == nullptr)
        return;

    appProperties->getUserSettings()->setValue(conversationFolderKey, folder.getFullPathName());
    appProperties->getUserSettings()->saveIfNeeded();
}

void ConversationStore::setArchiveFolder(const juce::File& folder)
{
    if (appProperties == nullptr)
        return;

    appProperties->getUserSettings()->setValue(archiveFolderKey, folder.getFullPathName());
    appProperties->getUserSettings()->saveIfNeeded();
}

juce::String ConversationStore::now()
{
    return juce::Time::getCurrentTime().toISO8601(true);
}

juce::String ConversationStore::calculateHash(const StoredConversation& conversation,
                                               const ConversationBlock& block)
{
    juce::MemoryOutputStream canonical;
    appendHashField(canonical, schemaName);
    appendHashField(canonical, juce::String(schemaVersion));
    appendHashField(canonical, conversation.id);
    appendHashField(canonical, conversation.createdAt);
    appendHashField(canonical, juce::String(block.index));
    appendHashField(canonical, block.timestamp);
    appendHashField(canonical, block.role);
    appendHashField(canonical, block.content);
    appendHashField(canonical, block.previousHash);

    return juce::SHA256(canonical.getData(), canonical.getDataSize()).toHexString();
}

juce::String ConversationStore::makeTitle(const juce::String& content)
{
    auto title = content.replaceCharacters("\r\n\t", "   ").trim();
    while (title.contains("  "))
        title = title.replace("  ", " ");

    if (title.length() > 60)
        title = title.substring(0, 57).trimEnd() + "...";
    return title.isNotEmpty() ? title : juce::String("New conversation");
}

bool ConversationStore::parseConversation(const juce::File& file,
                                           StoredConversation& conversation,
                                           juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Conversation file does not exist: " + file.getFullPathName();
        return false;
    }

    juce::var root;
    auto parseResult = juce::JSON::parse(file.loadFileAsString(), root);
    if (parseResult.failed())
    {
        error = "Invalid conversation JSON: " + parseResult.getErrorMessage();
        return false;
    }

    auto* object = root.getDynamicObject();
    if (object == nullptr
        || object->getProperty("schema").toString() != schemaName
        || static_cast<int>(object->getProperty("schemaVersion")) != schemaVersion)
    {
        error = "Unsupported conversation schema.";
        return false;
    }

    conversation = {};
    conversation.id = object->getProperty("id").toString();
    conversation.title = object->getProperty("title").toString();
    conversation.createdAt = object->getProperty("createdAt").toString();
    conversation.updatedAt = object->getProperty("updatedAt").toString();

    auto* integrity = object->getProperty("integrity").getDynamicObject();
    if (integrity == nullptr || integrity->getProperty("algorithm").toString() != "SHA-256")
    {
        error = "Conversation has invalid integrity metadata.";
        return false;
    }

    const auto declaredBlockCount = static_cast<int>(integrity->getProperty("blockCount"));

    auto* blocks = object->getProperty("blocks").getArray();
    if (blocks == nullptr)
    {
        error = "Conversation has no block array.";
        return false;
    }

    for (const auto& value : *blocks)
    {
        auto* blockObject = value.getDynamicObject();
        if (blockObject == nullptr)
        {
            error = "Conversation contains an invalid block.";
            return false;
        }

        ConversationBlock block;
        block.index = static_cast<int>(blockObject->getProperty("index"));
        block.timestamp = blockObject->getProperty("timestamp").toString();
        block.role = blockObject->getProperty("role").toString();
        block.content = blockObject->getProperty("content").toString();
        block.previousHash = blockObject->getProperty("previousHash").toString();
        block.hash = blockObject->getProperty("hash").toString();
        conversation.blocks.push_back(std::move(block));
    }

    const auto expectedHeadHash = conversation.blocks.empty()
        ? zeroHash() : conversation.blocks.back().hash;
    if (declaredBlockCount != static_cast<int>(conversation.blocks.size())
        || integrity->getProperty("headHash").toString() != expectedHeadHash)
    {
        error = "Conversation head hash does not match its block chain.";
        return false;
    }

    return true;
}

juce::var ConversationStore::serialiseConversation(const StoredConversation& conversation)
{
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty("schema", schemaName);
    root->setProperty("schemaVersion", schemaVersion);
    root->setProperty("id", conversation.id);
    root->setProperty("title", conversation.title);
    root->setProperty("createdAt", conversation.createdAt);
    root->setProperty("updatedAt", conversation.updatedAt);

    juce::Array<juce::var> blocks;
    for (const auto& block : conversation.blocks)
    {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty("index", block.index);
        value->setProperty("timestamp", block.timestamp);
        value->setProperty("role", block.role);
        value->setProperty("content", block.content);
        value->setProperty("previousHash", block.previousHash);
        value->setProperty("hash", block.hash);
        blocks.add(juce::var(value.get()));
    }
    root->setProperty("blocks", blocks);

    auto integrity = juce::DynamicObject::Ptr(new juce::DynamicObject());
    integrity->setProperty("algorithm", "SHA-256");
    integrity->setProperty("blockCount", static_cast<int>(conversation.blocks.size()));
    integrity->setProperty("headHash", conversation.blocks.empty()
        ? zeroHash() : conversation.blocks.back().hash);
    root->setProperty("integrity", juce::var(integrity.get()));
    return juce::var(root.get());
}
