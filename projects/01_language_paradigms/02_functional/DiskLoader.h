#pragma once

// The disk half of FRust's module loading (see DiskLoader.cpp). Used by the command-line tools and by
// hosts whose sources live in real folders. Not part of frust_lang: an application that keeps its
// sources in the VFS does not link or call any of this.

#include "AST.h"
#include "CompilerApi.h"

#include <string>
#include <vector>

namespace frust {

// `use self::x;` resolved against files next to `baseDir` ("x.frust" then "x.fr").
bool ResolveSelfUsesFromDisk(Program* prog, AstArena& arena, const std::string& baseDir, std::vector<std::string>& errors);

// `use pod;` / `import pod, "v";` resolved from frate.json in the working directory and the Frate cache.
bool ResolveImportsFromDisk(Program* prog, AstArena& arena, std::vector<std::string>& errors);

SourceProvider DiskSourceProvider();  // a name is a path; returns its contents
PodProvider FratePodProvider();       // frate.json in the working directory and the Frate cache

// Makes the real disk the answer to the loader's two hooks (SetImportResolver, SetFileReader), so
// ResolveImports and the plugin host's path-based loads work as they always did. Safe to call more than once.
void InstallDiskResolvers();

} // namespace frust
