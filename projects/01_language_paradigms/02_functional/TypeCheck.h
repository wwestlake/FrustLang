#pragma once

// The check that runs before any code is generated.
//
// Code generation trusts that a program makes sense: that `+` gets two numbers, that `if` gets a true/false, that a
// function is called with the arguments it declares. When it does not - `"a" + "b"`, `if ("x")`, `-"x"` - the
// low-level code library underneath refuses and aborts the whole process, which is unacceptable for a compiler that
// runs inside an application. This pass walks the whole program first, works out what kind of value each expression
// is, and reports what does not make sense as an ordinary error with its line and column. Code generation then only
// ever sees programs whose basic types line up.
//
// It knows five kinds of value: whole numbers, decimals, true/false, text, and "unknown". Anything it cannot be sure
// of (a struct, an enum, a generic, a pointer, a closure, the result of a method) is unknown, and unknown never
// causes an error, so the check can only reject programs that are clearly wrong and never a program the language
// accepts. It does not change the program.

#include "AST.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace frust {

class TypeChecker {
public:
    // Called once per problem with where it is and what is wrong.
    using Report = std::function<void(const SourceLoc&, const std::string&)>;

    // Returns true when the program has no problems.
    bool check(const Program& program, const Report& report) {
        reportError = report;
        errorCount = 0;
        collectFunctions(program);

        pushScope();   // top-level statements share one scope
        for (const Decl* decl : program.decls) {
            if (decl == nullptr) continue;
            if (decl->kind == DeclKind::Function && decl->functionDecl) {
                checkFunction(*decl->functionDecl, false);
            } else if (decl->kind == DeclKind::Impl && decl->implDecl) {
                for (const FunctionDecl* method : decl->implDecl->methods)
                    if (method) checkFunction(*method, true);
            } else if (decl->kind == DeclKind::TopLevelStmt && decl->topLevelStmt) {
                typeOf(decl->topLevelStmt);
            }
        }
        popScope();
        return errorCount == 0;
    }

private:
    enum class Kind { Unknown, Int, Float, Bool, Text };

    struct Signature {
        std::vector<Kind> params;
        Kind result = Kind::Unknown;
        bool ambiguous = false;
        bool variadic = false;
    };

    struct Variable {
        Kind kind = Kind::Unknown;
    };

    Report reportError;
    int errorCount = 0;
    std::map<std::string, Signature> functions;
    std::vector<std::map<std::string, Variable>> scopes;
    Kind currentReturn = Kind::Unknown;

    void error(const SourceLoc& loc, const std::string& message) {
        ++errorCount;
        if (reportError) reportError(loc, message);
    }

    // ---- kinds ---------------------------------------------------------

    static Kind kindOfType(const TypeExpr* type) {
        if (type == nullptr || type->ptrKind != SmartPtrKind::None || type->isRawPointer || !type->genericArgs.empty())
            return Kind::Unknown;
        const std::string& n = type->name;
        if (n == "i8" || n == "u8" || n == "i16" || n == "u16" || n == "i32" || n == "u32" || n == "i64" || n == "u64"
            || n == "usize" || n == "isize")
            return Kind::Int;
        if (n == "f32" || n == "f64") return Kind::Float;
        if (n == "bool") return Kind::Bool;
        if (n == "String" || n == "string") return Kind::Text;
        return Kind::Unknown;
    }

    static bool isNumber(Kind k) { return k == Kind::Int || k == Kind::Float || k == Kind::Bool; }

    // A text value where a number belongs, or the other way round.
    static bool clash(Kind a, Kind b) {
        return (a == Kind::Text && isNumber(b)) || (b == Kind::Text && isNumber(a));
    }

    static const char* describe(Kind k) {
        switch (k) {
            case Kind::Int: return "a whole number";
            case Kind::Float: return "a decimal number";
            case Kind::Bool: return "a true/false value";
            case Kind::Text: return "text";
            default: return "a value";
        }
    }

    static const char* symbol(BinaryOp op) {
        switch (op) {
            case BinaryOp::Add: return "+";   case BinaryOp::Sub: return "-";   case BinaryOp::Mul: return "*";
            case BinaryOp::Div: return "/";   case BinaryOp::Mod: return "%";   case BinaryOp::Eq: return "==";
            case BinaryOp::Neq: return "!=";  case BinaryOp::Lt: return "<";    case BinaryOp::Gt: return ">";
            case BinaryOp::Le: return "<=";   case BinaryOp::Ge: return ">=";   case BinaryOp::BitOr: return "|";
            case BinaryOp::BitXor: return "^"; case BinaryOp::BitAnd: return "&"; case BinaryOp::Shl: return "<<";
            case BinaryOp::Shr: return ">>";
        }
        return "?";
    }

    // ---- scopes and functions -------------------------------------------

    void pushScope() { scopes.emplace_back(); }
    void popScope() { if (!scopes.empty()) scopes.pop_back(); }

    void declare(const std::string& name, Kind kind) {
        if (!scopes.empty()) scopes.back()[name] = Variable { kind };
    }

    Kind lookup(const std::string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) return found->second.kind;
        }
        return Kind::Unknown;
    }

    void collectFunctions(const Program& program) {
        functions.clear();
        for (const Decl* decl : program.decls) {
            if (decl == nullptr || decl->kind != DeclKind::Function || decl->functionDecl == nullptr) continue;
            const FunctionDecl& fn = *decl->functionDecl;
            Signature sig;
            for (const Param& p : fn.params) sig.params.push_back(kindOfType(p.type));
            sig.result = kindOfType(fn.returnType);
            const auto existing = functions.find(fn.name);
            if (existing != functions.end()) existing->second.ambiguous = true;
            else functions[fn.name] = sig;
        }
    }

    // The signature of a called name, when it is one plain function we know exactly.
    const Signature* signatureOf(const Expr& callee) const {
        std::string name;
        if (callee.kind == ExprKind::Identifier) {
            name = callee.text;
        } else if (callee.kind == ExprKind::Path) {
            for (size_t i = 0; i < callee.pathSegments.size(); ++i) name += (i ? "::" : "") + callee.pathSegments[i];
        } else {
            return nullptr;
        }
        // A local variable of the same name (a closure, say) is not the function.
        if (callee.kind == ExprKind::Identifier) {
            for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
                if (it->count(name)) return nullptr;
        }
        const auto found = functions.find(name);
        if (found == functions.end() || found->second.ambiguous) return nullptr;
        return &found->second;
    }

    void checkFunction(const FunctionDecl& fn, bool isMethod) {
        if (fn.body == nullptr) return;
        pushScope();
        if (isMethod) declare("self", Kind::Unknown);
        for (const Param& p : fn.params) declare(p.name, kindOfType(p.type));

        const Kind savedReturn = currentReturn;
        currentReturn = kindOfType(fn.returnType);
        const Kind bodyKind = typeOf(fn.body);
        if (clash(currentReturn, bodyKind))
            error(fn.body->loc, std::string("this function returns ") + describe(currentReturn)
                                    + ", but its last expression is " + describe(bodyKind));
        currentReturn = savedReturn;
        popScope();
    }

    // ---- expressions ----------------------------------------------------

    void visit(const Expr* e) { if (e) typeOf(e); }

    void visitChildren(const Expr& e) {
        visit(e.lhs);
        visit(e.rhs);
        visit(e.condExpr);
        visit(e.elseExpr);
        for (const Expr* a : e.args) visit(a);
        for (const Expr* s : e.statements) visit(s);
        for (const auto& f : e.fields) visit(f.value);
    }

    static void collectBindings(const Pattern* p, std::vector<std::string>& names) {
        if (p == nullptr) return;
        if (p->kind == PatternKind::Binding) names.push_back(p->text);
        for (const Pattern* sub : p->subPatterns) collectBindings(sub, names);
        for (const auto& f : p->fieldPatterns) collectBindings(f.value, names);
    }

    void checkCondition(const Expr& e, const char* what) {
        if (!e.condExpr) return;
        const Kind k = typeOf(e.condExpr);
        if (k == Kind::Text)
            error(e.condExpr->loc, std::string("the condition of '") + what + "' must be a true/false value or a number, not text");
    }

    Kind typeOf(const Expr* e) {
        if (e == nullptr) return Kind::Unknown;
        const Expr& x = *e;

        switch (x.kind) {
            case ExprKind::IntLiteral: return Kind::Int;
            case ExprKind::FloatLiteral: return Kind::Float;
            case ExprKind::BoolLiteral: return Kind::Bool;
            case ExprKind::StringLiteral: return Kind::Text;

            case ExprKind::Identifier: return lookup(x.text);

            case ExprKind::Unary: {
                const Kind k = typeOf(x.lhs);
                switch (x.unaryOp) {
                    case UnaryOp::Neg:
                        if (k == Kind::Text) { error(x.loc, "'-' cannot be applied to text; it works on numbers"); return Kind::Unknown; }
                        return k == Kind::Bool ? Kind::Int : k;
                    case UnaryOp::Not:
                        if (k == Kind::Text || k == Kind::Float) {
                            error(x.loc, std::string("'!' cannot be applied to ") + describe(k) + "; it works on true/false values and whole numbers");
                            return Kind::Bool;
                        }
                        return Kind::Bool;
                    case UnaryOp::Deref: return Kind::Unknown;
                }
                return Kind::Unknown;
            }

            case ExprKind::Binary: return binaryType(x);

            case ExprKind::Assign: {
                const Kind target = typeOf(x.lhs);
                const Kind value = typeOf(x.rhs);
                if (clash(target, value))
                    error(x.loc, std::string("cannot put ") + describe(value) + " into a variable that holds " + describe(target));
                return target;
            }

            case ExprKind::Cast: {
                const Kind from = typeOf(x.lhs);
                const Kind to = kindOfType(x.typeAnnotation);
                if (clash(from, to))
                    error(x.loc, std::string("cannot convert ") + describe(from) + " to " + describe(to) + " with 'as'");
                return to;
            }

            case ExprKind::Call: return callType(x);

            case ExprKind::Member: {
                const Kind k = typeOf(x.lhs);
                if (isNumber(k))
                    error(x.loc, std::string(describe(k)) + " has no members or methods");
                return Kind::Unknown;
            }

            case ExprKind::Index: {
                const Kind base = typeOf(x.lhs);
                const Kind index = typeOf(x.rhs);
                if (isNumber(base))
                    error(x.loc, std::string("cannot index into ") + describe(base));
                if (index == Kind::Text || index == Kind::Float)
                    error(x.loc, std::string("an index must be a whole number, not ") + describe(index));
                return Kind::Unknown;
            }

            case ExprKind::Block: {
                pushScope();
                Kind last = Kind::Unknown;
                for (const Expr* s : x.statements) last = typeOf(s);
                popScope();
                return last;
            }

            case ExprKind::Let: {
                const Kind init = typeOf(x.lhs);
                const Kind declared = kindOfType(x.typeAnnotation);
                if (clash(declared, init))
                    error(x.loc, std::string("'") + x.text + "' is declared as " + describe(declared) + " but given " + describe(init));
                declare(x.text, declared != Kind::Unknown ? declared : init);
                return Kind::Unknown;
            }

            case ExprKind::Return: {
                const Kind value = typeOf(x.lhs);
                if (clash(currentReturn, value))
                    error(x.loc, std::string("this function returns ") + describe(currentReturn) + ", but this returns " + describe(value));
                return Kind::Unknown;
            }

            case ExprKind::If: {
                checkCondition(x, "if");
                const Kind thenKind = typeOf(x.lhs);
                if (x.elseExpr) {
                    const Kind elseKind = typeOf(x.elseExpr);
                    if (clash(thenKind, elseKind))
                        error(x.elseExpr->loc, std::string("the two branches of this 'if' give different kinds of value: the first gives ")
                                                   + describe(thenKind) + ", the 'else' gives " + describe(elseKind));
                    return thenKind == elseKind ? thenKind : Kind::Unknown;
                }
                return Kind::Unknown;
            }

            case ExprKind::While:
                checkCondition(x, "while");
                visit(x.lhs);
                return Kind::Unknown;

            case ExprKind::For: {
                const Kind from = typeOf(x.condExpr);
                const Kind to = typeOf(x.rhs);
                if (from == Kind::Text || to == Kind::Text)
                    error(x.loc, "the bounds of a 'for' range must be numbers, not text");
                pushScope();
                declare(x.text, Kind::Int);
                visit(x.lhs);
                popScope();
                return Kind::Unknown;
            }

            case ExprKind::ArrayLiteral: {
                // A list holds one kind of value: [1, 2, 3], not [1, "x", 2.5].
                Kind first = Kind::Unknown;
                for (const Expr* item : x.args) {
                    const Kind k = typeOf(item);
                    if (k == Kind::Unknown) continue;
                    if (first == Kind::Unknown) first = k;
                    else if (k != first)
                        error(item->loc, std::string("every item in a list must be the same kind of value: this one is ")
                                             + describe(k) + ", but the first is " + describe(first));
                }
                return Kind::Unknown;
            }

            case ExprKind::Match: {
                visit(x.condExpr);
                Kind firstArm = Kind::Unknown;
                for (const MatchArm& arm : x.matchArms) {
                    pushScope();
                    std::vector<std::string> names;
                    collectBindings(arm.pattern, names);
                    for (const auto& n : names) declare(n, Kind::Unknown);
                    const Kind armKind = typeOf(arm.body);
                    popScope();
                    if (armKind == Kind::Unknown) continue;
                    if (firstArm == Kind::Unknown) firstArm = armKind;
                    else if (clash(firstArm, armKind))
                        error(arm.loc, std::string("the arms of this 'match' give different kinds of value: the first gives ")
                                           + describe(firstArm) + ", this one gives " + describe(armKind));
                }
                return Kind::Unknown;
            }

            case ExprKind::Closure: {
                pushScope();
                for (const Param& p : x.params) declare(p.name, Kind::Unknown);
                const Kind savedReturn = currentReturn;
                currentReturn = Kind::Unknown;
                visit(x.lhs);
                currentReturn = savedReturn;
                popScope();
                return Kind::Unknown;
            }

            case ExprKind::Handle: {
                visitChildren(x);
                for (const HandleCase& c : x.handleCases) {
                    pushScope();
                    for (const Param& p : c.params) declare(p.name, Kind::Unknown);
                    visit(c.body);
                    popScope();
                }
                return Kind::Unknown;
            }

            default:
                // Path, Loop, Break, Continue, ArrayLiteral, StructLiteral, SmartPtrNew, BuildTime, Quote,
                // Unquote, Perform, Resume, EnumVariantNew: look inside, but say nothing about the result.
                visitChildren(x);
                return Kind::Unknown;
        }
    }

    static bool isNullName(const Expr* e) { return e && e->kind == ExprKind::Identifier && e->text == "null"; }

    Kind binaryType(const Expr& x) {
        const Kind l = typeOf(x.lhs);
        const Kind r = typeOf(x.rhs);
        const BinaryOp op = x.binaryOp;

        const bool equality = op == BinaryOp::Eq || op == BinaryOp::Neq;
        const bool ordering = op == BinaryOp::Lt || op == BinaryOp::Gt || op == BinaryOp::Le || op == BinaryOp::Ge;
        const bool arithmetic = op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul || op == BinaryOp::Div || op == BinaryOp::Mod;
        const bool bitwise = op == BinaryOp::BitOr || op == BinaryOp::BitXor || op == BinaryOp::BitAnd;

        if (l == Kind::Text || r == Kind::Text) {
            if (equality) {
                if (clash(l, r))
                    error(x.loc, std::string("cannot compare ") + describe(l) + " with " + describe(r));
                else if (!isNullName(x.lhs) && !isNullName(x.rhs))   // `s == null` is a real pointer test; only text against text is wrong
                    error(x.loc, std::string("'") + symbol(op) + "' on text compares where the texts are stored, not their characters, "
                                     "so it is never what you mean; use " + (op == BinaryOp::Eq ? "text_equals(a, b)" : "!text_equals(a, b)")
                                     + " (or text_equals_ignore_case(a, b))");
                return Kind::Bool;
            }
            error(x.loc, std::string("the operator '") + symbol(op) + "' does not work on text; it works on numbers"
                             + (op == BinaryOp::Add ? " (there is no text concatenation)" : ""));
            return (ordering) ? Kind::Bool : Kind::Unknown;
        }

        if (equality || ordering) return Kind::Bool;
        if (arithmetic) {
            if (l == Kind::Float || r == Kind::Float) return Kind::Float;
            if (isNumber(l) && isNumber(r)) return Kind::Int;
            return Kind::Unknown;
        }
        if (bitwise && l == Kind::Bool && r == Kind::Bool) return Kind::Bool;
        if (isNumber(l) && isNumber(r)) return Kind::Int;
        return Kind::Unknown;
    }

    // text_equals, text_equals_ignore_case, text_starts_with, text_ends_with and text_contains take two texts and give a bool;
    // text_compare takes two and gives a number; text_length takes one and gives a number.
    Kind textBuiltinType(const Expr& x, const std::vector<Kind>& argKinds) {
        const std::string& name = x.lhs->text;
        const bool one = name == "text_length";
        const bool boolResult = name == "text_equals" || name == "text_equals_ignore_case" || name == "text_starts_with"
                                || name == "text_ends_with" || name == "text_contains";
        if (!one && !boolResult && name != "text_compare") return Kind::Unknown;

        const size_t wanted = one ? 1 : 2;
        if (argKinds.size() != wanted) {
            error(x.loc, "'" + name + "' takes " + std::to_string(wanted) + " argument(s), but " + std::to_string(argKinds.size()) + " were given");
        } else {
            for (size_t i = 0; i < argKinds.size(); ++i)
                if (isNumber(argKinds[i]) || argKinds[i] == Kind::Bool)
                    error(x.args[i]->loc, "argument " + std::to_string(i + 1) + " of '" + name + "' must be text, not " + describe(argKinds[i]));
        }
        return boolResult ? Kind::Bool : Kind::Int;
    }

    Kind callType(const Expr& x) {
        std::vector<Kind> argKinds;
        for (const Expr* a : x.args) argKinds.push_back(typeOf(a));

        // A method or field call: look inside the object, say nothing about the result.
        if (x.lhs && x.lhs->kind == ExprKind::Member) {
            typeOf(x.lhs);
            return Kind::Unknown;
        }
        if (x.lhs && x.lhs->kind != ExprKind::Identifier && x.lhs->kind != ExprKind::Path) {
            const Kind callee = typeOf(x.lhs);
            if (callee == Kind::Text || isNumber(callee))
                error(x.loc, std::string("cannot call ") + describe(callee) + " like a function");
            return Kind::Unknown;
        }

        if (x.lhs && x.lhs->kind == ExprKind::Identifier) {
            const Kind held = lookup(x.lhs->text);
            if (isNumber(held) || held == Kind::Text) {
                error(x.loc, std::string("'") + x.lhs->text + "' holds " + describe(held) + ", not a function");
                return Kind::Unknown;
            }
        }

        const Signature* sig = x.lhs ? signatureOf(*x.lhs) : nullptr;
        if (sig == nullptr) {
            // The built-in text functions (a program that defines one of its own is handled by its signature above).
            if (x.lhs && x.lhs->kind == ExprKind::Identifier)
                return textBuiltinType(x, argKinds);
            return Kind::Unknown;
        }

        const std::string name = x.lhs->kind == ExprKind::Identifier ? x.lhs->text : "function";
        if (x.explicitGenericArgs.empty() && argKinds.size() != sig->params.size()) {
            error(x.loc, std::string("'") + name + "' takes " + std::to_string(sig->params.size()) + " argument(s), but "
                             + std::to_string(argKinds.size()) + " were given");
            return sig->result;
        }
        for (size_t i = 0; i < argKinds.size() && i < sig->params.size(); ++i) {
            if (clash(sig->params[i], argKinds[i]))
                error(x.args[i]->loc, std::string("argument ") + std::to_string(i + 1) + " of '" + name + "' must be "
                                          + describe(sig->params[i]) + ", not " + describe(argKinds[i]));
        }
        return sig->result;
    }
};

} // namespace frust
