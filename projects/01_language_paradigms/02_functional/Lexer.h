#pragma once

// FlexLexer.h's own multiple-inclusion guard is keyed on yyFlexLexer already
// being defined, not a normal #pragma once/include-guard - this mirrors
// Flex's own documented pattern for a project with more than one
// flex-generated scanner.
#if !defined(yyFlexLexer) && !defined(FlexLexerOnce)
#include <FlexLexer.h>
#endif

#include <string>

#include "parser.hpp"

namespace frust {

// Wraps the Flex-generated scanner body (see frust.l's YY_DECL redefine) as
// a member function returning Parser::symbol_type directly - api.token.
// constructor lets the grammar's %token declarations double as symbol_type
// factory functions (Parser::make_IDENT(...), etc.), which NextToken()
// below calls once per matched token instead of the classic yylval-out-
// parameter style.
class Lexer final : public yyFlexLexer {
public:
    explicit Lexer(std::istream* in);

    Parser::symbol_type NextToken();

    std::vector<std::string> errors;

private:
    std::string UnescapeString(const char* raw, int length) const;

    Parser::location_type loc_;
};

} // namespace frust
