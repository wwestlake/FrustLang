#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace frust {

// AST nodes are plain kind-tagged structs with raw-pointer children, owned
// by an AstArena (below) rather than by std::unique_ptr child-to-parent
// ownership. A Bison `%define api.value.type variant` grammar has to move
// semantic values around constantly as rules reduce, and move-only
// unique_ptr-in-a-variant is a well-known source of subtle bugs in
// hand-written grammars. Raw pointers are trivially copyable, so the
// generated parser has nothing tricky to get right - the arena is what
// actually keeps every node alive, exactly like a bump allocator in a real
// compiler front end.

struct SourceLoc {
    int line = 0;
    int col = 0;
};

enum class SmartPtrKind { None, Own, Shared, Weak, Raw, Ref };

// How a method's first parameter binds the receiver - `self` / `&self` /
// `&mut self`. All three lower to a pointer in codegen in v1 (no move/copy
// semantics distinction yet) - this only records what the source wrote.
enum class SelfKind { None, ByValue, Ref, MutRef };

// ---------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------

enum class RefinementKind { None, Range, Compare };
enum class CompareOp { Eq, Neq, Lt, Le, Gt, Ge };

struct TypeExpr;

// One entry inside `<...>` generic args - either a nested type (`Vector<shared Node>`)
// or a bare integer constant (`Array<f32, 3>`, `Mat<3, 4>`).
struct TypeArg {
    bool isIntConst = false;
    int64_t intConst = 0;
    TypeExpr* type = nullptr; // set when !isIntConst
};

struct TypeExpr {
    SourceLoc loc;

    SmartPtrKind ptrKind = SmartPtrKind::None; // own/shared/weak/& prefix (Raw uses isRawPointer instead)
    bool isRawPointer = false;                 // `raw* T`

    std::string name; // base type name: f32, i32, Node, Array, Mat, usize, ...
    std::vector<TypeArg> genericArgs; // `<...>`

    // `f32[-1.0 .. 1.0]` or `i32[!= 0]`
    RefinementKind refinementKind = RefinementKind::None;
    double refLow = 0.0, refHigh = 0.0;  // Range
    CompareOp refCmpOp = CompareOp::Eq;
    double refCmpValue = 0.0;            // Compare
};

// ---------------------------------------------------------------------
// Expressions & statements (statements are just expression kinds - Frust
// is expression-oriented, so there's no separate Stmt hierarchy)
// ---------------------------------------------------------------------

enum class UnaryOp { Neg, Not, Deref };
enum class BinaryOp { Add, Sub, Mul, Div, Mod, Eq, Neq, Lt, Gt, Le, Ge };

enum class ExprKind {
    IntLiteral, FloatLiteral, BoolLiteral, StringLiteral,
    Identifier, Path,
    Unary, Binary, Assign, Cast,
    Call, Member, Index,
    SmartPtrNew, StructLiteral,
    Block, Let, Return, If, While,
    Loop, For, Break, Continue,
    BuildTime, Quote, Unquote,
    Perform, Resume, Handle,
};

struct Expr;

struct Param {
    std::string name;
    TypeExpr* type = nullptr; // null for untyped handle-case bindings
    SourceLoc loc;
};

struct StructFieldInit {
    std::string name;
    Expr* value = nullptr;
};

struct HandleCase {
    std::string effectName;
    std::vector<Param> params; // untyped bindings
    Expr* body = nullptr;
    SourceLoc loc;
};

struct Expr {
    ExprKind kind;
    SourceLoc loc;

    int64_t intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    bool isMut = false; // Let

    // StringLiteral value / Identifier name / Member field name / Perform effect name / For loop var name.
    std::string text;
    std::vector<std::string> pathSegments; // Path (a::b::c)

    UnaryOp unaryOp{};
    BinaryOp binaryOp{};
    SmartPtrKind smartPtrKind = SmartPtrKind::None; // SmartPtrNew

    TypeExpr* typeAnnotation = nullptr; // Let's declared type / Cast's target type

    // lhs is always the loop body for While/Loop/For (kept uniform on
    // purpose - see While's existing convention - so hasPerform's recursion
    // into lhs covers every loop kind with no extra cases needed).
    Expr* lhs = nullptr; // Unary/Cast operand, Binary/Assign/Index/Member/Call object, Let/Return/Unquote/Resume value, While/Loop/For body
    Expr* rhs = nullptr; // Binary/Assign rhs, Index index expr, For range end
    Expr* condExpr = nullptr; // If/While condition, For range start
    Expr* elseExpr = nullptr; // If else body

    std::vector<Expr*> args;          // Call args, Perform args
    std::vector<Expr*> statements;    // Block body
    std::vector<StructFieldInit> fields; // StructLiteral
    std::vector<HandleCase> handleCases; // Handle
};

// ---------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------

struct FunctionDecl {
    std::string name;
    bool isPub = false;
    bool isUnsafe = false;
    bool isExtern = false;
    bool isEntry = false;
    std::vector<Param> params;
    TypeExpr* returnType = nullptr; // null => inferred/unit
    Expr* body = nullptr;           // null => prototype-only declaration
    SourceLoc loc;

    // Set only for methods declared inside an `impl` block (ImplDecl, below).
    // selfTypeName is stamped by impl_decl's grammar action once the whole
    // method list is known, since a lone method_decl can't see its enclosing
    // impl block's type name at its own reduction. `self` itself is not
    // present in `params` - codegen inserts it as a synthetic first
    // parameter using selfTypeName/selfKind.
    bool isMethod = false;
    std::string selfTypeName;
    SelfKind selfKind = SelfKind::None;
};

struct StructField {
    std::string name;
    TypeExpr* type = nullptr;
};

struct StructDecl {
    std::string name;
    std::vector<StructField> fields;
    SourceLoc loc;
};

struct TypeAliasDecl {
    std::string name;
    TypeExpr* aliasedType = nullptr;
    SourceLoc loc;
};

struct EffectDecl {
    std::string name;
    std::vector<Param> params;
    TypeExpr* returnType = nullptr; // null => unit
    SourceLoc loc;
};

struct PortDecl {
    bool isOutput = false;
    std::string name;
    TypeExpr* type = nullptr; // null => untyped/inferred
    SourceLoc loc;
};

struct ComponentDecl {
    std::string name;
    std::vector<Param> params;
    std::string interfaceName; // empty => none
    std::vector<PortDecl> ports;
    std::vector<Expr*> bodyStatements; // wiring assignments, e.g. `o = s * f`
    SourceLoc loc;
};

struct UseDecl {
    std::vector<std::string> pathSegments; // e.g. ["dsp_utils", "Gain"]
    SourceLoc loc;
};

// `impl TypeName { fn method(&mut self, ...) -> T = {...} }` - methods on
// an existing struct. Not full `class`/`implements` sugar (see
// docs/16-Imperative-Classes-and-Objects.md for that larger, not-yet-built
// design) - just the load-bearing self/method piece underneath it.
struct ImplDecl {
    std::string typeName; // the struct this impl block adds methods to
    std::vector<FunctionDecl*> methods;
    SourceLoc loc;
};

// TopLevelStmt exists because the REPL accepts bare statements directly
// (`let dist = 100.0 * Meter`, `dist / time`, per FRUST_LANG_SPEC.md's REPL
// section) with no enclosing `fn`. Folding it into DeclKind rather than
// giving Program a second, separately-ordered list keeps top-level ordering
// uniform between files (all decls) and REPL input (all statements).
enum class DeclKind { Function, Struct, TypeAlias, Effect, Component, Use, TopLevelStmt, Impl };

struct Decl {
    DeclKind kind;
    FunctionDecl* functionDecl = nullptr;
    StructDecl* structDecl = nullptr;
    TypeAliasDecl* typeAliasDecl = nullptr;
    EffectDecl* effectDecl = nullptr;
    ComponentDecl* componentDecl = nullptr;
    UseDecl* useDecl = nullptr;
    Expr* topLevelStmt = nullptr;
    ImplDecl* implDecl = nullptr;
};

struct Program {
    std::vector<Decl*> decls;
};

// ---------------------------------------------------------------------
// Arena
// ---------------------------------------------------------------------

// Owns every AST node's actual storage. A Program (and everything it points
// to) is only valid as long as the AstArena that built it stays alive.
class AstArena {
public:
    TypeExpr* NewType(SourceLoc loc) {
        types_.push_back(std::make_unique<TypeExpr>());
        types_.back()->loc = loc;
        return types_.back().get();
    }

    Expr* NewExpr(ExprKind kind, SourceLoc loc) {
        exprs_.push_back(std::make_unique<Expr>());
        exprs_.back()->kind = kind;
        exprs_.back()->loc = loc;
        return exprs_.back().get();
    }

    FunctionDecl* NewFunctionDecl() {
        functionDecls_.push_back(std::make_unique<FunctionDecl>());
        return functionDecls_.back().get();
    }

    StructDecl* NewStructDecl() {
        structDecls_.push_back(std::make_unique<StructDecl>());
        return structDecls_.back().get();
    }

    TypeAliasDecl* NewTypeAliasDecl() {
        typeAliasDecls_.push_back(std::make_unique<TypeAliasDecl>());
        return typeAliasDecls_.back().get();
    }

    EffectDecl* NewEffectDecl() {
        effectDecls_.push_back(std::make_unique<EffectDecl>());
        return effectDecls_.back().get();
    }

    ComponentDecl* NewComponentDecl() {
        componentDecls_.push_back(std::make_unique<ComponentDecl>());
        return componentDecls_.back().get();
    }

    UseDecl* NewUseDecl() {
        useDecls_.push_back(std::make_unique<UseDecl>());
        return useDecls_.back().get();
    }

    ImplDecl* NewImplDecl() {
        implDecls_.push_back(std::make_unique<ImplDecl>());
        return implDecls_.back().get();
    }

    Decl* NewDecl(DeclKind kind) {
        decls_.push_back(std::make_unique<Decl>());
        decls_.back()->kind = kind;
        return decls_.back().get();
    }

    Program* NewProgram() {
        programs_.push_back(std::make_unique<Program>());
        return programs_.back().get();
    }

private:
    std::vector<std::unique_ptr<TypeExpr>> types_;
    std::vector<std::unique_ptr<Expr>> exprs_;
    std::vector<std::unique_ptr<FunctionDecl>> functionDecls_;
    std::vector<std::unique_ptr<StructDecl>> structDecls_;
    std::vector<std::unique_ptr<TypeAliasDecl>> typeAliasDecls_;
    std::vector<std::unique_ptr<EffectDecl>> effectDecls_;
    std::vector<std::unique_ptr<ComponentDecl>> componentDecls_;
    std::vector<std::unique_ptr<UseDecl>> useDecls_;
    std::vector<std::unique_ptr<ImplDecl>> implDecls_;
    std::vector<std::unique_ptr<Decl>> decls_;
    std::vector<std::unique_ptr<Program>> programs_; // always exactly one in practice; vector keeps ownership uniform.
};

} // namespace frust
