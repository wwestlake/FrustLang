#pragma once

// The embeddable compiler: FRust source text in, diagnostics and an object
// file out, all in memory.
//
// Nothing here opens, creates or deletes a file. A host application (the
// Djehuti Station Script panel, the agent runtime, the language server) hands
// over source as strings and gets back structured diagnostics and the object
// code as bytes. Anything else the compiler would read from disk - the
// sibling files a `use self::name;` names, a pod's sources - comes from
// providers the host supplies, so the host decides where those live (in
// Station's case, inside the VFS container).
//
// The command-line compiler (frust_compiler --emit-obj) is a thin wrapper over
// this: it reads its input files into SourceFile values and writes the object
// bytes to the output path. It is the only piece that touches the disk.
//
// Public types are plain std types; no LLVM, AST or bison type appears here,
// so a consumer only needs this header and to link frust_lang.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace frust {

struct SourceFile {
    // Used in diagnostics, and to resolve `use self::` relative to it. Not opened.
    std::string name;
    std::string text;
};

struct Diagnostic {
    enum class Severity { Error, Warning };
    enum class Phase { Lexer, Parser, Modules, Codegen, Backend };

    Severity severity = Severity::Error;
    Phase phase = Phase::Codegen;
    std::string file;   // empty when the compiler did not say which file
    int line = 0;       // 1-based; 0 when the compiler did not say
    int column = 0;     // 1-based; 0 when the compiler did not say
    std::string message;
};

// Supplies a file the compile asks for by name. Returns false if there is no
// such file. Used for `use self::name;`, which asks for "name.frust" and then
// "name.fr".
using SourceProvider = std::function<bool(const std::string& name, std::string& text)>;

// One pod, as needed to compile against it.
struct PodSource {
    std::string ns;                  // namespace the pod's declarations are prefixed with
    std::vector<SourceFile> sources; // every file of the pod; the entry is lib.fr, the rest are what its `use self::x;` lines name
};

// Supplies a pod by name and exact version for `import pod, "version";`.
using PodProvider = std::function<bool(const std::string& name, const std::string& version, PodSource& pod)>;

struct CompileRequest {
    std::vector<SourceFile> sources;   // compiled together, as one unit
    std::string podNamespace;          // prefix for this unit's declarations ("" for none)
    SourceProvider siblingFiles;       // for `use self::x;`; without it, self-use is an error
    PodProvider pods;                  // for `import pod, "v";`; without it, import is an error

    bool emitObject = true;            // false: check only (parse and generate, do not write an object)
    bool captureIr = false;            // also return LLVM IR text before and after optimization
};

struct CompileResult {
    bool ok = false;                   // true only if there are no errors
    std::vector<Diagnostic> diagnostics;
    std::vector<std::uint8_t> object;  // the object file, when ok and emitObject
    std::string irBeforeOptimization;  // only when captureIr
    std::string irAfterOptimization;   // only when captureIr

    bool hasErrors() const {
        for (const auto& d : diagnostics)
            if (d.severity == Diagnostic::Severity::Error) return true;
        return false;
    }
};

// Compiles one unit. Never throws for bad source; problems come back as
// diagnostics. Safe to call from several threads at once (each call has its
// own compiler state).
CompileResult Compile(const CompileRequest& request);

// Ready-made providers that read the real disk, for the command-line compiler
// and any tool that works on real folders. Compile() itself never uses them;
// a host that must not touch the disk simply does not pass them.
SourceProvider DiskSourceProvider();  // a name is a path; returns its contents
PodProvider FratePodProvider();       // frate.json in the working directory and the Frate cache

// Human-readable form of a diagnostic: "file:line:col: error: message".
std::string FormatDiagnostic(const Diagnostic& diagnostic);

} // namespace frust
