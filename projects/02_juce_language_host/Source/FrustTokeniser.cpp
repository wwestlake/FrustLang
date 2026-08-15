#include "FrustTokeniser.h"

namespace {

// Mirrors grammar/frust.l's keyword list - that file is the actual source
// of truth for what's a keyword; update this alongside it.
bool isFrustKeyword(juce::String::CharPointerType token, int tokenLength) noexcept
{
    static const char* const keywords[] = {
        "fn", "pub", "unsafe", "let", "mut", "return", "struct", "type",
        "effect", "perform", "handle", "resume", "with", "component",
        "in", "out", "build_time", "quote", "unquote", "as",
        "own", "shared", "weak", "raw", "true", "false",
        nullptr
    };

    for (int i = 0; keywords[i] != nullptr; ++i)
        if (token.compare(juce::CharPointer_ASCII(keywords[i])) == 0)
            return true;

    return false;
}

int parseIdentifier(juce::CodeDocument::Iterator& source) noexcept
{
    int tokenLength = 0;
    juce::String::CharPointerType::CharType buffer[64] = {};
    juce::String::CharPointerType possible(buffer);

    while (juce::CppTokeniserFunctions::isIdentifierBody(source.peekNextChar())) {
        auto c = source.nextChar();
        if (tokenLength < 63) possible.write(c);
        ++tokenLength;
    }

    if (tokenLength > 0 && tokenLength <= 20) {
        possible.writeNull();
        if (isFrustKeyword(juce::String::CharPointerType(buffer), tokenLength))
            return FrustTokeniser::tokenType_keyword;
    }

    return FrustTokeniser::tokenType_identifier;
}

} // namespace

int FrustTokeniser::readNextToken(juce::CodeDocument::Iterator& source)
{
    source.skipWhitespace();
    auto firstChar = source.peekNextChar();

    switch (firstChar) {
        case 0:
            break;

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        {
            auto result = juce::CppTokeniserFunctions::parseNumber(source);
            if (result == tokenType_error) source.skip();
            return result;
        }

        case ',': case ';':
            source.skip();
            return tokenType_punctuation;

        case '.':
            source.skip();
            juce::CppTokeniserFunctions::skipIfNextCharMatches(source, '.');
            return tokenType_punctuation;

        case '(': case ')':
        case '{': case '}':
        case '[': case ']':
            source.skip();
            return tokenType_bracket;

        case '"':
            juce::CppTokeniserFunctions::skipQuotedString(source);
            return tokenType_string;

        case ':':
            source.skip();
            juce::CppTokeniserFunctions::skipIfNextCharMatches(source, ':');
            return tokenType_operator;

        case '-':
            source.skip();
            juce::CppTokeniserFunctions::skipIfNextCharMatches(source, '>');
            return tokenType_operator;

        case '=':
            source.skip();
            juce::CppTokeniserFunctions::skipIfNextCharMatches(source, '=', '>');
            return tokenType_operator;

        case '/':
        {
            source.skip();
            auto nextChar = source.peekNextChar();

            if (nextChar == '/') {
                source.skipToEndOfLine();
                return tokenType_comment;
            }
            if (nextChar == '*') {
                source.skip();
                juce::CppTokeniserFunctions::skipComment(source);
                return tokenType_comment;
            }
            return tokenType_operator;
        }

        case '+': case '*': case '%': case '!':
            source.skip();
            juce::CppTokeniserFunctions::skipIfNextCharMatches(source, '=');
            return tokenType_operator;

        case '<': case '>':
            source.skip();
            juce::CppTokeniserFunctions::skipIfNextCharMatches(source, '=');
            return tokenType_operator;

        case '&':
            source.skip();
            return tokenType_operator;

        default:
            if (juce::CppTokeniserFunctions::isIdentifierStart(firstChar))
                return parseIdentifier(source);

            source.skip();
            break;
    }

    return tokenType_error;
}

juce::CodeEditorComponent::ColourScheme FrustTokeniser::getDefaultColourScheme()
{
    static const juce::CodeEditorComponent::ColourScheme::TokenType types[] = {
        { "Error",       juce::Colour(0xffcc4444) },
        { "Comment",     juce::Colour(0xff6a9955) },
        { "Keyword",     juce::Colour(0xff569cd6) },
        { "Operator",    juce::Colour(0xffd4d4d4) },
        { "Identifier",  juce::Colour(0xffd4d4d4) },
        { "Integer",     juce::Colour(0xffb5cea8) },
        { "Float",       juce::Colour(0xffb5cea8) },
        { "String",      juce::Colour(0xffce9178) },
        { "Bracket",     juce::Colour(0xffd4d4d4) },
        { "Punctuation", juce::Colour(0xffd4d4d4) }
    };

    juce::CodeEditorComponent::ColourScheme cs;
    for (auto& t : types) cs.set(t.name, juce::Colour(t.colour));
    return cs;
}
