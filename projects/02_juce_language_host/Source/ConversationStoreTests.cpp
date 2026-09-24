#include "ConversationStore.h"

#include <iostream>

int main()
{
    if (auto* externalFolder = std::getenv("FRUST_CONVERSATION_TEST_FOLDER"))
    {
        const auto activeFolder = juce::File(juce::String(externalFolder));
        ConversationStore externalStore(nullptr, activeFolder, {});
        juce::Array<juce::File> files;
        activeFolder.findChildFiles(files, juce::File::findFiles, false, "*.json");

        for (const auto& file : files)
        {
            StoredConversation loaded;
            juce::String error;
            if (!externalStore.load(file.getFileNameWithoutExtension(), loaded, error))
            {
                std::cerr << "External conversation failed: "
                          << file.getFullPathName() << "\n"
                          << error << std::endl;
                return 1;
            }
        }

        std::cout << "ConversationStore: validated " << files.size()
                  << " external conversation file(s).\n";
        return 0;
    }

    const auto testRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("frust-conversation-store-" + juce::Uuid().toString());
    const auto activeFolder = testRoot.getChildFile("active");
    const auto archiveFolder = testRoot.getChildFile("archive");
    ConversationStore store(nullptr, activeFolder, archiveFolder);

    auto fail = [&testRoot] (const juce::String& message) {
        testRoot.deleteRecursively();
        std::cerr << message << std::endl;
        return 1;
    };

    auto conversation = store.createConversation();
    if (!store.append(conversation, "user", "Set the selected track gain to -3 dB")
        || !store.append(conversation, "assistant", "The selected track gain is now -3 dB."))
        return fail("Could not append conversation blocks.");

    juce::String error;
    if (!store.save(conversation, error))
        return fail("Save failed: " + error);

    StoredConversation loaded;
    if (!store.load(conversation.id, loaded, error) || loaded.blocks.size() != 2)
        return fail("Reload failed: " + error);

    const auto file = activeFolder.getChildFile(conversation.id + ".json");
    const auto originalJson = file.loadFileAsString();
    const auto blocksOffset = originalJson.indexOf("\"blocks\"");
    auto tamperedJson = originalJson.substring(0, blocksOffset)
        + originalJson.substring(blocksOffset).replaceFirstOccurrenceOf(
            "Set the selected track gain to -3 dB",
            "Set the selected track gain to +12 dB");
    if (!file.replaceWithText(tamperedJson))
        return fail("Could not prepare the tamper test.");

    if (store.load(conversation.id, loaded, error) || !error.containsIgnoreCase("altered"))
        return fail("Tampered conversation was not rejected: " + error);

    if (!store.save(conversation, error) || !store.archive(conversation, error))
        return fail("Archive failed: " + error);

    if (file.existsAsFile()
        || !archiveFolder.getChildFile(conversation.id + ".json").existsAsFile())
        return fail("Archive did not move the conversation file.");

    ConversationStore archiveReader(nullptr, archiveFolder, {});
    if (!archiveReader.load(conversation.id, loaded, error))
        return fail("Archived conversation failed verification: " + error);

    testRoot.deleteRecursively();
    std::cout << "ConversationStore: save, reload, tamper detection, and archive passed.\n";
    return 0;
}
