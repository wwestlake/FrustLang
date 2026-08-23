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

#include <map>

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
#include <set>
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

        indexEffectDecls(prog);
        indexInterfaceDecls(prog);
        if (!compileManifestDecl(prog)) return false;

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

        // Vtables: needs every method's real llvm::Function to exist as a
        // DECLARATION (Pass 1, above) - a vtable slot just references that
        // Function's address, which is stable as soon as it's declared,
        // not only once its body is filled in. Has to run before Pass 2
        // (not after): a function body compiled in Pass 2 - e.g.
        // `let a: Automation = RampAutomation { ... }` inside `start_app`
        // - needs the vtable to already exist the moment it runs, and
        // Pass 2 compiles decls in file order with no guarantee an impl
        // block textually precedes every function that constructs an
        // interface value from it.
        bool ok = true;
        for (auto* decl : prog.decls) {
            if (decl->kind != DeclKind::Impl || decl->implDecl->interfaceName.empty()) continue;
            if (!buildVtable(*decl->implDecl)) ok = false;
        }

        // Pass 2: fill in bodies.
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

    // EVERY function/method's FunctionDecl (free functions too, unlike
    // `methods` above), keyed by its LLVM symbol name - lets call-site
    // argument coercion see each parameter's real declared TypeExpr, not
    // just its resolved llvm::Type*. Needed specifically for interface-
    // typed parameters: every interface resolves to the same generic
    // fat-pointer LLVM shape, so "which interface does this parameter
    // want" can only be answered from the original TypeExpr's name, the
    // same "opaque pointer erases identity" problem namedValueStructType
    // already exists to solve, one level up at the call site.
    std::unordered_map<std::string, const FunctionDecl*> functionDeclsByName;

    // `interface Name { fn method(&mut self, ...) -> T ... }` - a named,
    // checked capability contract. Populated by indexInterfaceDecls()
    // before Pass 1, same timing as effectDecls.
    std::map<std::string, InterfaceDecl*> interfaceDecls;

    // One vtable global per (interfaceName, concreteTypeName) pair that has
    // a real `impl InterfaceName for TypeName { ... }` block - built in a
    // Pass 3 after Pass 2 compiles the methods those vtables point at
    // (needs their real llvm::Function addresses to already exist).
    // Keyed by mangleMethodName(interfaceName, typeName) - same composite-
    // key shape already used for methods, just reused for a different pair
    // of names.
    std::unordered_map<std::string, llvm::GlobalVariable*> vtables;

    // variable/param name -> interface name, the fat-pointer-typed sibling
    // of namedValueStructType. A name here holds a genuine 2-word
    // { ptr data, ptr vtable } value (see resolveType's ASTExpr-adjacent
    // interface handling), not a plain struct pointer.
    std::unordered_map<std::string, std::string> namedValueInterfaceType;

    // variable/param name -> pointee type name, for `raw* T`-typed
    // bindings (LANGUAGE_GAPS.md #1). Same reasoning as
    // namedValueStructType: under opaque pointers, `*ptr`'s
    // compileUnary has no way to recover what T is from the compiled
    // llvm::Value alone (a raw pointer's LLVM type carries no pointee
    // info) - only the STATIC Frust type (TypeExpr::isRawPointer/.name)
    // ever knew, so it has to be recorded here at the binding site.
    std::unordered_map<std::string, std::string> namedValueRawPointeeType;

    // variable/param name -> element type name, for `Vector<T>`-typed
    // bindings (LANGUAGE_GAPS.md #3). A Vector<T> value is a pointer to
    // a shared, element-type-erased heap header (vectorHeaderType()) -
    // T only matters for computing element size/stride at each
    // push/get/index site, the same "opaque pointer erases identity,
    // only the static declared type ever knew" reasoning as
    // namedValueStructType/namedValueRawPointeeType. `Vector::new()`
    // has no way to know T on its own (Frust has no real generic
    // function syntax) - only the enclosing `let`'s own type annotation
    // ever carries it, so construction is special-cased in the Let
    // branch of compileExpr, not in compileCall.
    std::unordered_map<std::string, std::string> namedValueVectorElementType;

    // A closure value (LANGUAGE_GAPS.md #6) is a fat pointer
    // { ptr code, ptr env } - the exact same shape/mechanism
    // wrapAsInterface already builds for interface dispatch, reused
    // here rather than inventing a second representation. The fat
    // pointer's LLVM type alone can't say what to pass/expect at a
    // call site (opaque pointers again), so a closure-typed binding's
    // real param/return LLVM types are recorded here at the closure
    // literal's construction site and consulted by the call-dispatch
    // path instead of compileIndirectCall's generic (and wrong, for a
    // fat pointer) plain-pointer-callee assumption.
    struct ClosureSignature {
        std::vector<llvm::Type*> paramTypes;
        llvm::Type* returnType = nullptr;
    };
    std::unordered_map<std::string, ClosureSignature> namedValueClosureSignature;
    int closureCounter = 0;

    // `struct Box<T> { ... }` templates (LANGUAGE_GAPS.md #4) - name ->
    // the AST declaration, NOT an LLVM type. indexStructs() does not
    // eagerly create an LLVM struct type for a generic struct's bare
    // name (there's no single real type for "Box" alone - only for a
    // concrete instantiation like "Box<i64>"); this is what
    // getOrCreateMonomorphizedStruct looks up when a concrete use site
    // needs one, lazily, memoized into structTypes/structFieldIndex
    // under the mangled "Box<i64>"-style name so every OTHER struct-
    // handling code path in this file (field access, method calls,
    // sizeof) works on a monomorphized instantiation with zero further
    // special-casing.
    std::unordered_map<std::string, const StructDecl*> genericStructTemplates;

    void indexInterfaceDecls(const Program& prog) {
        for (auto* decl : prog.decls) {
            if (decl->kind == DeclKind::Interface) {
                interfaceDecls[decl->interfaceDecl->name] = decl->interfaceDecl;
            }
        }
    }

    // `manifest "...";` (frust_plugin_host/FrustPluginHost.cpp reads this
    // by name before a plugin is ever JIT-linked or executed - see
    // frust_plugin_load's "no manifest, no load" gate). Emits a
    // PrivateLinkage global under a FIXED name, same pattern as the
    // print_f64 format-string global above - private linkage is fine
    // (and preferred: it never becomes a resolvable extern JIT symbol a
    // plugin's own code could collide with) because the host inspects it
    // directly on the in-memory llvm::Module object, not through the
    // JIT's symbol table. At most one `manifest` decl per program - a
    // second one is a real compile error, not a silent overwrite.
    bool compileManifestDecl(const Program& prog) {
        const ManifestDecl* found = nullptr;
        for (auto* decl : prog.decls) {
            if (decl->kind != DeclKind::Manifest) continue;
            if (found) {
                std::cerr << "frust: codegen error: more than one 'manifest' declaration in the same program\n";
                return false;
            }
            found = decl->manifestDecl;
        }
        if (!found) return true; // no manifest decl at all is not a codegen error - the host decides what that means.

        llvm::Constant* jsonConst = llvm::ConstantDataArray::getString(context, found->json);
        new llvm::GlobalVariable(module, jsonConst->getType(), true,
            llvm::GlobalValue::PrivateLinkage, jsonConst, kFrustPluginManifestGlobalName);
        return true;
    }

    llvm::StructType* fatPointerType() {
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
        return llvm::StructType::get(context, {ptrTy, ptrTy});
    }

    // `{ ptr data, i64 length, i64 capacity }` - the ONE shared,
    // element-type-erased LLVM shape every `Vector<T>` instance uses,
    // regardless of T (LANGUAGE_GAPS.md #3). A Vector<T> value is a
    // pointer to one of these, heap-allocated by compileVectorNew.
    // Anonymous (not llvm::StructType::create with a name) since,
    // unlike user structs, there's no Frust-level name to recover it
    // by - namedValueVectorElementType is what tells push/get/index
    // which element type T to use at each site.
    llvm::StructType* vectorHeaderType() {
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(context);
        return llvm::StructType::get(context, {ptrTy, i64Ty, i64Ty});
    }

    // Direct module.getOrInsertFunction wiring for malloc/realloc -
    // same pattern compileProgram's own prologue already uses for
    // printf (print_f64's implementation). Vector<T> is a compiler
    // built-in, not user Frust code, so it doesn't rely on the user's
    // own source having written `extern fn malloc(...)` - the compiler
    // wires these up directly, once, regardless of what the program
    // itself declares.
    llvm::FunctionCallee getMallocFn() {
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(context);
        return module.getOrInsertFunction("malloc", llvm::FunctionType::get(ptrTy, {i64Ty}, false));
    }
    llvm::FunctionCallee getReallocFn() {
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(context);
        return module.getOrInsertFunction("realloc", llvm::FunctionType::get(ptrTy, {ptrTy, i64Ty}, false));
    }

    // `Vector::new()` - zero-initializes a fresh header (data=null,
    // length=0, capacity=0). The actual element buffer isn't allocated
    // until the first push (compileVectorMethodCall grows from 0 on
    // first use) - an empty Vector<T> that's never pushed to never
    // allocates an element buffer at all, only its small header.
    llvm::Value* compileVectorNew() {
        llvm::StructType* hdrTy = vectorHeaderType();
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(context);
        uint64_t hdrSize = module.getDataLayout().getTypeAllocSize(hdrTy);

        llvm::Value* headerPtr = builder.CreateCall(getMallocFn(),
            {llvm::ConstantInt::get(i64Ty, hdrSize)}, "vecheader");

        builder.CreateStore(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
            builder.CreateStructGEP(hdrTy, headerPtr, 0));
        builder.CreateStore(llvm::ConstantInt::get(i64Ty, 0), builder.CreateStructGEP(hdrTy, headerPtr, 1));
        builder.CreateStore(llvm::ConstantInt::get(i64Ty, 0), builder.CreateStructGEP(hdrTy, headerPtr, 2));
        return headerPtr;
    }

    // `receiver.push(x)` / `.len()` / `.get(i)` - the only three
    // Vector<T> operations this pass ships (LANGUAGE_GAPS.md #3).
    // `push` grows the element buffer (realloc, doubling from a base
    // capacity of 4) exactly when length == capacity, matching the
    // standard amortized-growth strategy - no PHI needed for the
    // conditional grow: both the grow and no-grow paths converge on
    // the same afterGrowBB and the element pointer is always computed
    // AFTER that point by re-loading the (possibly just-updated) data
    // field, so it's correct either way without merging SSA values by
    // hand.
    llvm::Value* compileVectorMethodCall(const Expr& expr, const Expr& member, const std::string& elemTypeName) {
        llvm::Value* headerPtr = compileExpr(member.lhs);
        if (!headerPtr) return nullptr;

        llvm::StructType* hdrTy = vectorHeaderType();
        llvm::Type* elemTy = resolveTypeByName(elemTypeName);
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(context);
        llvm::Value* dataFieldPtr = builder.CreateStructGEP(hdrTy, headerPtr, 0);
        llvm::Value* lenFieldPtr = builder.CreateStructGEP(hdrTy, headerPtr, 1);
        llvm::Value* capFieldPtr = builder.CreateStructGEP(hdrTy, headerPtr, 2);

        if (member.text == "len") {
            if (!expr.args.empty()) {
                std::cerr << "frust: codegen error: Vector<T>'s 'len' takes no arguments\n";
                return nullptr;
            }
            return builder.CreateLoad(i64Ty, lenFieldPtr, "veclen");
        }

        if (member.text == "get") {
            if (expr.args.size() != 1) {
                std::cerr << "frust: codegen error: Vector<T>'s 'get' takes exactly 1 argument\n";
                return nullptr;
            }
            llvm::Value* idx = compileExpr(expr.args[0]);
            if (!idx) return nullptr;
            llvm::Value* dataPtr = builder.CreateLoad(ptrTy, dataFieldPtr, "vecdata");
            llvm::Value* elemPtr = builder.CreateGEP(elemTy, dataPtr, idx, "vecelemptr");
            return builder.CreateLoad(elemTy, elemPtr, "vecget");
        }

        if (member.text == "push") {
            if (expr.args.size() != 1) {
                std::cerr << "frust: codegen error: Vector<T>'s 'push' takes exactly 1 argument\n";
                return nullptr;
            }
            llvm::Value* val = compileExpr(expr.args[0]);
            if (!val) return nullptr;
            val = coerceToType(val, elemTy);

            llvm::Value* length = builder.CreateLoad(i64Ty, lenFieldPtr, "veclen");
            llvm::Value* capacity = builder.CreateLoad(i64Ty, capFieldPtr, "veccap");

            llvm::Function* theFunction = builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* growBB = llvm::BasicBlock::Create(context, "vecgrow", theFunction);
            llvm::BasicBlock* afterGrowBB = llvm::BasicBlock::Create(context, "vecgrowdone", theFunction);
            llvm::Value* needsGrow = builder.CreateICmpEQ(length, capacity, "vecneedsgrow");
            builder.CreateCondBr(needsGrow, growBB, afterGrowBB);

            builder.SetInsertPoint(growBB);
            llvm::Value* isZero = builder.CreateICmpEQ(capacity, llvm::ConstantInt::get(i64Ty, 0), "veccapzero");
            llvm::Value* doubled = builder.CreateMul(capacity, llvm::ConstantInt::get(i64Ty, 2), "vecdoubled");
            llvm::Value* newCapacity = builder.CreateSelect(isZero, llvm::ConstantInt::get(i64Ty, 4), doubled, "vecnewcap");
            uint64_t elemSize = module.getDataLayout().getTypeAllocSize(elemTy);
            llvm::Value* newByteSize = builder.CreateMul(newCapacity, llvm::ConstantInt::get(i64Ty, elemSize), "vecnewbytes");
            llvm::Value* oldData = builder.CreateLoad(ptrTy, dataFieldPtr, "vecolddata");
            llvm::Value* newData = builder.CreateCall(getReallocFn(), {oldData, newByteSize}, "vecnewdata");
            builder.CreateStore(newData, dataFieldPtr);
            builder.CreateStore(newCapacity, capFieldPtr);
            builder.CreateBr(afterGrowBB);

            builder.SetInsertPoint(afterGrowBB);
            llvm::Value* dataPtr = builder.CreateLoad(ptrTy, dataFieldPtr, "vecdata");
            llvm::Value* elemPtr = builder.CreateGEP(elemTy, dataPtr, length, "vecelemptr");
            builder.CreateStore(val, elemPtr);
            llvm::Value* newLength = builder.CreateAdd(length, llvm::ConstantInt::get(i64Ty, 1), "vecnewlen");
            builder.CreateStore(newLength, lenFieldPtr);
            return newLength;
        }

        std::cerr << "frust: codegen error: Vector<T> has no method '" << member.text << "'\n";
        return nullptr;
    }

    // (continueTarget, breakTarget) per enclosing loop, innermost last.
    std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> loopStack;

    llvm::Value* currentCoroHandle = nullptr;
    llvm::Value* currentPromiseAlloc = nullptr;
    llvm::Value* currentCoroId = nullptr;
    llvm::BasicBlock* currentCleanupBB = nullptr;
    llvm::BasicBlock* currentSuspendBB = nullptr;
    llvm::Type* currentFnRetType = nullptr;
    
    llvm::Value* currentActiveHandleForResume = nullptr;
    llvm::Value* currentActivePromiseForResume = nullptr;

    // Populated by indexEffectDecls() before any perform/handle codegen
    // - real declared param types for each `effect Name(...)`, needed so
    // compilePerform/compileHandle can pack/unpack more than one
    // argument, of the actual declared type, instead of always assuming
    // "exactly one f64." Without this, `effect Log(msg: String)` (the
    // spec's own example) can't work - a String silently truncated
    // through a double-typed slot.
    std::map<std::string, EffectDecl*> effectDecls;

    void indexEffectDecls(const Program& prog) {
        for (auto* decl : prog.decls) {
            if (decl->kind == DeclKind::Effect) {
                effectDecls[decl->effectDecl->name] = decl->effectDecl;
            }
        }
    }

    llvm::StructType* getPromiseType() {
        // { i32 effect_id, ptr arg_buffer, f64 resume_val, f64 return_val }
        // arg_buffer points to a malloc'd buffer holding one 8-byte slot
        // per performed argument, each slot storing that argument's own
        // real type (not forced through f64) - see compilePerform/
        // compileHandle. Null when the effect takes no arguments.
        // resume_val/return_val stay f64-only for this pass - not
        // needed by the spec's own effect examples, which only need the
        // *argument* side to carry a real type.
        return llvm::StructType::get(context, {
            llvm::Type::getInt32Ty(context),
            llvm::PointerType::getUnqual(context),
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

            // `struct Box<T> { ... }` (LANGUAGE_GAPS.md #4) - no single
            // real LLVM type exists for "Box" alone (field types can
            // reference T, which isn't a real type until a concrete use
            // site supplies one). Remember the template; monomorphize
            // lazily per concrete instantiation instead - see
            // getOrCreateMonomorphizedStruct.
            if (!sd.genericParams.empty()) {
                genericStructTemplates[sd.name] = &sd;
                continue;
            }

            std::vector<llvm::Type*> fieldTypes;
            auto& fieldIndex = structFieldIndex[sd.name];
            for (size_t i = 0; i < sd.fields.size(); ++i) {
                fieldTypes.push_back(resolveType(sd.fields[i].type));
                fieldIndex[sd.fields[i].name] = static_cast<int>(i);
            }
            structTypes[sd.name] = llvm::StructType::create(context, fieldTypes, sd.name);
        }
    }

    static std::string monomorphizedStructName(const std::string& baseName,
                                                 const std::vector<std::string>& concreteArgNames) {
        std::string mangled = baseName + "<";
        for (size_t i = 0; i < concreteArgNames.size(); ++i) {
            if (i) mangled += ",";
            mangled += concreteArgNames[i];
        }
        mangled += ">";
        return mangled;
    }

    // Lazily monomorphizes baseName<concreteArgNames...> the first time
    // it's actually needed (a concrete use site - a type annotation, a
    // struct-literal construction) - memoized into structTypes/
    // structFieldIndex under the mangled name, same maps every other
    // struct already lives in, so field access/method calls/sizeof all
    // work on a monomorphized instantiation with zero further special-
    // casing beyond this function existing. Substitution is a flat
    // name->name replacement on each field's declared type name - a
    // field typed exactly as a generic parameter (`value: T`) gets the
    // concrete type; anything else resolves normally. Nested generics
    // (a field typed `Vector<T>` inside `struct Box<T>`) are NOT
    // substituted - real, out-of-scope-for-v1 limitation, not silently
    // mishandled: resolveType would just resolve T as an unknown type
    // name and fall through to i64, so avoid this shape for now.
    llvm::StructType* getOrCreateMonomorphizedStruct(const std::string& baseName,
                                                       const std::vector<std::string>& concreteArgNames) {
        std::string mangled = monomorphizedStructName(baseName, concreteArgNames);
        auto existingIt = structTypes.find(mangled);
        if (existingIt != structTypes.end()) return existingIt->second;

        auto templateIt = genericStructTemplates.find(baseName);
        if (templateIt == genericStructTemplates.end()) {
            std::cerr << "frust: codegen error: '" << baseName << "' is not a generic struct\n";
            return nullptr;
        }
        const StructDecl& templateDecl = *templateIt->second;
        if (templateDecl.genericParams.size() != concreteArgNames.size()) {
            std::cerr << "frust: codegen error: '" << baseName << "' expects "
                       << templateDecl.genericParams.size() << " type argument(s), got "
                       << concreteArgNames.size() << "\n";
            return nullptr;
        }

        std::unordered_map<std::string, std::string> substitution;
        for (size_t i = 0; i < templateDecl.genericParams.size(); ++i) {
            substitution[templateDecl.genericParams[i]] = concreteArgNames[i];
        }

        std::vector<llvm::Type*> fieldTypes;
        auto& fieldIndex = structFieldIndex[mangled];
        for (size_t i = 0; i < templateDecl.fields.size(); ++i) {
            const std::string& fieldTypeName = templateDecl.fields[i].type->name;
            auto subIt = substitution.find(fieldTypeName);
            llvm::Type* fieldTy = (subIt != substitution.end())
                ? resolveTypeByName(subIt->second)
                : resolveType(templateDecl.fields[i].type);
            fieldTypes.push_back(fieldTy);
            fieldIndex[templateDecl.fields[i].name] = static_cast<int>(i);
        }
        llvm::StructType* structTy = llvm::StructType::create(context, fieldTypes, mangled);
        structTypes[mangled] = structTy;
        return structTy;
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
        if (expr->kind == ExprKind::SmartPtrNew) {
            // `own Foo { ... }` / `raw Foo { ... }` - same struct identity
            // as the literal it wraps, just heap-allocated instead of
            // stack (compileHeapStructLiteral). Let/call-site coercion
            // and method dispatch shouldn't care which allocation
            // strategy produced the pointer.
            return inferStructTypeName(expr->lhs);
        }
        if (expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier) {
            // A plain free-function call whose declared return type names
            // a struct - e.g. `let inst: Foo = make_foo();` (a
            // constructor-style function). Was previously nullopt
            // unconditionally ("struct-returning Call... deliberately out
            // of scope"), which meant a constructor's result could never
            // be used anywhere inferStructTypeName gates (interface
            // coercion, method calls on the result) even though the
            // pointer itself is perfectly valid - functionDeclsByName
            // (added for interface-typed call-argument coercion) already
            // has exactly the info needed to answer this now. Scoped to
            // free functions only, not method-call results - not needed
            // yet, and a Path-based static-method call shape doesn't
            // exist in this language currently anyway.
            auto it = functionDeclsByName.find(expr->lhs->text);
            if (it != functionDeclsByName.end()) {
                return resolveStructTypeName(it->second->returnType);
            }
        }
        return std::nullopt;
    }

    // Which pointee type (if any) a `raw* T`-typed expression statically
    // has, sourced from namedValueRawPointeeType - same "opaque pointer
    // erases identity" reasoning as inferStructTypeName, one level
    // simpler: only a plain named variable/param is covered in v1
    // (LANGUAGE_GAPS.md #1). To dereference the result of pointer
    // arithmetic (`ptr + n`), bind it to an explicitly `raw* T`-typed
    // `let` first - the same "anchor the type via a named binding"
    // convention inferStructTypeName already establishes for structs.
    std::optional<std::string> inferRawPointeeTypeName(const Expr* expr) {
        if (!expr) return std::nullopt;
        if (expr->kind == ExprKind::Identifier) {
            auto it = namedValueRawPointeeType.find(expr->text);
            if (it != namedValueRawPointeeType.end()) return it->second;
        }
        return std::nullopt;
    }

    // Builds a minimal, non-arena TypeExpr from just a plain type name -
    // TypeExpr has no arena-required constructor (a plain struct), so
    // this is safe to stack-allocate. Lets namedValueRawPointeeType's
    // stored strings (i64, a struct name, ...) go through the SAME
    // resolveType() every other type name in this file resolves
    // through, rather than a second, parallel name-to-llvm::Type switch
    // that could silently drift out of sync with resolveType's.
    llvm::Type* resolveTypeByName(const std::string& name) {
        TypeExpr t;
        t.name = name;
        return resolveType(&t);
    }

    llvm::Type* resolveType(const TypeExpr* type, int depth = 0) {
        if (!type) return llvm::Type::getInt64Ty(context); // untyped => default i64

        if (depth > 16) {
            std::cerr << "frust: codegen error: type alias cycle involving '" << type->name << "'\n";
            return llvm::Type::getInt64Ty(context);
        }

        // `raw* T` is always a pointer, regardless of what T names -
        // LANGUAGE_GAPS.md #1. Checked before alias resolution and
        // everything else below: this was a real, pre-existing gap -
        // resolveType previously ignored isRawPointer entirely and
        // resolved `raw* i64` to plain i64 (the pointee's VALUE type,
        // via the primitive-name check below), not a pointer. Found
        // while building this pass's deref/pointer-arithmetic support,
        // which needs a raw*-typed value to actually BE an LLVM pointer
        // for CreateGEP/CreateLoad/CreateStore to work at all.
        if (type->isRawPointer) return llvm::PointerType::getUnqual(context);

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
        // Opaque handle to a runtime-constructed AST node (FRUST_LANG_SPEC.md
        // 1.1's `quote`/`unquote`/`build_time`) - see buildQuoteTree below.
        // Under the hood it's just a frust::Expr* built live by the
        // frust_ast_* runtime functions, but Frust code never dereferences
        // it directly, so an opaque pointer is all callers need.
        if (n == "ASTExpr") return llvm::PointerType::getUnqual(context);

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

        // Generic struct instantiation (`Box<i64>`, `Result<i64,String>`)
        // - LANGUAGE_GAPS.md #4. A generic struct's bare name is never
        // in structTypes directly (see indexStructs) - only its
        // monomorphized instantiations are, memoized here on first use.
        if (genericStructTemplates.count(n) && !type->genericArgs.empty()) {
            std::vector<std::string> concreteArgNames;
            bool allTypeArgs = true;
            for (auto& arg : type->genericArgs) {
                if (arg.isIntConst || !arg.type) { allTypeArgs = false; break; }
                concreteArgNames.push_back(arg.type->name);
            }
            if (allTypeArgs && getOrCreateMonomorphizedStruct(n, concreteArgNames)) {
                return llvm::PointerType::getUnqual(context); // structs are always pointer-represented
            }
        }

        // A declared `interface Name { ... }` - values of this type are a
        // fat pointer { data, vtable }, not a plain struct pointer (see
        // buildVtable/compileMethodCall's interface-dispatch branch).
        if (interfaceDecls.count(n)) return fatPointerType();

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
        // Generic struct instantiation (LANGUAGE_GAPS.md #4) - a
        // function's declared return type of `Result<i64, String>`
        // never appears in structTypes under its bare name ("Result"
        // alone was never eagerly registered - see indexStructs). Real
        // gap, found before it could bite: without this, a call whose
        // return type is a generic struct (e.g. `let r: Result<i64,
        // String> = safe_divide(10, 2);`) would fail to be recognized
        // as a struct at all via inferStructTypeName's Call branch,
        // silently breaking field access on the result. Monomorphize
        // (idempotent, memoized) and return the mangled name, same as
        // resolveType's own generic-struct branch.
        if (genericStructTemplates.count(type->name) && !type->genericArgs.empty()) {
            std::vector<std::string> concreteArgNames;
            for (auto& arg : type->genericArgs) {
                if (arg.isIntConst || !arg.type) return std::nullopt;
                concreteArgNames.push_back(arg.type->name);
            }
            if (getOrCreateMonomorphizedStruct(type->name, concreteArgNames)) {
                return monomorphizedStructName(type->name, concreteArgNames);
            }
        }
        return std::nullopt;
    }

    // Same alias-following as resolveStructTypeName, but for interface
    // names instead of struct names.
    std::optional<std::string> resolveInterfaceName(const TypeExpr* type, int depth = 0) {
        if (!type || depth > 16) return std::nullopt;
        auto aliasIt = typeAliases.find(type->name);
        if (aliasIt != typeAliases.end()) return resolveInterfaceName(aliasIt->second, depth + 1);
        if (interfaceDecls.count(type->name)) return type->name;
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
        // Was missing entirely - a float value coerced toward an integer
        // target (e.g. `let x: i64 = <f64 expr>;`) fell through to
        // `return value` completely unconverted, silently leaving a
        // float-typed value where an integer was declared. Usually
        // invisible until the mismatched value reaches a strict boundary
        // (a function's declared return type), where LLVM's IR verifier
        // finally rejects it outright - confirmed directly with `let
        // as_i64: i64 = <f64 result>; as_i64` as a function's tail
        // expression.
        if (target->isIntegerTy() && value->getType()->isFloatingPointTy())
            return builder.CreateFPToSI(value, target);
        return value;
    }

    // Refinement types (`i32[!= 0]`, `f32[-1.0..1.0]`) were parsed into
    // RefinementInfo on TypeExpr and then never looked at anywhere in
    // codegen - FRUST_LANG_SPEC.md's explicit claim ("Guaranteed
    // zero-panic compile-time safety") was not backed by anything. Real
    // fix, scoped honestly: this is RUNTIME enforcement (a check emitted
    // at each refined parameter's function entry, panicking on
    // violation), not a compile-time proof that a violation can never
    // occur - true compile-time proof needs real value-range analysis
    // across the whole call graph, a much bigger project. Runtime
    // enforcement still delivers the spec's actual promise (divide-by-
    // zero genuinely cannot happen - the program stops first), just
    // caught at the call site rather than proven impossible ahead of
    // time.
    //
    // A `type NonZeroI32 = i32[!= 0]` alias attaches the refinement to
    // the ALIAS TARGET's TypeExpr, not to every use site's own TypeExpr
    // (`denominator: NonZeroI32` carries no refinement info directly) -
    // this walks the same alias chain resolveType() does to find it.
    const TypeExpr* resolveRefinementType(const TypeExpr* type, int depth = 0) {
        if (!type || depth > 16) return nullptr;
        if (type->refinementKind != RefinementKind::None) return type;
        auto aliasIt = typeAliases.find(type->name);
        if (aliasIt != typeAliases.end()) return resolveRefinementType(aliasIt->second, depth + 1);
        return nullptr;
    }

    void emitRefinementCheck(llvm::Value* paramVal, const TypeExpr* refType, const std::string& paramName, llvm::Function* llvmFn) {
        bool isFloat = paramVal->getType()->isFloatingPointTy();

        llvm::Value* violated = nullptr;
        std::string reason;
        if (refType->refinementKind == RefinementKind::Range) {
            llvm::Value* low = isFloat ? (llvm::Value*)llvm::ConstantFP::get(paramVal->getType(), refType->refLow)
                                        : (llvm::Value*)llvm::ConstantInt::get(paramVal->getType(), static_cast<int64_t>(refType->refLow), true);
            llvm::Value* high = isFloat ? (llvm::Value*)llvm::ConstantFP::get(paramVal->getType(), refType->refHigh)
                                         : (llvm::Value*)llvm::ConstantInt::get(paramVal->getType(), static_cast<int64_t>(refType->refHigh), true);
            llvm::Value* tooLow = isFloat ? builder.CreateFCmpOLT(paramVal, low) : builder.CreateICmpSLT(paramVal, low);
            llvm::Value* tooHigh = isFloat ? builder.CreateFCmpOGT(paramVal, high) : builder.CreateICmpSGT(paramVal, high);
            violated = builder.CreateOr(tooLow, tooHigh);
            reason = "refinement violated: '" + paramName + "' out of range [" + std::to_string(refType->refLow) + ".." + std::to_string(refType->refHigh) + "]";
        } else if (refType->refinementKind == RefinementKind::Compare) {
            llvm::Value* threshold = isFloat ? (llvm::Value*)llvm::ConstantFP::get(paramVal->getType(), refType->refCmpValue)
                                              : (llvm::Value*)llvm::ConstantInt::get(paramVal->getType(), static_cast<int64_t>(refType->refCmpValue), true);
            // Violation is the NEGATION of the required predicate - e.g.
            // `i32[!= 0]` requires val != 0, so it's violated exactly
            // when val == 0.
            switch (refType->refCmpOp) {
                case CompareOp::Eq:  violated = isFloat ? builder.CreateFCmpONE(paramVal, threshold) : builder.CreateICmpNE(paramVal, threshold); break;
                case CompareOp::Neq: violated = isFloat ? builder.CreateFCmpOEQ(paramVal, threshold) : builder.CreateICmpEQ(paramVal, threshold); break;
                case CompareOp::Lt:  violated = isFloat ? builder.CreateFCmpOGE(paramVal, threshold) : builder.CreateICmpSGE(paramVal, threshold); break;
                case CompareOp::Le:  violated = isFloat ? builder.CreateFCmpOGT(paramVal, threshold) : builder.CreateICmpSGT(paramVal, threshold); break;
                case CompareOp::Gt:  violated = isFloat ? builder.CreateFCmpOLE(paramVal, threshold) : builder.CreateICmpSLE(paramVal, threshold); break;
                case CompareOp::Ge:  violated = isFloat ? builder.CreateFCmpOLT(paramVal, threshold) : builder.CreateICmpSLT(paramVal, threshold); break;
            }
            reason = "refinement violated: '" + paramName + "' failed its value predicate";
        } else {
            return; // RefinementKind::None - nothing to check
        }

        llvm::BasicBlock* panicBB = llvm::BasicBlock::Create(context, "refinement_panic_" + paramName, llvmFn);
        llvm::BasicBlock* okBB = llvm::BasicBlock::Create(context, "refinement_ok_" + paramName, llvmFn);
        builder.CreateCondBr(violated, panicBB, okBB);

        builder.SetInsertPoint(panicBB);
        llvm::FunctionCallee printFn = module.getOrInsertFunction("frust_print_str",
            llvm::FunctionType::get(llvm::Type::getVoidTy(context), {llvm::PointerType::getUnqual(context)}, false));
        llvm::Constant* msgConst = llvm::ConstantDataArray::getString(context, reason);
        auto* msgGlobal = new llvm::GlobalVariable(module, msgConst->getType(), true, llvm::GlobalValue::PrivateLinkage, msgConst, ".refinement_msg");
        builder.CreateCall(printFn, {msgGlobal});
        // exit(1), not abort(): abort() under the Windows Debug CRT pops
        // an interactive "Debug Error" Abort/Retry/Ignore dialog instead
        // of terminating the process - confirmed by hand (the JIT-run
        // process hung indefinitely on a real violation until killed).
        // exit() terminates immediately with no dialog, on every
        // platform this targets.
        llvm::FunctionCallee exitFn = module.getOrInsertFunction("exit", llvm::FunctionType::get(llvm::Type::getVoidTy(context), {llvm::Type::getInt32Ty(context)}, false));
        builder.CreateCall(exitFn, {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 1)});
        builder.CreateUnreachable();

        builder.SetInsertPoint(okBB);
    }

    // Live AST metaprogramming (FRUST_LANG_SPEC.md 1.1: `quote`/`unquote`/
    // `build_time`) - had zero codegen before this. Rather than a separate
    // compile-time interpreter, this compiles `quote { ... }` into REAL
    // runtime code: when the surrounding function is actually CALLED, it
    // builds a genuine AST node tree on the spot (via the frust_ast_*
    // runtime functions in Main.cpp), using whatever real values are live
    // at that call - `unquote(expr)` compiles `expr` the ordinary way and
    // splices its real runtime value in as a literal node. The result is
    // an opaque ASTExpr handle a program can hand to frust_jit_eval_f32 to
    // compile-and-run it immediately, live, no rebuild. Deliberately
    // scoped to what the spec's own polynomial example needs - literals,
    // a free identifier (a parameter reference like `x`), binary/unary
    // ops, and unquote; anything else inside a quote block is a named
    // compile error, not a silent miscompile.
    llvm::Value* buildQuoteTree(const Expr* node) {
        if (!node) {
            std::cerr << "frust: codegen error: empty quote block\n";
            return nullptr;
        }

        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);

        switch (node->kind) {
            case ExprKind::Block: {
                if (node->statements.empty()) {
                    std::cerr << "frust: codegen error: quote { } has no template expression\n";
                    return nullptr;
                }
                return buildQuoteTree(node->statements.back());
            }

            case ExprKind::IntLiteral:
            case ExprKind::FloatLiteral: {
                double v = (node->kind == ExprKind::IntLiteral) ? static_cast<double>(node->intValue) : node->floatValue;
                llvm::FunctionCallee fn = module.getOrInsertFunction("frust_ast_lit_f64",
                    llvm::FunctionType::get(ptrTy, {llvm::Type::getDoubleTy(context)}, false));
                return builder.CreateCall(fn, {llvm::ConstantFP::get(llvm::Type::getDoubleTy(context), v)});
            }

            case ExprKind::Identifier: {
                llvm::FunctionCallee fn = module.getOrInsertFunction("frust_ast_param",
                    llvm::FunctionType::get(ptrTy, {ptrTy}, false));
                llvm::Constant* nameConst = llvm::ConstantDataArray::getString(context, node->text);
                auto* nameGlobal = new llvm::GlobalVariable(module, nameConst->getType(), true, llvm::GlobalValue::PrivateLinkage, nameConst, ".quote_param_name");
                return builder.CreateCall(fn, {nameGlobal});
            }

            case ExprKind::Binary: {
                llvm::Value* lhs = buildQuoteTree(node->lhs);
                llvm::Value* rhs = buildQuoteTree(node->rhs);
                if (!lhs || !rhs) return nullptr;
                llvm::FunctionCallee fn = module.getOrInsertFunction("frust_ast_binary",
                    llvm::FunctionType::get(ptrTy, {llvm::Type::getInt32Ty(context), ptrTy, ptrTy}, false));
                llvm::Value* opCode = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), static_cast<int32_t>(node->binaryOp));
                return builder.CreateCall(fn, {opCode, lhs, rhs});
            }

            case ExprKind::Unary: {
                llvm::Value* operand = buildQuoteTree(node->lhs);
                if (!operand) return nullptr;
                llvm::FunctionCallee fn = module.getOrInsertFunction("frust_ast_unary",
                    llvm::FunctionType::get(ptrTy, {llvm::Type::getInt32Ty(context), ptrTy}, false));
                llvm::Value* opCode = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), static_cast<int32_t>(node->unaryOp));
                return builder.CreateCall(fn, {opCode, operand});
            }

            case ExprKind::Unquote: {
                llvm::Value* liveVal = compileExpr(node->lhs);
                if (!liveVal) return nullptr;
                llvm::Value* asDouble = coerceToType(liveVal, llvm::Type::getDoubleTy(context));
                llvm::FunctionCallee fn = module.getOrInsertFunction("frust_ast_lit_f64",
                    llvm::FunctionType::get(ptrTy, {llvm::Type::getDoubleTy(context)}, false));
                return builder.CreateCall(fn, {asDouble});
            }

            default:
                std::cerr << "frust: codegen error: unsupported inside quote { ... } for now\n";
                return nullptr;
        }
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
            case ExprKind::SmartPtrNew: return compileHeapStructLiteral(*expr);
            case ExprKind::Closure: return compileClosureLiteral(*expr);
            case ExprKind::ArrayLiteral: return compileArrayLiteral(*expr);
            case ExprKind::Index: return compileIndex(*expr);
            case ExprKind::Member: return compileMember(*expr);
            case ExprKind::Assign: return compileAssign(*expr);

            case ExprKind::Let: {
                // `Vector::new()` construction (LANGUAGE_GAPS.md #3) -
                // recognized HERE, before the generic compileExpr(expr->lhs)
                // below, because `Vector::new()`'s call site has no way to
                // know the element type T on its own (Frust has no real
                // generic function syntax) - only this let's own type
                // annotation ever carries it. compileCall has no access to
                // that context, so this can't be handled there.
                if (expr->typeAnnotation && expr->typeAnnotation->name == "Vector"
                    && expr->typeAnnotation->genericArgs.size() == 1
                    && !expr->typeAnnotation->genericArgs[0].isIntConst
                    && expr->typeAnnotation->genericArgs[0].type
                    && expr->lhs->kind == ExprKind::Call
                    && expr->lhs->lhs->kind == ExprKind::Path
                    && expr->lhs->lhs->pathSegments.size() == 2
                    && expr->lhs->lhs->pathSegments[0] == "Vector"
                    && expr->lhs->lhs->pathSegments[1] == "new") {
                    llvm::Value* header = compileVectorNew();
                    if (!header) return nullptr;
                    namedValues[expr->text] = header;
                    namedValueVectorElementType[expr->text] = expr->typeAnnotation->genericArgs[0].type->name;
                    return header;
                }

                // Generic struct construction (LANGUAGE_GAPS.md #4) -
                // `let r: Result<i64, String> = Result { ... };` or
                // `let r: Result<i64, String> = own Result { ... };`.
                // Same reasoning as Vector::new() above: the literal's
                // own syntax (`Result { ... }`) never carries the
                // concrete type arguments - only this let's type
                // annotation does, so this has to be recognized here,
                // before the generic compileExpr(expr->lhs) below.
                if (expr->typeAnnotation && genericStructTemplates.count(expr->typeAnnotation->name)) {
                    const Expr* literalExpr = expr->lhs;
                    bool isHeap = (literalExpr->kind == ExprKind::SmartPtrNew);
                    if (isHeap) literalExpr = literalExpr->lhs;

                    if (literalExpr && literalExpr->kind == ExprKind::StructLiteral
                        && !literalExpr->pathSegments.empty()
                        && literalExpr->pathSegments.front() == expr->typeAnnotation->name) {
                        std::vector<std::string> concreteArgNames;
                        bool allTypeArgs = true;
                        for (auto& arg : expr->typeAnnotation->genericArgs) {
                            if (arg.isIntConst || !arg.type) { allTypeArgs = false; break; }
                            concreteArgNames.push_back(arg.type->name);
                        }
                        if (!allTypeArgs) {
                            std::cerr << "frust: codegen error: '" << expr->typeAnnotation->name
                                       << "' type arguments must be types, not integers\n";
                            return nullptr;
                        }
                        if (!getOrCreateMonomorphizedStruct(expr->typeAnnotation->name, concreteArgNames)) return nullptr;
                        std::string mangled = monomorphizedStructName(expr->typeAnnotation->name, concreteArgNames);

                        llvm::Value* instancePtr = isHeap
                            ? compileHeapStructLiteral(*expr->lhs, mangled)
                            : compileStructLiteral(*literalExpr, mangled);
                        if (!instancePtr) return nullptr;
                        namedValues[expr->text] = instancePtr;
                        namedValueStructType[expr->text] = mangled;
                        return instancePtr;
                    }
                }

                auto* val = compileExpr(expr->lhs);
                if (!val) return nullptr;

                // A closure literal is self-describing (its own params/
                // return type are right there in `|params| -> Ret { }`,
                // unlike Vector::new()'s untyped call site above) - so
                // construction itself needs no Let-anchoring, only the
                // resulting SIGNATURE needs recording here, under the
                // bound name, for compileCall's call-dispatch path
                // (namedValueClosureSignature) to use later instead of
                // compileIndirectCall's generic (and wrong, for a fat
                // pointer) plain-pointer-callee assumption.
                if (expr->lhs->kind == ExprKind::Closure) {
                    ClosureSignature sig;
                    for (auto& p : expr->lhs->params) sig.paramTypes.push_back(resolveType(p.type));
                    sig.returnType = expr->lhs->typeAnnotation ? resolveType(expr->lhs->typeAnnotation) : llvm::Type::getVoidTy(context);
                    namedValueClosureSignature[expr->text] = sig;
                }

                // Record the pointee type for a `raw* T`-typed let,
                // regardless of which storage branch below actually
                // runs (plain/mut/struct) - this is bookkeeping for
                // compileUnary's Deref case (LANGUAGE_GAPS.md #1), not
                // a change to how the value itself gets stored.
                if (expr->typeAnnotation && expr->typeAnnotation->isRawPointer) {
                    namedValueRawPointeeType[expr->text] = expr->typeAnnotation->name;
                }

                // `let x: SomeInterface = <concrete struct value>;` - wrap
                // the concrete pointer as a fat pointer for that interface,
                // using the vtable buildVtable already emitted for this
                // exact (interface, concrete type) pair. Checked before the
                // plain-struct path below since an interface-typed let's
                // initializer is still a concrete struct value at this
                // point (a SineAutomation, say) - it's the DECLARED type
                // that says "treat this polymorphically from here on."
                if (expr->typeAnnotation && interfaceDecls.count(expr->typeAnnotation->name)) {
                    auto concreteTypeName = inferStructTypeName(expr->lhs);
                    if (!concreteTypeName) {
                        std::cerr << "frust: codegen error: '" << expr->text << ": " << expr->typeAnnotation->name
                                   << "' needs a struct-valued initializer\n";
                        return nullptr;
                    }
                    llvm::Value* fat = wrapAsInterface(val, expr->typeAnnotation->name, *concreteTypeName);
                    if (!fat) return nullptr;
                    namedValues[expr->text] = fat;
                    namedValueInterfaceType[expr->text] = expr->typeAnnotation->name;
                    return fat;
                }

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
                // Was CreateRet(val) unconditionally - only the implicit
                // block-tail-value return path (end of compileFunction)
                // coerced to the function's declared return type. An
                // explicit `return expr` bypassed coercion entirely,
                // producing an LLVM verifier failure ("ret float ...
                // double") for anything needing float<->double or
                // int<->float promotion - e.g. spec's own
                // f32-refinement-typed parameter returned from an f64
                // function.
                llvm::Value* coerced = currentFnRetType ? coerceToType(val, currentFnRetType) : val;
                builder.CreateRet(coerced);
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

            case ExprKind::Quote: return buildQuoteTree(expr->lhs);

            // A pure marker in this design - build_time { ... } is just an
            // ordinary block that happens to (usually) end in a quote.
            // Everything before that (the `let a = coefficients[0]` style
            // setup) is completely normal live code, run every time this
            // function is actually called.
            case ExprKind::BuildTime: return compileExpr(expr->lhs);

            // Reached only when `unquote(...)` shows up somewhere
            // buildQuoteTree's own walk didn't already handle it - i.e.
            // used outside a quote { ... } block, which is meaningless
            // (there's no template being spliced into).
            case ExprKind::Unquote:
                std::cerr << "frust: codegen error: unquote() is only valid inside quote { ... }\n";
                return nullptr;

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

        // Pointer + integer arithmetic (`raw* T`) - LANGUAGE_GAPS.md #1.
        // Checked before anything else below: a raw pointer's LLVM type
        // is an opaque `ptr`, indistinguishable from String/a struct
        // pointer/any other pointer-shaped value in this file, so only
        // the STATIC AST-level type (inferRawPointeeTypeName) can say
        // "this operand is actually a raw pointer, do GEP-based element-
        // stride arithmetic, not integer add/sub." `ptr - n` is
        // supported (negate the offset, same GEP); `n - ptr` and
        // `ptr - ptr` (pointer difference) are not - a real, separate
        // feature (needs sizeof-based division), not attempted here.
        if (expr.binaryOp == BinaryOp::Add || expr.binaryOp == BinaryOp::Sub) {
            auto lhsPointee = inferRawPointeeTypeName(expr.lhs);
            if (lhsPointee) {
                llvm::Type* elemTy = resolveTypeByName(*lhsPointee);
                llvm::Value* offset = (expr.binaryOp == BinaryOp::Sub)
                    ? builder.CreateNeg(rhs, "negoffset") : rhs;
                return builder.CreateGEP(elemTy, lhs, offset, "ptraddtmp");
            }
            if (expr.binaryOp == BinaryOp::Add) {
                auto rhsPointee = inferRawPointeeTypeName(expr.rhs);
                if (rhsPointee) {
                    llvm::Type* elemTy = resolveTypeByName(*rhsPointee);
                    return builder.CreateGEP(elemTy, rhs, lhs, "ptraddtmp");
                }
            }
        }

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

    // Deref (`*expr`, for `raw* T`) - LANGUAGE_GAPS.md #1. Reads the
    // pointee type from namedValueRawPointeeType (inferRawPointeeTypeName)
    // rather than from `operand`'s own LLVM type, which - like every
    // other pointer-shaped value in this file - is opaque and carries no
    // pointee info.
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
            case UnaryOp::Deref: {
                auto pointeeTypeName = inferRawPointeeTypeName(expr.lhs);
                if (!pointeeTypeName) {
                    std::cerr << "frust: codegen error: cannot dereference - only a raw*-typed "
                                 "named variable or parameter can be dereferenced today\n";
                    return nullptr;
                }
                llvm::Type* pointeeTy = resolveTypeByName(*pointeeTypeName);
                return builder.CreateLoad(pointeeTy, operand, "derefload");
            }
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

        if (expr.lhs->kind == ExprKind::Unary && expr.lhs->unaryOp == UnaryOp::Deref) {
            // `*ptr = value` - LANGUAGE_GAPS.md #1's write side. Same
            // pointee-type lookup as compileUnary's read side
            // (inferRawPointeeTypeName), needed here too so the stored
            // value gets coerced to the right width/kind before the
            // store (e.g. an i32 literal into an `raw* i64` target).
            const Expr& derefTarget = *expr.lhs->lhs;
            auto pointeeTypeName = inferRawPointeeTypeName(&derefTarget);
            if (!pointeeTypeName) {
                std::cerr << "frust: codegen error: cannot assign through a dereference - only a "
                             "raw*-typed named variable or parameter can be dereferenced today\n";
                return nullptr;
            }
            auto* ptrVal = compileExpr(&derefTarget);
            if (!ptrVal) return nullptr;
            auto* val = compileExpr(expr.rhs);
            if (!val) return nullptr;

            llvm::Type* pointeeTy = resolveTypeByName(*pointeeTypeName);
            val = coerceToType(val, pointeeTy);
            builder.CreateStore(val, ptrVal);
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
        // `v[i]` for a Vector<T> (LANGUAGE_GAPS.md #3) - checked before
        // compiling `base` generically below, same reasoning as
        // compileMethodCall's Vector<T> branch: this only works for a
        // plain named variable (namedValueVectorElementType lookup),
        // same "anchor via a named binding" convention used throughout
        // this file for anything opaque-pointer-typed.
        if (expr.lhs->kind == ExprKind::Identifier) {
            auto vecElemIt = namedValueVectorElementType.find(expr.lhs->text);
            if (vecElemIt != namedValueVectorElementType.end()) {
                llvm::Value* headerPtr = compileExpr(expr.lhs);
                if (!headerPtr) return nullptr;
                llvm::Value* idx = compileExpr(expr.rhs);
                if (!idx) return nullptr;
                llvm::StructType* hdrTy = vectorHeaderType();
                llvm::Type* elemTy = resolveTypeByName(vecElemIt->second);
                llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
                llvm::Value* dataFieldPtr = builder.CreateStructGEP(hdrTy, headerPtr, 0);
                llvm::Value* dataPtr = builder.CreateLoad(ptrTy, dataFieldPtr, "vecdata");
                llvm::Value* elemPtr = builder.CreateGEP(elemTy, dataPtr, idx, "vecelemptr");
                return builder.CreateLoad(elemTy, elemPtr, "vecindex");
            }
        }

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

    // Shared by compileStructLiteral (stack) and compileHeapStructLiteral
    // (heap) - both just need "given a base pointer already sized for
    // structTy, store each initializer expression into its field slot."
    // Returns false (message already printed) on a real error.
    bool initStructFields(const Expr& expr, const std::string& typeName, llvm::StructType* structTy,
                           std::unordered_map<std::string, int>& fieldIndex, llvm::Value* basePtr) {
        for (auto& init : expr.fields) {
            auto fieldIt = fieldIndex.find(init.name);
            if (fieldIt == fieldIndex.end()) {
                std::cerr << "frust: codegen error: struct '" << typeName << "' has no field '" << init.name << "'\n";
                return false;
            }
            auto* val = compileExpr(init.value);
            if (!val) return false;
            val = coerceToType(val, structTy->getElementType(fieldIt->second));
            llvm::Value* fieldPtr = builder.CreateStructGEP(structTy, basePtr, fieldIt->second);
            builder.CreateStore(val, fieldPtr);
        }
        // Fields not present in the literal are left uninitialized (no
        // default-value story exists yet) - fine for v1, matches "no
        // validation for scenarios not yet required."
        return true;
    }

    // typeNameOverride: when non-empty, used instead of the literal's
    // own bare path name - the generic-struct-construction path in the
    // Let branch of compileExpr passes the already-monomorphized
    // mangled name here (e.g. "Box<i64>"), since the literal's own
    // syntax (`Box { value: 42 }`) never carries the concrete type
    // arguments itself - LANGUAGE_GAPS.md #4.
    llvm::Value* compileStructLiteral(const Expr& expr, const std::string& typeNameOverride = "") {
        if (typeNameOverride.empty() && expr.pathSegments.empty()) {
            std::cerr << "frust: codegen error: struct literal missing a type name\n";
            return nullptr;
        }
        std::string typeName = typeNameOverride.empty() ? expr.pathSegments.front() : typeNameOverride;
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
        // inside nested blocks the literal happens to appear. This
        // pointer does NOT survive the enclosing function returning - see
        // compileHeapStructLiteral for the `own`/`raw` alternative that
        // does.
        llvm::Function* theFunction = builder.GetInsertBlock()->getParent();
        llvm::IRBuilder<> tmpBuilder(&theFunction->getEntryBlock(), theFunction->getEntryBlock().begin());
        llvm::AllocaInst* alloca = tmpBuilder.CreateAlloca(structTy, nullptr, typeName);

        if (!initStructFields(expr, typeName, structTy, fieldIndex, alloca)) return nullptr;
        return alloca;
    }

    // `own StructName { ... }` / `raw StructName { ... }` (ExprKind::
    // SmartPtrNew wrapping a StructLiteral) - was entirely unimplemented
    // (fell to compileExpr's generic "unsupported expression kind"
    // error). The gap this closes: a plain struct literal always
    // stack-allocas in the CONSTRUCTING function's own entry block, so a
    // function meant to build-and-return an instance (a constructor) was
    // handing back a dangling pointer the moment it returned - real,
    // silent undefined behavior, not just a missing-feature error,
    // because nothing caught it. `own`/`raw` heap-allocate via malloc
    // instead, so the returned pointer is genuinely valid for as long as
    // the caller wants to keep using it.
    //
    // `own` vs `raw` is purely a documented ownership-discipline
    // DISTINCTION for now, not an enforced one - Frust has no semantic-
    // analysis pass to check "exactly one owner"/"never freed twice"
    // (same pre-existing gap noted throughout this file for struct
    // mutation not being gated on `mut`), so both currently just malloc
    // and return the pointer. `shared`/`weak` genuinely need reference
    // counting (a header word, retain/release) - real, separate,
    // deliberately not built here; rejected with a clear error instead
    // of silently behaving like `own`.
    // typeNameOverride: see compileStructLiteral's own doc - same
    // reasoning, for `own Box { ... }`/`raw Box { ... }`.
    llvm::Value* compileHeapStructLiteral(const Expr& smartPtrExpr, const std::string& typeNameOverride = "") {
        if (smartPtrExpr.smartPtrKind == SmartPtrKind::Shared || smartPtrExpr.smartPtrKind == SmartPtrKind::Weak) {
            std::cerr << "frust: codegen error: 'shared'/'weak' construction isn't implemented yet (needs real reference counting) - only 'own'/'raw' heap construction is\n";
            return nullptr;
        }
        const Expr* lit = smartPtrExpr.lhs;
        if (!lit || lit->kind != ExprKind::StructLiteral) {
            std::cerr << "frust: codegen error: 'own'/'raw' currently only wraps a struct literal, e.g. `own Foo { ... }`\n";
            return nullptr;
        }
        if (typeNameOverride.empty() && lit->pathSegments.empty()) {
            std::cerr << "frust: codegen error: struct literal missing a type name\n";
            return nullptr;
        }
        std::string typeName = typeNameOverride.empty() ? lit->pathSegments.front() : typeNameOverride;
        auto typeIt = structTypes.find(typeName);
        if (typeIt == structTypes.end()) {
            std::cerr << "frust: codegen error: unknown struct type '" << typeName << "'\n";
            return nullptr;
        }
        llvm::StructType* structTy = typeIt->second;
        auto& fieldIndex = structFieldIndex[typeName];

        uint64_t sizeBytes = module.getDataLayout().getTypeAllocSize(structTy);
        llvm::FunctionCallee mallocFn = module.getOrInsertFunction("malloc",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(context), {llvm::Type::getInt64Ty(context)}, false));
        llvm::Value* heapPtr = builder.CreateCall(mallocFn, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), sizeBytes)});

        if (!initStructFields(*lit, typeName, structTy, fieldIndex, heapPtr)) return nullptr;
        return heapPtr;
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

        // Interface-typed receiver (a fat pointer, from a `let x:
        // SomeInterface = ...` binding) - genuine dynamic dispatch through
        // the vtable buildVtable emitted, not a static name lookup. Checked
        // first: an interface-typed identifier is never also in
        // namedValueStructType, so this and the static path below are
        // mutually exclusive, not competing.
        if (member.lhs->kind == ExprKind::Identifier) {
            auto ifaceNameIt = namedValueInterfaceType.find(member.lhs->text);
            if (ifaceNameIt != namedValueInterfaceType.end()) {
                return compileInterfaceMethodCall(expr, member, ifaceNameIt->second);
            }
            // Vector<T> (LANGUAGE_GAPS.md #3) - a compiler built-in, not
            // a real user-defined method, checked here for the same
            // reason the interface check above runs first: mutually
            // exclusive membership, not competing priority.
            auto vecElemIt = namedValueVectorElementType.find(member.lhs->text);
            if (vecElemIt != namedValueVectorElementType.end()) {
                return compileVectorMethodCall(expr, member, vecElemIt->second);
            }
        }

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

        auto methodDeclIt = methods.find(mangled);
        const FunctionDecl* methodDecl = (methodDeclIt != methods.end()) ? methodDeclIt->second : nullptr;

        std::vector<llvm::Value*> args;
        args.push_back(selfPtr);
        auto argTypeIt = callee->arg_begin();
        ++argTypeIt; // skip self's type when coercing declared args
        for (size_t i = 0; i < expr.args.size(); ++i) {
            auto* v = compileExpr(expr.args[i]);
            if (!v) return nullptr;
            // methodDecl->params has no self entry either (self is
            // synthetic, added separately - see declareFunctionSignature),
            // so index i lines up directly here too.
            if (methodDecl && i < methodDecl->params.size()) {
                v = coerceArgForParam(v, expr.args[i], methodDecl->params[i].type);
            } else {
                v = coerceToType(v, argTypeIt->getType());
            }
            if (!v) return nullptr;
            args.push_back(v);
            ++argTypeIt;
        }
        return builder.CreateCall(callee, args, callee->getReturnType()->isVoidTy() ? "" : "calltmp");
    }

    // Genuine dynamic dispatch: `receiver.method(args)` where receiver is a
    // fat pointer { data, vtable } for `interfaceName`. Extracts both
    // words, loads the method's function pointer out of the vtable at its
    // declared index, and calls through it - a real LLVM indirect call
    // (the callee address is a runtime value, not a compile-time-known
    // name), same mechanism compileIndirectCall already uses elsewhere in
    // this file, just with a real, checked FunctionType built from the
    // interface's own declared signature instead of a guessed one.
    llvm::Value* compileInterfaceMethodCall(const Expr& expr, const Expr& member, const std::string& interfaceName) {
        auto ifaceIt = interfaceDecls.find(interfaceName);
        if (ifaceIt == interfaceDecls.end()) {
            std::cerr << "frust: codegen internal error: unknown interface '" << interfaceName << "'\n";
            return nullptr;
        }
        InterfaceDecl* iface = ifaceIt->second;

        int methodIndex = -1;
        const InterfaceMethodSig* sig = nullptr;
        for (size_t i = 0; i < iface->methods.size(); ++i) {
            if (iface->methods[i].name == member.text) { methodIndex = static_cast<int>(i); sig = &iface->methods[i]; break; }
        }
        if (!sig) {
            std::cerr << "frust: codegen error: interface '" << interfaceName << "' has no method '" << member.text << "'\n";
            return nullptr;
        }
        if (sig->params.size() != expr.args.size()) {
            std::cerr << "frust: codegen error: '" << member.text << "' expects " << sig->params.size()
                       << " argument(s), got " << expr.args.size() << "\n";
            return nullptr;
        }

        llvm::Value* fatVal = compileExpr(member.lhs);
        if (!fatVal) return nullptr;
        llvm::Value* dataPtr = builder.CreateExtractValue(fatVal, {0});
        llvm::Value* vtablePtr = builder.CreateExtractValue(fatVal, {1});

        llvm::Type* ptrTy = llvm::PointerType::getUnqual(context);
        llvm::Value* slotPtr = builder.CreateConstInBoundsGEP1_64(ptrTy, vtablePtr, methodIndex);
        llvm::Value* fnPtr = builder.CreateLoad(ptrTy, slotPtr);

        llvm::Type* retTy = sig->returnType ? resolveType(sig->returnType) : llvm::Type::getVoidTy(context);
        std::vector<llvm::Type*> paramTys = {ptrTy}; // self
        for (auto& p : sig->params) paramTys.push_back(resolveType(p.type));
        llvm::FunctionType* fnTy = llvm::FunctionType::get(retTy, paramTys, false);

        std::vector<llvm::Value*> args = {dataPtr};
        for (size_t i = 0; i < expr.args.size(); ++i) {
            llvm::Value* v = compileExpr(expr.args[i]);
            if (!v) return nullptr;
            v = coerceArgForParam(v, expr.args[i], sig->params[i].type);
            if (!v) return nullptr;
            args.push_back(v);
        }
        return builder.CreateCall(fnTy, fnPtr, args, retTy->isVoidTy() ? "" : "ifacecalltmp");
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

        // A closure-typed name (LANGUAGE_GAPS.md #6) - checked before
        // module.getFunction()/compileIndirectCall's generic dispatch
        // below, since a closure value is a fat pointer { ptr code, ptr
        // env }, not the plain single `ptr` callee compileIndirectCall
        // assumes. Uses the real signature recorded at the closure's
        // `let`-binding site (namedValueClosureSignature) instead of
        // compileIndirectCall's hardcoded-i64-return convention.
        if (expr.lhs->kind == ExprKind::Identifier) {
            auto sigIt = namedValueClosureSignature.find(expr.lhs->text);
            if (sigIt != namedValueClosureSignature.end()) {
                return compileClosureCall(expr, expr.lhs->text, sigIt->second);
            }
        }

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

        auto declIt = functionDeclsByName.find(targetName);
        const FunctionDecl* targetDecl = (declIt != functionDeclsByName.end()) ? declIt->second : nullptr;

        std::vector<llvm::Value*> args;
        auto argTypeIt = callee->arg_begin();
        for (size_t i = 0; i < expr.args.size(); ++i) {
            auto* v = compileExpr(expr.args[i]);
            if (!v) return nullptr;
            // targetDecl->params has no synthetic self entry for a free
            // function, so index i lines up directly - unlike
            // compileMethodCall's args[0]=self offset.
            if (targetDecl && i < targetDecl->params.size()) {
                v = coerceArgForParam(v, expr.args[i], targetDecl->params[i].type);
            } else {
                v = coerceToType(v, argTypeIt->getType());
            }
            if (!v) return nullptr;
            args.push_back(v);
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
    // Calling a closure-typed value: unpack the fat pointer built by
    // compileClosureLiteral ({ ptr code, ptr env }) and call code(env,
    // args...) using the REAL param/return types recorded at the
    // closure's `let`-binding site - not compileIndirectCall's generic
    // plain-pointer-callee, hardcoded-i64-return assumption, which would
    // misinterpret a 2-word fat-pointer aggregate as a bare callee
    // address entirely.
    llvm::Value* compileClosureCall(const Expr& expr, const std::string& name, const ClosureSignature& sig) {
        llvm::Value* fat = compileExpr(expr.lhs);
        if (!fat) return nullptr;
        if (expr.args.size() != sig.paramTypes.size()) {
            std::cerr << "frust: codegen error: closure '" << name << "' expects " << sig.paramTypes.size()
                       << " argument(s), got " << expr.args.size() << "\n";
            return nullptr;
        }

        llvm::Value* codePtr = builder.CreateExtractValue(fat, {0}, "closure.code");
        llvm::Value* envPtr = builder.CreateExtractValue(fat, {1}, "closure.env");

        std::vector<llvm::Value*> args;
        args.push_back(envPtr);
        std::vector<llvm::Type*> trampolineParamTypes;
        trampolineParamTypes.push_back(llvm::PointerType::getUnqual(context));
        for (size_t i = 0; i < expr.args.size(); ++i) {
            llvm::Value* v = compileExpr(expr.args[i]);
            if (!v) return nullptr;
            v = coerceToType(v, sig.paramTypes[i]);
            if (!v) return nullptr;
            args.push_back(v);
            trampolineParamTypes.push_back(sig.paramTypes[i]);
        }

        llvm::FunctionType* fnTy = llvm::FunctionType::get(sig.returnType, trampolineParamTypes, false);
        return builder.CreateCall(fnTy, codePtr, args, sig.returnType->isVoidTy() ? "" : "closurecalltmp");
    }

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

    // A tiny raw-heap-buffer helper, mirroring buffer.fr's own runtime-
    // helper pattern (8-byte slots, store/load whatever type actually
    // belongs there - opaque pointers carry no type info, so this needs
    // no bitcast) but implemented directly in codegen since this is
    // compiler-internal plumbing, not something Frust source calls.
    llvm::Value* mallocRaw(uint64_t sizeBytes) {
        llvm::FunctionCallee mallocFn = module.getOrInsertFunction("malloc",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(context), {llvm::Type::getInt64Ty(context)}, false));
        return builder.CreateCall(mallocFn, {builder.getInt64(sizeBytes)});
    }
    llvm::Value* slotAddr(llvm::Value* basePtr, int index) {
        return builder.CreateConstInBoundsGEP1_64(llvm::Type::getInt8Ty(context), basePtr, index * 8);
    }

    llvm::Value* compilePerform(const Expr& expr) {
        if (!currentCoroHandle) {
            std::cerr << "frust: perform used outside of a coroutine function\n";
            return nullptr;
        }

        // Real declared param types come from the matching `effect`
        // declaration - without this, every performed argument was
        // silently forced through a single f64 slot (a String argument,
        // e.g. the spec's own `effect Log(msg: String)`, would corrupt
        // the pointer through a double reinterpretation), and only
        // args[0] was ever sent at all - extra arguments were silently
        // dropped, no error.
        auto declIt = effectDecls.find(expr.text);
        if (declIt == effectDecls.end()) {
            std::cerr << "frust: codegen error: unknown effect '" << expr.text << "' - no matching 'effect' declaration\n";
            return nullptr;
        }
        EffectDecl* decl = declIt->second;
        if (expr.args.size() != decl->params.size()) {
            std::cerr << "frust: codegen error: effect '" << expr.text << "' expects " << decl->params.size()
                       << " argument(s), got " << expr.args.size() << "\n";
            return nullptr;
        }

        int effectId = std::hash<std::string>{}(expr.text) & 0x7FFFFFFF;
        llvm::Value* effectIdVal = builder.getInt32(effectId);

        llvm::Value* argBufPtr;
        if (expr.args.empty()) {
            argBufPtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context));
        } else {
            argBufPtr = mallocRaw(8 * expr.args.size());
            for (size_t i = 0; i < expr.args.size(); ++i) {
                llvm::Value* argVal = compileExpr(expr.args[i]);
                if (!argVal) return nullptr;
                llvm::Type* declaredTy = resolveType(decl->params[i].type);
                argVal = coerceToType(argVal, declaredTy);
                builder.CreateStore(argVal, slotAddr(argBufPtr, static_cast<int>(i)));
            }
        }

        llvm::Value* idPtr = builder.CreateStructGEP(getPromiseType(), currentPromiseAlloc, 0);
        builder.CreateStore(effectIdVal, idPtr);

        llvm::Value* argPtr = builder.CreateStructGEP(getPromiseType(), currentPromiseAlloc, 1);
        builder.CreateStore(argBufPtr, argPtr);

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

            // Bind EVERY handler param (not just params[0] - the original
            // code silently dropped any effect argument past the first),
            // each to its own real declared type from the matching
            // `effect` decl, unpacked from compilePerform's arg buffer.
            // Also actually restores whatever the name was bound to
            // before, or removes the binding entirely if it wasn't bound
            // at all - the original code saved oldParam but never
            // restored it, so a handler param that shadowed an outer
            // local permanently corrupted that local for the rest of the
            // function.
            auto declIt = effectDecls.find(hc.effectName);
            EffectDecl* decl = (declIt != effectDecls.end()) ? declIt->second : nullptr;

            std::vector<std::pair<std::string, llvm::Value*>> savedParams;
            llvm::Value* argBufPtrLoaded = nullptr;
            if (!hc.params.empty() && decl && !decl->params.empty()) {
                llvm::Value* argPtrSlot = builder.CreateStructGEP(getPromiseType(), promisePtr, 1);
                argBufPtrLoaded = builder.CreateLoad(llvm::PointerType::getUnqual(context), argPtrSlot);
            }
            for (size_t i = 0; i < hc.params.size(); ++i) {
                const std::string& pName = hc.params[i].name;
                savedParams.push_back({ pName, namedValues.count(pName) ? namedValues[pName] : nullptr });

                if (decl && i < decl->params.size() && argBufPtrLoaded) {
                    llvm::Type* declaredTy = resolveType(decl->params[i].type);
                    llvm::Value* argVal = builder.CreateLoad(declaredTy, slotAddr(argBufPtrLoaded, static_cast<int>(i)));
                    namedValues[pName] = argVal;
                }
            }

            llvm::Value* oldHandleForResume = currentActiveHandleForResume;
            llvm::Value* oldPromiseForResume = currentActivePromiseForResume;
            currentActiveHandleForResume = coroHandle;
            currentActivePromiseForResume = promisePtr;

            compileExpr(hc.body);

            currentActiveHandleForResume = oldHandleForResume;
            currentActivePromiseForResume = oldPromiseForResume;

            for (auto& saved : savedParams) {
                if (saved.second) namedValues[saved.first] = saved.second;
                else namedValues.erase(saved.first);
            }

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

    // Pass-3 step: for one `impl InterfaceName for TypeName { ... }` block
    // (its methods already compiled to real llvm::Functions by Pass 2),
    // emit a constant array of function pointers, one slot per interface
    // method in DECLARATION order, each pointing at this type's compiled
    // implementation. This is the actual vtable a fat-pointer interface
    // value's second word points at.
    bool buildVtable(const ImplDecl& impl) {
        auto ifaceIt = interfaceDecls.find(impl.interfaceName);
        if (ifaceIt == interfaceDecls.end()) {
            std::cerr << "frust: codegen error: '" << impl.typeName << "' implements unknown interface '" << impl.interfaceName << "'\n";
            return false;
        }
        InterfaceDecl* iface = ifaceIt->second;

        std::vector<llvm::Constant*> slots;
        slots.reserve(iface->methods.size());
        for (auto& sig : iface->methods) {
            std::string mangled = mangleMethodName(impl.typeName, sig.name);
            llvm::Function* fn = module.getFunction(mangled);
            if (!fn) {
                std::cerr << "frust: codegen error: '" << impl.typeName << "' does not implement '"
                           << impl.interfaceName << "::" << sig.name << "' (required by the interface)\n";
                return false;
            }
            slots.push_back(fn);
        }

        llvm::ArrayType* vtableTy = llvm::ArrayType::get(llvm::PointerType::getUnqual(context), slots.size());
        llvm::Constant* init = llvm::ConstantArray::get(vtableTy, slots);
        std::string vtableName = "." + impl.interfaceName + "$" + impl.typeName + ".vtable";
        auto* global = new llvm::GlobalVariable(module, vtableTy, true, llvm::GlobalValue::PrivateLinkage, init, vtableName);
        vtables[mangleMethodName(impl.interfaceName, impl.typeName)] = global;
        return true;
    }

    // Wraps a concrete struct pointer (already known to be `concreteTypeName`)
    // as a fat pointer satisfying `interfaceName`, using the vtable
    // buildVtable already built for that exact pair. Returns nullptr (with
    // a message already printed) if no such `impl Interface for Type` block
    // exists - "used a struct where an interface was expected" without ever
    // implementing it is a real error, not a silent gap.
    llvm::Value* wrapAsInterface(llvm::Value* concretePtr, const std::string& interfaceName, const std::string& concreteTypeName) {
        auto it = vtables.find(mangleMethodName(interfaceName, concreteTypeName));
        if (it == vtables.end()) {
            std::cerr << "frust: codegen error: '" << concreteTypeName << "' does not implement interface '" << interfaceName << "'\n";
            return nullptr;
        }
        llvm::Value* fat = llvm::UndefValue::get(fatPointerType());
        fat = builder.CreateInsertValue(fat, concretePtr, {0});
        fat = builder.CreateInsertValue(fat, it->second, {1});
        return fat;
    }

    // Recursively collects free-variable references in a closure body -
    // an Identifier that names something already bound in the ENCLOSING
    // scope (namedValues, as of the closure literal's own construction
    // site) but isn't one of the closure's own parameters. Same
    // recursive-walk shape as hasPerform (lhs/rhs/condExpr/elseExpr/
    // statements/args/handleCases) but has to be a member (not a free
    // function like hasPerform) since it needs namedValues/
    // module.getFunction() to tell "capture the outer variable" apart
    // from "a known top-level function name" and "purely local to the
    // closure body" - a name only ever bound INSIDE the closure (e.g. a
    // nested `let`) is never in the OUTER namedValues at this point, so
    // it's excluded for free, no separate "bound names grows as we
    // descend" tracking needed. Named limitation: no shadowing
    // awareness - a closure-body `let` that reuses an outer variable's
    // name is misidentified as referencing the outer capture. Avoid
    // reusing a captured name for a closure-local, same as every other
    // real, bounded v1 cut in this file.
    void collectFreeVariables(const Expr* node, const std::set<std::string>& boundNames,
                               std::set<std::string>& seen, std::vector<std::string>& order) {
        if (!node) return;
        if (node->kind == ExprKind::Identifier) {
            const std::string& name = node->text;
            if (!boundNames.count(name) && namedValues.count(name) && !seen.count(name) && !module.getFunction(name)) {
                seen.insert(name);
                order.push_back(name);
            }
            return;
        }
        collectFreeVariables(node->lhs, boundNames, seen, order);
        collectFreeVariables(node->rhs, boundNames, seen, order);
        collectFreeVariables(node->condExpr, boundNames, seen, order);
        collectFreeVariables(node->elseExpr, boundNames, seen, order);
        for (auto* s : node->statements) collectFreeVariables(s, boundNames, seen, order);
        for (auto* a : node->args) collectFreeVariables(a, boundNames, seen, order);
        for (auto& hc : node->handleCases) collectFreeVariables(hc.body, boundNames, seen, order);
    }

    // `|params| -> RetType { body }` (LANGUAGE_GAPS.md #6). Represented
    // as the exact same fat pointer wrapAsInterface builds for interface
    // dispatch - { ptr code, ptr env } - reused deliberately, not a
    // second mechanism. Capture is BY VALUE ONLY, copied into a real,
    // heap-allocated (malloc'd) env struct at the closure literal's own
    // construction site - a real, named v1 limitation: by-reference
    // capture would need lifetime/escape analysis this project doesn't
    // have. `perform` inside a closure body is rejected outright - no
    // coroutine-trampoline machinery exists for a closure's own
    // generated function.
    llvm::Value* compileClosureLiteral(const Expr& expr) {
        if (hasPerform(expr.lhs)) {
            std::cerr << "frust: codegen error: 'perform' inside a closure body isn't supported yet\n";
            return nullptr;
        }

        std::set<std::string> paramNames;
        for (auto& p : expr.params) paramNames.insert(p.name);
        std::set<std::string> seen;
        std::vector<std::string> captured;
        collectFreeVariables(expr.lhs, paramNames, seen, captured);

        std::vector<llvm::Type*> capturedTypes;
        for (auto& name : captured) capturedTypes.push_back(namedValues[name]->getType());
        llvm::StructType* envTy = llvm::StructType::get(context, capturedTypes);

        uint64_t envSize = module.getDataLayout().getTypeAllocSize(envTy);
        llvm::Value* envPtr = builder.CreateCall(getMallocFn(),
            {llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), envSize)});
        for (size_t i = 0; i < captured.size(); ++i) {
            llvm::Value* fieldPtr = builder.CreateStructGEP(envTy, envPtr, (unsigned)i);
            builder.CreateStore(namedValues[captured[i]], fieldPtr);
        }

        std::vector<llvm::Type*> paramTypes;
        for (auto& p : expr.params) paramTypes.push_back(resolveType(p.type));
        llvm::Type* retType = expr.typeAnnotation ? resolveType(expr.typeAnnotation) : llvm::Type::getVoidTy(context);

        std::vector<llvm::Type*> trampolineParamTypes;
        trampolineParamTypes.push_back(llvm::PointerType::getUnqual(context)); // env
        for (auto* t : paramTypes) trampolineParamTypes.push_back(t);
        llvm::FunctionType* trampolineTy = llvm::FunctionType::get(retType, trampolineParamTypes, false);
        std::string trampolineName = "closure$" + std::to_string(closureCounter++);
        llvm::Function* trampolineFn = llvm::Function::Create(trampolineTy, llvm::Function::InternalLinkage, trampolineName, module);

        // Save the enclosing (currently-being-compiled) function's whole
        // scope - compiling the trampoline body repoints the builder and
        // every namedValue* map at a fresh function entirely, and none
        // of that may leak back once this closure literal is done.
        auto savedIP = builder.saveIP();
        auto savedNamedValues = namedValues;
        auto savedStructType = namedValueStructType;
        auto savedRawPointee = namedValueRawPointeeType;
        auto savedVectorElem = namedValueVectorElementType;
        auto savedInterfaceType = namedValueInterfaceType;
        auto savedClosureSig = namedValueClosureSignature;
        llvm::Type* savedRetType = currentFnRetType;
        bool savedBlockTerminated = blockTerminated;

        auto restoreState = [&]() {
            builder.restoreIP(savedIP);
            namedValues = savedNamedValues;
            namedValueStructType = savedStructType;
            namedValueRawPointeeType = savedRawPointee;
            namedValueVectorElementType = savedVectorElem;
            namedValueInterfaceType = savedInterfaceType;
            namedValueClosureSignature = savedClosureSig;
            currentFnRetType = savedRetType;
            blockTerminated = savedBlockTerminated;
        };

        namedValues.clear();
        namedValueStructType.clear();
        namedValueRawPointeeType.clear();
        namedValueVectorElementType.clear();
        namedValueInterfaceType.clear();
        namedValueClosureSignature.clear();

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", trampolineFn);
        builder.SetInsertPoint(bb);
        blockTerminated = false;
        currentFnRetType = retType;

        auto argIt = trampolineFn->args().begin();
        llvm::Value* envArg = &*argIt;
        envArg->setName("env");
        ++argIt;

        for (size_t i = 0; i < captured.size(); ++i) {
            const std::string& name = captured[i];
            llvm::Value* fieldPtr = builder.CreateStructGEP(envTy, envArg, (unsigned)i);
            namedValues[name] = builder.CreateLoad(capturedTypes[i], fieldPtr, name);
            if (savedStructType.count(name)) namedValueStructType[name] = savedStructType[name];
            if (savedRawPointee.count(name)) namedValueRawPointeeType[name] = savedRawPointee[name];
            if (savedVectorElem.count(name)) namedValueVectorElementType[name] = savedVectorElem[name];
            if (savedInterfaceType.count(name)) namedValueInterfaceType[name] = savedInterfaceType[name];
            if (savedClosureSig.count(name)) namedValueClosureSignature[name] = savedClosureSig[name];
        }

        for (auto& p : expr.params) {
            argIt->setName(p.name);
            namedValues[p.name] = &*argIt;
            if (auto structName = resolveStructTypeName(p.type)) namedValueStructType[p.name] = *structName;
            if (auto ifaceName = resolveInterfaceName(p.type)) namedValueInterfaceType[p.name] = *ifaceName;
            if (p.type && p.type->isRawPointer) namedValueRawPointeeType[p.name] = p.type->name;
            ++argIt;
        }

        llvm::Value* result = compileExpr(expr.lhs);
        if (!blockTerminated) {
            if (retType->isVoidTy()) {
                builder.CreateRetVoid();
            } else if (result) {
                builder.CreateRet(coerceToType(result, retType));
            } else {
                std::cerr << "frust: codegen error: closure body has no return value\n";
                restoreState();
                trampolineFn->eraseFromParent();
                return nullptr;
            }
        }

        std::string errMsg;
        llvm::raw_string_ostream os(errMsg);
        bool verifyFailed = llvm::verifyFunction(*trampolineFn, &os);

        restoreState();

        if (verifyFailed) {
            std::cerr << "frust: LLVM verification failed for closure: " << os.str() << "\n";
            trampolineFn->eraseFromParent();
            return nullptr;
        }

        llvm::Value* fat = llvm::UndefValue::get(fatPointerType());
        fat = builder.CreateInsertValue(fat, trampolineFn, {0});
        fat = builder.CreateInsertValue(fat, envPtr, {1});
        return fat;
    }

    // Call-site argument coercion, interface-aware. Was: every call site
    // just did coerceToType(v, argTypeIt->getType()), which only ever sees
    // the resolved llvm::Type* - fine for numeric widening, but every
    // interface resolves to the exact same generic { ptr, ptr } shape, so
    // there's no way to recover "which interface" from the LLVM type
    // alone. This looks at the parameter's real declared TypeExpr instead:
    // if it names an interface and the argument is still a plain concrete
    // value (not already wrapped), wrap it via wrapAsInterface using the
    // argument EXPRESSION's own inferred struct type - the same
    // AST-level lookup namedValueStructType/inferStructTypeName already
    // provide for the `let x: Interface = ...` case, now reused at call
    // sites too. Anything else falls through to the existing numeric/
    // pointer coercion unchanged.
    llvm::Value* coerceArgForParam(llvm::Value* argVal, const Expr* argExpr, const TypeExpr* paramType) {
        if (auto ifaceName = resolveInterfaceName(paramType)) {
            if (argVal->getType() == fatPointerType()) return argVal; // already a fat pointer - trust it as-is
            auto concreteTypeName = inferStructTypeName(argExpr);
            if (!concreteTypeName) {
                std::cerr << "frust: codegen error: argument for interface-typed parameter '" << *ifaceName
                           << "' needs a struct-valued expression\n";
                return nullptr;
            }
            return wrapAsInterface(argVal, *ifaceName, *concreteTypeName);
        }
        return coerceToType(argVal, resolveType(paramType));
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
        functionDeclsByName[llvmName] = &fn;
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

        llvm::Type* prevFnRetType = currentFnRetType;
        currentFnRetType = declaredRetType;

        namedValues.clear();
        namedValueStructType.clear();
        namedValueRawPointeeType.clear();
        namedValueVectorElementType.clear();
        namedValueClosureSignature.clear();

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
            // Interface-typed parameter (a fat pointer at the ABI level,
            // per resolveType) - record it the same way namedValueStructType
            // does for concrete structs, so a method call on this parameter
            // inside the function body (compileMethodCall) dispatches
            // through the vtable instead of failing "unknown struct type".
            if (auto ifaceName = resolveInterfaceName(p.type)) {
                namedValueInterfaceType[p.name] = *ifaceName;
            }
            if (p.type && p.type->isRawPointer) {
                namedValueRawPointeeType[p.name] = p.type->name;
            }
            ++argIt;
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", llvmFn);
        builder.SetInsertPoint(bb);
        blockTerminated = false;

        for (auto& p : fn.params) {
            if (const TypeExpr* refType = resolveRefinementType(p.type)) {
                emitRefinementCheck(namedValues[p.name], refType, p.name, llvmFn);
            }
        }

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
        currentFnRetType = prevFnRetType;

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

        llvm::Type* prevFnRetType = currentFnRetType;
        currentFnRetType = retType;

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", llvmFn);
        builder.SetInsertPoint(bb);
        blockTerminated = false;

        llvm::Value* result = compileExpr(body);
        currentFnRetType = prevFnRetType;
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
