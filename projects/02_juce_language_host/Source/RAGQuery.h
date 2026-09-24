#pragma once
#include <juce_core/juce_core.h>

namespace rag {
    struct RetrievedNode
    {
        juce::String id;
        juce::String type;
        juce::String name;
        juce::String sourceFile;
        juce::String content;
    };

    struct RetrievalResult
    {
        juce::String query;
        juce::StringArray tokens;
        std::vector<RetrievedNode> nodes;
        juce::String context;
    };

    juce::File getKnowledgeDatabaseFile();
    juce::File getGlobalMemoryCardsFile();
    juce::File getProjectMemoryCardsFile(const juce::File& projectRoot);
    RetrievalResult getRetrievalForQuery(const juce::String& query);
    RetrievalResult getRetrievalForQuery(const juce::String& query, const juce::File& projectRoot);
    juce::String getContextForQuery(const juce::String& query);
    juce::String getContextForQuery(const juce::String& query, const juce::File& projectRoot);
}
