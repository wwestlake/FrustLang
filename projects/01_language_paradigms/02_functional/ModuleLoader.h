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
}
