#pragma once

#include <memory>
#include <string>
#include <vector>

namespace frust {

struct Expr; // AST.h's type - only named here, never defined; see evaluateStmt().

// One-shot REPL evaluation: parses a single line, JIT-compiles it, runs it,
// and returns a human-readable result (or error message) as plain text -
// no LLVM/AST/bison types leak into this header, so a consumer (like the
// JUCE host's console panel) only needs this one header and to link
// frust_lang, nothing else.
//
// `let`-bound variables DO persist across evaluate() calls within a
// session, but not by keeping JIT state alive - each call still gets a
// fresh module. Instead, every bound name's *computed value* (always an
// f64 - see evaluate()'s implementation notes) is remembered in plain C++
// state and replayed as a constant into each new compile. That's also
// exactly what makes exportAsJson()/importFromJson() possible: the whole
// session context is just a name->number map, trivially serializable.
class ReplSession {
public:
    ReplSession();
    ~ReplSession();

    ReplSession(const ReplSession&) = delete;
    ReplSession& operator=(const ReplSession&) = delete;

    std::string evaluate(const std::string& line);

    // F#-Interactive-style "run this script into the live session": parses
    // the whole source as one program (statements need their usual ";"
    // separators - see frust.y's block-statement comment for why) and
    // evaluates each top-level statement in order, threading bound-variable
    // state through exactly as if each had been typed into evaluate() one
    // at a time. Returns one result string per top-level statement, in
    // source order (declarations among them report "(defined)", same as
    // evaluate() does for a single one). A whole-script parse error
    // produces a single-element vector: {"(parse error)"}.
    std::vector<std::string> runScript(const std::string& source);

    // Forgets every bound variable. Does not affect anything already
    // printed by the caller - this is data-reset, not a display clear.
    void reset();

    // Serializes the current bound-variable context as JSON text
    // (`{"x": 7.4, "y": 3}`-shaped). Always succeeds (an empty context
    // serializes to "{}").
    std::string exportAsJson() const;

    // Replaces the current bound-variable context with the one parsed from
    // `json` (same shape as exportAsJson() produces). Returns false (and
    // leaves the existing context untouched) if `json` doesn't parse as a
    // flat object of numbers.
    bool importFromJson(const std::string& json);

    // One entry per bound variable, for a UI (e.g. the IDE's context
    // panel) to display. `typeName` is always "f64" today - there's only
    // one runtime value kind right now (see this header's earlier note);
    // this field exists so a UI can already show a Type column without
    // needing to change its shape once real types land.
    struct Binding {
        std::string name;
        std::string typeName;
        double value;
    };
    std::vector<Binding> listBindings() const;

private:
    // Shared by evaluate() and runScript(): compiles+JITs+runs one already-
    // parsed top-level statement against the current bound-variable state,
    // persisting it if the statement was a `let`.
    std::string evaluateStmt(Expr* stmtExpr);

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace frust
