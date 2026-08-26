#include "ShellTokeniser.h"

namespace
{
enum TokenType { errorToken, commentToken, keywordToken, commandToken, variableToken, stringToken, numberToken, operatorToken, identifierToken };

bool isKeyword(const juce::String& word, ShellTokeniser::Language language)
{
    static const char* const powerShell[] = { "cd", "set-location", "get-childitem", "get-content", "set-content", "get-item", "remove-item", "copy-item", "move-item", "where-object", "foreach-object", "if", "else", "foreach", "function", "return", "try", "catch", "throw", "param", "import-module", "write-host", nullptr };
    static const char* const bash[] = { "cd", "echo", "export", "source", "alias", "if", "then", "else", "fi", "for", "in", "do", "done", "while", "case", "esac", "function", "return", "local", "readonly", "unset", "sudo", nullptr };
    const auto* words = language == ShellTokeniser::Language::powerShell ? powerShell : bash;
    for (int i = 0; words[i] != nullptr; ++i)
        if (word.equalsIgnoreCase(words[i])) return true;
    return false;
}
}

int ShellTokeniser::readNextToken(juce::CodeDocument::Iterator& source)
{
    source.skipWhitespace();
    const auto first = source.peekNextChar();
    if (first == 0) return errorToken;
    if (first == '#') { source.skipToEndOfLine(); return commentToken; }
    if (first == '"' || first == '\'') { juce::CppTokeniserFunctions::skipQuotedString(source); return stringToken; }
    if (first == '$') {
        source.skip();
        while (juce::CppTokeniserFunctions::isIdentifierBody(source.peekNextChar())) source.skip();
        return variableToken;
    }
    if (juce::CharacterFunctions::isDigit(first)) return juce::CppTokeniserFunctions::parseNumber(source) == 0 ? numberToken : numberToken;
    if (juce::CppTokeniserFunctions::isIdentifierStart(first) || first == '-' || first == '.') {
        juce::String word;
        while (juce::CppTokeniserFunctions::isIdentifierBody(source.peekNextChar()) || source.peekNextChar() == '-') word += source.nextChar();
        return isKeyword(word, language) ? keywordToken : commandToken;
    }
    source.skip();
    return operatorToken;
}

juce::CodeEditorComponent::ColourScheme ShellTokeniser::getDefaultColourScheme()
{
    juce::CodeEditorComponent::ColourScheme scheme;
    const juce::CodeEditorComponent::ColourScheme::TokenType types[] = {
        { "Error", juce::Colour(0xfff14c4c) }, { "Comment", juce::Colour(0xff6a9955) },
        { "Keyword", juce::Colour(0xff569cd6) }, { "Command", juce::Colour(0xffdcdcaa) },
        { "Variable", juce::Colour(0xff9cdcfe) }, { "String", juce::Colour(0xffce9178) },
        { "Number", juce::Colour(0xffb5cea8) }, { "Operator", juce::Colour(0xffd4d4d4) },
        { "Identifier", juce::Colour(0xffd4d4d4) }
    };
    for (const auto& type : types) scheme.set(type.name, juce::Colour(type.colour));
    return scheme;
}
