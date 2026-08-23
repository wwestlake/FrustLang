%require "3.8"
%language "c++"
%skeleton "lalr1.cc"

%define api.namespace {frust}
%define api.parser.class {Parser}
%define api.token.constructor
%define api.value.type variant
%define api.location.file "location.hh"
%define parse.error detailed
%locations

// 4 known, understood shift/reduce conflicts remain (all resolved correctly
// by bison's default shift-preference for every valid Frust program):
//  1. `effect NAME` with both params and a return type omitted, immediately
//     followed by a top-level statement starting with "(" - LALR can't
//     prove from 1 token of lookahead whether "(" starts this effect's
//     (empty) param list or a new statement, though a real param list
//     requires `IDENT ":"` next and would fail to match either way.
//  2/3. A generic type argument's own `<...>` suffix, in both the plain
//     and `raw*` type-prefix forms - a spurious LALR state-merge artifact;
//     there's no position in the grammar where a *complete* type_expr is
//     ever legitimately followed by "<", so this can't affect a valid
//     program's parse.
//  4. `type NAME = type_base` (type_refinement_opt empty so far), followed
//     directly by "[" with no separator - could be the start of
//     type_refinement_opt's own `"[" compare_op signed_num "]"`/
//     `"[" a..b "]"` suffix continuing this same type_expr, or (since
//     type_alias_decl needs no trailing ";" and a new decl/stmt can start
//     right after) the "[" of an unrelated array_literal (Vec<N>
//     construction) as the next top-level statement. Only reachable by a
//     type alias with an empty refinement immediately followed, with zero
//     separator, by a bare array-literal statement - not a shape any real
//     Frust program has a reason to write. Resolves via shift (attaches to
//     the refinement) same as every other conflict here.
// This number is a checked invariant, not a shrug: if a grammar change
// makes it move, that's a signal to go re-diagnose with `--report=all`
// (see the diagnosis notes in this file's git history), not to bump it
// without looking.
%expect 4

%code requires {
    #include <cstdint>
    #include <string>
    #include <vector>
    #include "AST.h"

    namespace frust {
    class Lexer;

    // Parser-internal helper for the `[ ... ]` refinement-type suffix -
    // not a real AST node, just a convenient carrier for type_refinement_opt
    // before its fields get copied onto the owning TypeExpr.
    struct RefinementInfo {
        RefinementKind kind = RefinementKind::None;
        double low = 0.0, high = 0.0;
        CompareOp cmpOp = CompareOp::Eq;
        double cmpValue = 0.0;
    };

    // Parser-internal carrier for component_body: ports always precede
    // wiring statements textually (`in`/`out` decls, then assignments like
    // `o = s * f`), so this keeps them as two separately-ordered lists
    // rather than trying to interleave them in Decl/AST itself.
    struct ComponentBodyItems {
        std::vector<PortDecl> ports;
        std::vector<Expr*> stmts;
    };
    }
}

%param { frust::Lexer& lexer }
%parse-param { frust::AstArena& arena }
%parse-param { std::vector<std::string>& parseErrors }
%parse-param { frust::Program*& result }
%parse-param { std::vector<frust::ParseError>& structuredErrors }

%code {
    #include "Lexer.h"

    namespace frust {
    static Parser::symbol_type yylex(Lexer& lexer) { return lexer.NextToken(); }

    namespace {
    SourceLoc ToSourceLoc(const Parser::location_type& loc) {
        return SourceLoc{ loc.begin.line, loc.begin.column };
    }
    } // namespace
    } // namespace frust
}

%token
    FN "fn" PUB "pub" UNSAFE "unsafe" EXTERN "extern" ENTRY "entry" USE "use" LET "let" MUT "mut" RETURN "return"
    IF "if" ELSE "else" WHILE "while" LOOP "loop" FOR "for" BREAK "break" CONTINUE "continue"
    IMPL "impl" SELF "self" INTERFACE "interface" MANIFEST "manifest"
    STRUCT "struct" TYPE "type" EFFECT "effect" PERFORM "perform"
    HANDLE "handle" RESUME "resume" WITH "with" COMPONENT "component"
    IN "in" OUT "out" BUILD_TIME "build_time" QUOTE "quote" UNQUOTE "unquote"
    AS "as" OWN "own" SHARED "shared" WEAK "weak" RAW "raw"
    TRUE "true" FALSE "false"
    LBRACE "{" RBRACE "}" LPAREN "(" RPAREN ")" LBRACKET "[" RBRACKET "]"
    COMMA "," SEMI ";" COLON ":" DOT "." DCOLON "::" DOTDOT ".."
    ARROW "->" FATARROW "=>" EQUAL "="
    EQEQ "==" NEQ "!=" LT "<" GT ">" LE "<=" GE ">="
    PLUS "+" MINUS "-" STAR "*" SLASH "/" PERCENT "%" BANG "!" AMP "&"
    PIPE "|" CARET "^" SHL "<<" SHR ">>"
;

%token <int64_t> INT_LITERAL
%token <double> FLOAT_LITERAL
%token <std::string> STRING_LITERAL
%token <std::string> IDENT

// Standard bison operator-precedence declarations (lowest to highest) -
// this is what actually resolves the binary-chain shift/reduce ambiguity
// correctly (always prefer extending the current operator chain over
// reducing early), rather than relying on grammar nesting to do it, which
// is the more fragile approach for a grammar this size.
%right "="
%left "|"
%left "^"
%left "&"
%left "==" "!="
%left "<" ">" "<=" ">="
%left "<<" ">>"
%left "+" "-"
%left "*" "/" "%"
%precedence "as"
%precedence UNARY_PREC
%left "::" "." "(" "[" "{"

%type <frust::Decl*> decl trailing_stmt_decl_opt
%type <std::vector<frust::Decl*>> decl_list
%type <frust::FunctionDecl*> function_decl
%type <frust::StructDecl*> struct_decl
%type <std::vector<std::string>> generic_params_opt generic_param_list
%type <frust::TypeAliasDecl*> type_alias_decl
%type <frust::EffectDecl*> effect_decl
%type <frust::ComponentDecl*> component_decl
%type <frust::UseDecl*> use_decl
%type <frust::ImplDecl*> impl_decl
%type <frust::FunctionDecl*> method_decl
%type <std::vector<frust::FunctionDecl*>> method_decl_list
%type <frust::SelfKind> self_param
%type <std::vector<frust::Param>> method_param_list_opt
%type <frust::InterfaceDecl*> interface_decl
%type <frust::InterfaceMethodSig> interface_method_sig
%type <std::vector<frust::InterfaceMethodSig>> interface_method_sig_list
%type <frust::ManifestDecl*> manifest_decl

%type <bool> pub_opt unsafe_opt extern_opt entry_opt
%type <frust::TypeExpr*> return_type_opt type_expr type_base type_annot_opt
%type <frust::SmartPtrKind> type_ptr_prefix_opt smart_ptr_expr_qualifier
%type <std::vector<frust::TypeArg>> type_generic_args_opt type_arg_list
%type <frust::TypeArg> type_arg
%type <frust::RefinementInfo> type_refinement_opt
%type <frust::CompareOp> compare_op
%type <double> signed_num

%type <bool> mut_opt
%type <std::vector<frust::Param>> param_list_opt param_list effect_params_opt handle_param_list_opt handle_param_list
%type <frust::Param> param

%type <std::string> interface_opt
%type <frust::ComponentBodyItems> component_body_opt component_body
%type <std::vector<frust::PortDecl>> port_decl_semi_list
%type <std::vector<frust::Expr*>> wiring_semi_list
%type <frust::Expr*> wiring_trailing_opt
%type <frust::PortDecl> port_decl

%type <std::vector<std::string>> ident_path
%type <frust::Expr*> expr postfix_expr primary_expr literal
%type <frust::Expr*> stmt trailing_stmt_opt block_expr build_time_expr quote_expr handle_expr struct_literal if_expr while_expr
%type <frust::Expr*> loop_expr for_expr break_expr continue_expr array_literal
%type <std::vector<frust::Expr*>> semi_stmt_list arg_list_opt arg_list
%type <std::vector<frust::StructFieldInit>> struct_field_init_list_opt struct_field_init_list
%type <frust::StructFieldInit> struct_field_init
%type <std::vector<frust::HandleCase>> handle_case_list_opt handle_case_list
%type <frust::HandleCase> handle_case

%%

// A trailing bare statement (no ";") is allowed only once, at the very end
// of the program - exactly the REPL's one-statement-per-line usage (`dist /
// time`, no semicolon, per FRUST_LANG_SPEC.md). Any *additional* bare
// statement earlier in the same parse must be ";"-terminated (see decl's
// `stmt ";"` alternative) so the boundary between it and whatever follows
// is never ambiguous - see the block comment above stmt_end.
program:
    decl_list trailing_stmt_decl_opt {
        Program* prog = arena.NewProgram();
        prog->decls = std::move($1);
        if ($2) prog->decls.push_back($2);
        result = prog;
    }
;

decl_list:
    %empty          { $$ = {}; }
  | decl_list decl  { $$ = std::move($1); $$.push_back($2); }
;

trailing_stmt_decl_opt:
    %empty { $$ = nullptr; }
  | stmt   { $$ = arena.NewDecl(DeclKind::TopLevelStmt); $$->topLevelStmt = $1; }
;

decl:
    function_decl   { $$ = arena.NewDecl(DeclKind::Function); $$->functionDecl = $1; }
  | struct_decl     { $$ = arena.NewDecl(DeclKind::Struct); $$->structDecl = $1; }
  | type_alias_decl { $$ = arena.NewDecl(DeclKind::TypeAlias); $$->typeAliasDecl = $1; }
  | effect_decl     { $$ = arena.NewDecl(DeclKind::Effect); $$->effectDecl = $1; }
  | component_decl  { $$ = arena.NewDecl(DeclKind::Component); $$->componentDecl = $1; }
  | use_decl        { $$ = arena.NewDecl(DeclKind::Use); $$->useDecl = $1; }
  | impl_decl       { $$ = arena.NewDecl(DeclKind::Impl); $$->implDecl = $1; }
  | interface_decl  { $$ = arena.NewDecl(DeclKind::Interface); $$->interfaceDecl = $1; }
  | manifest_decl   { $$ = arena.NewDecl(DeclKind::Manifest); $$->manifestDecl = $1; }
  // Bare top-level statements - the REPL types `let dist = 100.0 * Meter`,
  // `dist / time`, etc. directly with no enclosing `fn` (FRUST_LANG_SPEC.md
  // REPL section). Requires a trailing ";" when more than one appears in the
  // same parse (see stmt_end's comment) - the single-line REPL case, where
  // this is the last/only thing in the parse, is covered by trailing_stmt_decl_opt.
  | stmt ";" { $$ = arena.NewDecl(DeclKind::TopLevelStmt); $$->topLevelStmt = $1; }
;

pub_opt:    %empty { $$ = false; } | "pub"    { $$ = true; } ;
unsafe_opt: %empty { $$ = false; } | "unsafe" { $$ = true; } ;
extern_opt: %empty { $$ = false; } | "extern" { $$ = true; } ;
entry_opt:  %empty { $$ = false; } | "entry"  { $$ = true; } ;

// -----------------------------------------------------------------------
// Functions - the body is optional so `fn multiply(a: Mat<3,4>, b: Mat<4,2>)
// -> Mat<3,2>` (a signature-only prototype) is valid alongside a normal
// `fn f(...) -> T = expr` definition.
// -----------------------------------------------------------------------

function_decl:
    pub_opt unsafe_opt extern_opt entry_opt "fn" IDENT "(" param_list_opt ")" return_type_opt "=" expr {
        $$ = arena.NewFunctionDecl();
        $$->isPub = $1; $$->isUnsafe = $2; $$->isExtern = $3; $$->isEntry = $4;
        $$->name = $6; $$->params = std::move($8);
        $$->returnType = $10; $$->body = $12; $$->loc = ToSourceLoc(@5);
    }
  | pub_opt unsafe_opt extern_opt entry_opt "fn" IDENT "(" param_list_opt ")" return_type_opt ";" {
        $$ = arena.NewFunctionDecl();
        $$->isPub = $1; $$->isUnsafe = $2; $$->isExtern = $3; $$->isEntry = $4;
        $$->name = $6; $$->params = std::move($8);
        $$->returnType = $10; $$->body = nullptr; $$->loc = ToSourceLoc(@5);
    }
;

return_type_opt: %empty { $$ = nullptr; } | "->" type_expr { $$ = $2; } ;

param_list_opt: %empty     { $$ = {}; } | param_list { $$ = std::move($1); } ;
param_list:
    param                { $$ = {}; $$.push_back(std::move($1)); }
  | param_list "," param { $$ = std::move($1); $$.push_back(std::move($3)); }
;
param: IDENT ":" type_expr { $$ = Param{ $1, $3, ToSourceLoc(@1) }; } ;

// -----------------------------------------------------------------------
// Structs, type aliases, effects
// -----------------------------------------------------------------------

struct_decl:
    "struct" IDENT generic_params_opt "{" param_list_opt "}" {
        $$ = arena.NewStructDecl();
        $$->name = $2; $$->genericParams = std::move($3); $$->loc = ToSourceLoc(@1);
        for (auto& p : $5) $$->fields.push_back(StructField{ p.name, p.type });
    }
;

// `struct Box<T> { ... }` / `struct Result<T, E> { ... }` -
// LANGUAGE_GAPS.md #4. Deliberately a bare identifier list, not
// type_generic_args_opt (which is for CONSUMING type arguments at a
// USE site, e.g. `Box<i64>` in a type_expr) - this is the opposite
// direction, DECLARING the parameter names a generic struct's field
// types can reference. One token of lookahead after IDENT ("{" vs
// "<") keeps this conflict-free with the plain (non-generic) form.
generic_params_opt: %empty { $$ = {}; } | "<" generic_param_list ">" { $$ = std::move($2); } ;
generic_param_list:
    IDENT                          { $$ = {}; $$.push_back($1); }
  | generic_param_list "," IDENT   { $$ = std::move($1); $$.push_back($3); }
;

// -----------------------------------------------------------------------
// impl blocks: `impl TypeName { fn method(&mut self, ...) -> T = {...} }`
// method_decl is deliberately its own grammar, not a reuse of
// function_decl/param_list - `self` doesn't fit param_list's `IDENT ":"
// type_expr` shape, and param_list is shared with free functions/effects/
// handlers, which have no self concept at all. Methods always have a body
// in v1 (no prototype-only methods, unlike free functions).
// -----------------------------------------------------------------------

impl_decl:
    "impl" IDENT "{" method_decl_list "}" {
        $$ = arena.NewImplDecl();
        $$->typeName = $2; $$->methods = std::move($4); $$->loc = ToSourceLoc(@1);
        // method_decl (below) can't know its enclosing impl block's type name
        // at its own reduction - impl_decl only finishes building the method
        // list *after* every method_decl has already reduced - so it's
        // stamped onto each FunctionDecl here instead.
        for (auto* m : $$->methods) m->selfTypeName = $2;
    }
  // `impl InterfaceName for TypeName { ... }` - this block also satisfies
  // InterfaceName's contract (Codegen.h emits a vtable for the pair).
  // Reuses "for" (already a token, for-loops) rather than a new keyword.
  | "impl" IDENT "for" IDENT "{" method_decl_list "}" {
        $$ = arena.NewImplDecl();
        $$->interfaceName = $2; $$->typeName = $4; $$->methods = std::move($6); $$->loc = ToSourceLoc(@1);
        for (auto* m : $$->methods) m->selfTypeName = $4;
    }
;
method_decl_list:
    %empty                        { $$ = {}; }
  | method_decl_list method_decl  { $$ = std::move($1); $$.push_back($2); }
;
method_decl:
    pub_opt "fn" IDENT "(" self_param method_param_list_opt ")" return_type_opt "=" expr {
        $$ = arena.NewFunctionDecl();
        $$->isPub = $1; $$->isMethod = true; $$->selfKind = $5;
        $$->name = $3; $$->params = std::move($6);
        $$->returnType = $8; $$->body = $10; $$->loc = ToSourceLoc(@2);
    }
;
self_param:
    "&" "mut" "self" { $$ = SelfKind::MutRef; }
  | "&" "self"        { $$ = SelfKind::Ref; }
  | "self"             { $$ = SelfKind::ByValue; }
;
method_param_list_opt: %empty { $$ = {}; } | "," param_list { $$ = std::move($2); } ;

// -----------------------------------------------------------------------
// Interfaces: `interface Name { fn method(&mut self, ...) -> T ... }` -
// a named, checked capability contract, no bodies. Reuses self_param/
// method_param_list_opt/return_type_opt exactly like method_decl does -
// the only difference is no "=" expr body.
// -----------------------------------------------------------------------

interface_decl:
    "interface" IDENT "{" interface_method_sig_list "}" {
        $$ = arena.NewInterfaceDecl();
        $$->name = $2; $$->methods = std::move($4); $$->loc = ToSourceLoc(@1);
    }
;
interface_method_sig_list:
    %empty                                          { $$ = {}; }
  | interface_method_sig_list interface_method_sig  { $$ = std::move($1); $$.push_back($2); }
;
interface_method_sig:
    "fn" IDENT "(" self_param method_param_list_opt ")" return_type_opt {
        $$ = InterfaceMethodSig{ $2, std::move($5), $7, ToSourceLoc(@1) };
    }
;

type_alias_decl:
    "type" IDENT "=" type_expr {
        $$ = arena.NewTypeAliasDecl();
        $$->name = $2; $$->aliasedType = $4; $$->loc = ToSourceLoc(@1);
    }
;

effect_decl:
    "effect" IDENT effect_params_opt return_type_opt {
        $$ = arena.NewEffectDecl();
        $$->name = $2; $$->params = std::move($3); $$->returnType = $4; $$->loc = ToSourceLoc(@1);
    }
;
effect_params_opt: %empty { $$ = {}; } | "(" param_list_opt ")" { $$ = std::move($2); } ;

// -----------------------------------------------------------------------
// Components: `component Gain(f: f32) : AudioProcessor { in s; out o; o = s * f }`
// -----------------------------------------------------------------------

component_decl:
    "component" IDENT "(" param_list_opt ")" interface_opt "{" component_body_opt "}" {
        $$ = arena.NewComponentDecl();
        $$->name = $2; $$->params = std::move($4); $$->interfaceName = $6; $$->loc = ToSourceLoc(@1);
        $$->ports = std::move($8.ports);
        $$->bodyStatements = std::move($8.stmts);
    }
;
interface_opt: %empty { $$ = {}; } | ":" IDENT { $$ = $2; } ;

// -----------------------------------------------------------------------
// Use Decl
// -----------------------------------------------------------------------

use_decl:
    "use" ident_path ";" {
        $$ = arena.NewUseDecl();
        $$->pathSegments = std::move($2);
        $$->loc = ToSourceLoc(@1);
    }
  | "use" "self" "::" IDENT ";" {
        // `use self::math;` - names a sibling file (src/math.fr) frate
        // should compile into this pod alongside this file. "self" is the
        // reserved SELF token (used for method self-params), not a plain
        // IDENT, so it needs this dedicated alternative rather than being
        // foldable into ident_path (which would make "self" a valid path
        // segment everywhere, not just here).
        $$ = arena.NewUseDecl();
        $$->isSelfUse = true;
        $$->pathSegments.push_back($4);
        $$->loc = ToSourceLoc(@1);
    }
;

// -----------------------------------------------------------------------
// Plugin manifest: `manifest "{ ...raw JSON... }";` - a plugin's own
// self-describing metadata, embedded directly in the source rather than
// living in a separate companion file. Just a keyword + an existing
// STRING_LITERAL - no new lexing/escaping machinery needed. At most one
// per program (Codegen.h's compileManifestDecl rejects a second one).
// -----------------------------------------------------------------------

manifest_decl:
    "manifest" STRING_LITERAL ";" {
        $$ = arena.NewManifestDecl();
        $$->json = $2; $$->loc = ToSourceLoc(@1);
    }
;

// Ports (`in`/`out`) always precede wiring statements (`o = s * f`)
// textually - same ";"-required-except-possibly-last rule as block_expr's
// semi_stmt_list/trailing_stmt_opt, and for the identical reason (a bare
// wiring statement can start with "-"/"*"/"!", so consecutive statements
// need an unambiguous boundary).
// No %empty alternative here - component_body is itself fully nullable
// (all three of its parts can be empty), so adding one would just be a
// second, ambiguous derivation of the same "empty body" input (that was a
// real reduce/reduce conflict on "}" until this comment).
component_body_opt: component_body { $$ = std::move($1); } ;
component_body:
    port_decl_semi_list wiring_semi_list wiring_trailing_opt {
        $$ = ComponentBodyItems{};
        $$.ports = std::move($1);
        $$.stmts = std::move($2);
        if ($3) $$.stmts.push_back($3);
    }
;
port_decl_semi_list:
    %empty                          { $$ = {}; }
  | port_decl_semi_list port_decl ";" { $$ = std::move($1); $$.push_back(std::move($2)); }
;
wiring_semi_list:
    %empty                     { $$ = {}; }
  | wiring_semi_list expr ";"  { $$ = std::move($1); $$.push_back($2); }
;
wiring_trailing_opt: %empty { $$ = nullptr; } | expr { $$ = $1; } ;

port_decl:
    "in" IDENT type_annot_opt  { $$ = PortDecl{ false, $2, $3, ToSourceLoc(@1) }; }
  | "out" IDENT type_annot_opt { $$ = PortDecl{ true, $2, $3, ToSourceLoc(@1) }; }
;
type_annot_opt: %empty { $$ = nullptr; } | ":" type_expr { $$ = $2; } ;

// -----------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------

type_expr:
    type_base type_refinement_opt {
        $$ = $1;
        $$->refinementKind = $2.kind;
        $$->refLow = $2.low; $$->refHigh = $2.high;
        $$->refCmpOp = $2.cmpOp; $$->refCmpValue = $2.cmpValue;
    }
;

type_base:
    type_ptr_prefix_opt IDENT type_generic_args_opt {
        $$ = arena.NewType(ToSourceLoc(@2));
        $$->ptrKind = $1; $$->name = $2; $$->genericArgs = std::move($3);
    }
  | "raw" "*" IDENT type_generic_args_opt {
        $$ = arena.NewType(ToSourceLoc(@1));
        $$->ptrKind = SmartPtrKind::Raw; $$->isRawPointer = true;
        $$->name = $3; $$->genericArgs = std::move($4);
    }
;

type_ptr_prefix_opt:
    %empty   { $$ = SmartPtrKind::None; }
  | "own"    { $$ = SmartPtrKind::Own; }
  | "shared" { $$ = SmartPtrKind::Shared; }
  | "weak"   { $$ = SmartPtrKind::Weak; }
  | "&"      { $$ = SmartPtrKind::Ref; }
;

type_generic_args_opt: %empty { $$ = {}; } | "<" type_arg_list ">" { $$ = std::move($2); } ;
type_arg_list:
    type_arg                 { $$ = {}; $$.push_back(std::move($1)); }
  | type_arg_list "," type_arg { $$ = std::move($1); $$.push_back(std::move($3)); }
;
type_arg:
    type_expr    { $$ = TypeArg{ false, 0, $1 }; }
  | INT_LITERAL  { $$ = TypeArg{ true, $1, nullptr }; }
;

type_refinement_opt:
    %empty                             { $$ = RefinementInfo{}; }
  | "[" signed_num ".." signed_num "]" {
        $$ = RefinementInfo{}; $$.kind = RefinementKind::Range; $$.low = $2; $$.high = $4;
    }
  | "[" compare_op signed_num "]" {
        $$ = RefinementInfo{}; $$.kind = RefinementKind::Compare; $$.cmpOp = $2; $$.cmpValue = $3;
    }
;
compare_op:
    "==" { $$ = CompareOp::Eq; } | "!=" { $$ = CompareOp::Neq; }
  | "<"  { $$ = CompareOp::Lt; } | "<=" { $$ = CompareOp::Le; }
  | ">"  { $$ = CompareOp::Gt; } | ">=" { $$ = CompareOp::Ge; }
;
signed_num:
    FLOAT_LITERAL       { $$ = $1; }
  | INT_LITERAL          { $$ = static_cast<double>($1); }
  | "-" FLOAT_LITERAL    { $$ = -$2; }
  | "-" INT_LITERAL      { $$ = -static_cast<double>($2); }
;

// -----------------------------------------------------------------------
// Statements. Frust is expression-oriented, so a "statement" is just an
// expression - but unlike top-level decls (which are all unambiguously
// keyword-first), statements can be bare expressions, and a bare
// expression can start with "-" or "*" (unary negation / deref) - the same
// tokens that can *continue* the previous statement's expression as a
// binary subtraction or multiplication. Without an explicit separator, `a
// -b` is genuinely ambiguous between "one expr, `a - b`" and "two
// statements, `a` then `-b`" - no amount of lookahead resolves that, it's
// a real grammar ambiguity, not just an LALR limitation. So: statements
// inside a block must be ";"-terminated, except optionally the very last
// one, which becomes the block's value (Rust's rule, for the same reason).
// -----------------------------------------------------------------------

opt_semi: %empty | ";" ;

block_expr:
    "{" semi_stmt_list trailing_stmt_opt "}" {
        $$ = arena.NewExpr(ExprKind::Block, ToSourceLoc(@1));
        $$->statements = std::move($2);
        if ($3) $$->statements.push_back($3);
    }
;
semi_stmt_list:
    %empty                    { $$ = {}; }
  | semi_stmt_list stmt ";"   { $$ = std::move($1); $$.push_back($2); }
;
trailing_stmt_opt: %empty { $$ = nullptr; } | stmt { $$ = $1; } ;

stmt:
    "let" mut_opt IDENT type_annot_opt "=" expr {
        $$ = arena.NewExpr(ExprKind::Let, ToSourceLoc(@1));
        $$->isMut = $2; $$->text = $3; $$->typeAnnotation = $4; $$->lhs = $6;
    }
  | "return" expr {
        $$ = arena.NewExpr(ExprKind::Return, ToSourceLoc(@1)); $$->lhs = $2;
    }
  | expr { $$ = $1; }
;
mut_opt: %empty { $$ = false; } | "mut" { $$ = true; } ;

// -----------------------------------------------------------------------
// Expressions. Precedence/associativity is resolved by the %left/%right/
// %precedence declarations above, not by grammar nesting - the standard
// bison idiom (see the manual's own calculator example) for an operator
// grammar this size. postfix_expr/primary_expr stay as their own
// nonterminals below since they're a different kind of thing (call/member/
// index chains, and the literal/atom cases), not more operator precedence.
// -----------------------------------------------------------------------

expr:
    expr "=" expr  { $$ = arena.NewExpr(ExprKind::Assign, ToSourceLoc(@1)); $$->lhs = $1; $$->rhs = $3; }
  | expr "==" expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Eq;  $$->lhs = $1; $$->rhs = $3; }
  | expr "!=" expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Neq; $$->lhs = $1; $$->rhs = $3; }
  | expr "<"  expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Lt;  $$->lhs = $1; $$->rhs = $3; }
  | expr ">"  expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Gt;  $$->lhs = $1; $$->rhs = $3; }
  | expr "<=" expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Le;  $$->lhs = $1; $$->rhs = $3; }
  | expr ">=" expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Ge;  $$->lhs = $1; $$->rhs = $3; }
  | expr "+" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Add; $$->lhs = $1; $$->rhs = $3; }
  | expr "-" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Sub; $$->lhs = $1; $$->rhs = $3; }
  | expr "*" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Mul; $$->lhs = $1; $$->rhs = $3; }
  | expr "/" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Div; $$->lhs = $1; $$->rhs = $3; }
  | expr "%" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Mod; $$->lhs = $1; $$->rhs = $3; }
  | expr "|" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::BitOr;  $$->lhs = $1; $$->rhs = $3; }
  | expr "^" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::BitXor; $$->lhs = $1; $$->rhs = $3; }
  | expr "&" expr  { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::BitAnd; $$->lhs = $1; $$->rhs = $3; }
  | expr "<<" expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Shl;    $$->lhs = $1; $$->rhs = $3; }
  | expr ">>" expr { $$ = arena.NewExpr(ExprKind::Binary, ToSourceLoc(@1)); $$->binaryOp = BinaryOp::Shr;    $$->lhs = $1; $$->rhs = $3; }
  | expr "as" type_expr { $$ = arena.NewExpr(ExprKind::Cast, ToSourceLoc(@1)); $$->lhs = $1; $$->typeAnnotation = $3; }
  | "-" expr %prec UNARY_PREC { $$ = arena.NewExpr(ExprKind::Unary, ToSourceLoc(@1)); $$->unaryOp = UnaryOp::Neg;   $$->lhs = $2; }
  | "!" expr %prec UNARY_PREC { $$ = arena.NewExpr(ExprKind::Unary, ToSourceLoc(@1)); $$->unaryOp = UnaryOp::Not;   $$->lhs = $2; }
  | "*" expr %prec UNARY_PREC { $$ = arena.NewExpr(ExprKind::Unary, ToSourceLoc(@1)); $$->unaryOp = UnaryOp::Deref; $$->lhs = $2; }
  | postfix_expr %prec UNARY_PREC { $$ = $1; }
;

postfix_expr:
    postfix_expr "." IDENT {
        $$ = arena.NewExpr(ExprKind::Member, $1->loc); $$->lhs = $1; $$->text = $3;
    }
  | postfix_expr "(" arg_list_opt ")" {
        $$ = arena.NewExpr(ExprKind::Call, $1->loc); $$->lhs = $1; $$->args = std::move($3);
    }
  | postfix_expr "[" expr "]" {
        $$ = arena.NewExpr(ExprKind::Index, $1->loc); $$->lhs = $1; $$->rhs = $3;
    }
  | primary_expr { $$ = $1; }
;

arg_list_opt: %empty { $$ = {}; } | arg_list { $$ = std::move($1); } ;
arg_list:
    expr               { $$ = {}; $$.push_back($1); }
  | arg_list "," expr  { $$ = std::move($1); $$.push_back($3); }
;

ident_path:
    IDENT                  { $$ = {}; $$.push_back($1); }
  | ident_path "::" IDENT  { $$ = std::move($1); $$.push_back($3); }
;

literal:
    INT_LITERAL    { $$ = arena.NewExpr(ExprKind::IntLiteral, ToSourceLoc(@1));    $$->intValue = $1; }
  | FLOAT_LITERAL  { $$ = arena.NewExpr(ExprKind::FloatLiteral, ToSourceLoc(@1));  $$->floatValue = $1; }
  | STRING_LITERAL { $$ = arena.NewExpr(ExprKind::StringLiteral, ToSourceLoc(@1)); $$->text = $1; }
  | "true"         { $$ = arena.NewExpr(ExprKind::BoolLiteral, ToSourceLoc(@1));   $$->boolValue = true; }
  | "false"        { $$ = arena.NewExpr(ExprKind::BoolLiteral, ToSourceLoc(@1));   $$->boolValue = false; }
;

smart_ptr_expr_qualifier:
    "own" { $$ = SmartPtrKind::Own; } | "shared" { $$ = SmartPtrKind::Shared; }
  | "weak" { $$ = SmartPtrKind::Weak; } | "raw" { $$ = SmartPtrKind::Raw; }
;

struct_literal:
    ident_path "{" struct_field_init_list_opt "}" {
        $$ = arena.NewExpr(ExprKind::StructLiteral, ToSourceLoc(@1));
        $$->pathSegments = std::move($1); $$->fields = std::move($3);
    }
;
struct_field_init_list_opt: %empty { $$ = {}; } | struct_field_init_list { $$ = std::move($1); } ;
struct_field_init_list:
    struct_field_init                            { $$ = {}; $$.push_back(std::move($1)); }
  | struct_field_init_list "," struct_field_init  { $$ = std::move($1); $$.push_back(std::move($3)); }
;
struct_field_init: IDENT ":" expr { $$ = StructFieldInit{ $1, $3 }; } ;

build_time_expr: "build_time" block_expr { $$ = arena.NewExpr(ExprKind::BuildTime, ToSourceLoc(@1)); $$->lhs = $2; } ;
quote_expr:      "quote" block_expr      { $$ = arena.NewExpr(ExprKind::Quote, ToSourceLoc(@1)); $$->lhs = $2; } ;

if_expr:
    "if" expr block_expr {
        $$ = arena.NewExpr(ExprKind::If, ToSourceLoc(@1));
        $$->condExpr = $2; $$->lhs = $3; $$->elseExpr = nullptr;
    }
  | "if" expr block_expr "else" block_expr {
        $$ = arena.NewExpr(ExprKind::If, ToSourceLoc(@1));
        $$->condExpr = $2; $$->lhs = $3; $$->elseExpr = $5;
    }
  | "if" expr block_expr "else" if_expr {
        $$ = arena.NewExpr(ExprKind::If, ToSourceLoc(@1));
        $$->condExpr = $2; $$->lhs = $3; $$->elseExpr = $5;
    }
;

while_expr:
    "while" expr block_expr {
        $$ = arena.NewExpr(ExprKind::While, ToSourceLoc(@1));
        $$->condExpr = $2; $$->lhs = $3;
    }
;

// `lhs` is always the loop body across While/Loop/For (see AST.h) - keeps
// hasPerform's existing recursion into lhs covering every loop kind for
// free, and makes "which field is the body" a single rule instead of a
// per-kind lookup.
loop_expr:
    "loop" block_expr {
        $$ = arena.NewExpr(ExprKind::Loop, ToSourceLoc(@1));
        $$->lhs = $2;
    }
;

// Range-only iteration in v1 - no general iterator protocol, no collection
// type exists yet either. ".." stays scoped to this one production rather
// than becoming a general binary expr operator, so it introduces no new
// operator-precedence conflicts.
for_expr:
    "for" IDENT "in" expr ".." expr block_expr {
        $$ = arena.NewExpr(ExprKind::For, ToSourceLoc(@1));
        $$->text = $2; $$->condExpr = $4; $$->rhs = $6; $$->lhs = $7;
    }
;

break_expr:    "break"    { $$ = arena.NewExpr(ExprKind::Break, ToSourceLoc(@1)); } ;
continue_expr: "continue" { $$ = arena.NewExpr(ExprKind::Continue, ToSourceLoc(@1)); } ;

// `[e1, e2, ..., eN]` - constructs a fixed-size Vec<N> (N = element count).
// Reuses arg_list_opt (already used by Call's argument list) rather than a
// new comma-list production. Conflict-free by the same precedent "(" already
// sets: "(" plays two roles (parenthesized-expr as a primary_expr
// alternative vs. Call's own "(" one level up in postfix_expr) without
// ambiguity, and this "[" sits in a fresh-primary_expr parser state distinct
// from postfix_expr "[" expr "]" (Index), which only fires after a
// postfix_expr has already reduced.
array_literal: "[" arg_list_opt "]" {
    $$ = arena.NewExpr(ExprKind::ArrayLiteral, ToSourceLoc(@1));
    $$->args = std::move($2);
} ;

handle_expr:
    "handle" expr "with" "{" handle_case_list_opt "}" {
        $$ = arena.NewExpr(ExprKind::Handle, ToSourceLoc(@1)); $$->lhs = $2; $$->handleCases = std::move($5);
    }
;
handle_case_list_opt: %empty { $$ = {}; } | handle_case_list { $$ = std::move($1); } ;
handle_case_list:
    handle_case                    { $$ = {}; $$.push_back(std::move($1)); }
  | handle_case_list handle_case   { $$ = std::move($1); $$.push_back(std::move($2)); }
;
handle_case:
    "effect" IDENT "(" handle_param_list_opt ")" "=>" expr opt_semi {
        $$ = HandleCase{ $2, std::move($4), $7, ToSourceLoc(@1) };
    }
;
handle_param_list_opt: %empty { $$ = {}; } | handle_param_list { $$ = std::move($1); } ;
handle_param_list:
    IDENT                       { $$ = {}; $$.push_back(Param{ $1, nullptr, ToSourceLoc(@1) }); }
  | handle_param_list "," IDENT { $$ = std::move($1); $$.push_back(Param{ $3, nullptr, ToSourceLoc(@3) }); }
;

primary_expr:
    literal          { $$ = $1; }
  | ident_path %prec UNARY_PREC {
        if ($1.size() == 1) {
            $$ = arena.NewExpr(ExprKind::Identifier, ToSourceLoc(@1)); $$->text = $1[0];
        } else {
            $$ = arena.NewExpr(ExprKind::Path, ToSourceLoc(@1)); $$->pathSegments = std::move($1);
        }
    }
  | "(" expr ")" { $$ = $2; }
  | block_expr      { $$ = $1; }
  | build_time_expr { $$ = $1; }
  | quote_expr      { $$ = $1; }
  | if_expr         { $$ = $1; }
  | while_expr      { $$ = $1; }
  | loop_expr       { $$ = $1; }
  | for_expr        { $$ = $1; }
  | break_expr      { $$ = $1; }
  | continue_expr   { $$ = $1; }
  | array_literal   { $$ = $1; }
  // "self" is a reserved keyword, not folded into IDENT, so it needs its
  // own alternative here rather than going through ident_path - but once
  // tokenized this makes it behave exactly like any other identifier, so
  // `self.field` / `self.method(...)` parse for free via the existing
  // postfix_expr "." IDENT member-access chain, no further grammar needed.
  | "self" { $$ = arena.NewExpr(ExprKind::Identifier, ToSourceLoc(@1)); $$->text = "self"; }
  | "unquote" "(" expr ")" { $$ = arena.NewExpr(ExprKind::Unquote, ToSourceLoc(@1)); $$->lhs = $3; }
  | "perform" IDENT "(" arg_list_opt ")" {
        $$ = arena.NewExpr(ExprKind::Perform, ToSourceLoc(@1)); $$->text = $2; $$->args = std::move($4);
    }
  | "resume" "(" arg_list_opt ")" {
        $$ = arena.NewExpr(ExprKind::Resume, ToSourceLoc(@1));
        $$->lhs = $3.empty() ? nullptr : $3.front();
    }
  | handle_expr { $$ = $1; }
  | struct_literal { $$ = $1; }
  // struct_literal doesn't need its own alternative here - it's already
  // reachable via postfix_expr -> primary_expr -> struct_literal, so adding
  // it explicitly would just be a second, ambiguous derivation of the same
  // input (that was a real reduce/reduce conflict until this comment).
  | smart_ptr_expr_qualifier postfix_expr %prec UNARY_PREC {
        $$ = arena.NewExpr(ExprKind::SmartPtrNew, ToSourceLoc(@1)); $$->smartPtrKind = $1; $$->lhs = $2;
    }
  // Closure literal. "|" only ever appears here as a fresh primary_expr
  // start (no expr has been reduced yet) versus as the infix BitOr
  // operator (which always follows an already-reduced expr) - different
  // parser states, no conflict with `expr "|" expr` above. No "||" token
  // exists in the lexer, so the zero-param case (`|| -> T { ... }`) is
  // just two adjacent PIPE tokens with an empty param_list_opt between.
  | "|" param_list_opt "|" return_type_opt block_expr {
        $$ = arena.NewExpr(ExprKind::Closure, ToSourceLoc(@1));
        $$->params = std::move($2);
        $$->typeAnnotation = $4;
        $$->lhs = $5;
    }
;

%%

namespace frust {

void Parser::error(const location_type& loc, const std::string& msg) {
    parseErrors.push_back("frust: syntax error at line " + std::to_string(loc.begin.line) +
                          ", col " + std::to_string(loc.begin.column) + ": " + msg);
    structuredErrors.push_back({loc.begin.line, loc.begin.column, msg});
}

} // namespace frust
