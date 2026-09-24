#pragma once

#include <juce_core/juce_core.h>

#include <string>

// JSON for talking to model providers, correct whatever the text holds. See Json.cpp for why JUCE 7's own JSON is not used for it.
namespace ai_provider::json
{
// Joins surrogate pairs that JUCE 7's JSON reader split into two characters back into one; a lone half becomes U+FFFD.
juce::String repairSurrogates(const juce::String& text);

// The same repair, through every string in a parsed value.
juce::var repaired(const juce::var& value);

// Writes a value as JSON: UTF-8, only the escapes JSON allows, non-finite numbers as null.
std::string toJson(const juce::var& value);
}
