#pragma once

#include "AST.h"
#include <string>
#include <vector>

namespace frust {
    // Resolves `use` declarations in the given program.
    // 1. Finds all UseDecls.
    // 2. Uses FrateResolver/Cache to locate the requested pods.
    // 3. Parses their `lib.fr`.
    // 4. Prepends the pod namespace to all declarations in the imported pod.
    // 5. Appends the imported declarations into `prog`.
    bool ResolveImports(Program* prog, AstArena& arena, std::vector<std::string>& errors);

    // Resolves `use self::X;` declarations against files sitting next
    // to `baseDir` - tries `baseDir/X.frust` first (the plugin-file
    // convention), then `baseDir/X.fr` (the library/pod-file
    // convention), and merges each resolved file's declarations
    // directly into `prog->decls` (no namespace prefixing - self-use
    // is same-unit, sibling-file inclusion, not cross-pod import).
    // Returns false (appending to `errors`) on any resolution/parse
    // failure; a no-op (returns true) if `prog` has no self-use decls
    // at all. Used both by ResolveImports (a pod's own lib.fr naming
    // its sibling files) and, directly, by anything single-file that
    // wants multi-file support without frate's own "pass every file as
    // a separate compiler argument" mechanism (LANGUAGE_GAPS.md #8 -
    // frust_plugin_load's single-file limitation).
    bool ResolveSelfUses(Program* prog, AstArena& arena, const std::string& baseDir, std::vector<std::string>& errors);
}
