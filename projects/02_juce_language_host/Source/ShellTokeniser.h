#pragma once

#include <JuceHeader.h>

// Lightweight interactive-shell syntax highlighter. It deliberately colors
// PowerShell and Bash rather than FRust: the terminal executes shell commands.
class ShellTokeniser final : public juce::CodeTokeniser
{
public:
    enum class Language { powerShell, bash };

    explicit ShellTokeniser(Language language) : language(language) {}

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

private:
    Language language;
};
