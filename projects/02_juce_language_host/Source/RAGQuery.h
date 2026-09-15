#pragma once
#include <juce_core/juce_core.h>

namespace rag {
    juce::File getKnowledgeDatabaseFile();
    juce::String getContextForQuery(const juce::String& query);
}
