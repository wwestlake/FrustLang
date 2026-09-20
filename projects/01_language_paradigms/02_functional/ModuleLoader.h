#pragma once

#include "AST.h"
#include "CompilerApi.h"
#include <functional>
#include <istream>
#include <string>
#include <vector>

namespace frust {
    // Resolves `use` declarations in the given program.
    // 1. Finds all UseDecls.
    // 2. Asks the installed ImportResolver (if any) to locate the requested pods.
    // 3. Parses their `lib.fr`.
    // 4. Prepends the pod namespace to all declarations in the imported pod.
    // 5. Appends the imported declarations into `prog`.
    bool ResolveImports(Program* prog, AstArena& arena, std::vector<std::string>& errors);

    // Parses one program from a stream (no import resolution).
    Program* ParseProgram(std::istream& input, AstArena& arena, std::vector<std::string>& parseErrors);

    // How ResolveImports and the plugin host's path-based loads reach the outside world. frust_lang
    // itself touches no file: a front end installs what it needs. The command line and the older
    // file-based hosts install the real disk (DiskLoader.h, InstallDiskResolvers); an application
    // that keeps its sources in the VFS installs nothing and compiles through a HostEnvironment.
    using ImportResolver = std::function<bool(Program*, AstArena&, std::vector<std::string>&)>;
    using FileReader = std::function<bool(const std::string& path, std::string& text)>;
    void SetImportResolver(ImportResolver resolver);
    void SetFileReader(FileReader reader);
    bool ReadFileThroughHost(const std::string& path, std::string& text);  // false if no reader is installed

    // Same, but the sibling files come from `files` (asked for "X.frust" and
    // then "X.fr") instead of from a directory. Touches no file.
    bool ResolveSelfUsesWith(Program* prog, AstArena& arena, const SourceProvider& files, std::vector<std::string>& errors);

    // Merges an imported pod's declarations into `prog` under `qualifiedName`
    // (prefixing them, and marking functions extern so they are not compiled
    // again). Used by ResolveImports and the embeddable compiler.
    void MergeImportedPod(Program* prog, Program* podProg, const std::string& qualifiedName);
}
