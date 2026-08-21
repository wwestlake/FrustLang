#pragma once

// AST content hashing (FRUST_LANG_SPEC.md 1.4: "AST content hashing for
// zero-latency OrcJIT hot-reloading"). Was named in the spec, never built -
// frust_plugin_host's frust_plugin_reload() unconditionally tore down and
// rebuilt the whole JITDylib on every call, even when the source on disk
// hadn't meaningfully changed (a save with no edits, a file-watcher firing
// twice, a comment-only edit).
//
// Hashes structural CONTENT only - ExprKind/BinaryOp/etc. tags, literal
// values, identifier/string text, and recursive children - deliberately
// never SourceLoc (line/col). Two ASTs that differ only in formatting or
// comment placement must hash identically, or "skip reload when nothing
// changed" would misfire on every whitespace tweak, defeating the point.
//
// BLAKE3 is used via llvm::BLAKE3 (llvm/Support/BLAKE3.h) - already a
// transitive dependency of every target that links LLVM here, so this adds
// no new external dependency.

#include <cstdint>
#include <string>
#include <type_traits>

#include "AST.h"

#include <llvm/Support/BLAKE3.h>

namespace frust {

namespace detail {

inline void hashBytes(llvm::BLAKE3& hasher, const void* data, size_t len) {
    hasher.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(data), len));
}

template <typename T>
inline void hashPod(llvm::BLAKE3& hasher, const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "hashPod requires a trivially-copyable type");
    hashBytes(hasher, &value, sizeof(T));
}

inline void hashStr(llvm::BLAKE3& hasher, const std::string& s) {
    // Length-prefixed so "ab","c" and "a","bc" (adjacent string fields)
    // can never collide to the same byte stream.
    uint64_t len = s.size();
    hashPod(hasher, len);
    if (!s.empty()) hasher.update(llvm::StringRef(s));
}

void hashType(llvm::BLAKE3& hasher, const TypeExpr* type);
void hashExpr(llvm::BLAKE3& hasher, const Expr* expr);

inline void hashParam(llvm::BLAKE3& hasher, const Param& p) {
    hashStr(hasher, p.name);
    hashType(hasher, p.type);
}

inline void hashType(llvm::BLAKE3& hasher, const TypeExpr* type) {
    bool present = (type != nullptr);
    hashPod(hasher, present);
    if (!present) return;

    hashPod(hasher, type->ptrKind);
    hashPod(hasher, type->isRawPointer);
    hashStr(hasher, type->name);

    uint64_t argCount = type->genericArgs.size();
    hashPod(hasher, argCount);
    for (auto& arg : type->genericArgs) {
        hashPod(hasher, arg.isIntConst);
        if (arg.isIntConst) {
            hashPod(hasher, arg.intConst);
        } else {
            hashType(hasher, arg.type);
        }
    }

    hashPod(hasher, type->refinementKind);
    if (type->refinementKind == RefinementKind::Range) {
        hashPod(hasher, type->refLow);
        hashPod(hasher, type->refHigh);
    } else if (type->refinementKind == RefinementKind::Compare) {
        hashPod(hasher, type->refCmpOp);
        hashPod(hasher, type->refCmpValue);
    }
}

inline void hashExpr(llvm::BLAKE3& hasher, const Expr* expr) {
    bool present = (expr != nullptr);
    hashPod(hasher, present);
    if (!present) return;

    hashPod(hasher, expr->kind);
    hashPod(hasher, expr->intValue);
    hashPod(hasher, expr->floatValue);
    hashPod(hasher, expr->boolValue);
    hashPod(hasher, expr->isMut);
    hashStr(hasher, expr->text);

    uint64_t pathCount = expr->pathSegments.size();
    hashPod(hasher, pathCount);
    for (auto& seg : expr->pathSegments) hashStr(hasher, seg);

    hashPod(hasher, expr->unaryOp);
    hashPod(hasher, expr->binaryOp);
    hashPod(hasher, expr->smartPtrKind);

    hashType(hasher, expr->typeAnnotation);

    hashExpr(hasher, expr->lhs);
    hashExpr(hasher, expr->rhs);
    hashExpr(hasher, expr->condExpr);
    hashExpr(hasher, expr->elseExpr);

    uint64_t argCount = expr->args.size();
    hashPod(hasher, argCount);
    for (auto* a : expr->args) hashExpr(hasher, a);

    uint64_t stmtCount = expr->statements.size();
    hashPod(hasher, stmtCount);
    for (auto* s : expr->statements) hashExpr(hasher, s);

    uint64_t fieldCount = expr->fields.size();
    hashPod(hasher, fieldCount);
    for (auto& f : expr->fields) {
        hashStr(hasher, f.name);
        hashExpr(hasher, f.value);
    }

    uint64_t handleCaseCount = expr->handleCases.size();
    hashPod(hasher, handleCaseCount);
    for (auto& hc : expr->handleCases) {
        hashStr(hasher, hc.effectName);
        uint64_t paramCount = hc.params.size();
        hashPod(hasher, paramCount);
        for (auto& p : hc.params) hashParam(hasher, p);
        hashExpr(hasher, hc.body);
    }
}

inline void hashDecl(llvm::BLAKE3& hasher, const Decl* decl) {
    hashPod(hasher, decl->kind);
    switch (decl->kind) {
        case DeclKind::Function: {
            auto* fn = decl->functionDecl;
            hashStr(hasher, fn->name);
            hashPod(hasher, fn->isPub);
            hashPod(hasher, fn->isUnsafe);
            hashPod(hasher, fn->isExtern);
            hashPod(hasher, fn->isEntry);
            uint64_t paramCount = fn->params.size();
            hashPod(hasher, paramCount);
            for (auto& p : fn->params) hashParam(hasher, p);
            hashType(hasher, fn->returnType);
            hashExpr(hasher, fn->body);
            hashPod(hasher, fn->isMethod);
            hashStr(hasher, fn->selfTypeName);
            hashPod(hasher, fn->selfKind);
            break;
        }
        case DeclKind::Struct: {
            auto* sd = decl->structDecl;
            hashStr(hasher, sd->name);
            uint64_t fieldCount = sd->fields.size();
            hashPod(hasher, fieldCount);
            for (auto& f : sd->fields) {
                hashStr(hasher, f.name);
                hashType(hasher, f.type);
            }
            break;
        }
        case DeclKind::TypeAlias: {
            auto* ta = decl->typeAliasDecl;
            hashStr(hasher, ta->name);
            hashType(hasher, ta->aliasedType);
            break;
        }
        case DeclKind::Effect: {
            auto* ed = decl->effectDecl;
            hashStr(hasher, ed->name);
            uint64_t paramCount = ed->params.size();
            hashPod(hasher, paramCount);
            for (auto& p : ed->params) hashParam(hasher, p);
            hashType(hasher, ed->returnType);
            break;
        }
        case DeclKind::Component: {
            auto* cd = decl->componentDecl;
            hashStr(hasher, cd->name);
            uint64_t paramCount = cd->params.size();
            hashPod(hasher, paramCount);
            for (auto& p : cd->params) hashParam(hasher, p);
            hashStr(hasher, cd->interfaceName);
            uint64_t portCount = cd->ports.size();
            hashPod(hasher, portCount);
            for (auto& port : cd->ports) {
                hashPod(hasher, port.isOutput);
                hashStr(hasher, port.name);
                hashType(hasher, port.type);
            }
            uint64_t bodyCount = cd->bodyStatements.size();
            hashPod(hasher, bodyCount);
            for (auto* s : cd->bodyStatements) hashExpr(hasher, s);
            break;
        }
        case DeclKind::Use: {
            auto* ud = decl->useDecl;
            uint64_t segCount = ud->pathSegments.size();
            hashPod(hasher, segCount);
            for (auto& seg : ud->pathSegments) hashStr(hasher, seg);
            hashPod(hasher, ud->isSelfUse);
            break;
        }
        case DeclKind::TopLevelStmt:
            hashExpr(hasher, decl->topLevelStmt);
            break;
        case DeclKind::Impl: {
            auto* id = decl->implDecl;
            hashStr(hasher, id->typeName);
            uint64_t methodCount = id->methods.size();
            hashPod(hasher, methodCount);
            for (auto* m : id->methods) {
                Decl fakeMethodDecl;
                fakeMethodDecl.kind = DeclKind::Function;
                fakeMethodDecl.functionDecl = m;
                hashDecl(hasher, &fakeMethodDecl);
            }
            break;
        }
    }
}

} // namespace detail

// Returns a 64-char lowercase hex BLAKE3 digest of `prog`'s structural
// content. Two programs that are byte-identical after stripping comments,
// whitespace, and source positions hash identically; anything else differs.
inline std::string computeAstHash(const Program& prog) {
    llvm::BLAKE3 hasher;
    uint64_t declCount = prog.decls.size();
    detail::hashPod(hasher, declCount);
    for (auto* d : prog.decls) detail::hashDecl(hasher, d);

    llvm::BLAKE3Result<> result = hasher.final();
    std::string hex;
    hex.reserve(result.size() * 2);
    static const char* digits = "0123456789abcdef";
    for (uint8_t byte : result) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0xF]);
    }
    return hex;
}

} // namespace frust
