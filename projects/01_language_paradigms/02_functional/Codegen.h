#pragma once

// Deliberately small first pass: literals, binary ops (int-vs-float-correct,
// unlike the very first prototype this project had), identifiers, `let`,
// `return`, plain function calls, and function decls with real per-param/
// return types (including following type aliases). No effects, components,
// quote/unquote, structs, smart pointers, or casts yet - those come once
// this slice is proven out end-to-end through the JIT.
//
// There's no separate semantic-analysis pass yet either, so type
// consistency is handled here, ad hoc, by just looking at the LLVM types of
// already-codegen'd operands (codegen proceeds bottom-up, so by the time a
// BinaryExpr is compiled, both operands already exist as concrete
// llvm::Value*s to inspect) rather than via a real inference/checking pass.

#include "AST.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace frust {

inline bool hasPerform(const Expr* expr) {
    if (!expr) return false;
    if (expr->kind == ExprKind::Perform) return true;
    if (hasPerform(expr->lhs)) return true;
    if (hasPerform(expr->rhs)) return true;
    if (hasPerform(expr->condExpr)) return true;
    if (hasPerform(expr->elseExpr)) return true;
    for (auto* s : expr->statements) if (hasPerform(s)) return true;
    for (auto* a : expr->args) if (hasPerform(a)) return true;
    for (auto& hc : expr->handleCases) if (hasPerform(hc.body)) return true;
    return false;
}

class Codegen {
public:
    Codegen(llvm::LLVMContext& ctx, llvm::Module& mod)
        : context(ctx), module(mod), builder(ctx) {}

    // Pre-binds `name` to a constant f64 value before compiling - how the
    // REPL replays previously-`let`-bound variables into each new,
    // otherwise-fresh compile. See ReplSession's header comment for why
    // values are tracked as plain doubles rather than kept alive as live
    // JIT state across calls.
    void seedConstant(const std::string& name, double value) {
        namedValues[name] = llvm::ConstantFP::get(context, llvm::APFloat(value));
    }

    // Codegens every function/method decl with a body. Returns false if any
    // failed (a message was already printed to stderr).
    bool compileProgram(const Program& prog) {
        indexTypeAliases(prog);
        indexStructs(prog); // must precede signature declaration below - param/return types can name a struct

        // Synthesize print_f64
        llvm::Constant* formatStr = llvm::ConstantDataArray::getString(context, "%f\n");
        llvm::GlobalVariable* formatVar = new llvm::GlobalVariable(module, formatStr->getType(), true, llvm::GlobalValue::PrivateLinkage, formatStr, ".str.print_f64");
        llvm::FunctionType* printfType = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {llvm::PointerType::getUnqual(context)}, true);
        llvm::FunctionCallee printfFunc = module.getOrInsertFunction("printf", printfType);
        llvm::FunctionType* printF64Type = llvm::FunctionType::get(llvm::Type::getVoidTy(context), {llvm::Type::getDoubleTy(context)}, false);
        llvm::Function* printF64Func = llvm::Function::Create(printF64Type, llvm::Function::ExternalLinkage, "print_f64", module);
        llvm::BasicBlock* printBb = llvm::BasicBlock::Create(context, "entry", printF64Func);
        llvm::IRBuilder<> printBuilder(printBb);
        llvm::Value* formatPtr = printBuilder.CreatePointerCast(formatVar, llvm::PointerType::getUnqual(context));
        printBuilder.CreateCall(printfFunc, {formatPtr, printF64Func->getArg(0)});
        printBuilder.CreateRetVoid();

        // Pass 1: declare every function/method *signature* up front, so
        // calls resolve regardless of textual order - module.getFunction()
        // lookups (compileCall, method dispatch) only succeed once a
        // signature has been declared. Without this, sibling methods in the
        // same impl block calling each other would be order-dependent.
        for (auto* decl : prog.decls) {
            if (decl->kind == DeclKind::Function) {
                declareFunctionSignature(*decl->functionDecl);
            } else if (decl->kind == DeclKind::Impl) {
                for (auto* m : decl->implDecl->methods) declareFunctionSignature(*m);
            }
        }

        // Pass 2: fill in bodies.
        bool ok = true;
        for (auto* decl : prog.decls) {
            if (decl->kind == DeclKind::Function && (decl->functionDecl->body != nullptr || decl->functionDecl->isExtern)) {
                if (!compileFunction(*decl->functionDecl)) ok = false;
            } else if (decl->kind == DeclKind::Impl) {
                for (auto* m : decl->implDecl->methods) {
                    if (!compileFunction(*m)) ok = false;
                }
            }
        }
        return ok;
    }

private:
    llvm::LLVMContext& context;
    llvm::Module& module;
    llvm::IRBuilder<> builder;

    std::unordered_map<std::string, llvm::Value*> namedValues;
    std::unordered_map<std::string, const TypeExpr*> typeAliases;
    bool blockTerminated = false; // set once `return` emits a terminator; later stmts in that block are skipped.

    // Struct support. LLVM struct types are created *named*
    // (llvm::StructType::create(..., name)) specifically so structTypes'
    // keys double as the name recoverable from the type itself - but under
    // this project's exclusively-opaque-pointer LLVM usage
    // (PointerType::getUnqual(context) everywhere), a *pointer* to a struct
    // carries no pointee info at all, so "which struct is this value" can
    // never be recovered from an llvm::Value's type once it's behind a
    // pointer. namedValueStructType is the side table that makes that
    // possible anyway - see inferStructTypeName.
    std::unordered_map<std::string, llvm::StructType*> structTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> structFieldIndex; // struct name -> field name -> GEP index
    std::unordered_map<std::string, std::string> namedValueStructType; // variable/param name -> struct type name

    // impl-block methods, keyed by the same "TypeName::methodName" scheme
    // compileCall's existing Path handling already produces for lookups.
    std::unordered_map<std::string, const FunctionDecl*> methods;

    // (continueTarget, breakTarget) per enclosing loop, innermost last.
    std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> loopStack;

    llvm::Value* currentCoroHandle = nullptr;
    llvm::Value* currentPromiseAlloc = nullptr;
    llvm::Value* currentCoroId = nullptr;
    llvm::BasicBlock* currentCleanupBB = nullptr;
    llvm::BasicBlock* currentSuspendBB = nullptr;
    
    llvm::Value* currentActiveHandleForResume = nullptr;
    llvm::Value* currentActivePromiseForResume = nullptr;

    llvm::StructType* getPromiseType() {
        // { i32 effect_id, f64 yield_val, f64 resume_val, f64 return_val }
        return llvm::StructType::get(context, {
            llvm::Type::getInt32Ty(context),
            llvm::Type::getDoubleTy(context),
            llvm::Type::getDoubleTy(context),
            llvm::Type::getDoubleTy(context)
        });
    }

    void indexTypeAliases(const Program& prog) {
        for (auto* decl : prog.decls)
            if (decl->kind == DeclKind::TypeAlias)
                typeAliases[decl->typeAliasDecl->name] = decl->typeAliasDecl->aliasedType;
    }

    // Must run before any function/method signature is declared - param and
    // return types can name a struct, and resolveType needs structTypes
    // populated to resolve them.
    void indexStructs(const Program& prog) {
        for (auto* decl : prog.decls) {
            if (decl->kind != DeclKind::Struct) continue;
            const StructDecl& sd = *decl->structDecl;

            std::vector<llvm::Type*> fieldTypes;
            auto& fieldIndex = structFieldIndex[sd.name];
            for (size_t i = 0; i < sd.fields.size(); ++i) {
                fieldTypes.push_back(resolveType(sd.fields[i].type));
                fieldIndex[sd.fields[i].name] = static_cast<int>(i);
            }
            structTypes[sd.name] = llvm::StructType::create(context, fieldTypes, sd.name);
        }
    }

    // Which Frust struct type (if any) a given expression statically is,
    // sourced from namedValueStructType rather than from the compiled LLVM
    // value's type (impossible under opaque pointers - see the comment on
    // namedValueStructType above). Only the handful of expr kinds that can
    // *be* a struct in v1 are covered; everything else (nested Member,
    // struct-returning Call) is deliberately out of scope for this pass and
    // returns nullopt, same as it already falls through to "unsupported".
    std::optional<std::string> inferStructTypeName(const Expr* expr) {
        if (!expr) return std::nullopt;
        if (expr->kind == ExprKind::Identifier) {
            auto it = namedValueStructType.find(expr->text);
            if (it != namedValueStructType.end()) return it->second;
            return std::nullopt;
        }
        if (expr->kind == ExprKind::StructLiteral) {
            if (expr->pathSegments.empty()) return std::nullopt;
            return expr->pathSegments.front();
        }
        return std::nullopt;
    }

    llvm::Type* resolveType(const TypeExpr* type, int depth = 0) {
        if (!type) return llvm::Type::getInt64Ty(context); // untyped => default i64

        if (depth > 16) {
            std::cerr << "frust: codegen error: type alias cycle involving '" << type->name << "'\n";
            return llvm::Type::getInt64Ty(context);
        }

        auto aliasIt = typeAliases.find(type->name);
        if (aliasIt != typeAliases.end()) return resolveType(aliasIt->second, depth + 1);

        const std::string& n = type->name;
        if (n == "i8" || n == "u8")   return llvm::Type::getInt8Ty(context);
        if (n == "i16" || n == "u16") return llvm::Type::getInt16Ty(context);
        if (n == "i32" || n == "u32") return llvm::Type::getInt32Ty(context);
        if (n == "i64" || n == "u64" || n == "usize" || n == "isize") return llvm::Type::getInt64Ty(context);
        if (n == "f32") return llvm::Type::getFloatTy(context);
        if (n == "f64") return llvm::Type::getDoubleTy(context);
        if (n == "bool") return llvm::Type::getInt1Ty(context);
        if (n == "String") return llvm::PointerType::getUnqual(context); // null-terminated i8*, matching FRUST_LANG_SPEC.md's `effect Log(msg: String)`

        // Vec<N> - a fixed-size, compile-time-N f32 vector (linear algebra,
        // not the unimplemented growable-collection `Vector<T>` from the
        // spec doc - deliberately different name to avoid confusion).
        // Represented as a genuine LLVM vector type, not a pointer - unlike
        // structs/String, a Vec<N> value's own LLVM type is directly
        // inspectable via getType()->isVectorTy() even under opaque
        // pointers (opaque pointers only erase pointee info for pointers;
        // a vector type isn't a pointer at all), so no namedValueStructType-
        // style side table is needed for it.
        if (n == "Vec" && type->genericArgs.size() == 1 && type->genericArgs[0].isIntConst) {
            int64_t count = type->genericArgs[0].intConst;
            if (count <= 0) {
                std::cerr << "frust: codegen error: Vec<" << count << "> - size must be a positive integer\n";
                return llvm::Type::getInt64Ty(context);
            }
            return llvm::FixedVectorType::get(llvm::Type::getFloatTy(context), static_cast<unsigned>(count));
        }

        auto structIt = structTypes.find(n);
        if (structIt != structTypes.end()) return llvm::PointerType::getUnqual(context); // structs are always passed/held by pointer

        std::cerr << "frust: codegen does not support type '" << n << "' yet - defaulting to i64\n";
        return llvm::Type::getInt64Ty(context);
    }

    // Same alias-following as resolveType, but answers "is this a struct,
    // and if so which one" instead of producing an llvm::Type - needed
    // because, once resolved to a pointer, that question can no longer be
    // answered from the LLVM type alone (opaque pointers - see
    // namedValueStructType's declaration).
    std::optional<std::string> resolveStructTypeName(const TypeExpr* type, int depth = 0) {
        if (!type || depth > 16) return std::nullopt;
        auto aliasIt = typeAliases.find(type->name);
        if (aliasIt != typeAliases.end()) return resolveStructTypeName(aliasIt->second, depth + 1);
        if (structTypes.count(type->name)) return type->name;
        return std::nullopt;
    }

    // Coerces `value` to `target` when they merely differ in numeric kind/
    // width (the only implicit conversion codegen does, in the absence of a
    // real type checker). Returns value unchanged if already matching or if
    // no sensible coercion applies.
    llvm::Value* coerceToType(llvm::Value* value, llvm::Type* target) {
        if (value->getType() == target) return value;
        if (target->isFloatingPointTy() && value->getType()->isIntegerTy())
            return builder.CreateSIToFP(value, target);
        if (target->isFloatingPointTy() && value->getType()->isFloatingPointTy())
            return builder.CreateFPCast(value, target);
        if (target->isIntegerTy() && value->getType()->isIntegerTy())
            return builder.CreateIntCast(value, target, /*isSigned=*/true);
        return value;
    }

    llvm::Value* compileExpr(const Expr* expr) {
        if (!expr) return nullptr;

        switch (expr->kind) {
            case ExprKind::IntLiteral:
                return llvm::ConstantInt::get(context, llvm::APInt(64, static_cast<uint64_t>(expr->intValue), true));
            case ExprKind::FloatLiteral:
                return llvm::ConstantFP::get(context, llvm::APFloat(expr->floatValue));
            case ExprKind::BoolLiteral:
                return llvm::ConstantInt::get(context, llvm::APInt(1, expr->boolValue ? 1u : 0u));

            // A null-terminated i8* global constant - the `String` type
            // (see resolveType) is exactly this pointer, nothing richer
            // (no length-prefixed/owned string type exists yet).
            case ExprKind::StringLiteral: {
                llvm::Constant* strConst = llvm::ConstantDataArray::getString(context, expr->text);
                auto* global = new llvm::GlobalVariable(module, strConst->getType(), true,
                    llvm::GlobalValue::PrivateLinkage, strConst, ".str");
                return builder.CreatePointerCast(global, llvm::PointerType::getUnqual(context));
            }

            case ExprKind::Identifier: {
                // `null` - a null pointer constant, same `ptr` representation
                // as String/any struct/any function value here. Recognized
                // as a magic identifier rather than a real keyword/grammar
                // token: mem.fr and file_io.fr already document needing this
                // exact thing ("no way to pass a null String literal") for
                // things like a caller-optional buffer or (as of this
                // change) CreateThread/pthread_create's optional out-params.
                // No Frust-level way to *test* a value against null yet
                // (no pointer-equality comparison implemented) - this only
                // covers producing one to pass outward, not consuming one
                // back.
                if (expr->text == "null") {
                    return llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context));
                }
                auto it = namedValues.find(expr->text);
                if (it == namedValues.end()) {
                    // Not a local/param - if it names a known top-level
                    // function and we're not the callee of an immediate
                    // Call (compileCall handles that path itself, straight
                    // to a `call` instruction), treat the bare name as a
                    // C-style function-to-pointer decay: the function's own
                    // address, exactly the same `ptr` representation String
                    // already uses (see resolveType's String case) - so it
                    // can be stored into a String-typed local, passed to an
                    // extern fn expecting one, or held in a struct field,
                    // with zero new type-system surface. Frust code can't
                    // yet call this value back indirectly (no ExprKind for
                    // an indirect call) - that's a real, separate feature,
                    // not implemented by this change. This exists to let a
                    // Frust function's address reach a C API that invokes
                    // it itself, e.g. CreateThread/pthread_create's start
                    // routine.
                    if (llvm::Function* fn = module.getFunction(expr->text)) {
                        return fn;
                    }
                    std::cerr << "frust: codegen error: unknown identifier '" << expr->text << "'\n";
                    return nullptr;
                }
                // Struct-typed bindings are always pointer-represented (see
                // namedValueStructType's declaration) - return the address
                // as-is, never auto-load. Auto-loading would produce an LLVM
                // aggregate value, breaking every GEP-based field-access/
                // mutation/method-dispatch path that assumes "a struct value
                // in hand is always its address."
                if (namedValueStructType.count(expr->text)) return it->second;
                if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(it->second)) {
                    return builder.CreateLoad(alloca->getAllocatedType(), alloca, expr->text);
                }
                return it->second;
            }

            case ExprKind::Path: {
                std::string fullName = expr->pathSegments.front();
                for (size_t i = 1; i < expr->pathSegments.size(); ++i) fullName += "::" + expr->pathSegments[i];
                auto it = namedValues.find(fullName);
                if (it == namedValues.end()) {
                    std::cerr << "frust: codegen error: unknown path '" << fullName << "'\n";
                    return nullptr;
                }
                return it->second;
            }

            case ExprKind::Binary: return compileBinary(*expr);
            case ExprKind::Unary: return compileUnary(*expr);
            case ExprKind::Call: return compileCall(*expr);
            case ExprKind::If: return compileIf(*expr);
            case ExprKind::While: return compileWhile(*expr);
            case ExprKind::Loop: return compileLoop(*expr);
            case ExprKind::For: return compileFor(*expr);
            case ExprKind::Break: return compileBreak(*expr);
            case ExprKind::Continue: return compileContinue(*expr);
            case ExprKind::StructLiteral: return compileStructLiteral(*expr);
            case ExprKind::ArrayLiteral: return compileArrayLiteral(*expr);
            case ExprKind::Index: return compileIndex(*expr);
            case ExprKind::Member: return compileMember(*expr);
            case ExprKind::Assign: return compileAssign(*expr);

            case ExprKind::Let: {
                auto* val = compileExpr(expr->lhs);
                if (!val) return nullptr;

                auto structTypeName = inferStructTypeName(expr->lhs);
                if (structTypeName) {
                    // Structs are always pointer-represented, and a struct
                    // value already IS an address (StructLiteral allocas
                    // itself) - bind that pointer directly, no extra
                    // wrapping alloca, regardless of `mut`. Note this means
                    // struct field mutation isn't actually gated on `mut`
                    // here - same pre-existing gap as everywhere else in
                    // this file (see the header comment: no real semantic-
                    // analysis pass exists yet), not a new one.
                    namedValues[expr->text] = val;
                    namedValueStructType[expr->text] = *structTypeName;
                } else if (expr->isMut) {
                    llvm::Function* theFunction = builder.GetInsertBlock()->getParent();
                    llvm::IRBuilder<> tmpBuilder(&theFunction->getEntryBlock(), theFunction->getEntryBlock().begin());
                    llvm::AllocaInst* alloca = tmpBuilder.CreateAlloca(val->getType(), nullptr, expr->text);
                    builder.CreateStore(val, alloca);
                    namedValues[expr->text] = alloca;
                } else {
                    namedValues[expr->text] = val;
                }
                return val;
            }

            case ExprKind::Return: {
                auto* val = compileExpr(expr->lhs);
                if (!val) return nullptr;
                builder.CreateRet(val);
                blockTerminated = true;
                return val;
            }

            case ExprKind::Block: {
                llvm::Value* last = nullptr;
                for (auto* stmt : expr->statements) {
                    if (blockTerminated) break;
                    last = compileExpr(stmt);
                    if (!last) return nullptr;
                }
                return last;
            }

            case ExprKind::Perform: return compilePerform(*expr);
            case ExprKind::Handle: return compileHandle(*expr);
            case ExprKind::Resume: return compileResume(*expr);

            default:
                std::cerr << "frust: codegen does not support this expression kind yet\n";
                return nullptr;
        }
    }

    // Vec<N> operands - handled here, before compileBinary's scalar isFloat
    // check, since isFloatingPointTy() returns false for <N x float> (it
    // only recognizes scalar FP kinds); left unguarded, two Vec<N> operands
    // would fall into compileBinary's integer branch and crash calling
    // getIntegerBitWidth() on a vector type.
    llvm::Value* compileVectorBinary(const Expr& expr, llvm::Value* lhs, llvm::Value* rhs) {
        bool lhsVec = lhs->getType()->isVectorTy();
        bool rhsVec = rhs->getType()->isVectorTy();

        if (expr.binaryOp == BinaryOp::Add || expr.binaryOp == BinaryOp::Sub) {
            if (!lhsVec || !rhsVec) {
                std::cerr << "frust: codegen error: '+'/'-' needs two Vec<N> operands of the same size (vector-scalar +/- isn't supported)\n";
                return nullptr;
            }
            unsigned lw = llvm::cast<llvm::FixedVectorType>(lhs->getType())->getNumElements();
            unsigned rw = llvm::cast<llvm::FixedVectorType>(rhs->getType())->getNumElements();
            if (lw != rw) {
                std::cerr << "frust: codegen error: Vec<" << lw << "> and Vec<" << rw << "> have different sizes\n";
                return nullptr;
            }
            return expr.binaryOp == BinaryOp::Add
                ? builder.CreateFAdd(lhs, rhs, "vaddtmp")
                : builder.CreateFSub(lhs, rhs, "vsubtmp");
        }

        if (expr.binaryOp == BinaryOp::Mul || expr.binaryOp == BinaryOp::Div) {
            if (lhsVec && rhsVec) {
                std::cerr << "frust: codegen error: '*'/'/ ' between two vectors is ambiguous - use dot(a, b) for a dot product\n";
                return nullptr;
            }
            if (expr.binaryOp == BinaryOp::Div && !lhsVec) {
                std::cerr << "frust: codegen error: scalar / Vec<N> is not supported - only Vec<N> / scalar\n";
                return nullptr;
            }
            llvm::Value* vec = lhsVec ? lhs : rhs;
            llvm::Value* scalar = lhsVec ? rhs : lhs;
            scalar = coerceToType(scalar, llvm::Type::getFloatTy(context));
            unsigned count = llvm::cast<llvm::FixedVectorType>(vec->getType())->getNumElements();
            llvm::Value* splat = builder.CreateVectorSplat(count, scalar);
            return expr.binaryOp == BinaryOp::Mul
                ? builder.CreateFMul(vec, splat, "vmultmp")
                : builder.CreateFDiv(vec, splat, "vdivtmp");
        }

        std::cerr << "frust: codegen does not support this operator on Vec<N> yet\n";
        return nullptr;
    }

    llvm::Value* compileBinary(const Expr& expr) {
        auto* lhs = compileExpr(expr.lhs);
        auto* rhs = compileExpr(expr.rhs);
        if (!lhs || !rhs) return nullptr;

        if (lhs->getType()->isVectorTy() || rhs->getType()->isVectorTy()) {
            return compileVectorBinary(expr, lhs, rhs);
        }

        bool isFloat = lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy();

        if (isFloat) {
            if (!lhs->getType()->isFloatingPointTy()) lhs = builder.CreateSIToFP(lhs, rhs->getType());
            if (!rhs->getType()->isFloatingPointTy()) rhs = builder.CreateSIToFP(rhs, lhs->getType());
            if (lhs->getType() != rhs->getType()) rhs = builder.CreateFPCast(rhs, lhs->getType());
        } else if (lhs->getType() != rhs->getType()) {
            // A `bool` (i1) widened via SIGN extension is a real bug, not
            // just a style choice: SExt treats a 1-bit value's only bit as
            // the sign bit, so `true` (bit pattern 1) becomes -1 (all bits
            // set) instead of 1. Every other integer width in this
            // language is genuinely signed, so SExt is correct there -
            // bool is the one exception, and needs zero extension instead
            // (true -> 1, false -> 0, regardless of target width).
            unsigned lw = lhs->getType()->getIntegerBitWidth();
            unsigned rw = rhs->getType()->getIntegerBitWidth();
            if (lw < rw) {
                lhs = lhs->getType()->isIntegerTy(1) ? builder.CreateZExt(lhs, rhs->getType()) : builder.CreateSExt(lhs, rhs->getType());
            } else {
                rhs = rhs->getType()->isIntegerTy(1) ? builder.CreateZExt(rhs, lhs->getType()) : builder.CreateSExt(rhs, lhs->getType());
            }
        }

        switch (expr.binaryOp) {
            case BinaryOp::Add: return isFloat ? builder.CreateFAdd(lhs, rhs, "addtmp") : builder.CreateAdd(lhs, rhs, "addtmp");
            case BinaryOp::Sub: return isFloat ? builder.CreateFSub(lhs, rhs, "subtmp") : builder.CreateSub(lhs, rhs, "subtmp");
            case BinaryOp::Mul: return isFloat ? builder.CreateFMul(lhs, rhs, "multmp") : builder.CreateMul(lhs, rhs, "multmp");
            case BinaryOp::Div: return isFloat ? builder.CreateFDiv(lhs, rhs, "divtmp") : builder.CreateSDiv(lhs, rhs, "divtmp");
            case BinaryOp::Mod: return isFloat ? builder.CreateFRem(lhs, rhs, "modtmp") : builder.CreateSRem(lhs, rhs, "modtmp");
            case BinaryOp::Eq:  return isFloat ? builder.CreateFCmpOEQ(lhs, rhs, "eqtmp") : builder.CreateICmpEQ(lhs, rhs, "eqtmp");
            case BinaryOp::Neq: return isFloat ? builder.CreateFCmpONE(lhs, rhs, "netmp") : builder.CreateICmpNE(lhs, rhs, "netmp");
            case BinaryOp::Lt:  return isFloat ? builder.CreateFCmpOLT(lhs, rhs, "lttmp") : builder.CreateICmpSLT(lhs, rhs, "lttmp");
            case BinaryOp::Gt:  return isFloat ? builder.CreateFCmpOGT(lhs, rhs, "gttmp") : builder.CreateICmpSGT(lhs, rhs, "gttmp");
            case BinaryOp::Le:  return isFloat ? builder.CreateFCmpOLE(lhs, rhs, "letmp") : builder.CreateICmpSLE(lhs, rhs, "letmp");
            case BinaryOp::Ge:  return isFloat ? builder.CreateFCmpOGE(lhs, rhs, "getmp") : builder.CreateICmpSGE(lhs, rhs, "getmp");
            // Bitwise ops are integer-only - no meaningful bit pattern to
            // AND/OR/XOR/shift for a float, so these error rather than
            // silently reinterpreting bits (same "explicit, not implicit"
            // stance as the rest of this codegen). Shr uses an arithmetic
            // (sign-preserving) shift, matching how C/Rust's `>>` behaves
            // on a signed integer by default - Frust's integers are signed
            // throughout (see compileBinary's existing SExt/ICmpS* usage).
            case BinaryOp::BitOr:
                if (isFloat) { std::cerr << "frust: codegen error: '|' is not defined for float operands\n"; return nullptr; }
                return builder.CreateOr(lhs, rhs, "ortmp");
            case BinaryOp::BitXor:
                if (isFloat) { std::cerr << "frust: codegen error: '^' is not defined for float operands\n"; return nullptr; }
                return builder.CreateXor(lhs, rhs, "xortmp");
            case BinaryOp::BitAnd:
                if (isFloat) { std::cerr << "frust: codegen error: '&' is not defined for float operands\n"; return nullptr; }
                return builder.CreateAnd(lhs, rhs, "andtmp");
            case BinaryOp::Shl:
                if (isFloat) { std::cerr << "frust: codegen error: '<<' is not defined for float operands\n"; return nullptr; }
                return builder.CreateShl(lhs, rhs, "shltmp");
            case BinaryOp::Shr:
                if (isFloat) { std::cerr << "frust: codegen error: '>>' is not defined for float operands\n"; return nullptr; }
                return builder.CreateAShr(lhs, rhs, "shrtmp");
        }
        return nullptr;
    }

    // Deref (`*expr`, for `raw* T`) isn't handled here - out of scope for
    // now (same "not yet" status as most of the smart-pointer system),
    // falls through to the same "unsupported expression kind" signal as
    // before this existed rather than silently misbehaving.
    llvm::Value* compileUnary(const Expr& expr) {
        auto* operand = compileExpr(expr.lhs);
        if (!operand) return nullptr;

        switch (expr.unaryOp) {
            case UnaryOp::Neg:
                return operand->getType()->isFloatingPointTy()
                    ? builder.CreateFNeg(operand, "negtmp")
                    : builder.CreateNeg(operand, "negtmp");
            case UnaryOp::Not:
                return builder.CreateNot(operand, "nottmp");
            case UnaryOp::Deref:
                std::cerr << "frust: codegen does not support pointer dereference yet\n";
                return nullptr;
        }
        return nullptr;
    }

    llvm::Value* compileAssign(const Expr& expr) {
        if (expr.lhs->kind == ExprKind::Member) {
            const Expr& member = *expr.lhs;
            auto typeName = inferStructTypeName(member.lhs);
            if (!typeName) {
                std::cerr << "frust: codegen error: cannot assign to a member of an unknown struct type\n";
                return nullptr;
            }
            auto structIt = structTypes.find(*typeName);
            if (structIt == structTypes.end()) {
                std::cerr << "frust: codegen error: unknown struct type '" << *typeName << "'\n";
                return nullptr;
            }
            auto& fieldIndex = structFieldIndex[*typeName];
            auto fieldIt = fieldIndex.find(member.text);
            if (fieldIt == fieldIndex.end()) {
                std::cerr << "frust: codegen error: struct '" << *typeName << "' has no field '" << member.text << "'\n";
                return nullptr;
            }

            auto* basePtr = compileExpr(member.lhs);
            if (!basePtr) return nullptr;
            auto* val = compileExpr(expr.rhs);
            if (!val) return nullptr;

            llvm::StructType* structTy = structIt->second;
            val = coerceToType(val, structTy->getElementType(fieldIt->second));
            llvm::Value* fieldPtr = builder.CreateStructGEP(structTy, basePtr, fieldIt->second);
            builder.CreateStore(val, fieldPtr);
            return val;
        }

        if (expr.lhs->kind == ExprKind::Index) {
            // `v[i] = x` for a `mut` Vec<N> variable. Vec<N> values are
            // genuine SSA vector values (see compileArrayLiteral), not
            // pointer-backed - there's no address to GEP into and store
            // through the way struct-field assignment does above.
            // Instead: load the current vector out of v's alloca,
            // CreateInsertElement to produce a NEW vector with index i
            // replaced, store that back - the standard LLVM pattern for
            // "mutating" one lane of an SSA vector value.
            const Expr& indexExpr = *expr.lhs;
            if (indexExpr.lhs->kind != ExprKind::Identifier) {
                std::cerr << "frust: codegen error: indexed assignment only supports a plain variable base (v[i] = x, not expr[i] = x)\n";
                return nullptr;
            }
            auto vecIt = namedValues.find(indexExpr.lhs->text);
            if (vecIt == namedValues.end() || !llvm::isa<llvm::AllocaInst>(vecIt->second)) {
                std::cerr << "frust: codegen error: cannot assign into an element of immutable or unknown variable '" << indexExpr.lhs->text << "'\n";
                return nullptr;
            }
            auto* vecAlloca = llvm::cast<llvm::AllocaInst>(vecIt->second);
            if (!vecAlloca->getAllocatedType()->isVectorTy()) {
                std::cerr << "frust: codegen error: indexed assignment is only supported for Vec<N> variables\n";
                return nullptr;
            }

            auto* idxVal = compileExpr(indexExpr.rhs);
            if (!idxVal) return nullptr;
            auto* val = compileExpr(expr.rhs);
            if (!val) return nullptr;

            llvm::Value* current = builder.CreateLoad(vecAlloca->getAllocatedType(), vecAlloca);
            llvm::Type* elemTy = llvm::cast<llvm::VectorType>(vecAlloca->getAllocatedType())->getElementType();
            val = coerceToType(val, elemTy);
            llvm::Value* updated = builder.CreateInsertElement(current, val, idxVal);
            builder.CreateStore(updated, vecAlloca);
            return val;
        }

        if (expr.lhs->kind != ExprKind::Identifier) {
            std::cerr << "frust: codegen error: assignment to non-identifier\n";
            return nullptr;
        }
        auto it = namedValues.find(expr.lhs->text);
        if (it == namedValues.end() || !llvm::isa<llvm::AllocaInst>(it->second)) {
            std::cerr << "frust: codegen error: cannot assign to immutable or unknown variable '" << expr.lhs->text << "'\n";
            return nullptr;
        }
        auto* val = compileExpr(expr.rhs);
        if (!val) return nullptr;
        val = coerceToType(val, llvm::cast<llvm::AllocaInst>(it->second)->getAllocatedType());
        builder.CreateStore(val, it->second);
        return val;
    }

    // `[e1, e2, ..., eN]` -> Vec<N>. A genuine SSA vector value (see
    // resolveType's Vec<N> comment) - no alloca needed, unlike struct
    // literals right below this.
    llvm::Value* compileArrayLiteral(const Expr& expr) {
        if (expr.args.empty()) {
            std::cerr << "frust: codegen error: array literal must have at least one element\n";
            return nullptr;
        }

        llvm::Type* elemTy = llvm::Type::getFloatTy(context);
        llvm::Type* vecTy = llvm::FixedVectorType::get(elemTy, static_cast<unsigned>(expr.args.size()));
        llvm::Value* acc = llvm::UndefValue::get(vecTy);

        for (size_t i = 0; i < expr.args.size(); ++i) {
            auto* val = compileExpr(expr.args[i]);
            if (!val) return nullptr;
            val = coerceToType(val, elemTy);
            acc = builder.CreateInsertElement(acc, val, static_cast<uint64_t>(i));
        }
        return acc;
    }

    // v[i] - only vector indexing exists so far (Vec<N>). A compile-time-
    // constant index gets a real bounds check (N is known statically from
    // the vector's LLVM type); a runtime-variable index is not bounds-
    // checked yet - that needs real bounds-check codegen, out of scope here.
    llvm::Value* compileIndex(const Expr& expr) {
        auto* base = compileExpr(expr.lhs);
        if (!base) return nullptr;

        if (!base->getType()->isVectorTy()) {
            std::cerr << "frust: codegen does not support indexing this expression yet\n";
            return nullptr;
        }

        unsigned count = llvm::cast<llvm::FixedVectorType>(base->getType())->getNumElements();
        if (expr.rhs->kind == ExprKind::IntLiteral) {
            int64_t idx = expr.rhs->intValue;
            if (idx < 0 || static_cast<uint64_t>(idx) >= count) {
                std::cerr << "frust: codegen error: index " << idx << " out of range for Vec<" << count << ">\n";
                return nullptr;
            }
        }

        auto* indexVal = compileExpr(expr.rhs);
        if (!indexVal) return nullptr;
        return builder.CreateExtractElement(base, indexVal);
    }

    llvm::Value* compileStructLiteral(const Expr& expr) {
        if (expr.pathSegments.empty()) {
            std::cerr << "frust: codegen error: struct literal missing a type name\n";
            return nullptr;
        }
        const std::string& typeName = expr.pathSegments.front();
        auto typeIt = structTypes.find(typeName);
        if (typeIt == structTypes.end()) {
            std::cerr << "frust: codegen error: unknown struct type '" << typeName << "'\n";
            return nullptr;
        }
        llvm::StructType* structTy = typeIt->second;
        auto& fieldIndex = structFieldIndex[typeName];

        // Alloca'd in the entry block (same pattern `mut` locals already
        // use) rather than at the current insert point - keeps every
        // struct's storage mem2reg-friendly and independent of how deep
        // inside nested blocks the literal happens to appear.
        llvm::Function* theFunction = builder.GetInsertBlock()->getParent();
        llvm::IRBuilder<> tmpBuilder(&theFunction->getEntryBlock(), theFunction->getEntryBlock().begin());
        llvm::AllocaInst* alloca = tmpBuilder.CreateAlloca(structTy, nullptr, typeName);

        for (auto& init : expr.fields) {
            auto fieldIt = fieldIndex.find(init.name);
            if (fieldIt == fieldIndex.end()) {
                std::cerr << "frust: codegen error: struct '" << typeName << "' has no field '" << init.name << "'\n";
                return nullptr;
            }
            auto* val = compileExpr(init.value);
            if (!val) return nullptr;
            val = coerceToType(val, structTy->getElementType(fieldIt->second));
            llvm::Value* fieldPtr = builder.CreateStructGEP(structTy, alloca, fieldIt->second);
            builder.CreateStore(val, fieldPtr);
        }
        // Fields not present in the literal are left uninitialized (no
        // default-value story exists yet) - fine for v1, matches "no
        // validation for scenarios not yet required."
        return alloca;
    }

    llvm::Value* compileMember(const Expr& expr) {
        auto typeName = inferStructTypeName(expr.lhs);
        if (!typeName) {
            // Covers every case out of scope for v1 too: nested-struct
            // fields, struct-returning calls, etc. - falls through to the
            // same "unsupported" signal as before this feature existed.
            std::cerr << "frust: codegen does not support this member-access expression yet\n";
            return nullptr;
        }
        auto structIt = structTypes.find(*typeName);
        if (structIt == structTypes.end()) {
            std::cerr << "frust: codegen error: unknown struct type '" << *typeName << "'\n";
            return nullptr;
        }
        auto& fieldIndex = structFieldIndex[*typeName];
        auto fieldIt = fieldIndex.find(expr.text);
        if (fieldIt == fieldIndex.end()) {
            std::cerr << "frust: codegen error: struct '" << *typeName << "' has no field '" << expr.text << "'\n";
            return nullptr;
        }

        auto* basePtr = compileExpr(expr.lhs);
        if (!basePtr) return nullptr;

        llvm::StructType* structTy = structIt->second;
        llvm::Value* fieldPtr = builder.CreateStructGEP(structTy, basePtr, fieldIt->second);
        return builder.CreateLoad(structTy->getElementType(fieldIt->second), fieldPtr, expr.text);
    }

    // `obj.method(args)` already parses as Call(Member(obj, "method"), args)
    // via the ordinary postfix chain - no grammar changes needed for this
    // shape. What's new is recognizing that pattern here and dispatching it
    // as a method call (base pointer prepended as `self`) instead of the
    // "no such thing as calling a member" error this would otherwise hit.
    llvm::Value* compileMethodCall(const Expr& expr) {
        const Expr& member = *expr.lhs;
        auto typeName = inferStructTypeName(member.lhs);
        if (!typeName) {
            std::cerr << "frust: codegen error: cannot call a method on an expression of unknown struct type\n";
            return nullptr;
        }
        std::string mangled = mangleMethodName(*typeName, member.text);
        llvm::Function* callee = module.getFunction(mangled);
        if (!callee || !methods.count(mangled)) {
            std::cerr << "frust: codegen error: no such method '" << member.text << "' on struct '" << *typeName << "'\n";
            return nullptr;
        }

        auto* selfPtr = compileExpr(member.lhs);
        if (!selfPtr) return nullptr;

        std::size_t declaredArgCount = callee->arg_size() - 1; // minus the synthetic self param
        if (declaredArgCount != expr.args.size()) {
            std::cerr << "frust: codegen error: '" << member.text << "' expects " << declaredArgCount
                       << " argument(s), got " << expr.args.size() << "\n";
            return nullptr;
        }

        std::vector<llvm::Value*> args;
        args.push_back(selfPtr);
        auto argTypeIt = callee->arg_begin();
        ++argTypeIt; // skip self's type when coercing declared args
        for (auto* a : expr.args) {
            auto* v = compileExpr(a);
            if (!v) return nullptr;
            args.push_back(coerceToType(v, argTypeIt->getType()));
            ++argTypeIt;
        }
        return builder.CreateCall(callee, args, callee->getReturnType()->isVoidTy() ? "" : "calltmp");
    }

    // Assumes a/b are already-verified same-size vector values.
    llvm::Value* vecDotProduct(llvm::Value* a, llvm::Value* b) {
        unsigned n = llvm::cast<llvm::FixedVectorType>(a->getType())->getNumElements();
        llvm::Value* prod = builder.CreateFMul(a, b);
        llvm::Value* sum = builder.CreateExtractElement(prod, static_cast<uint64_t>(0));
        for (unsigned i = 1; i < n; ++i) {
            sum = builder.CreateFAdd(sum, builder.CreateExtractElement(prod, static_cast<uint64_t>(i)));
        }
        return sum;
    }

    // Assumes v is already a verified vector value.
    llvm::Value* vecLength(llvm::Value* v) {
        llvm::Value* sq = vecDotProduct(v, v);
        llvm::Function* sqrtFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::sqrt, {llvm::Type::getFloatTy(context)});
        return builder.CreateCall(sqrtFn, {sq});
    }

    // dot/length/normalize are unconditionally reserved builtin names in
    // v1 (checked here, before the ordinary module.getFunction lookup) -
    // a user-defined function with one of these names is permanently
    // shadowed. Documented tradeoff, not a silent trap.
    llvm::Value* compileVecBuiltinCall(const std::string& name, const Expr& expr) {
        if (name == "dot") {
            if (expr.args.size() != 2) {
                std::cerr << "frust: codegen error: dot() expects exactly 2 Vec<N> arguments\n";
                return nullptr;
            }
            auto* a = compileExpr(expr.args[0]);
            auto* b = compileExpr(expr.args[1]);
            if (!a || !b) return nullptr;
            if (!a->getType()->isVectorTy() || !b->getType()->isVectorTy()) {
                std::cerr << "frust: codegen error: dot() expects two Vec<N> arguments\n";
                return nullptr;
            }
            unsigned na = llvm::cast<llvm::FixedVectorType>(a->getType())->getNumElements();
            unsigned nb = llvm::cast<llvm::FixedVectorType>(b->getType())->getNumElements();
            if (na != nb) {
                std::cerr << "frust: codegen error: dot() between Vec<" << na << "> and Vec<" << nb << "> - sizes must match\n";
                return nullptr;
            }
            return vecDotProduct(a, b);
        }
        if (name == "length") {
            if (expr.args.size() != 1) {
                std::cerr << "frust: codegen error: length() expects exactly 1 Vec<N> argument\n";
                return nullptr;
            }
            auto* v = compileExpr(expr.args[0]);
            if (!v || !v->getType()->isVectorTy()) {
                std::cerr << "frust: codegen error: length() expects a Vec<N> argument\n";
                return nullptr;
            }
            return vecLength(v);
        }
        // name == "normalize"
        if (expr.args.size() != 1) {
            std::cerr << "frust: codegen error: normalize() expects exactly 1 Vec<N> argument\n";
            return nullptr;
        }
        auto* v = compileExpr(expr.args[0]);
        if (!v || !v->getType()->isVectorTy()) {
            std::cerr << "frust: codegen error: normalize() expects a Vec<N> argument\n";
            return nullptr;
        }
        llvm::Value* len = vecLength(v);
        unsigned n = llvm::cast<llvm::FixedVectorType>(v->getType())->getNumElements();
        llvm::Value* splat = builder.CreateVectorSplat(n, len);
        return builder.CreateFDiv(v, splat, "normalizetmp");
    }

    llvm::Value* compileCall(const Expr& expr) {
        if (expr.lhs->kind == ExprKind::Member) return compileMethodCall(expr);

        std::string targetName;
        if (expr.lhs->kind == ExprKind::Identifier) {
            targetName = expr.lhs->text;
            if (targetName == "dot" || targetName == "length" || targetName == "normalize") {
                return compileVecBuiltinCall(targetName, expr);
            }
            if (targetName == "call_i64" || targetName == "call_f64" || targetName == "call_bool" || targetName == "call_str") {
                return compileTypedIndirectCall(targetName, expr);
            }
        } else if (expr.lhs->kind == ExprKind::Path) {
            targetName = expr.lhs->pathSegments.front();
            for (size_t i = 1; i < expr.lhs->pathSegments.size(); ++i) targetName += "::" + expr.lhs->pathSegments[i];
        } else {
            // Not a bare name/path - the callee is some other expression
            // (e.g. a local holding a function value received as a
            // parameter or loaded from a buffer). Try it as an indirect
            // call rather than rejecting outright.
            return compileIndirectCall(expr);
        }

        llvm::Function* callee = module.getFunction(targetName);
        if (!callee) {
            // Not a known top-level function - if it's a local/param
            // (e.g. a String-typed variable holding a function's address,
            // via the ExprKind::Identifier address-of-decay above), try an
            // indirect call before giving up.
            if (namedValues.count(targetName)) {
                return compileIndirectCall(expr);
            }
            std::cerr << "frust: codegen error: unknown function '" << targetName << "'\n";
            return nullptr;
        }
        if (callee->arg_size() != expr.args.size()) {
            std::cerr << "frust: codegen error: '" << expr.lhs->text << "' expects " << callee->arg_size()
                       << " argument(s), got " << expr.args.size() << "\n";
            return nullptr;
        }

        std::vector<llvm::Value*> args;
        auto argTypeIt = callee->arg_begin();
        for (auto* a : expr.args) {
            auto* v = compileExpr(a);
            if (!v) return nullptr;
            args.push_back(coerceToType(v, argTypeIt->getType()));
            ++argTypeIt;
        }
        return builder.CreateCall(callee, args, callee->getReturnType()->isVoidTy() ? "" : "calltmp");
    }

    // Calling a function VALUE (not a statically-known name) - the
    // companion to ExprKind::Identifier's address-of decay, which lets a
    // Frust function's address be stored/passed around but not, until
    // this, called back. LLVM's opaque pointers carry no signature info,
    // so there's no way to recover the real callee's exact parameter/
    // return types from the pointer alone - this builds a FunctionType
    // from what's actually known at the call site (each argument's own
    // compiled type) and a fixed v1 convention for the part that isn't
    // knowable: an indirectly-called function via bare `f(args)` syntax
    // always returns i64. That's not arbitrary - every worker-style
    // function in this pod (task.fr, thread.fr's spawn target
    // convention) already returns i64 for exactly this reason. A bare
    // indirect call to something that returns f64/bool/String will
    // read/interpret garbage - for that, use the explicit
    // call_f64/call_bool/call_str forms below instead. Real return-type
    // *inference* through a value (e.g. from a `let x: f64 = ...`
    // binding's declared type) would need threading an expected-type
    // hint through compileExpr's Block/Let/If/Return cases broadly -
    // deliberately not done: explicit call_TYPE forms are lower-risk
    // (purely additive, zero chance of changing what already-verified
    // code compiles to) and match this language's existing "nothing is
    // inferred, everything explicit" character.
    llvm::Value* compileIndirectCall(const Expr& expr) {
        llvm::Value* calleeVal = compileExpr(expr.lhs);
        if (!calleeVal) return nullptr;

        std::vector<llvm::Value*> args;
        std::vector<llvm::Type*> argTypes;
        for (auto* a : expr.args) {
            auto* v = compileExpr(a);
            if (!v) return nullptr;
            args.push_back(v);
            argTypes.push_back(v->getType());
        }

        llvm::FunctionType* fnTy = llvm::FunctionType::get(llvm::Type::getInt64Ty(context), argTypes, false);
        return builder.CreateCall(fnTy, calleeVal, args, "indirectcalltmp");
    }

    // The explicit, typed sibling of compileIndirectCall - opt in with
    // call_i64/call_f64/call_bool/call_str(fn_value, args...) when the
    // callee doesn't return i64. expr.args[0] is the function value;
    // the rest are the real arguments passed through to it.
    llvm::Value* compileTypedIndirectCall(const std::string& builtinName, const Expr& expr) {
        if (expr.args.empty()) {
            std::cerr << "frust: codegen error: " << builtinName << "() needs a function value as its first argument\n";
            return nullptr;
        }
        llvm::Value* calleeVal = compileExpr(expr.args[0]);
        if (!calleeVal) return nullptr;

        std::vector<llvm::Value*> args;
        std::vector<llvm::Type*> argTypes;
        for (size_t i = 1; i < expr.args.size(); ++i) {
            auto* v = compileExpr(expr.args[i]);
            if (!v) return nullptr;
            args.push_back(v);
            argTypes.push_back(v->getType());
        }

        llvm::Type* retTy;
        if (builtinName == "call_f64") retTy = llvm::Type::getDoubleTy(context);
        else if (builtinName == "call_bool") retTy = llvm::Type::getInt1Ty(context);
        else if (builtinName == "call_str") retTy = llvm::PointerType::getUnqual(context);
        else retTy = llvm::Type::getInt64Ty(context); // call_i64

        llvm::FunctionType* fnTy = llvm::FunctionType::get(retTy, argTypes, false);
        return builder.CreateCall(fnTy, calleeVal, args, "typedindirectcalltmp");
    }

    llvm::Value* compileIf(const Expr& expr) {
        llvm::Value* condV = compileExpr(expr.condExpr);
        if (!condV) return nullptr;
        
        // Convert condition to bool (i1)
        if (condV->getType()->isFloatingPointTy()) {
            condV = builder.CreateFCmpONE(condV, llvm::ConstantFP::get(context, llvm::APFloat(0.0)), "ifcond");
        } else if (condV->getType()->isIntegerTy() && condV->getType()->getIntegerBitWidth() != 1) {
            condV = builder.CreateICmpNE(condV, llvm::ConstantInt::get(condV->getType(), 0), "ifcond");
        }

        llvm::Function* theFunction = builder.GetInsertBlock()->getParent();
        
        llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(context, "then", theFunction);
        llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context, "else");
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "ifcont");
        
        bool hasElse = expr.elseExpr != nullptr;
        builder.CreateCondBr(condV, thenBB, hasElse ? elseBB : mergeBB);
        
        // Emit Then Block
        builder.SetInsertPoint(thenBB);
        llvm::Value* thenV = compileExpr(expr.lhs);
        if (!thenV) return nullptr;
        
        bool thenTerminated = blockTerminated;
        if (!thenTerminated) builder.CreateBr(mergeBB);
        thenBB = builder.GetInsertBlock();
        blockTerminated = false;
        
        llvm::Value* elseV = nullptr;
        bool elseTerminated = false;
        if (hasElse) {
            theFunction->insert(theFunction->end(), elseBB);
            builder.SetInsertPoint(elseBB);
            elseV = compileExpr(expr.elseExpr);
            if (!elseV) return nullptr;
            
            elseTerminated = blockTerminated;
            if (!elseTerminated) builder.CreateBr(mergeBB);
            elseBB = builder.GetInsertBlock();
            blockTerminated = false;
        }
        
        theFunction->insert(theFunction->end(), mergeBB);
        builder.SetInsertPoint(mergeBB);
        if (thenTerminated && hasElse && elseTerminated) {
            builder.CreateUnreachable();
            blockTerminated = true;
            return llvm::ConstantInt::getTrue(context); // Unreachable dummy value
        }
        
        if (!hasElse || !thenV->getType()->isFirstClassType() || !elseV->getType()->isFirstClassType()) {
            return llvm::ConstantInt::getTrue(context); // Dummy void-like value
        }
        
        if (thenTerminated) return elseV;
        if (elseTerminated) return thenV;
        
        llvm::Type* mergeType = thenV->getType();
        if (thenV->getType() != elseV->getType()) {
            // Need coercion
            if (thenV->getType()->isFloatingPointTy() || elseV->getType()->isFloatingPointTy()) {
                mergeType = llvm::Type::getDoubleTy(context);
            } else {
                unsigned lw = thenV->getType()->getIntegerBitWidth();
                unsigned rw = elseV->getType()->getIntegerBitWidth();
                mergeType = lw > rw ? thenV->getType() : elseV->getType();
            }
            thenV = coerceToType(thenV, mergeType);
            elseV = coerceToType(elseV, mergeType);
        }
        
        llvm::PHINode* phi = builder.CreatePHI(mergeType, 2, "iftmp");
        phi->addIncoming(thenV, thenBB);
        phi->addIncoming(elseV, elseBB);
        return phi;
    }

    llvm::Value* compileWhile(const Expr& expr) {
        llvm::Function* theFunction = builder.GetInsertBlock()->getParent();

        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "whilecond", theFunction);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "whilebody");
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "whilecont");

        builder.CreateBr(condBB);
        builder.SetInsertPoint(condBB);

        llvm::Value* condV = compileExpr(expr.condExpr);
        if (!condV) return nullptr;

        // Convert condition to bool (i1)
        if (condV->getType()->isFloatingPointTy()) {
            condV = builder.CreateFCmpONE(condV, llvm::ConstantFP::get(context, llvm::APFloat(0.0)), "ifcond");
        } else if (condV->getType()->isIntegerTy() && condV->getType()->getIntegerBitWidth() != 1) {
            condV = builder.CreateICmpNE(condV, llvm::ConstantInt::get(condV->getType(), 0), "ifcond");
        }

        builder.CreateCondBr(condV, bodyBB, mergeBB);

        theFunction->insert(theFunction->end(), bodyBB);
        builder.SetInsertPoint(bodyBB);

        // continue re-checks the condition (condBB), break exits (mergeBB) -
        // same targets Loop/For use, just condBB doubles as While's own
        // "recheck" step instead of needing a dedicated increment block.
        loopStack.push_back({condBB, mergeBB});
        llvm::Value* bodyV = compileExpr(expr.lhs);
        loopStack.pop_back();
        if (!bodyV) return nullptr;

        if (!blockTerminated) {
            builder.CreateBr(condBB);
        }
        blockTerminated = false; // Reset for merge block

        theFunction->insert(theFunction->end(), mergeBB);
        builder.SetInsertPoint(mergeBB);

        // While loops in Frust return void/dummy 0.
        return llvm::ConstantFP::get(context, llvm::APFloat(0.0));
    }

    // bodyBB always exists (has a br from the block before the loop and,
    // usually, a back-edge from its own tail) so it's never a dangling
    // reference; mergeBB can legitimately end up with zero predecessors if
    // the body never `break`s (no other construct feeds it, unlike While/
    // For's condBB->mergeBB edge) - that's dead/unreachable LLVM IR, not a
    // verifier failure, so don't "fix" it if you see it later.
    llvm::Value* compileLoop(const Expr& expr) {
        llvm::Function* theFunction = builder.GetInsertBlock()->getParent();

        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "loopbody", theFunction);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "loopcont");

        builder.CreateBr(bodyBB);
        builder.SetInsertPoint(bodyBB);

        loopStack.push_back({bodyBB, mergeBB});
        llvm::Value* bodyV = compileExpr(expr.lhs);
        loopStack.pop_back();
        if (!bodyV) return nullptr;

        if (!blockTerminated) {
            builder.CreateBr(bodyBB);
        }
        blockTerminated = false;

        theFunction->insert(theFunction->end(), mergeBB);
        builder.SetInsertPoint(mergeBB);

        return llvm::ConstantFP::get(context, llvm::APFloat(0.0));
    }

    // Range-only iteration (`for i in a..b { }`, exclusive upper bound,
    // matching Rust convention) - no general iterator protocol, no
    // collection type exists yet either. `continue` targets a dedicated
    // increment block (incrBB), not condBB directly and not folded into
    // bodyBB's tail - that's what makes continue actually skip to
    // "increment then recheck" rather than re-running the body's start.
    llvm::Value* compileFor(const Expr& expr) {
        llvm::Function* theFunction = builder.GetInsertBlock()->getParent();

        llvm::Value* startV = compileExpr(expr.condExpr);
        llvm::Value* endV = compileExpr(expr.rhs);
        if (!startV || !endV) return nullptr;
        llvm::Type* counterTy = llvm::Type::getInt64Ty(context);
        startV = coerceToType(startV, counterTy);
        endV = coerceToType(endV, counterTy);

        llvm::IRBuilder<> tmpBuilder(&theFunction->getEntryBlock(), theFunction->getEntryBlock().begin());
        llvm::AllocaInst* counterAlloca = tmpBuilder.CreateAlloca(counterTy, nullptr, expr.text);
        builder.CreateStore(startV, counterAlloca);

        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "forcond", theFunction);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "forbody");
        llvm::BasicBlock* incrBB = llvm::BasicBlock::Create(context, "forincr");
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "forcont");

        builder.CreateBr(condBB);
        builder.SetInsertPoint(condBB);
        llvm::Value* cur = builder.CreateLoad(counterTy, counterAlloca, expr.text);
        llvm::Value* cond = builder.CreateICmpSLT(cur, endV, "forcond");
        builder.CreateCondBr(cond, bodyBB, mergeBB);

        theFunction->insert(theFunction->end(), bodyBB);
        builder.SetInsertPoint(bodyBB);
        // Bound before compiling the body so the body can read the loop
        // var like any other identifier - re-loaded fresh each reference,
        // same as any other alloca-backed binding.
        namedValues[expr.text] = counterAlloca;

        loopStack.push_back({incrBB, mergeBB});
        llvm::Value* bodyV = compileExpr(expr.lhs);
        loopStack.pop_back();
        if (!bodyV) return nullptr;

        if (!blockTerminated) {
            builder.CreateBr(incrBB);
        }
        blockTerminated = false;

        theFunction->insert(theFunction->end(), incrBB);
        builder.SetInsertPoint(incrBB);
        llvm::Value* cur2 = builder.CreateLoad(counterTy, counterAlloca, expr.text);
        llvm::Value* next = builder.CreateAdd(cur2, llvm::ConstantInt::get(counterTy, 1));
        builder.CreateStore(next, counterAlloca);
        builder.CreateBr(condBB);

        theFunction->insert(theFunction->end(), mergeBB);
        builder.SetInsertPoint(mergeBB);

        return llvm::ConstantFP::get(context, llvm::APFloat(0.0));
    }

    llvm::Value* compileBreak(const Expr& expr) {
        (void)expr;
        if (loopStack.empty()) {
            std::cerr << "frust: codegen error: 'break' used outside of a loop\n";
            return nullptr;
        }
        builder.CreateBr(loopStack.back().second);
        blockTerminated = true;
        return llvm::ConstantFP::get(context, llvm::APFloat(0.0));
    }

    llvm::Value* compileContinue(const Expr& expr) {
        (void)expr;
        if (loopStack.empty()) {
            std::cerr << "frust: codegen error: 'continue' used outside of a loop\n";
            return nullptr;
        }
        builder.CreateBr(loopStack.back().first);
        blockTerminated = true;
        return llvm::ConstantFP::get(context, llvm::APFloat(0.0));
    }

    llvm::Value* compilePerform(const Expr& expr) {
        if (!currentCoroHandle) {
            std::cerr << "frust: perform used outside of a coroutine function\n";
            return nullptr;
        }

        int effectId = std::hash<std::string>{}(expr.text) & 0x7FFFFFFF;
        llvm::Value* effectIdVal = builder.getInt32(effectId);
        
        llvm::Value* yieldVal = llvm::ConstantFP::get(context, llvm::APFloat(0.0));
        if (!expr.args.empty()) {
            llvm::Value* argVal = compileExpr(expr.args[0]);
            if (argVal) yieldVal = coerceToType(argVal, llvm::Type::getDoubleTy(context));
        }
        
        llvm::Value* idPtr = builder.CreateStructGEP(getPromiseType(), currentPromiseAlloc, 0);
        builder.CreateStore(effectIdVal, idPtr);
        
        llvm::Value* argPtr = builder.CreateStructGEP(getPromiseType(), currentPromiseAlloc, 1);
        builder.CreateStore(yieldVal, argPtr);
        
        llvm::Function* coroSaveFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_save);
        llvm::Function* coroSuspendFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_suspend);
        
        llvm::Value* saveToken = builder.CreateCall(coroSaveFn, {currentCoroHandle});
        llvm::Value* suspendResult = builder.CreateCall(coroSuspendFn, {saveToken, builder.getInt1(false)});
        
        llvm::Function* llvmFn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* resumeBB = llvm::BasicBlock::Create(context, "resume", llvmFn);
        
        llvm::SwitchInst* sw = builder.CreateSwitch(suspendResult, currentSuspendBB, 2);
        sw->addCase(builder.getInt8(0), resumeBB);
        sw->addCase(builder.getInt8(1), currentCleanupBB);
        
        builder.SetInsertPoint(resumeBB);
        
        llvm::Value* resumePtr = builder.CreateStructGEP(getPromiseType(), currentPromiseAlloc, 2);
        return builder.CreateLoad(llvm::Type::getDoubleTy(context), resumePtr, "resumeVal");
    }

    llvm::Value* compileHandle(const Expr& expr) {
        llvm::Value* coroHandle = compileExpr(expr.lhs);
        if (!coroHandle) return nullptr;
        
        llvm::Function* llvmFn = builder.GetInsertBlock()->getParent();
        
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(context, "handle_loop", llvmFn);
        llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(context, "handle_done", llvmFn);
        llvm::BasicBlock* dispatchBB = llvm::BasicBlock::Create(context, "handle_dispatch", llvmFn);
        
        builder.CreateBr(loopBB);
        builder.SetInsertPoint(loopBB);
        
        llvm::Function* coroDoneFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_done);
        llvm::Value* isDone = builder.CreateCall(coroDoneFn, {coroHandle});
        builder.CreateCondBr(isDone, doneBB, dispatchBB);
        
        builder.SetInsertPoint(dispatchBB);
        
        llvm::Function* coroPromiseFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_promise);
        llvm::Value* promiseI8 = builder.CreateCall(coroPromiseFn, {
            coroHandle, 
            builder.getInt32(8),
            builder.getInt1(false)
        });
        llvm::Value* promisePtr = builder.CreateBitCast(promiseI8, llvm::PointerType::getUnqual(getPromiseType()));
        
        llvm::Value* idPtr = builder.CreateStructGEP(getPromiseType(), promisePtr, 0);
        llvm::Value* effectIdVal = builder.CreateLoad(llvm::Type::getInt32Ty(context), idPtr);
        
        llvm::SwitchInst* sw = builder.CreateSwitch(effectIdVal, loopBB, expr.handleCases.size());
        
        for (const auto& hc : expr.handleCases) {
            llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(context, "handle_case_" + hc.effectName, llvmFn);
            int eId = std::hash<std::string>{}(hc.effectName) & 0x7FFFFFFF;
            sw->addCase(builder.getInt32(eId), caseBB);
            
            builder.SetInsertPoint(caseBB);
            
            llvm::Value* oldParam = nullptr;
            if (!hc.params.empty()) {
                const std::string& pName = hc.params[0].name;
                if (namedValues.count(pName)) oldParam = namedValues[pName];
                
                llvm::Value* argPtr = builder.CreateStructGEP(getPromiseType(), promisePtr, 1);
                llvm::Value* argVal = builder.CreateLoad(llvm::Type::getDoubleTy(context), argPtr);
                namedValues[pName] = argVal;
            }
            
            llvm::Value* oldHandleForResume = currentActiveHandleForResume;
            llvm::Value* oldPromiseForResume = currentActivePromiseForResume;
            currentActiveHandleForResume = coroHandle;
            currentActivePromiseForResume = promisePtr;
            
            compileExpr(hc.body);
            
            currentActiveHandleForResume = oldHandleForResume;
            currentActivePromiseForResume = oldPromiseForResume;
            
            if (!blockTerminated) {
                // After executing the handler (which should include a resume()), loop back to check if it yielded another effect or finished.
                builder.CreateBr(loopBB);
            }
        }
        
        builder.SetInsertPoint(doneBB);
        
        llvm::Function* coroPromiseFn2 = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_promise);
        llvm::Value* promiseI8_2 = builder.CreateCall(coroPromiseFn2, {coroHandle, builder.getInt32(8), builder.getInt1(false)});
        llvm::Value* promisePtr2 = builder.CreateBitCast(promiseI8_2, llvm::PointerType::getUnqual(getPromiseType()));
        
        llvm::Value* retPtr = builder.CreateStructGEP(getPromiseType(), promisePtr2, 3);
        llvm::Value* finalRetVal = builder.CreateLoad(llvm::Type::getDoubleTy(context), retPtr);
        
        llvm::Function* coroDestroyFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_destroy);
        builder.CreateCall(coroDestroyFn, {coroHandle});
        
        return finalRetVal;
    }

    llvm::Value* compileResume(const Expr& expr) {
        if (!currentActiveHandleForResume) {
            std::cerr << "frust: resume used outside of a handler\n";
            return nullptr;
        }
        
        llvm::Value* resumeVal = llvm::ConstantFP::get(context, llvm::APFloat(0.0));
        if (expr.lhs) {
            llvm::Value* rv = compileExpr(expr.lhs);
            if (rv) resumeVal = coerceToType(rv, llvm::Type::getDoubleTy(context));
        }
        
        llvm::Value* resumePtr = builder.CreateStructGEP(getPromiseType(), currentActivePromiseForResume, 2);
        builder.CreateStore(resumeVal, resumePtr);
        
        llvm::Function* coroResumeFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_resume);
        builder.CreateCall(coroResumeFn, {currentActiveHandleForResume});
        
        return llvm::ConstantFP::get(context, llvm::APFloat(0.0));
    }

    // "TypeName::methodName" - matches the same "::"-joining convention
    // compileCall's Path handling already produces for lookups, so calling
    // a method's LLVM function symbol works via the exact same string
    // format as any other qualified call.
    static std::string mangleMethodName(const std::string& typeName, const std::string& methodName) {
        return typeName + "::" + methodName;
    }

public:
    // Pass-1 step: create the llvm::Function (signature only, no body) so
    // module.getFunction() lookups succeed for any call regardless of
    // textual declaration order - see compileProgram's two-pass comment.
    llvm::Function* declareFunctionSignature(const FunctionDecl& fn) {
        std::string llvmName = fn.isMethod ? mangleMethodName(fn.selfTypeName, fn.name) : fn.name;
        bool isCoro = hasPerform(fn.body);

        std::vector<llvm::Type*> paramTypes;
        if (fn.isMethod) paramTypes.push_back(llvm::PointerType::getUnqual(context)); // self, always by pointer in v1
        for (auto& p : fn.params) paramTypes.push_back(resolveType(p.type));

        llvm::Type* declaredRetType = fn.returnType ? resolveType(fn.returnType) : llvm::Type::getVoidTy(context);
        llvm::Type* retType = isCoro ? llvm::PointerType::getUnqual(context) : declaredRetType;

        llvm::FunctionType* ft = llvm::FunctionType::get(retType, paramTypes, false);
        llvm::Function* llvmFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, llvmName, module);
        // Was unconditional - marked every function as a presplit
        // coroutine to LLVM's coroutine-splitting pass regardless of
        // whether it actually contains a `perform` (isCoro, computed
        // above). An ordinary function that never suspends has no
        // business being run through coroutine lowering.
        if (isCoro) llvmFn->setPresplitCoroutine();

        if (fn.isMethod) methods[llvmName] = &fn;
        return llvmFn;
    }

    // Pass-2 step: fill in the body for a signature declareFunctionSignature
    // already created.
    llvm::Function* compileFunction(const FunctionDecl& fn) {
        std::string llvmName = fn.isMethod ? mangleMethodName(fn.selfTypeName, fn.name) : fn.name;
        llvm::Function* llvmFn = module.getFunction(llvmName);
        if (!llvmFn) {
            std::cerr << "frust: codegen internal error: '" << llvmName << "' has no declared signature\n";
            return nullptr;
        }

        bool isCoro = hasPerform(fn.body);
        llvm::Type* declaredRetType = fn.returnType ? resolveType(fn.returnType) : llvm::Type::getVoidTy(context);

        if (fn.isExtern) {
            return llvmFn;
        }

        namedValues.clear();
        namedValueStructType.clear();

        auto argIt = llvmFn->args().begin();
        if (fn.isMethod) {
            argIt->setName("self");
            namedValues["self"] = &*argIt;
            namedValueStructType["self"] = fn.selfTypeName;
            ++argIt;
        }
        for (auto& p : fn.params) {
            argIt->setName(p.name);
            namedValues[p.name] = &*argIt;
            if (auto structName = resolveStructTypeName(p.type)) {
                namedValueStructType[p.name] = *structName;
            }
            ++argIt;
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", llvmFn);
        builder.SetInsertPoint(bb);
        blockTerminated = false;

        llvm::Value* prevCoroHandle = currentCoroHandle;
        llvm::Value* prevPromiseAlloc = currentPromiseAlloc;
        llvm::Value* prevCoroId = currentCoroId;
        llvm::BasicBlock* prevCleanupBB = currentCleanupBB;

        if (isCoro) {
            llvm::Function* coroIdFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_id);
            llvm::Function* coroSizeFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_size, {llvm::Type::getInt64Ty(context)});
            llvm::Function* coroBeginFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_begin);

            llvm::Value* nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context));
            currentPromiseAlloc = builder.CreateAlloca(getPromiseType(), nullptr, "promise");
            llvm::Value* promiseI8 = currentPromiseAlloc;

            currentCoroId = builder.CreateCall(coroIdFn, { builder.getInt32(32), promiseI8, nullPtr, nullPtr });
            llvm::Value* coroSize = builder.CreateCall(coroSizeFn);
            
            llvm::FunctionCallee mallocFn = module.getOrInsertFunction("malloc", 
                llvm::FunctionType::get(llvm::PointerType::getUnqual(context), {llvm::Type::getInt64Ty(context)}, false));
            llvm::Value* frameAlloc = builder.CreateCall(mallocFn, {coroSize});
            
            currentCoroHandle = builder.CreateCall(coroBeginFn, {currentCoroId, frameAlloc});
            currentCleanupBB = llvm::BasicBlock::Create(context, "cleanup", llvmFn);
            currentSuspendBB = llvm::BasicBlock::Create(context, "suspend", llvmFn);
        }

        llvm::Value* result = compileExpr(fn.body);

        if (!blockTerminated) {
            if (isCoro) {
                if (result) {
                    llvm::Value* retPtr = builder.CreateStructGEP(getPromiseType(), currentPromiseAlloc, 3);
                    builder.CreateStore(coerceToType(result, llvm::Type::getDoubleTy(context)), retPtr);
                }
                
                llvm::BasicBlock* finalSuspendBB = llvm::BasicBlock::Create(context, "final_suspend", llvmFn);
                builder.CreateBr(finalSuspendBB);
                builder.SetInsertPoint(finalSuspendBB);
                
                llvm::Function* coroSaveFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_save);
                llvm::Value* saveToken = builder.CreateCall(coroSaveFn, {currentCoroHandle});
                
                llvm::Function* coroSuspendFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_suspend);
                llvm::Value* suspendResult = builder.CreateCall(coroSuspendFn, {saveToken, builder.getInt1(true)});
                
                llvm::SwitchInst* sw = builder.CreateSwitch(suspendResult, currentSuspendBB, 2);
                sw->addCase(builder.getInt8(0), currentCleanupBB);
                sw->addCase(builder.getInt8(1), currentCleanupBB);
                
                blockTerminated = true;
            } else {
                if (declaredRetType->isVoidTy()) {
                    builder.CreateRetVoid();
                } else if (result) {
                    builder.CreateRet(coerceToType(result, declaredRetType));
                } else {
                    std::cerr << "frust: codegen error: function '" << fn.name << "' has no return value\n";
                    llvmFn->eraseFromParent();
                    return nullptr;
                }
            }
        }

        if (isCoro) {
            builder.SetInsertPoint(currentCleanupBB);
            
            llvm::Function* coroFreeFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_free);
            llvm::Value* memToFree = builder.CreateCall(coroFreeFn, {currentCoroId, currentCoroHandle});
            
            llvm::BasicBlock* freeBB = llvm::BasicBlock::Create(context, "freeBB", llvmFn);
            llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "endBB", llvmFn);
            
            llvm::Value* isNull = builder.CreateICmpEQ(memToFree, llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context)));
            builder.CreateCondBr(isNull, endBB, freeBB);
            
            builder.SetInsertPoint(freeBB);
            llvm::FunctionCallee freeFn = module.getOrInsertFunction("free", 
                llvm::FunctionType::get(llvm::Type::getVoidTy(context), {llvm::PointerType::getUnqual(context)}, false));
            builder.CreateCall(freeFn, {memToFree});
            builder.CreateBr(endBB);
            
            builder.SetInsertPoint(endBB);
            llvm::Function* coroEndFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::coro_end);
            builder.CreateCall(coroEndFn, {currentCoroHandle, builder.getInt1(false), llvm::ConstantTokenNone::get(context)});
            builder.CreateRet(currentCoroHandle);
            
            builder.SetInsertPoint(currentSuspendBB);
            builder.CreateBr(endBB);
        }

        currentCoroHandle = prevCoroHandle;
        currentPromiseAlloc = prevPromiseAlloc;
        currentCoroId = prevCoroId;
        currentCleanupBB = prevCleanupBB;

        std::string errMsg;
        llvm::raw_string_ostream os(errMsg);
        if (llvm::verifyFunction(*llvmFn, &os)) {
            std::cerr << "frust: LLVM verification failed for '" << fn.name << "': " << os.str() << "\n";
            llvmFn->eraseFromParent();
            return nullptr;
        }
        
        // If this is the entry function, synthesize a standard C main that calls it
        if (fn.isEntry) {
            llvm::FunctionType* mainFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false);
            llvm::Function* cMainFn = llvm::Function::Create(mainFt, llvm::Function::ExternalLinkage, "main", module);
            llvm::BasicBlock* mainBb = llvm::BasicBlock::Create(context, "entry", cMainFn);
            builder.SetInsertPoint(mainBb);
            
            llvm::Value* retVal = builder.CreateCall(llvmFn);
            if (llvmFn->getReturnType()->isVoidTy()) {
                builder.CreateRet(builder.getInt32(0));
            } else {
                builder.CreateRet(coerceToType(retVal, llvm::Type::getInt32Ty(context)));
            }
        }
        
        return llvmFn;
    }

    // For the REPL: wraps a single bare top-level statement (`dist / time`,
    // `let x = 5`, ...) in a zero-arg function that always returns f64 -
    // there's no declared signature to work from for a bare statement, and
    // "show REPL results as a double" is a simple, honest v1 choice rather
    // than trying to infer a real return type ahead of compiling the body.
    llvm::Function* compileAnonymous(const std::string& name, const Expr* body) {
        llvm::Type* retType = llvm::Type::getDoubleTy(context);
        llvm::FunctionType* ft = llvm::FunctionType::get(retType, {}, false);
        llvm::Function* llvmFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module);

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", llvmFn);
        builder.SetInsertPoint(bb);
        blockTerminated = false;

        llvm::Value* result = compileExpr(body);
        if (!blockTerminated) {
            if (!result) {
                std::cerr << "frust: codegen error: expression produced no value\n";
                llvmFn->eraseFromParent();
                return nullptr;
            }
            builder.CreateRet(coerceToType(result, retType));
        }

        std::string errMsg;
        llvm::raw_string_ostream os(errMsg);
        if (llvm::verifyFunction(*llvmFn, &os)) {
            std::cerr << "frust: LLVM verification failed: " << os.str() << "\n";
            llvmFn->eraseFromParent();
            return nullptr;
        }
        return llvmFn;
    }
};

} // namespace frust
