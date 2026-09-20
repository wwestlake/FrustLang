#pragma once

// The front half of the embeddable compiler, for hosts that need the merged
// program itself rather than an object file (the plugin host compiles it into
// a JIT module). Everything the source and pod providers supply is resolved
// here; no file is opened. See CompilerApi.h for the request and diagnostics.

#include "AST.h"
#include "CompilerApi.h"

namespace frust {

// Parses every source in `request` into `arena`, resolves `use self::`, `import`
// and bare `use pod;` through the request's providers, applies the pod
// namespace, and returns the merged program - or null, with diagnostics in
// `result`, if anything failed.
Program* BuildProgram(const CompileRequest& request, AstArena& arena, CompileResult& result);

} // namespace frust
