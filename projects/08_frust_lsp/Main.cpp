// frust_lsp: a Language Server Protocol implementation for Frust, speaking
// LSP over stdio (Content-Length-framed JSON-RPC 2.0). Links frust_lang
// directly (Lexer/Parser/AstArena/AST.h) - the same parser the real
// compiler uses, so diagnostics here can never drift from what actually
// compiles. Deliberately does NOT go through ReplSession (that API only
// returns numeric one-shot eval results, not an AST).
//
// v1 scope (see the approved plan for the full rationale): diagnostics,
// hover, completion, documentSymbol, and definition are all SAME-FILE
// only - no cross-pod resolution (that's ModuleLoader's job, and adding
// it here is real additional complexity, not done in this pass).
//
// hover/definition use word-under-cursor + name lookup against the
// current file's top-level declarations, not full AST position
// hit-testing - Frust's AST only stores a start SourceLoc per node (no
// end position/span), so precise span-based hit-testing isn't available
// without adding that to the AST first. Word-under-cursor is a standard,
// real technique many language servers start with - not a shortcut that
// silently produces wrong answers, just a documented precision boundary.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST.h"
#include "Lexer.h"
#include "parser.hpp"

#include <juce_core/juce_core.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

using namespace frust;

namespace {

// ---------------------------------------------------------------------
// stdio transport: Content-Length-framed JSON-RPC, per the LSP spec.
// ---------------------------------------------------------------------

// On Windows, stdin/stdout default to text mode, which silently rewrites
// \n <-> \r\n - fatal for a byte-exact Content-Length framing. No-op on
// Linux, where there's no such translation to begin with.
void setBinaryStdio() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

std::optional<std::string> readMessage(std::istream& in) {
    size_t contentLength = 0;
    std::string line;
    bool sawContentLength = false;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // blank line -> end of headers

        const std::string prefix = "Content-Length: ";
        if (line.rfind(prefix, 0) == 0) {
            contentLength = static_cast<size_t>(std::stoul(line.substr(prefix.size())));
            sawContentLength = true;
        }
        // Other headers (Content-Type) are ignored - LSP always sends
        // Content-Length; Content-Type, when present, is constant.
    }

    if (!sawContentLength) return std::nullopt; // EOF or malformed stream

    std::string body(contentLength, '\0');
    in.read(&body[0], static_cast<std::streamsize>(contentLength));
    if (static_cast<size_t>(in.gcount()) != contentLength) return std::nullopt;
    return body;
}

void writeMessage(std::ostream& out, const juce::var& body) {
    juce::String json = juce::JSON::toString(body, true); // true = all on one line
    std::string bodyStd = json.toStdString();
    out << "Content-Length: " << bodyStd.size() << "\r\n\r\n" << bodyStd;
    out.flush();
}

juce::var makeResponse(const juce::var& id, const juce::var& result) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("jsonrpc", "2.0");
    obj->setProperty("id", id);
    obj->setProperty("result", result);
    return juce::var(obj);
}

juce::var makeNotification(const juce::String& method, const juce::var& params) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("jsonrpc", "2.0");
    obj->setProperty("method", method);
    obj->setProperty("params", params);
    return juce::var(obj);
}

juce::var makePosition(int line, int col) {
    // LSP positions are 0-based; Frust's SourceLoc/bison locations are
    // 1-based (matches how frust_compiler already prints "line 21, col
    // 8" to a human). Convert once, here, rather than scattering -1s.
    auto* obj = new juce::DynamicObject();
    obj->setProperty("line", juce::jmax(0, line - 1));
    obj->setProperty("character", juce::jmax(0, col - 1));
    return juce::var(obj);
}

juce::var makeRange(int line, int col, int endCol) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("start", makePosition(line, col));
    obj->setProperty("end", makePosition(line, endCol));
    return juce::var(obj);
}

// ---------------------------------------------------------------------
// Parsing - same 4-line pattern already duplicated in Main.cpp/
// ModuleLoader.cpp/ReplSession.cpp (see AST.h's ParseError comment for
// why structuredErrors is a separate parameter from the existing
// human-formatted parseErrors vector).
// ---------------------------------------------------------------------

Program* parseFrustSource(const std::string& text, AstArena& arena, std::vector<ParseError>& structuredErrors) {
    std::istringstream input(text);
    Lexer lexer(&input);
    Program* result = nullptr;
    std::vector<std::string> parseErrors; // unused here - see AST.h's ParseError
    Parser parser(lexer, arena, parseErrors, result, structuredErrors);
    parser.parse();
    return result;
}

// ---------------------------------------------------------------------
// Type/signature rendering, for hover text.
// ---------------------------------------------------------------------

std::string smartPtrKindName(SmartPtrKind kind) {
    switch (kind) {
        case SmartPtrKind::Own: return "own ";
        case SmartPtrKind::Shared: return "shared ";
        case SmartPtrKind::Weak: return "weak ";
        case SmartPtrKind::Raw: return "raw* ";
        case SmartPtrKind::Ref: return "&";
        default: return "";
    }
}

std::string renderType(const TypeExpr* type) {
    if (!type) return "()";
    std::string out = smartPtrKindName(type->ptrKind);
    if (type->isRawPointer) out += "raw* ";
    out += type->name;
    if (!type->genericArgs.empty()) {
        out += "<";
        for (size_t i = 0; i < type->genericArgs.size(); ++i) {
            if (i > 0) out += ", ";
            const auto& arg = type->genericArgs[i];
            out += arg.isIntConst ? std::to_string(arg.intConst) : renderType(arg.type);
        }
        out += ">";
    }
    return out;
}

std::string renderFunctionSignature(const FunctionDecl* fn) {
    std::string out = fn->isPub ? "pub fn " : "fn ";
    out += fn->name + "(";
    if (fn->isMethod) {
        out += (fn->selfKind == SelfKind::MutRef ? "&mut self" : fn->selfKind == SelfKind::Ref ? "&self" : "self");
        if (!fn->params.empty()) out += ", ";
    }
    for (size_t i = 0; i < fn->params.size(); ++i) {
        if (i > 0) out += ", ";
        out += fn->params[i].name + ": " + renderType(fn->params[i].type);
    }
    out += ") -> " + renderType(fn->returnType);
    return out;
}

std::string renderStructSignature(const StructDecl* s) {
    std::string out = "struct " + s->name + " { ";
    for (size_t i = 0; i < s->fields.size(); ++i) {
        if (i > 0) out += ", ";
        out += s->fields[i].name + ": " + renderType(s->fields[i].type);
    }
    out += " }";
    return out;
}

// ---------------------------------------------------------------------
// Static keyword/type list for completion (see grammar/frust.y's %token
// block and Codegen.h's resolveType() for the source of truth).
// ---------------------------------------------------------------------

const std::vector<std::string>& reservedKeywords() {
    static const std::vector<std::string> kws = {
        "fn", "pub", "unsafe", "extern", "entry", "use", "let", "mut", "return",
        "if", "else", "while", "loop", "for", "break", "continue",
        "impl", "self", "struct", "type", "effect", "perform",
        "handle", "resume", "with", "component",
        "in", "out", "build_time", "quote", "unquote",
        "as", "own", "shared", "weak", "raw",
        "true", "false",
    };
    return kws;
}

const std::vector<std::string>& primitiveTypes() {
    static const std::vector<std::string> types = {
        "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "usize", "isize",
        "f32", "f64", "bool", "String", "Vec",
    };
    return types;
}

// ---------------------------------------------------------------------
// Document store
// ---------------------------------------------------------------------

struct DocumentState {
    std::string text;
    std::unique_ptr<AstArena> arena;
    Program* program = nullptr;
    std::vector<ParseError> diagnostics;
};

std::unordered_map<std::string, DocumentState> documents;

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);
    return lines;
}

// Extracts the identifier word touching 0-based (line, character) - LSP's
// position convention - for hover/definition's word-under-cursor lookup.
std::optional<std::string> wordAt(const std::string& text, int lspLine, int lspChar) {
    auto lines = splitLines(text);
    if (lspLine < 0 || static_cast<size_t>(lspLine) >= lines.size()) return std::nullopt;
    const std::string& line = lines[static_cast<size_t>(lspLine)];
    if (line.empty()) return std::nullopt;

    auto isIdentChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };

    int pos = juce::jlimit(0, static_cast<int>(line.size()) - 1, lspChar);
    if (!isIdentChar(line[static_cast<size_t>(pos)])) {
        // Cursor might be right after the word (common case: end of a click).
        if (pos > 0 && isIdentChar(line[static_cast<size_t>(pos - 1)])) pos -= 1;
        else return std::nullopt;
    }

    int start = pos, end = pos;
    while (start > 0 && isIdentChar(line[static_cast<size_t>(start - 1)])) start -= 1;
    while (end + 1 < static_cast<int>(line.size()) && isIdentChar(line[static_cast<size_t>(end + 1)])) end += 1;

    return line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start + 1));
}

void reparseDocument(const std::string& uri, DocumentState& doc) {
    doc.arena = std::make_unique<AstArena>();
    doc.diagnostics.clear();
    doc.program = parseFrustSource(doc.text, *doc.arena, doc.diagnostics);
}

void publishDiagnostics(std::ostream& out, const std::string& uri, const DocumentState& doc) {
    juce::Array<juce::var> diags;
    for (const auto& err : doc.diagnostics) {
        auto* d = new juce::DynamicObject();
        d->setProperty("range", makeRange(err.line, err.col, err.col + 1));
        d->setProperty("severity", 1); // Error
        d->setProperty("source", "frust");
        d->setProperty("message", juce::String(err.message));
        diags.add(juce::var(d));
    }

    auto* params = new juce::DynamicObject();
    params->setProperty("uri", juce::String(uri));
    params->setProperty("diagnostics", diags);
    writeMessage(out, makeNotification("textDocument/publishDiagnostics", juce::var(params)));
}

// ---------------------------------------------------------------------
// LSP method handlers
// ---------------------------------------------------------------------

juce::var handleInitialize() {
    auto* textDocSync = new juce::DynamicObject();
    textDocSync->setProperty("openClose", true);
    textDocSync->setProperty("change", 1); // Full document sync

    auto* completionOptions = new juce::DynamicObject();
    completionOptions->setProperty("triggerCharacters", juce::Array<juce::var>{juce::var("."), juce::var(":")});

    auto* capabilities = new juce::DynamicObject();
    capabilities->setProperty("textDocumentSync", juce::var(textDocSync));
    capabilities->setProperty("hoverProvider", true);
    capabilities->setProperty("completionProvider", juce::var(completionOptions));
    capabilities->setProperty("definitionProvider", true);
    capabilities->setProperty("documentSymbolProvider", true);

    auto* serverInfo = new juce::DynamicObject();
    serverInfo->setProperty("name", "frust_lsp");
    serverInfo->setProperty("version", "0.1.0");

    auto* result = new juce::DynamicObject();
    result->setProperty("capabilities", juce::var(capabilities));
    result->setProperty("serverInfo", juce::var(serverInfo));
    return juce::var(result);
}

juce::var handleDocumentSymbol(const DocumentState& doc) {
    juce::Array<juce::var> symbols;
    if (!doc.program) return symbols;

    for (auto* decl : doc.program->decls) {
        if (decl->kind == DeclKind::Function && decl->functionDecl) {
            auto* sym = new juce::DynamicObject();
            sym->setProperty("name", juce::String(decl->functionDecl->name));
            sym->setProperty("kind", 12); // SymbolKind.Function
            auto range = makeRange(decl->functionDecl->loc.line, decl->functionDecl->loc.col,
                                    decl->functionDecl->loc.col + static_cast<int>(decl->functionDecl->name.size()));
            sym->setProperty("range", range);
            sym->setProperty("selectionRange", range);
            symbols.add(juce::var(sym));
        } else if (decl->kind == DeclKind::Struct && decl->structDecl) {
            auto* sym = new juce::DynamicObject();
            sym->setProperty("name", juce::String(decl->structDecl->name));
            sym->setProperty("kind", 23); // SymbolKind.Struct
            auto range = makeRange(decl->structDecl->loc.line, decl->structDecl->loc.col,
                                    decl->structDecl->loc.col + static_cast<int>(decl->structDecl->name.size()));
            sym->setProperty("range", range);
            sym->setProperty("selectionRange", range);
            symbols.add(juce::var(sym));
        } else if (decl->kind == DeclKind::Impl && decl->implDecl) {
            auto* sym = new juce::DynamicObject();
            sym->setProperty("name", "impl " + juce::String(decl->implDecl->typeName));
            sym->setProperty("kind", 3); // SymbolKind.Namespace
            auto range = makeRange(decl->implDecl->loc.line, decl->implDecl->loc.col, decl->implDecl->loc.col + 4);
            sym->setProperty("range", range);
            sym->setProperty("selectionRange", range);

            juce::Array<juce::var> children;
            for (auto* method : decl->implDecl->methods) {
                auto* childSym = new juce::DynamicObject();
                childSym->setProperty("name", juce::String(method->name));
                childSym->setProperty("kind", 6); // SymbolKind.Method
                auto childRange = makeRange(method->loc.line, method->loc.col,
                                             method->loc.col + static_cast<int>(method->name.size()));
                childSym->setProperty("range", childRange);
                childSym->setProperty("selectionRange", childRange);
                children.add(juce::var(childSym));
            }
            sym->setProperty("children", children);
            symbols.add(juce::var(sym));
        }
    }
    return symbols;
}

// Finds a top-level FunctionDecl or StructDecl by name in this document.
// Returns {isFunction, functionDecl, structDecl}.
struct FoundSymbol {
    FunctionDecl* fn = nullptr;
    StructDecl* st = nullptr;
};

FoundSymbol findSymbol(const DocumentState& doc, const std::string& name) {
    FoundSymbol found;
    if (!doc.program) return found;
    for (auto* decl : doc.program->decls) {
        if (decl->kind == DeclKind::Function && decl->functionDecl && decl->functionDecl->name == name) {
            found.fn = decl->functionDecl;
            return found;
        }
        if (decl->kind == DeclKind::Struct && decl->structDecl && decl->structDecl->name == name) {
            found.st = decl->structDecl;
            return found;
        }
        if (decl->kind == DeclKind::Impl && decl->implDecl) {
            for (auto* method : decl->implDecl->methods) {
                if (method->name == name) {
                    found.fn = method;
                    return found;
                }
            }
        }
    }
    return found;
}

juce::var handleHover(const DocumentState& doc, int lspLine, int lspChar) {
    auto word = wordAt(doc.text, lspLine, lspChar);
    if (!word) return juce::var(); // null result - no hover here

    FoundSymbol found = findSymbol(doc, *word);
    std::string markdown;
    if (found.fn) markdown = "```frust\n" + renderFunctionSignature(found.fn) + "\n```";
    else if (found.st) markdown = "```frust\n" + renderStructSignature(found.st) + "\n```";
    else return juce::var(); // not a symbol we know about in this file

    auto* contents = new juce::DynamicObject();
    contents->setProperty("kind", "markdown");
    contents->setProperty("value", juce::String(markdown));

    auto* result = new juce::DynamicObject();
    result->setProperty("contents", juce::var(contents));
    return juce::var(result);
}

juce::var handleDefinition(const DocumentState& doc, const std::string& uri, int lspLine, int lspChar) {
    auto word = wordAt(doc.text, lspLine, lspChar);
    if (!word) return juce::var();

    FoundSymbol found = findSymbol(doc, *word);
    SourceLoc loc;
    int nameLen = static_cast<int>(word->size());
    if (found.fn) loc = found.fn->loc;
    else if (found.st) loc = found.st->loc;
    else return juce::var();

    auto* result = new juce::DynamicObject();
    result->setProperty("uri", juce::String(uri));
    result->setProperty("range", makeRange(loc.line, loc.col, loc.col + nameLen));
    return juce::var(result);
}

juce::var handleCompletion(const DocumentState& doc) {
    juce::Array<juce::var> items;

    auto addItem = [&items](const std::string& label, int kind) {
        auto* item = new juce::DynamicObject();
        item->setProperty("label", juce::String(label));
        item->setProperty("kind", kind);
        items.add(juce::var(item));
    };

    for (const auto& kw : reservedKeywords()) addItem(kw, 14);   // Keyword
    for (const auto& ty : primitiveTypes()) addItem(ty, 7);      // Class (closest stand-in for a builtin type)

    if (doc.program) {
        for (auto* decl : doc.program->decls) {
            if (decl->kind == DeclKind::Function && decl->functionDecl) addItem(decl->functionDecl->name, 3);   // Function
            else if (decl->kind == DeclKind::Struct && decl->structDecl) addItem(decl->structDecl->name, 22);   // Struct
            else if (decl->kind == DeclKind::Impl && decl->implDecl) {
                for (auto* method : decl->implDecl->methods) addItem(method->name, 2); // Method
            }
        }
    }

    return items;
}

// ---------------------------------------------------------------------
// Main dispatch loop
// ---------------------------------------------------------------------

int getInt(const juce::var& v, int fallback = 0) {
    return v.isVoid() ? fallback : static_cast<int>(v);
}

} // namespace

int main() {
    setBinaryStdio();

    while (true) {
        auto msg = readMessage(std::cin);
        if (!msg) break; // client disconnected

        juce::var request = juce::JSON::parse(juce::String(*msg));
        if (!request.isObject()) continue;
        auto* obj = request.getDynamicObject();

        juce::String method = obj->getProperty("method").toString();
        juce::var params = obj->getProperty("params");
        bool hasId = obj->hasProperty("id");
        juce::var id = hasId ? obj->getProperty("id") : juce::var();

        if (method == "initialize") {
            writeMessage(std::cout, makeResponse(id, handleInitialize()));
        } else if (method == "initialized") {
            // notification, nothing to do
        } else if (method == "shutdown") {
            writeMessage(std::cout, makeResponse(id, juce::var()));
        } else if (method == "exit") {
            break;
        } else if (method == "textDocument/didOpen") {
            auto* p = params.getDynamicObject();
            auto* textDoc = p->getProperty("textDocument").getDynamicObject();
            std::string uri = textDoc->getProperty("uri").toString().toStdString();
            DocumentState& doc = documents[uri];
            doc.text = textDoc->getProperty("text").toString().toStdString();
            reparseDocument(uri, doc);
            publishDiagnostics(std::cout, uri, doc);
        } else if (method == "textDocument/didChange") {
            auto* p = params.getDynamicObject();
            auto* textDoc = p->getProperty("textDocument").getDynamicObject();
            std::string uri = textDoc->getProperty("uri").toString().toStdString();
            auto* changes = p->getProperty("contentChanges").getArray();
            if (changes && !changes->isEmpty()) {
                // Full document sync (see textDocumentSync.change = 1 in
                // handleInitialize) - each change's "text" IS the whole
                // new document, not a delta.
                auto* lastChange = changes->getLast().getDynamicObject();
                DocumentState& doc = documents[uri];
                doc.text = lastChange->getProperty("text").toString().toStdString();
                reparseDocument(uri, doc);
                publishDiagnostics(std::cout, uri, doc);
            }
        } else if (method == "textDocument/didClose") {
            auto* p = params.getDynamicObject();
            auto* textDoc = p->getProperty("textDocument").getDynamicObject();
            std::string uri = textDoc->getProperty("uri").toString().toStdString();
            documents.erase(uri);
        } else if (method == "textDocument/hover") {
            auto* p = params.getDynamicObject();
            std::string uri = p->getProperty("textDocument").getDynamicObject()->getProperty("uri").toString().toStdString();
            auto* pos = p->getProperty("position").getDynamicObject();
            auto it = documents.find(uri);
            juce::var result = (it != documents.end())
                ? handleHover(it->second, getInt(pos->getProperty("line")), getInt(pos->getProperty("character")))
                : juce::var();
            writeMessage(std::cout, makeResponse(id, result));
        } else if (method == "textDocument/definition") {
            auto* p = params.getDynamicObject();
            std::string uri = p->getProperty("textDocument").getDynamicObject()->getProperty("uri").toString().toStdString();
            auto* pos = p->getProperty("position").getDynamicObject();
            auto it = documents.find(uri);
            juce::var result = (it != documents.end())
                ? handleDefinition(it->second, uri, getInt(pos->getProperty("line")), getInt(pos->getProperty("character")))
                : juce::var();
            writeMessage(std::cout, makeResponse(id, result));
        } else if (method == "textDocument/documentSymbol") {
            auto* p = params.getDynamicObject();
            std::string uri = p->getProperty("textDocument").getDynamicObject()->getProperty("uri").toString().toStdString();
            auto it = documents.find(uri);
            juce::var result = (it != documents.end()) ? juce::var(handleDocumentSymbol(it->second)) : juce::var(juce::Array<juce::var>());
            writeMessage(std::cout, makeResponse(id, result));
        } else if (method == "textDocument/completion") {
            auto* p = params.getDynamicObject();
            std::string uri = p->getProperty("textDocument").getDynamicObject()->getProperty("uri").toString().toStdString();
            auto it = documents.find(uri);
            juce::var result = (it != documents.end()) ? juce::var(handleCompletion(it->second)) : juce::var(juce::Array<juce::var>());
            writeMessage(std::cout, makeResponse(id, result));
        } else if (hasId) {
            // Unknown request we don't support - respond with a
            // MethodNotFound error rather than silently hanging the client.
            auto* err = new juce::DynamicObject();
            err->setProperty("code", -32601);
            err->setProperty("message", "Method not found: " + method);
            auto* resp = new juce::DynamicObject();
            resp->setProperty("jsonrpc", "2.0");
            resp->setProperty("id", id);
            resp->setProperty("error", juce::var(err));
            writeMessage(std::cout, juce::var(resp));
        }
        // Unknown notifications (no id) are silently ignored per the LSP spec.
    }

    return 0;
}
