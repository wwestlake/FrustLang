%{
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

#include "AST.h"
#include "Lexer.h"
#include "parser.hpp"

#undef YY_DECL
#define YY_DECL frust::Parser::symbol_type frust::Lexer::NextToken()

// Advances the location's *end* position by the just-matched token's length
// before that token's own action runs, then loc_.step() (top of the rules
// section, and after every newline) collapses begin up to end - the
// standard Bison C++ lexer-integration pattern.
#define YY_USER_ACTION loc_.columns(yyleng);
%}

%option noyywrap nounput noinput batch never-interactive nounistd
%option c++
%option yyclass="frust::Lexer"

%x BLOCK_COMMENT

DIGIT  [0-9]
IDCHAR [A-Za-z_]
ID     {IDCHAR}({IDCHAR}|{DIGIT})*

%%

%{
  loc_.step();
%}

"//"[^\n]*              { /* line comment - ignore */ }
"/*"                    { BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>"*/"     { BEGIN(INITIAL); }
<BLOCK_COMMENT>\n       { loc_.lines(1); loc_.step(); }
<BLOCK_COMMENT>.        { /* consume */ }
<BLOCK_COMMENT><<EOF>>  {
    errors.push_back("frust: unterminated block comment at line " + std::to_string(loc_.begin.line));
    BEGIN(INITIAL);
}

[ \t\r]+   { loc_.step(); }
\n         { loc_.lines(1); loc_.step(); }

"fn"         { return Parser::make_FN(loc_); }
"extern"     { return Parser::make_EXTERN(loc_); }
"entry"      { return Parser::make_ENTRY(loc_); }
"use"        { return Parser::make_USE(loc_); }
"if"         { return Parser::make_IF(loc_); }
"else"       { return Parser::make_ELSE(loc_); }
"while"      { return Parser::make_WHILE(loc_); }
"loop"       { return Parser::make_LOOP(loc_); }
"for"        { return Parser::make_FOR(loc_); }
"break"      { return Parser::make_BREAK(loc_); }
"continue"   { return Parser::make_CONTINUE(loc_); }
"impl"       { return Parser::make_IMPL(loc_); }
"self"       { return Parser::make_SELF(loc_); }
"pub"        { return Parser::make_PUB(loc_); }
"unsafe"     { return Parser::make_UNSAFE(loc_); }
"let"        { return Parser::make_LET(loc_); }
"mut"        { return Parser::make_MUT(loc_); }
"return"     { return Parser::make_RETURN(loc_); }
"struct"     { return Parser::make_STRUCT(loc_); }
"type"       { return Parser::make_TYPE(loc_); }
"effect"     { return Parser::make_EFFECT(loc_); }
"perform"    { return Parser::make_PERFORM(loc_); }
"handle"     { return Parser::make_HANDLE(loc_); }
"resume"     { return Parser::make_RESUME(loc_); }
"with"       { return Parser::make_WITH(loc_); }
"component"  { return Parser::make_COMPONENT(loc_); }
"in"         { return Parser::make_IN(loc_); }
"out"        { return Parser::make_OUT(loc_); }
"build_time" { return Parser::make_BUILD_TIME(loc_); }
"quote"      { return Parser::make_QUOTE(loc_); }
"unquote"    { return Parser::make_UNQUOTE(loc_); }
"as"         { return Parser::make_AS(loc_); }
"own"        { return Parser::make_OWN(loc_); }
"shared"     { return Parser::make_SHARED(loc_); }
"weak"       { return Parser::make_WEAK(loc_); }
"raw"        { return Parser::make_RAW(loc_); }
"true"       { return Parser::make_TRUE(loc_); }
"false"      { return Parser::make_FALSE(loc_); }

{DIGIT}+"."{DIGIT}+ { return Parser::make_FLOAT_LITERAL(std::strtod(yytext, nullptr), loc_); }
{DIGIT}+            { return Parser::make_INT_LITERAL(std::strtoll(yytext, nullptr, 10), loc_); }

\"([^\"\\\n]|\\.)*\" { return Parser::make_STRING_LITERAL(UnescapeString(yytext, yyleng), loc_); }
\"([^\"\\\n]|\\.)*   {
    errors.push_back("frust: unterminated string literal at line " + std::to_string(loc_.begin.line));
    return Parser::make_STRING_LITERAL(std::string(), loc_);
}

{ID}  { return Parser::make_IDENT(std::string(yytext, yyleng), loc_); }

"::"  { return Parser::make_DCOLON(loc_); }
".."  { return Parser::make_DOTDOT(loc_); }
"->"  { return Parser::make_ARROW(loc_); }
"=>"  { return Parser::make_FATARROW(loc_); }
"=="  { return Parser::make_EQEQ(loc_); }
"!="  { return Parser::make_NEQ(loc_); }
"<="  { return Parser::make_LE(loc_); }
">="  { return Parser::make_GE(loc_); }

"{"  { return Parser::make_LBRACE(loc_); }
"}"  { return Parser::make_RBRACE(loc_); }
"("  { return Parser::make_LPAREN(loc_); }
")"  { return Parser::make_RPAREN(loc_); }
"["  { return Parser::make_LBRACKET(loc_); }
"]"  { return Parser::make_RBRACKET(loc_); }
","  { return Parser::make_COMMA(loc_); }
";"  { return Parser::make_SEMI(loc_); }
":"  { return Parser::make_COLON(loc_); }
"."  { return Parser::make_DOT(loc_); }
"="  { return Parser::make_EQUAL(loc_); }
"<"  { return Parser::make_LT(loc_); }
">"  { return Parser::make_GT(loc_); }
"+"  { return Parser::make_PLUS(loc_); }
"-"  { return Parser::make_MINUS(loc_); }
"*"  { return Parser::make_STAR(loc_); }
"/"  { return Parser::make_SLASH(loc_); }
"%"  { return Parser::make_PERCENT(loc_); }
"!"  { return Parser::make_BANG(loc_); }
"&"  { return Parser::make_AMP(loc_); }

.  {
    errors.push_back("frust: invalid character '" + std::string(yytext, yyleng) + "' at line " + std::to_string(loc_.begin.line));
}

<<EOF>>  { return Parser::make_YYEOF(loc_); }

%%

namespace frust {

Lexer::Lexer(std::istream* in) : yyFlexLexer(in) {}

std::string Lexer::UnescapeString(const char* raw, int length) const {
    // Strips the surrounding quotes and resolves \n \t \\ \" - the only
    // escapes the language needs right now.
    std::string result;
    result.reserve(static_cast<std::size_t>(length));
    for (int i = 1; i < length - 1; ++i) {
        if (raw[i] == '\\' && i + 1 < length - 1) {
            ++i;
            switch (raw[i]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += raw[i]; break;
            }
        } else {
            result += raw[i];
        }
    }
    return result;
}

} // namespace frust
