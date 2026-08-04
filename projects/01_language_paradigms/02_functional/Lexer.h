#pragma once

#include <string>
#include <vector>
#include <variant>
#include <string_view>
#include <iostream>

namespace ldfn {

enum class TokenType {
    // Keywords
    KwLet, KwIn, KwFn, KwMatch, KwWith, KwIf, KwThen, KwElse, KwType, KwPerform, KwHandle,
    
    // Identifiers & Literals
    Ident, ConstrIdent, IntLit, FloatLit, BoolLit, StringLit,

    // Operators & Delimiters
    Arrow, FatArrow, Equal, Plus, Minus, Star, Slash, Percent,
    EqEq, NotEq, Lt, LtEq, Gt, GtEq,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Colon, Semicolon, Pipe, Underscore,

    // Control
    Eof, Invalid
};

struct Token {
    TokenType type;
    std::string text;
    int line;
    int column;
};

class Lexer {
public:
    explicit Lexer(std::string_view source)
        : src(source), cursor(0), line(1), col(1) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (auto tok = nextToken()) {
            tokens.push_back(*tok);
            if (tok->type == TokenType::Eof) break;
        }
        return tokens;
    }

private:
    std::string_view src;
    size_t cursor;
    int line;
    int col;

    char peek() const { return cursor < src.size() ? src[cursor] : '\0'; }
    char peekNext() const { return cursor + 1 < src.size() ? src[cursor + 1] : '\0'; }
    
    char advance() {
        char c = peek();
        cursor++;
        if (c == '\n') { line++; col = 1; } else { col++; }
        return c;
    }

    void skipWhitespaceAndComments() {
        while (cursor < src.size()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
            } else if (c == '/' && peekNext() == '/') {
                while (peek() != '\n' && peek() != '\0') advance();
            } else {
                break;
            }
        }
    }

    std::optional<Token> nextToken() {
        skipWhitespaceAndComments();
        if (cursor >= src.size()) return Token{TokenType::Eof, "", line, col};

        int startLine = line;
        int startCol = col;
        char c = peek();

        // Identifiers & Keywords
        if (std::isalpha(c) || c == '_') {
            std::string ident;
            while (std::isalnum(peek()) || peek() == '_') {
                ident += advance();
            }
            if (ident == "_") return Token{TokenType::Underscore, ident, startLine, startCol};
            if (ident == "let") return Token{TokenType::KwLet, ident, startLine, startCol};
            if (ident == "in") return Token{TokenType::KwIn, ident, startLine, startCol};
            if (ident == "fn") return Token{TokenType::KwFn, ident, startLine, startCol};
            if (ident == "match") return Token{TokenType::KwMatch, ident, startLine, startCol};
            if (ident == "with") return Token{TokenType::KwWith, ident, startLine, startCol};
            if (ident == "if") return Token{TokenType::KwIf, ident, startLine, startCol};
            if (ident == "then") return Token{TokenType::KwThen, ident, startLine, startCol};
            if (ident == "else") return Token{TokenType::KwElse, ident, startLine, startCol};
            if (ident == "type") return Token{TokenType::KwType, ident, startLine, startCol};
            if (ident == "perform") return Token{TokenType::KwPerform, ident, startLine, startCol};
            if (ident == "handle") return Token{TokenType::KwHandle, ident, startLine, startCol};
            if (ident == "true" || ident == "false") return Token{TokenType::BoolLit, ident, startLine, startCol};

            TokenType type = std::isupper(ident[0]) ? TokenType::ConstrIdent : TokenType::Ident;
            return Token{type, ident, startLine, startCol};
        }

        // Numbers
        if (std::isdigit(c)) {
            std::string numStr;
            bool isFloat = false;
            while (std::isdigit(peek()) || (peek() == '.' && std::isdigit(peekNext()))) {
                if (peek() == '.') isFloat = true;
                numStr += advance();
            }
            return Token{isFloat ? TokenType::FloatLit : TokenType::IntLit, numStr, startLine, startCol};
        }

        // Two-character Operators
        if (c == '-' && peekNext() == '>') { advance(); advance(); return Token{TokenType::Arrow, "->", startLine, startCol}; }
        if (c == '=' && peekNext() == '>') { advance(); advance(); return Token{TokenType::FatArrow, "=>", startLine, startCol}; }
        if (c == '=' && peekNext() == '=') { advance(); advance(); return Token{TokenType::EqEq, "==", startLine, startCol}; }
        if (c == '!' && peekNext() == '=') { advance(); advance(); return Token{TokenType::NotEq, "!=", startLine, startCol}; }
        if (c == '<' && peekNext() == '=') { advance(); advance(); return Token{TokenType::LtEq, "<=", startLine, startCol}; }
        if (c == '>' && peekNext() == '=') { advance(); advance(); return Token{TokenType::GtEq, ">=", startLine, startCol}; }

        // Single-character Operators & Punctuation
        advance();
        switch (c) {
            case '=': return Token{TokenType::Equal, "=", startLine, startCol};
            case '+': return Token{TokenType::Plus, "+", startLine, startCol};
            case '-': return Token{TokenType::Minus, "-", startLine, startCol};
            case '*': return Token{TokenType::Star, "*", startLine, startCol};
            case '/': return Token{TokenType::Slash, "/", startLine, startCol};
            case '%': return Token{TokenType::Percent, "%", startLine, startCol};
            case '<': return Token{TokenType::Lt, "<", startLine, startCol};
            case '>': return Token{TokenType::Gt, ">", startLine, startCol};
            case '(': return Token{TokenType::LParen, "(", startLine, startCol};
            case ')': return Token{TokenType::RParen, ")", startLine, startCol};
            case '{': return Token{TokenType::LBrace, "{", startLine, startCol};
            case '}': return Token{TokenType::RBrace, "}", startLine, startCol};
            case '[': return Token{TokenType::LBracket, "[", startLine, startCol};
            case ']': return Token{TokenType::RBracket, "]", startLine, startCol};
            case ',': return Token{TokenType::Comma, ",", startLine, startCol};
            case ':': return Token{TokenType::Colon, ":", startLine, startCol};
            case ';': return Token{TokenType::Semicolon, ";", startLine, startCol};
            case '|': return Token{TokenType::Pipe, "|", startLine, startCol};
            default:  return Token{TokenType::Invalid, std::string(1, c), startLine, startCol};
        }
    }
};

} // namespace ldfn
