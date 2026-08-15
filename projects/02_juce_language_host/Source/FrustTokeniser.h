#pragma once

#include <JuceHeader.h>

// Syntax highlighter for Frust source in the code editor. Modeled directly
// on JUCE's own bundled CPlusPlusCodeTokeniser/LuaTokeniser (same
// CodeTokeniser interface, same CppTokeniserFunctions helpers for the
// generic bits - numbers, strings, identifiers, C-style comments) but with
// Frust's actual keyword list and operator set. This is presentation-layer
// code specific to the JUCE editor widget, so it lives here rather than in
// frust_lang - it doesn't reuse the real frust.l lexer (bridging flex's
// istream-based scanning to CodeDocument::Iterator's incremental-read
// protocol isn't worth it just for editor coloring), so keep this file's
// keyword list in sync with grammar/frust.l if that ever changes.
class FrustTokeniser : public juce::CodeTokeniser
{
public:
    FrustTokeniser() = default;
    ~FrustTokeniser() override = default;

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

    enum TokenType
    {
        tokenType_error = 0,
        tokenType_comment,
        tokenType_keyword,
        tokenType_operator,
        tokenType_identifier,
        tokenType_integer,
        tokenType_float,
        tokenType_string,
        tokenType_bracket,
        tokenType_punctuation
    };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrustTokeniser)
};
